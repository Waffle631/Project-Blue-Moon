#define _GNU_SOURCE
#include "runtime.h"
#include <stdlib.h>

/* The settings menu of the GPU library restarts through probe.c, which tests do not link. */
void runtime_restart(void) { abort(); }
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
/* Contracts of docs/specs/2026-10-14-spec-2-kernel.md, 3.2 (shadPS4 upstream event_flag.cpp). */
typedef int32_t (ABI *Create)(uint64_t *,const char *,uint32_t,uint64_t,const void *);
typedef int32_t (ABI *Delete)(uint64_t);
typedef int32_t (ABI *Set)(uint64_t,uint64_t);
typedef int32_t (ABI *Wait)(uint64_t,uint64_t,uint32_t,uint64_t *,uint32_t *);
typedef int32_t (ABI *Cancel)(uint64_t,uint64_t,int32_t *);
#define GET(t,n) ((t)runtime_resolve(n,0))
enum { AND=0x01, OR=0x02, CLEAR_ALL=0x10, CLEAR_PAT=0x20, SINGLE=0x10, MULTI=0x20 };
#define K_EPERM 0x80020001u
#define K_ESRCH 0x80020003u
#define K_EACCES 0x8002000Du
#define K_EINVAL 0x80020016u
#define K_ETIMEDOUT 0x8002003Cu
#define K_ENAMETOOLONG 0x8002003Fu
#define K_ECANCELED 0x80020055u
static Create create;
static Delete delete_ef;
static Set set_ef;
static Wait wait_ef;
static Cancel cancel_ef;
static void setup(void) {
    runtime_start(1);
    create=GET(Create,"BpFoboUJoZU#libkernel");
    delete_ef=GET(Delete,"8mql9OcQnd4#libkernel");
    wait_ef=GET(Wait,"JTvBflhYazQ#libkernel");
    set_ef=GET(Set,"IOnSvHzqu6A#libkernel");
    cancel_ef=GET(Cancel,"PZku4ZrXJqg#libkernel");
    assert(create && delete_ef && wait_ef && set_ef && cancel_ef);
}
/* Current pattern without blocking: an AND on every bit with timeout 0 reports it and clears nothing. */
static uint64_t pattern(uint64_t ef) {
    uint64_t r=0xdead; uint32_t t=0;
    int32_t e=wait_ef(ef,~UINT64_C(0),AND,&r,&t);
    assert(e==0 ? r==~UINT64_C(0) : (uint32_t)e==K_ETIMEDOUT);
    return r;
}
static uint64_t now_us(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000+(uint64_t)t.tv_nsec/1000;
}
/* 1: invalid parameters, attributes, stale and null handles, double delete. */
static void lifecycle(void) {
    struct { uint64_t handle; uint32_t canary; } s={0,0xdeadbeef};
    int opt=0;
    assert((uint32_t)create(NULL,"ef",0,0,NULL)==K_EINVAL);
    assert((uint32_t)create(&s.handle,NULL,0,0,NULL)==K_EINVAL);
    assert((uint32_t)create(&s.handle,"ef",0,0,&opt)==K_EINVAL);
    const uint32_t bad[]={0x03,0x04,0x0f,0x13,0x23,0x30,0x33,0x100};
    for (size_t i=0;i<sizeof bad/sizeof *bad;++i) assert((uint32_t)create(&s.handle,"ef",bad[i],0,NULL)==K_EINVAL);
    assert(s.handle==0 && s.canary==0xdeadbeef);
    char name[33]; memset(name,'a',32); name[32]=0;
    assert((uint32_t)create(&s.handle,name,0,0,NULL)==K_ENAMETOOLONG && s.handle==0);
    name[31]=0; /* 31 characters + terminator fit */
    const uint32_t good[]={0x00,0x01,0x02,0x10,0x11,0x12,0x20,0x21,0x22};
    uint64_t previous=0;
    for (size_t i=0;i<sizeof good/sizeof *good;++i) {
        assert(create(&s.handle,name,good[i],0,NULL)==0 && s.handle && s.handle!=previous && s.canary==0xdeadbeef);
        previous=s.handle; assert(delete_ef(s.handle)==0);
    }
    assert(create(&s.handle,"ef",MULTI,0x5,NULL)==0 && pattern(s.handle)==0x5);
    uint64_t old=s.handle,r=0; int32_t n=0; uint32_t t=0;
    assert(delete_ef(old)==0);
    assert((uint32_t)delete_ef(old)==K_ESRCH);
    assert((uint32_t)set_ef(old,1)==K_ESRCH);
    assert((uint32_t)wait_ef(old,1,AND,&r,&t)==K_ESRCH);
    assert((uint32_t)cancel_ef(old,0,&n)==K_ESRCH);
    assert((uint32_t)delete_ef(0)==K_ESRCH && (uint32_t)set_ef(0,1)==K_ESRCH);
    assert((uint32_t)wait_ef(0,0,0,NULL,NULL)==K_ESRCH && (uint32_t)cancel_ef(0,0,NULL)==K_ESRCH);
    assert(create(&s.handle,"ef",0,0,NULL)==0 && s.handle!=old);
    assert((uint32_t)set_ef(old,1)==K_ESRCH && set_ef(s.handle,0)==0);
    assert(delete_ef(s.handle)==0);
}
/* 2: AND and OR, met at once and not met with timeout 0; invalid bits and modes. */
static void modes(void) {
    uint64_t ef,r=0; uint32_t t=0;
    assert(create(&ef,"modes",MULTI,0x6,NULL)==0);
    assert((uint32_t)wait_ef(ef,0,AND,&r,&t)==K_EINVAL);
    const uint32_t bad[]={0x00,0x03,0x10,0x20,0x31,0x32,0x04|AND,0x40|OR,0x100|AND};
    for (size_t i=0;i<sizeof bad/sizeof *bad;++i) { t=0; assert((uint32_t)wait_ef(ef,0x2,bad[i],&r,&t)==K_EINVAL); }
    r=0; assert(wait_ef(ef,0x6,AND,&r,NULL)==0 && r==0x6);
    r=0; t=0; assert((uint32_t)wait_ef(ef,0x7,AND,&r,&t)==K_ETIMEDOUT && r==0x6 && t==0);
    r=0; assert(wait_ef(ef,0x3,OR,&r,NULL)==0 && r==0x6);
    r=0; t=0; assert((uint32_t)wait_ef(ef,0x9,OR,&r,&t)==K_ETIMEDOUT && r==0x6 && t==0);
    assert(wait_ef(ef,0x2,OR,NULL,NULL)==0);
    assert(pattern(ef)==0x6);
    assert(delete_ef(ef)==0);
}
/* 3: CLEAR_PAT clears only the requested bits, CLEAR_ALL everything; result is the pattern before. */
static void clears(void) {
    uint64_t ef,r=0; uint32_t t=0;
    assert(create(&ef,"clears",MULTI,0xf0f,NULL)==0);
    assert(wait_ef(ef,0x00f,AND|CLEAR_PAT,&r,NULL)==0 && r==0xf0f && pattern(ef)==0xf00);
    assert(wait_ef(ef,0x300,OR|CLEAR_PAT,&r,NULL)==0 && r==0xf00 && pattern(ef)==0xc00);
    assert(wait_ef(ef,0x400,OR|CLEAR_ALL,&r,NULL)==0 && r==0xc00 && pattern(ef)==0);
    assert(set_ef(ef,0x1)==0);
    r=0; assert((uint32_t)wait_ef(ef,0x3,AND|CLEAR_ALL,&r,&t)==K_ETIMEDOUT && r==0x1 && pattern(ef)==0x1);
    assert(delete_ef(ef)==0);
}
/* 4: a timed-out wait reports 0 left; a wait met at once leaves the timeout as given. */
static void timeouts(void) {
    uint64_t ef,r=0xdead; uint32_t t=2000;
    assert(create(&ef,"timeouts",MULTI,0,NULL)==0);
    uint64_t start=now_us();
    assert((uint32_t)wait_ef(ef,0x1,AND,&r,&t)==K_ETIMEDOUT && t==0 && r==0 && now_us()-start>=2000);
    assert(runtime_eventflag_waiters(ef)==0);
    assert(set_ef(ef,0x1)==0);
    t=1000000; assert(wait_ef(ef,0x1,AND|CLEAR_PAT,&r,&t)==0 && t==1000000 && r==0x1);
    assert(delete_ef(ef)==0);
}
typedef struct { uint64_t ef,bits,pattern; uint32_t mode,timeout; int forever; int32_t result; } Task;
static void *waiter(void *p) { Task *t=p; t->result=wait_ef(t->ef,t->bits,t->mode,&t->pattern,t->forever ? NULL : &t->timeout); return NULL; }
static void enrolled(uint64_t ef,unsigned count) {
    const struct timespec nap={0,1000000};
    for (int i=0;i<2000;++i) {
        if (runtime_eventflag_waiters(ef)==count) return;
        nanosleep(&nap,NULL);
    }
    assert(!"waiter enrollment timed out");
}
static void start(pthread_t *thread,Task *t,uint64_t ef,unsigned queued) {
    assert(pthread_create(thread,NULL,waiter,t)==0); enrolled(ef,queued);
}
static Task task(uint64_t ef,uint64_t bits,uint32_t mode) { return (Task){ef,bits,0xdead,mode,5000000,0,99}; }
/* 2 (blocking), 5 and 8: waits met by a later Set, partial wakeups, clear order, SINGLE. */
static void concurrency(void) {
    uint64_t ef; pthread_t ta,tb;
    assert(create(&ef,"and",MULTI,0,NULL)==0);
    Task a=task(ef,0x3,AND);
    start(&ta,&a,ef,1);
    assert(set_ef(ef,0x1)==0 && runtime_eventflag_waiters(ef)==1);
    assert(set_ef(ef,0x2)==0);
    assert(pthread_join(ta,NULL)==0 && a.result==0 && a.pattern==0x3 && a.timeout<5000000 && a.timeout>4000000);
    assert(delete_ef(ef)==0);
    /* Two waiters on different bits: each Set wakes only the one it satisfies. */
    assert(create(&ef,"split",MULTI,0,NULL)==0);
    a=task(ef,0x1,AND|CLEAR_PAT); Task b=task(ef,0x4,AND|CLEAR_PAT);
    start(&ta,&a,ef,1); start(&tb,&b,ef,2);
    assert(set_ef(ef,0x4)==0);
    assert(pthread_join(tb,NULL)==0 && b.result==0 && b.pattern==0x4);
    assert(runtime_eventflag_waiters(ef)==1 && pattern(ef)==0);
    assert(set_ef(ef,0x1)==0);
    assert(pthread_join(ta,NULL)==0 && a.result==0 && a.pattern==0x1 && pattern(ef)==0);
    assert(delete_ef(ef)==0);
    /* Set serves the queue in order and clears for each waiter before testing the next one. */
    assert(create(&ef,"order",MULTI,0,NULL)==0);
    a=task(ef,0x3,OR|CLEAR_PAT); b=task(ef,0x1,OR);
    start(&ta,&a,ef,1); start(&tb,&b,ef,2);
    assert(set_ef(ef,0x1)==0);
    assert(pthread_join(ta,NULL)==0 && a.result==0 && a.pattern==0x1);
    assert(runtime_eventflag_waiters(ef)==1 && pattern(ef)==0);
    assert(set_ef(ef,0x1)==0);
    assert(pthread_join(tb,NULL)==0 && b.result==0 && b.pattern==0x1 && pattern(ef)==0x1);
    assert(delete_ef(ef)==0);
    assert(create(&ef,"order2",MULTI,0,NULL)==0);
    a=task(ef,0x1,OR); b=task(ef,0x1,OR|CLEAR_ALL);
    start(&ta,&a,ef,1); start(&tb,&b,ef,2);
    assert(set_ef(ef,0x1)==0);
    assert(pthread_join(ta,NULL)==0 && a.result==0 && a.pattern==0x1);
    assert(pthread_join(tb,NULL)==0 && b.result==0 && b.pattern==0x1 && pattern(ef)==0);
    assert(delete_ef(ef)==0);
    /* SINGLE (attr 0 or 0x10): a second wait while one is queued fails with EPERM, even if met. */
    const uint32_t singles[]={0x00,SINGLE};
    for (size_t i=0;i<2;++i) {
        uint64_t r=0; uint32_t t=0;
        assert(create(&ef,"single",singles[i],0x2,NULL)==0);
        a=task(ef,0x1,AND);
        start(&ta,&a,ef,1);
        assert((uint32_t)wait_ef(ef,0x4,AND,&r,&t)==K_EPERM);
        assert((uint32_t)wait_ef(ef,0x2,AND,&r,&t)==K_EPERM);
        assert(set_ef(ef,0x1)==0);
        assert(pthread_join(ta,NULL)==0 && a.result==0 && a.pattern==0x3);
        t=0; assert(wait_ef(ef,0x2,AND,&r,&t)==0 && r==0x3);
        assert(delete_ef(ef)==0);
    }
    assert(create(&ef,"multi",MULTI,0,NULL)==0);
    a=task(ef,0x1,AND);
    start(&ta,&a,ef,1);
    uint64_t r=0; uint32_t t=0;
    assert((uint32_t)wait_ef(ef,0x4,AND,&r,&t)==K_ETIMEDOUT);
    assert(set_ef(ef,0x1)==0 && pthread_join(ta,NULL)==0 && a.result==0);
    assert(delete_ef(ef)==0);
}
/* 6 and 7: Cancel wakes everyone with the new pattern; Delete wakes everyone with EACCES. */
static void cancel_delete(void) {
    uint64_t ef; pthread_t ta,tb; int32_t n=-1;
    assert(create(&ef,"cancel",MULTI,0x8,NULL)==0);
    assert(cancel_ef(ef,0x8,&n)==0 && n==0);
    Task a=task(ef,0x1,AND),b=task(ef,0x2,OR|CLEAR_ALL); b.forever=1;
    start(&ta,&a,ef,1); start(&tb,&b,ef,2);
    assert(cancel_ef(ef,0x55,&n)==0 && n==2);
    assert(pthread_join(ta,NULL)==0 && (uint32_t)a.result==K_ECANCELED && a.pattern==0x55 && a.timeout>0);
    assert(pthread_join(tb,NULL)==0 && (uint32_t)b.result==K_ECANCELED && b.pattern==0x55);
    assert(runtime_eventflag_waiters(ef)==0 && pattern(ef)==0x55);
    assert(cancel_ef(ef,0x1,NULL)==0 && pattern(ef)==0x1);
    a=task(ef,0x80,AND); a.forever=1; b=task(ef,0x80,AND);
    start(&ta,&a,ef,1); start(&tb,&b,ef,2);
    assert(delete_ef(ef)==0);
    assert(pthread_join(ta,NULL)==0 && (uint32_t)a.result==K_EACCES);
    assert(pthread_join(tb,NULL)==0 && (uint32_t)b.result==K_EACCES);
    assert((uint32_t)delete_ef(ef)==K_ESRCH && (uint32_t)set_ef(ef,0x80)==K_ESRCH);
    assert(runtime_eventflag_waiters(ef)==0);
}
/* 9: four ping-pong pairs (8 threads) share one flag; 100 000 round trips, each wait bounded so a
 * lost wakeup fails as a timeout instead of hanging. */
