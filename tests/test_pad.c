#define _GNU_SOURCE
#include <assert.h>
#include <unistd.h>
#include "../src/runtime_pad.c"
#ifdef _WIN32
#define setenv(name, value, overwrite) _putenv_s(name, value)
#endif

static int capture;
int tsukigpu_overlay_captures_input(void) { return capture; }
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) {
    (void)table; (void)count; (void)name;
    return 0;
}

static void inject(const char *path, const char *tokens) {
    FILE *f=fopen(path,"w");
    assert(f);
    fputs(tokens,f);
    fclose(f);
    usleep(25000);
}
static PadData read_pad(void) {
    PadData d;
    assert(pad_read_state(1,&d)==0);
    return d;
}
static void pump(void) { SDL_UpdateJoysticks(); SDL_UpdateGamepads(); }

static void write_file(const char *path, const char *text) {
    FILE *f=fopen(path,"w");
    assert(f);
    fputs(text,f);
    fclose(f);
}
static size_t parse_file(const char *path, PadSample **samples) {
    FILE *f=fopen(path,"r");
    assert(f);
    size_t n=replay_parse(f,path,samples);
    fclose(f);
    return n;
}
/* The replay loads at the first read of the process; each case starts that process over. */
static void start_replay(const char *path, const char *text) {
    write_file(path,text);
    setenv("TSUKI_PAD_REPLAY",path,1);
    free(replay); replay=NULL; replay_count=replay_next=0; replay_state=0; pad_reads=0;
}
static void set_button(SDL_Joystick *joystick, SDL_GamepadButton button, bool down) {
    assert(SDL_SetJoystickVirtualButton(joystick,button,down));
    pump();
}
#define SAMPLE(read,buttons) #read " " #buttons " 128 128 128 128 0 0\n"

/* At read r (0-based) the last sample with reads<=r counts, whatever the clock says; the
 * gamepad's Circle is hidden while the replay plays and comes back once it is over. */
static void replay_waits_for_reads(const char *path, SDL_Joystick *joystick) {
    start_replay(path,"reads\n" SAMPLE(0,16384) SAMPLE(10,0));
    set_button(joystick,SDL_GAMEPAD_BUTTON_EAST,true);
    for (int read=1;read<=41;++read) assert(read_pad().buttons==(read<=10 ? BTN_CROSS : 0u));
    assert(read_pad().buttons==BTN_CIRCLE && replay_state==2); /* read 42: 30 reads after the last sample */
    set_button(joystick,SDL_GAMEPAD_BUTTON_EAST,false);
    assert(read_pad().buttons==0);
}
/* A recording from before the read clock (ms lines, no header) is not played at all. */
static void replay_rejects_ms_format(const char *path) {
    PadSample *samples=NULL;
    start_replay(path,SAMPLE(0,16384) SAMPLE(250,0));
    assert(parse_file(path,&samples)==0);
    free(samples);
    assert(read_pad().buttons==0 && replay_state==2);
}
/* A cut or edited file plays up to its last good line, never past it. */
static void replay_stops_at_bad_line(const char *path) {
    PadSample *samples=NULL;
    write_file(path,"reads\n" SAMPLE(0,1) SAMPLE(3,0) "5 x\n" SAMPLE(7,1));
    assert(parse_file(path,&samples)==2 && samples[0].read==0 && samples[1].read==3 && samples[1].buttons==0);
    free(samples); samples=NULL;
    write_file(path,"reads\n" SAMPLE(4,1) SAMPLE(2,0)); /* reads going back */
    assert(parse_file(path,&samples)==1 && samples[0].read==4);
    free(samples); samples=NULL;
    write_file(path,"reads\n" SAMPLE(0,1) "1 0 300 128 128 128 0 0\n"); /* an axis past 255 */
    assert(parse_file(path,&samples)==1);
    free(samples);
}
/* The last sample does not stay held forever: the real input comes back with nothing pressed. */
static void replay_finishes_and_releases(const char *path) {
    start_replay(path,"reads\n" SAMPLE(0,16384));
    for (int read=1;read<=31;++read) assert(read_pad().buttons==BTN_CROSS);
    assert(read_pad().buttons==0 && replay_state==2);
}
static void record_round_trip(const char *path) {
    FILE *f=fopen(path,"w");
    assert(f);
    fputs(PAD_REPLAY_HEADER "\n",f);
    const PadSample written[3]={{0,BTN_CROSS,{1,2,3,4},5,6},{3,0,{128,128,128,128},0,0},{7,BTN_OPTIONS|BTN_L2,{0,255,10,20},255,0}};
    for (int i=0;i<3;++i) record_write(f,written[i].read,&written[i]);
    fclose(f);
    PadSample *samples=NULL;
    assert(parse_file(path,&samples)==3);
    for (int i=0;i<3;++i) /* field by field: the struct has padding */
        assert(samples[i].read==written[i].read && samples[i].buttons==written[i].buttons &&
               !memcmp(samples[i].axes,written[i].axes,4) && samples[i].l2==written[i].l2 && samples[i].r2==written[i].r2);
    free(samples);
}
/* Reads taken by an open menu still tick the clock: a recording does not drift behind it. */
static void clock_counts_captured_reads(void) {
    pad_reads=0;
    capture=1; read_pad(); read_pad();
    capture=0; read_pad();
    assert(pad_reads==3);
}

