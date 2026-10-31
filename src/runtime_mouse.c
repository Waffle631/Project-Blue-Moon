/* libSceMouse on the host mouse. The window thread accumulates SDL events (gpu/shim/tsuki_mouse.h);
 * each sceMouseRead takes what arrived since the previous one through tsukigpu_mouse_read: deltas that
 * put the game's cursor where the pointer is.
 *
 * What the game does with it (eboot analysis, docs/specs/2026-10-14-spec-6-2d-raton-saves.md §4):
 * Init -> Open(user, 0, 0, &{1}) -> Read(handle, buffer of 8 reports, 8) every frame; it walks the
 * `ret` reports it gets back, so ret must never exceed `count` (one report is written, ret is 1).
 * It reads buttons (+12), xAxis (+16), yAxis (+20) and wheel (+24); there is no Close, and no
 * call site compares a result with 0x80DF....: the codes only reach the log. */
#include "runtime.h"
#include "gpu/tsukigpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#define MOUSE_ERROR_INVALID_ARG ((int32_t)0x80DF0001)
#define MOUSE_ERROR_INVALID_PORT ((int32_t)0x80DF0002)
#define MOUSE_ERROR_INVALID_HANDLE ((int32_t)0x80DF0003)
#define MOUSE_ERROR_ALREADY_OPENED ((int32_t)0x80DF0004)
#define MOUSE_ERROR_NOT_INITIALIZED ((int32_t)0x80DF0005)
#define MOUSE_ERROR_FATAL ((int32_t)0x80DF00FF)
#define MOUSE_PORTS 8 /* one port per user, type and index are always 0 */

/* SceMouseData: the game reads buttons, xAxis, yAxis and wheel; tilt is never reported. */
typedef struct {
    uint64_t timestamp; uint8_t connected, pad0[3]; uint32_t buttons;
    int32_t x_axis, y_axis, wheel, tilt; uint8_t reserve[8];
} MouseData;
_Static_assert(sizeof(MouseData)==40, "SceMouseData is 40 bytes");
_Static_assert(offsetof(MouseData,buttons)==12 && offsetof(MouseData,x_axis)==16 && offsetof(MouseData,wheel)==24,
    "the offsets the game reads");

static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static int initialized, disabled, next_handle=1; /* disabled: TSUKI_MOUSE=0, read at Init */
static struct { int32_t handle, user; } ports[MOUSE_PORTS]; /* handle 0 = free */

static uint64_t now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000000u+(uint64_t)t.tv_nsec/1000u; }
/* Port index of an open handle, or -1. Caller holds the lock. */
static int find_handle(int32_t handle) { if (handle<=0) return -1; for (int i=0; i<MOUSE_PORTS; i++) if (ports[i].handle==handle) return i; return -1; }

/* TSUKI_MOUSE=0 reports no mouse: a recorded route (TSUKI_PAD_REPLAY) does not record the mouse, and a
 * pointer left somewhere else would move the game's cursor on each replay. */
static ABI int32_t mouse_init(void) {
    const char *env=getenv("TSUKI_MOUSE");
    pthread_mutex_lock(&lock); initialized=1; disabled=env && !strcmp(env,"0"); pthread_mutex_unlock(&lock);
    if (disabled) puts("Runtime: mouse disabled (TSUKI_MOUSE=0)");
    return 0;
}
static ABI int32_t mouse_open(int32_t user,int32_t type,int32_t index,const void *param) {
    (void)param;
    pthread_mutex_lock(&lock); int32_t result;
    if (!initialized) result=MOUSE_ERROR_NOT_INITIALIZED;
    else if (type!=0 || index!=0) result=MOUSE_ERROR_INVALID_PORT;
    else {
        int free_slot=-1; result=0;
        for (int i=0; i<MOUSE_PORTS; i++) {
            if (ports[i].handle && ports[i].user==user) { result=MOUSE_ERROR_ALREADY_OPENED; break; }
            if (!ports[i].handle && free_slot<0) free_slot=i;
        }
        if (!result && free_slot<0) result=MOUSE_ERROR_FATAL;
        if (!result) { ports[free_slot].handle=result=next_handle++; ports[free_slot].user=user; }
    }
    pthread_mutex_unlock(&lock);
    printf("Runtime: sceMouseOpen(user %d, type %d, index %d) -> %s0x%x\n",user,type,index,result<0?"error ":"handle ",(unsigned)result);
    return result;
}
static ABI int32_t mouse_read(int32_t handle,unsigned char *data,int32_t count) {
    pthread_mutex_lock(&lock);
    int32_t result=!initialized ? MOUSE_ERROR_NOT_INITIALIZED : find_handle(handle)<0 ? MOUSE_ERROR_INVALID_HANDLE : (!data || count<1) ? MOUSE_ERROR_INVALID_ARG : 0;
    pthread_mutex_unlock(&lock);
    if (result) return result;
    int32_t dx=0, dy=0, wheel=0; uint32_t buttons=0;
    /* An open menu is the bridge's business (all zero): it alone knows where it left the game's cursor. */
    int present=tsukigpu_mouse_read(&dx,&dy,&wheel,&buttons) && !disabled; /* drained either way */
    MouseData report; memset(&report,0,sizeof(report));
    if (present) { report.timestamp=now_us(); report.connected=1; report.buttons=buttons; report.x_axis=dx; report.y_axis=dy; report.wheel=wheel; }
    memcpy(data,&report,sizeof(report));
    return 1; /* one report, whatever count is: the game walks `ret` reports */
}
static ABI int32_t mouse_close(int32_t handle) {
    pthread_mutex_lock(&lock); int32_t result; int slot;
    if (!initialized) result=MOUSE_ERROR_NOT_INITIALIZED;
    else if ((slot=find_handle(handle))<0) result=MOUSE_ERROR_INVALID_HANDLE;
    else { ports[slot].handle=0; result=0; }
    pthread_mutex_unlock(&lock); return result;
}

static const RuntimeExport exports[]={
    {"sceMouseInit",mouse_init}, {"sceMouseOpen",mouse_open}, {"sceMouseRead",mouse_read}, {"sceMouseClose",mouse_close},
};
uintptr_t runtime_mouse_resolve(const char *name) { return RUNTIME_LOOKUP(exports,name); }
