/* libSceMouse contract: what the game reads from SceMouseData, the handle rules and the error codes. */
#undef NDEBUG /* the checks are asserts; release builds define NDEBUG */
#include "../src/runtime_mouse.c"
#include <assert.h>

uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *nid) { (void)table; (void)count; (void)nid; return 0; }

/* Fake bridge: what the window thread has accumulated since the last read. Taking it clears the
 * movement and the wheel like the real one (gpu/shim/gow3_mouse.h), the buttons are state. */
static int32_t acc_dx, acc_dy, acc_wheel; static uint32_t acc_buttons; static int present=1;
int gow3gpu_mouse_read(int32_t *dx, int32_t *dy, int32_t *wheel, uint32_t *buttons) {
    *dx=acc_dx; *dy=acc_dy; *wheel=acc_wheel; *buttons=acc_buttons; acc_dx=acc_dy=acc_wheel=0; return present;
}

typedef struct { uint64_t timestamp; uint8_t connected; uint32_t buttons; int32_t x, y, wheel, tilt; } Report;
/* Reads through the guest ABI into a 8-report buffer like the game's 320 bytes; returns the count. */
static int32_t read_report(int32_t handle, Report *out) {
    unsigned char buffer[320]; memset(buffer,0xCD,sizeof(buffer));
    int32_t n=mouse_read(handle,buffer,8);
    out->timestamp=*(uint64_t *)(buffer+0); out->connected=buffer[8]; out->buttons=*(uint32_t *)(buffer+12);
    out->x=*(int32_t *)(buffer+16); out->y=*(int32_t *)(buffer+20); out->wheel=*(int32_t *)(buffer+24); out->tilt=*(int32_t *)(buffer+28);
    for (int i=32; i<40; i++) assert(buffer[i]==0); /* the reserved tail is zeroed, not left as the caller's garbage */
    if (n==1) for (int i=40; i<320; i++) assert(buffer[i]==0xCD); /* ret is the count of reports written: one */
    return n;
}

static void needs_init(void) {
    unsigned char data[40];
    assert(mouse_open(1,0,0,NULL)==MOUSE_ERROR_NOT_INITIALIZED);
    assert(mouse_read(1,data,1)==MOUSE_ERROR_NOT_INITIALIZED);
    assert(mouse_close(1)==MOUSE_ERROR_NOT_INITIALIZED);
    assert(MOUSE_ERROR_NOT_INITIALIZED<0 && MOUSE_ERROR_INVALID_ARG<0 && MOUSE_ERROR_INVALID_PORT<0
        && MOUSE_ERROR_INVALID_HANDLE<0 && MOUSE_ERROR_ALREADY_OPENED<0 && MOUSE_ERROR_FATAL<0);
}
static void handles(void) {
    unsigned char data[40];
    assert(mouse_init()==0 && mouse_init()==0);
    int32_t h=mouse_open(1,0,0,NULL); assert(h>0);
    assert(mouse_open(1,0,0,NULL)==MOUSE_ERROR_ALREADY_OPENED);
    assert(mouse_close(h)==0);
    assert(mouse_read(h,data,1)==MOUSE_ERROR_INVALID_HANDLE && mouse_close(h)==MOUSE_ERROR_INVALID_HANDLE);
    assert(mouse_close(h)==MOUSE_ERROR_INVALID_HANDLE); /* the open count must not go negative and free a port that is not open */
    int32_t again=mouse_open(1,0,0,NULL); assert(again>0 && again!=h);
    assert(mouse_read(again,data,1)==1);
    assert(mouse_open(1,0,0,NULL)==MOUSE_ERROR_ALREADY_OPENED); /* still opened after the stale closes */
    assert(mouse_open(2,0,0,NULL)>0); /* another user is another port */
    assert(mouse_open(3,1,0,NULL)==MOUSE_ERROR_INVALID_PORT && mouse_open(3,0,1,NULL)==MOUSE_ERROR_INVALID_PORT);
    assert(mouse_read(again,NULL,1)==MOUSE_ERROR_INVALID_ARG && mouse_read(again,data,0)==MOUSE_ERROR_INVALID_ARG
        && mouse_read(again,data,-1)==MOUSE_ERROR_INVALID_ARG);
    assert(mouse_read(0,data,1)==MOUSE_ERROR_INVALID_HANDLE && mouse_read(-1,data,1)==MOUSE_ERROR_INVALID_HANDLE);
    assert(mouse_read(again,data,5)==1); /* ret never exceeds what the game asked for, and the game iterates ret reports */
}
static void reads_motion_and_buttons(void) {
    int32_t h=mouse_open(7,0,0,NULL); assert(h>0);
    acc_dx=5; acc_dy=-3; acc_wheel=1; acc_buttons=0x1; present=1;
    Report r; assert(read_report(h,&r)==1);
    assert(r.connected==1 && r.x==5 && r.y==-3 && r.wheel==1 && r.buttons==0x1 && r.timestamp>0 && r.tilt==0);
}
static void accumulator_resets(void) {
    int32_t h=mouse_open(8,0,0,NULL); assert(h>0);
    acc_dx=5; acc_dy=-3; acc_wheel=1; acc_buttons=0x3; present=1;
    Report r; assert(read_report(h,&r)==1 && r.x==5);
    assert(read_report(h,&r)==1);
    assert(r.x==0 && r.y==0 && r.wheel==0 && r.buttons==0x3); /* movement is a delta, the buttons are state */
}
static void no_device(void) {
    int32_t h=mouse_open(9,0,0,NULL); assert(h>0);
    acc_dx=5; acc_dy=-3; acc_wheel=1; acc_buttons=0x1; present=0;
    Report r; assert(read_report(h,&r)==1);
    assert(r.connected==0 && r.timestamp==0 && r.x==0 && r.y==0 && r.wheel==0 && r.buttons==0 && r.tilt==0);
    present=1;
}
int main(void) {
    needs_init(); handles(); reads_motion_and_buttons(); accumulator_resets(); no_device();
    puts("mouse test: ok"); return 0;
}