int main(void) {
    const char *tmp=getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
    char path[1024];
    snprintf(path,sizeof(path),"%s/tsuki-pad-test-XXXXXX",tmp);
    int fd=mkstemp(path);
    assert(fd>=0);
    close(fd);
    setenv("TSUKI_PAD_FILE",path,1);
    setenv("TSUKI_PAD_MAP","touchpad=misc1",1); /* the real touchpad click stays */
    setenv("SDL_VIDEODRIVER","dummy",1);
    /* Only the virtual test controller is a gamepad, whatever is plugged in. */
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0x1d50/0x6189");
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD));
    assert(pad_init()==0 && pad_open(1,0,0,NULL)==1);

    /* touchpad, touchpad_left and touchpad_right are one button, the touchpad click: the game
     * gets it at once and with no touch (only a real touchpad reports fingers). */
    inject(path,"cross l3 touchpad_left");
    PadData data=read_pad();
    assert(data.buttons==(BTN_CROSS|BTN_L3|BTN_TOUCHPAD) && data.touch_count==0);
    inject(path,"touchpad_right");
    data=read_pad();
    assert(data.buttons==BTN_TOUCHPAD && data.touch_count==0);
    inject(path,"touchpad");
    data=read_pad();
    assert(data.buttons==BTN_TOUCHPAD && data.touch_count==0);
    inject(path,"");
    data=read_pad();
    assert(data.buttons==0 && data.touch_count==0);

    SDL_VirtualJoystickTouchpadDesc touch={.nfingers=2};
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes=SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
    desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
    desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;
    desc.name="tsuki test controller";
    desc.vendor_id=0x1d50;
    desc.product_id=0x6189;
    desc.ntouchpads=1;
    desc.touchpads=&touch;
    SDL_JoystickID id=SDL_AttachVirtualJoystick(&desc);
    assert(id!=0);
    SDL_Joystick *joystick=SDL_OpenJoystick(id);
    assert(joystick);
    /* A real touchpad: the fingers and the click go to the game as they are. */
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,0,true,0.75f,0.5f,1.0f));
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,1,true,0.25f,1.0f,1.0f));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_TOUCHPAD,true));
    pump();
    data=read_pad();
    assert(gamepad && data.touch_count==2 && (data.buttons & BTN_TOUCHPAD));
    assert(data.touches[0].x==1439 && data.touches[0].y==471 && data.touches[0].id==0);
    assert(data.touches[1].x==480 && data.touches[1].y==942 && data.touches[1].id==1);
    capture=1;
    data=read_pad();
    assert(data.touch_count==0 && data.buttons==0);
    capture=0;
    /* Everything released: the buttons held across the capture show again. */
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,0,false,0,0,0));
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,1,false,0,0,0));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_TOUCHPAD,false));
    pump();
    data=read_pad();
    assert(data.buttons==0 && data.touch_count==0);
    /* A click without a finger is only the button. */
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_TOUCHPAD,true));
    pump();
    data=read_pad();
    assert(data.buttons==BTN_TOUCHPAD && data.touch_count==0);
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_TOUCHPAD,false));
    /* TSUKI_PAD_MAP adds misc1 as a source. */
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_MISC1,true));
    pump();
    data=read_pad();
    assert(data.buttons==BTN_TOUCHPAD && data.touch_count==0);
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_MISC1,false));
    /* Back (Share, View) presses nothing by default, alone or with L1. */
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_BACK,true));
    pump();
    data=read_pad();
    assert(data.buttons==0 && data.touch_count==0);
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true));
    pump();
    data=read_pad();
    assert(data.buttons==BTN_L1 && data.touch_count==0);
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,false));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_BACK,false));
    pump();
    data=read_pad();
    assert(data.buttons==0 && data.touch_count==0);
    char replay_path[1100];
    snprintf(replay_path,sizeof(replay_path),"%s.replay",path);
    replay_waits_for_reads(replay_path,joystick);
    replay_rejects_ms_format(replay_path);
    replay_stops_at_bad_line(replay_path);
    replay_finishes_and_releases(replay_path);
    record_round_trip(replay_path);
    clock_counts_captured_reads();
    unlink(replay_path);
    SDL_CloseJoystick(joystick);
    if (gamepad) SDL_CloseGamepad(gamepad);
    gamepad=NULL;
    assert(SDL_DetachVirtualJoystick(id));
    SDL_Quit();
    unlink(path);
    puts("PASS: pad ABI, touchpad names, SDL touch coordinates, overlay capture, no default for Back, replay by reads");
}