enum { PAIRS=4, ROUNDS=25000 };
typedef struct { uint64_t ef,request,reply; } Pair;
static void exchange(uint64_t ef,uint64_t give,uint64_t take,int first) {
    uint64_t r; uint32_t t=5000000;
    if (first) assert(set_ef(ef,give)==0);
    assert(wait_ef(ef,take,AND|CLEAR_PAT,&r,&t)==0 && (r&take)==take);
    if (!first) assert(set_ef(ef,give)==0);
}
static void *ping(void *p) { Pair *q=p; for (int i=0;i<ROUNDS;++i) exchange(q->ef,q->request,q->reply,1); return NULL; }
static void *pong(void *p) { Pair *q=p; for (int i=0;i<ROUNDS;++i) exchange(q->ef,q->reply,q->request,0); return NULL; }
static void stress(void) {
    uint64_t ef; Pair pairs[PAIRS]; pthread_t threads[PAIRS*2];
    assert(create(&ef,"stress",MULTI|0x01,0,NULL)==0);
    for (int i=0;i<PAIRS;++i) {
        pairs[i]=(Pair){ef,UINT64_C(1)<<(2*i),UINT64_C(1)<<(2*i+1)};
        assert(pthread_create(&threads[2*i],NULL,ping,&pairs[i])==0);
        assert(pthread_create(&threads[2*i+1],NULL,pong,&pairs[i])==0);
    }
    for (int i=0;i<PAIRS*2;++i) assert(pthread_join(threads[i],NULL)==0);
    assert(runtime_eventflag_waiters(ef)==0 && pattern(ef)==0);
    assert(delete_ef(ef)==0);
}
int main(int argc,char **argv) {
    setup();
    if (argc>1 && !strcmp(argv[1],"--concurrency")) concurrency();
    else if (argc>1 && !strcmp(argv[1],"--cancel-delete")) cancel_delete();
    else if (argc>1 && !strcmp(argv[1],"--stress")) stress();
    else { lifecycle(); modes(); clears(); timeouts(); }
    puts("PASS: event flag contracts");
    return 0;
}
