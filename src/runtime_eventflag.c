/* Kernel event flags: 64-bit patterns, FIFO waiters and 64-bit IDs that are never reused (a stale
 * handle fails with ESRCH and no host memory reaches the guest). Contracts: shadPS4 upstream
 * kernel/threads/event_flag.cpp as corrected in docs/specs/2026-10-14-spec-2-kernel.md (3.2); unlike
 * upstream's broadcast, Set serves the queue in order and clears for each waiter before testing the
 * next, so overlapping CLEAR_PAT waits are deterministic. Priority flags are served FIFO, as the
 * semaphores of runtime_sema.c: host threads have no PS4 priority scheduler. */
#define _GNU_SOURCE
#include "runtime.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#define ERR(n) ((int32_t)(UINT32_C(0x80020000)|(n)))
enum { ATTR_MULTI=0x20, WAIT_AND=0x01, WAIT_OR=0x02, CLEAR_ALL=0x10, CLEAR_PAT=0x20 };
typedef struct Waiter {
    uint64_t bits, pattern;
    uint32_t mode;
    int32_t result;
    int done;
    pthread_cond_t event;
    struct Waiter *next;
} Waiter;
typedef struct Flag {
    uint64_t id, pattern;
    uint32_t attr;
    unsigned active;
    int deleted;
    char name[32];
    Waiter *first;
    struct Flag *next;
} Flag;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static Flag *registry;
static uint64_t next_id=1;
static size_t created, deleted, waits, sets, timed_out, canceled;
static Flag *find(uint64_t id) {
    for (Flag *f=registry;f;f=f->next) if (f->id==id) return f;
    return NULL;
}
static void host_check(int e) {
    if (e) { fprintf(stderr,"STOP: event flag host error %d\n",e); exit(21); }
}
/* Same deadline clock as runtime_sema.c: winpthreads condition variables accept only CLOCK_REALTIME. */
#ifdef _WIN32
#define WAIT_CLOCK CLOCK_REALTIME
#else
#define WAIT_CLOCK CLOCK_MONOTONIC
#endif
static uint64_t now_ns(void) {
    struct timespec t;
    if (clock_gettime(WAIT_CLOCK,&t)) { perror("clock_gettime"); exit(1); }
    return (uint64_t)t.tv_sec*1000000000+(uint64_t)t.tv_nsec;
}
static int met(uint64_t pattern,uint64_t bits,uint32_t mode) { return mode&WAIT_AND ? (pattern&bits)==bits : (pattern&bits)!=0; }
static void clear(Flag *f,uint64_t bits,uint32_t mode) {
    if (mode&CLEAR_ALL) f->pattern=0;
    else if (mode&CLEAR_PAT) f->pattern&=~bits;
}
static ABI int32_t ef_create(uint64_t *out,const char *name,uint32_t attr,uint64_t pattern,const void *options) {
    /* attr: low nibble 0/1 FIFO, 2 PRIO; high nibble 0/0x10 SINGLE, 0x20 MULTI. */
    if (!out || !name || options || (attr&0xf)>2 || (attr>>4)>2) return ERR(22);
    if (strlen(name)>=32) return ERR(63);
    Flag *f=calloc(1,sizeof(*f));
    if (!f) return ERR(12);
    pthread_mutex_lock(&lock);
    f->id=next_id++; f->attr=attr; f->pattern=pattern; strcpy(f->name,name);
    f->next=registry; registry=f; *out=f->id; ++created;
    printf("Runtime: event flag id=%" PRIu64 " '%s', attr=0x%x, pattern=0x%" PRIx64 "\n",f->id,name,attr,pattern);
    pthread_mutex_unlock(&lock);
    return 0;
}
static ABI int32_t ef_wait(uint64_t id,uint64_t bits,uint32_t mode,uint64_t *out,uint32_t *timeout) {
    pthread_mutex_lock(&lock);
    Flag *f=find(id);
    int32_t result=0;
    if (!f) result=ERR(3);
    else if (!bits || (mode&~0x33u) || (mode&3)==0 || (mode&3)==3 || (mode&0x30)==0x30) result=ERR(22);
    else if (!(f->attr&ATTR_MULTI) && f->first) result=ERR(1);
    else if (met(f->pattern,bits,mode)) { if (out) *out=f->pattern; clear(f,bits,mode); ++waits; }
    else if (timeout && !*timeout) { if (out) *out=f->pattern; result=ERR(60); ++timed_out; }
    else {
        if (getenv("TSUKI_TRACE_EVENTFLAG")) /* deadlock diagnosis */
            fprintf(stderr,"Runtime: blocking wait on event flag %" PRIu64 " '%s' (bits 0x%" PRIx64 ", mode 0x%x, pattern 0x%" PRIx64 ")\n",
                    id,f->name,bits,mode,f->pattern);
        Waiter w={.bits=bits,.mode=mode};
        pthread_condattr_t attr;
        host_check(pthread_condattr_init(&attr));
        host_check(pthread_condattr_setclock(&attr,WAIT_CLOCK));
        host_check(pthread_cond_init(&w.event,&attr));
        host_check(pthread_condattr_destroy(&attr));
        Waiter **tail=&f->first;
        while (*tail) tail=&(*tail)->next;
        *tail=&w; ++f->active;
        uint64_t deadline=timeout ? now_ns()+(uint64_t)*timeout*1000 : 0;
        struct timespec end={.tv_sec=(time_t)(deadline/1000000000),.tv_nsec=(long)(deadline%1000000000)};
        while (!w.done) {
            int e=timeout ? pthread_cond_timedwait(&w.event,&lock,&end) : pthread_cond_wait(&w.event,&lock);
            if (e==ETIMEDOUT && !w.done) {
                Waiter **p=&f->first;
                while (*p!=&w) p=&(*p)->next;
                *p=w.next; w.result=ERR(60); w.pattern=f->pattern; w.done=1; ++timed_out;
            } else if (e!=ETIMEDOUT) host_check(e);
        }
        result=w.result;
        if (out) *out=w.pattern;
        if (!result) ++waits;
        if (timeout) {
            uint64_t now=now_ns();
            *timeout=now>=deadline ? 0 : (uint32_t)((deadline-now)/1000); /* a timeout is past it */
        }
        host_check(pthread_cond_destroy(&w.event));
        if (!--f->active && f->deleted) free(f);
    }
    pthread_mutex_unlock(&lock);
    return result;
}
static ABI int32_t ef_set(uint64_t id,uint64_t bits) {
    pthread_mutex_lock(&lock);
    Flag *f=find(id);
    int32_t result=0;
    if (!f) result=ERR(3);
    else {
        f->pattern|=bits; ++sets;
        Waiter **p=&f->first;
        while (*p) {
            Waiter *w=*p;
            if (!met(f->pattern,w->bits,w->mode)) { p=&w->next; continue; }
            w->pattern=f->pattern; clear(f,w->bits,w->mode);
            *p=w->next; w->done=1;
            host_check(pthread_cond_signal(&w->event));
        }
    }
    pthread_mutex_unlock(&lock);
    return result;
}
/* Every queued waiter leaves with `result` and the current pattern. */
static int32_t wake_all(Flag *f,int32_t result) {
    int32_t count=0;
    while (f->first) {
        Waiter *w=f->first; f->first=w->next;
        w->result=result; w->pattern=f->pattern; w->done=1; ++count;
        host_check(pthread_cond_signal(&w->event));
    }
    return count;
}
static ABI int32_t ef_cancel(uint64_t id,uint64_t pattern,int32_t *waiters) {
    pthread_mutex_lock(&lock);
    Flag *f=find(id);
    int32_t result=0;
    if (!f) result=ERR(3);
    else {
        f->pattern=pattern; ++canceled;
        int32_t n=wake_all(f,ERR(85));
        if (waiters) *waiters=n;
    }
    pthread_mutex_unlock(&lock);
    return result;
}
static ABI int32_t ef_delete(uint64_t id) {
    pthread_mutex_lock(&lock);
    Flag **p=&registry;
    while (*p && (*p)->id!=id) p=&(*p)->next;
    if (!*p) { pthread_mutex_unlock(&lock); return ERR(3); }
    Flag *f=*p; *p=f->next; f->deleted=1; ++deleted;
    wake_all(f,ERR(13));
    if (!f->active) free(f);
    pthread_mutex_unlock(&lock);
    return 0;
}
/* Read-only test/diagnostic snapshot, not exposed to guest code. */
unsigned runtime_eventflag_waiters(uint64_t id) {
    pthread_mutex_lock(&lock);
    Flag *f=find(id); unsigned n=0;
    if (f) for (Waiter *w=f->first;w;w=w->next) ++n;
    pthread_mutex_unlock(&lock);
    return n;
}
uintptr_t runtime_eventflag_resolve(const char *name) {
    if (!strcmp(name,"BpFoboUJoZU#libkernel")) return (uintptr_t)ef_create;
    if (!strcmp(name,"8mql9OcQnd4#libkernel")) return (uintptr_t)ef_delete;
    if (!strcmp(name,"JTvBflhYazQ#libkernel")) return (uintptr_t)ef_wait;
    if (!strcmp(name,"IOnSvHzqu6A#libkernel")) return (uintptr_t)ef_set;
    if (!strcmp(name,"PZku4ZrXJqg#libkernel")) return (uintptr_t)ef_cancel;
    return 0;
}
void runtime_eventflag_report(void) {
    pthread_mutex_lock(&lock);
    printf("Runtime: event flags created=%zu, deleted=%zu, waits=%zu, sets=%zu, timeouts=%zu, cancels=%zu\n",
           created,deleted,waits,sets,timed_out,canceled);
    pthread_mutex_unlock(&lock);
}
