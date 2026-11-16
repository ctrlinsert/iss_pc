/* The mouse through the pad (src/runtime_pad.c): native camera motion independent of the right stick, buttons as PS4
 * buttons. Touchpad mode is gone: no mouse button turns the mouse into a finger. The
 * window's mouse is a stub the test drives; scePadReadState runs for real. */
#define _GNU_SOURCE
#include <assert.h>
#include <unistd.h>
#include "../src/runtime_pad.c"
/* No power-switch backend in this mouse test. */
int runtime_power_wheel_gameplay(void){return 1;}
void runtime_power_wheel_status(IssPowerWheelStatus *out){memset(out,0,sizeof(*out));}
int runtime_menu_scene(void){return ISS_MENU_SCENE_GAMEPLAY;}
void runtime_input_trace_flush(void){}
void runtime_power_wheel_request(int index){(void)index;}
void runtime_power_wheel_visual(int open,int choice){(void)open;(void)choice;}
void tlggpu_power_wheel_settings(int *enabled,int keys[4],int *instant){
    *enabled=0;*instant=0;memset(keys,0,4*sizeof(*keys));
}
// runtime_mouse_aim.c (the native aim hook) needs the guest; pad tests run without it.
int runtime_mouse_aim_installed(void) { return 0; }
static float direct_dx, direct_dy, direct_sensitivity;
static int direct_invert, direct_active;
void runtime_mouse_aim_hold_follow(int on);
void runtime_mouse_aim_hold_follow(int on) { (void)on; }
int runtime_mouse_aim_motion(float dx, float dy, float sensitivity, int invert_y, int active) {
    direct_dx=dx;direct_dy=dy;direct_sensitivity=sensitivity;
    direct_invert=invert_y;direct_active=active;return 0;
}

static TlgGpuMouse fake;
static TlgGpuInputSettings settings;
void tlggpu_mouse_take(TlgGpuMouse *mouse) {
    *mouse=fake;
    fake.dx=fake.dy=0; fake.pressed=0;
    fake.wheel_up=fake.wheel_down=fake.wheel_left=fake.wheel_right=0;
}
void tlggpu_input_settings(TlgGpuInputSettings *out) { *out=settings; }
uint64_t tlggpu_total_draws(void) { return 0; }
void tlggpu_frame_counters(TlgGpuFrameCounters *out) { memset(out,0,sizeof(*out)); }
int tlggpu_overlay_captures_input(void) { return 0; }
void tlggpu_overlay_notice(const char *text, float seconds) { (void)text; (void)seconds; }
void tlggpu_graffiti_guide(int force) { (void)force; }
float tlggpu_sensor_aim_scale(void) { return 1.0f; }
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) {
    (void)table; (void)count; (void)name;
    return 0;
}

enum { LEFT=1, MIDDLE=2, RIGHT=4, X1=8, X2=16 }; /* 1 << (SDL button - 1) */
static PadData read_pad(void) {
    PadData d;
    assert(pad_read_state(1,&d)==0);
    return d;
}
static void hold(uint32_t buttons) { fake.held=buttons; fake.pressed=buttons; }
/* A few samples of steady motion: counts per sample at ~50 Hz. */
static PadData move(float dx, float dy, int samples) {
    PadData d={0};
    for (int i=0;i<samples;++i) { fake.dx=dx; fake.dy=dy; usleep(20000); d=read_pad(); }
    return d;
}

int main(void) {
    setenv("SDL_VIDEODRIVER","dummy",1);
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0x1d50/0x6189");
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD));
    assert(pad_init()==0 && pad_open(1,0,0,NULL)==1);
    settings=(TlgGpuInputSettings){.mouse_enabled=1,.sensitivity=1.0f};
    fake.captured=1;

    /* Camera motion goes to the direct hook, never the legacy emulated stick. */
    PadData d=move(0,0,2);
    assert(d.right_x==128 && d.right_y==128);
    d=move(60,0,3);
    assert(d.right_x==128 && d.right_y==128 && direct_dx==60 && direct_active && direct_sensitivity==1.0f);
    d=move(-60,0,3);
    assert(d.right_x==128 && direct_dx==-60);
    d=move(0,60,3);
    assert(d.right_y==128 && direct_dy==60 && !direct_invert);
    settings.invert_y=1;
    d=move(0,60,3);
    assert(d.right_y==128 && direct_dy==60 && direct_invert);
    settings.invert_y=0;
    d=move(0,0,12);
    assert(d.right_x==128 && d.right_y==128); /* at rest the stick is exactly centred */
    /* Not captured (Esc, menu, focus lost): nothing. */
    fake.captured=0;
    d=move(60,0,3);
    assert(d.right_x==128);
    fake.captured=1;
    settings.mouse_enabled=0;
    d=move(60,0,3);
    assert(d.right_x==128);
    settings.mouse_enabled=1;
    move(0,0,3);

    /* Buttons: left R2, right L2, middle R3; the analog triggers read fully pressed. */
    hold(LEFT); d=move(0,0,1);
    assert((d.buttons & BTN_R2) && d.r2==255 && !(d.buttons & (BTN_L2|BTN_R3)));
    hold(RIGHT); d=move(0,0,1);
    assert((d.buttons & BTN_L2) && d.l2==255 && !(d.buttons & BTN_R2));
    hold(MIDDLE); d=move(0,0,1);
    assert((d.buttons & BTN_R3) && !(d.buttons & (BTN_L2|BTN_R2)));
    hold(0); d=move(0,0,1);
    assert(!(d.buttons & (BTN_L2|BTN_R2|BTN_R3)) && d.touch_count==0);
    /* A click shorter than a sample (pressed, already up) is not lost. */
    fake.pressed=LEFT; d=read_pad();
    assert(d.buttons & BTN_R2);

    /* No touchpad mode: X1 held with motion still turns the camera and never touches. */
    hold(X1); d=move(60,0,3);
    assert(d.touch_count==0 && direct_dx==60 && direct_active);
    hold(X1|LEFT); d=move(0,0,1);
    assert(d.touch_count==0 && (d.buttons & BTN_R2));
    hold(0); move(0,0,2);
    puts("PASS: mouse through the pad");
    return 0;
}
