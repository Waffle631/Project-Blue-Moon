#define _GNU_SOURCE
#include <assert.h>
#include <unistd.h>
#include "../src/runtime_pad.c"
#ifdef _WIN32
#define setenv(name, value, overwrite) _putenv_s(name, value)
#endif

static int capture;
int gow3gpu_overlay_captures_input(void) { return capture; }
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

int main(void) {
    const char *tmp=getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
    char path[1024];
    snprintf(path,sizeof(path),"%s/gow3-pad-test-XXXXXX",tmp);
    int fd=mkstemp(path);
    assert(fd>=0);
    close(fd);
    setenv("GOW3_PAD_FILE",path,1);
    setenv("GOW3_PAD_MAP","touchpad=misc1",1); /* the real touchpad click stays */
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
    desc.name="gow3 test controller";
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
    /* GOW3_PAD_MAP adds misc1 as a source. */
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
    SDL_CloseJoystick(joystick);
    if (gamepad) SDL_CloseGamepad(gamepad);
    gamepad=NULL;
    assert(SDL_DetachVirtualJoystick(id));
    SDL_Quit();
    unlink(path);
    puts("PASS: pad ABI, touchpad names, SDL touch coordinates, overlay capture, no default for Back");
}
