/* TLG_PAD_MAP / TLG_KEY_MAP parsing (src/runtime_pad.c). */
#define _GNU_SOURCE
#include <assert.h>
#include "../src/runtime_pad.c"
// runtime_mouse_aim.c (the native aim hook) needs the guest; pad tests run without it.
int runtime_mouse_aim_installed(void) { return 0; }
void runtime_power_wheel_status(IssPowerWheelStatus *out){memset(out,0,sizeof(*out));}
int runtime_menu_scene(void){return ISS_MENU_SCENE_GAMEPLAY;}
void runtime_input_trace_flush(void){}
void runtime_power_wheel_request(int index){(void)index;}
void runtime_power_wheel_visual(int open,int choice){(void)open;(void)choice;}
void tlggpu_power_wheel_settings(int *enabled,int keys[4],int *instant){*enabled=0;*instant=0;memset(keys,0,4*sizeof(*keys));}
int runtime_mouse_aim_motion(float dx, float dy, float sensitivity, int invert_y, int active) { (void)dx; (void)dy; (void)sensitivity; (void)invert_y; (void)active; return 0; }
void runtime_mouse_aim_hold_follow(int on);
void runtime_mouse_aim_hold_follow(int on) { (void)on; }

/* The GPU library (not linked here): the mouse and the input settings. */
void tlggpu_mouse_take(TlgGpuMouse *mouse) { memset(mouse,0,sizeof(*mouse)); }
void tlggpu_mouse_pad_hud(int a, float x, float y, int t, int c) { (void)a; (void)x; (void)y; (void)t; (void)c; }
void tlggpu_input_settings(TlgGpuInputSettings *settings) { memset(settings,0,sizeof(*settings)); }
uint64_t tlggpu_total_draws(void) { return 0; }
void tlggpu_frame_counters(TlgGpuFrameCounters *out) { memset(out,0,sizeof(*out)); }
int tlggpu_overlay_captures_input(void) { return 0; }
void tlggpu_overlay_notice(const char *text, float seconds) { (void)text; (void)seconds; }
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) {
    (void)table; (void)count; (void)name;
    return 0;
}

/* PS4 buttons a host source presses in `map`. */
static uint32_t pressed_by(const ButtonMap *map, int source) {
    uint32_t ps=0;
    for (size_t i=0;i<map->count;++i) if (map->entries[i].source==source) ps|=map->entries[i].ps;
    return ps;
}

int main(void) {
    ButtonMap keys=key_map;
    assert(apply_button_map(&keys,NULL,true)==0 && apply_button_map(&keys,"",true)==0);
    assert(keys.count==key_map.count);

    /* Swap: Q presses Cross, Space presses Triangle; Space no longer presses Cross. */
    assert(apply_button_map(&keys," cross = Q , triangle=Space",true)==2);
    assert(pressed_by(&keys,SDL_SCANCODE_Q)==BTN_CROSS);
    assert(pressed_by(&keys,SDL_SCANCODE_SPACE)==BTN_TRIANGLE);
    assert(pressed_by(&keys,SDL_SCANCODE_E)==BTN_SQUARE);

    /* Names with spaces, any case; the last entry for a button wins. */
    ButtonMap again=key_map;
    assert(apply_button_map(&again,"CIRCLE=left shift,circle=Left Ctrl",true)==2);
    assert(pressed_by(&again,SDL_SCANCODE_LCTRL)==BTN_CIRCLE);
    assert(pressed_by(&again,SDL_SCANCODE_LSHIFT)==0);

    /* Bad entries are skipped and leave the defaults alone. */
    ButtonMap bad=key_map;
    assert(apply_button_map(&bad,"bogus=Q,cross=NotAKey,circle,=Q,square=,,",true)==0);
    assert(bad.count==key_map.count && pressed_by(&bad,SDL_SCANCODE_SPACE)==BTN_CROSS);

    /* Gamepad: Xbox layout swap; 'touchpad' is the left click, Back stays the right click and
     * the real touchpad click keeps its fingers. */
    ButtonMap pad=pad_map;
    assert(apply_button_map(&pad,"cross=b,circle=a,touchpad=misc1",false)==3);
    assert(pressed_by(&pad,SDL_GAMEPAD_BUTTON_EAST)==BTN_CROSS);
    assert(pressed_by(&pad,SDL_GAMEPAD_BUTTON_SOUTH)==BTN_CIRCLE);
    assert(pressed_by(&pad,SDL_GAMEPAD_BUTTON_MISC1)==CLICK_LEFT);
    assert(pressed_by(&pad,SDL_GAMEPAD_BUTTON_BACK)==CLICK_RIGHT);
    assert(pressed_by(&pad,SDL_GAMEPAD_BUTTON_TOUCHPAD)==BTN_TOUCHPAD);
    assert(apply_button_map(&pad,"touchpad_right=guide,touchpad_left=paddle1",false)==2);
    assert(pressed_by(&pad,SDL_GAMEPAD_BUTTON_BACK)==0 && pressed_by(&pad,SDL_GAMEPAD_BUTTON_GUIDE)==CLICK_RIGHT);
    assert(pressed_by(&pad,SDL_GAMEPAD_BUTTON_MISC1)==0 && pressed_by(&pad,SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1)==CLICK_LEFT);
    assert(apply_button_map(&pad,"cross=Space",false)==0); /* a key name is no gamepad button */
    /* Keyboard: each side has a key of its own. */
    assert(pressed_by(&key_map,SDL_SCANCODE_TAB)==CLICK_LEFT && pressed_by(&key_map,SDL_SCANCODE_BACKSPACE)==CLICK_RIGHT);
    ButtonMap sides=key_map;
    assert(apply_button_map(&sides,"touchpad_right=Delete",true)==1);
    assert(pressed_by(&sides,SDL_SCANCODE_BACKSPACE)==0 && pressed_by(&sides,SDL_SCANCODE_DELETE)==CLICK_RIGHT);
    /* Host actions: the interact key, tilt/shake and the stick keys have defaults and remap. */
    assert(pressed_by(&key_map,SDL_SCANCODE_V)==ACT_INTERACT && pressed_by(&key_map,MOUSE_X2)==ACT_INTERACT);
    assert(pressed_by(&key_map,SDL_SCANCODE_T)==ACT_TILT_RIGHT && pressed_by(&key_map,SDL_SCANCODE_Y)==ACT_SHAKE);
    assert(pressed_by(&key_map,SDL_SCANCODE_W)==ACT_LS_UP && pressed_by(&key_map,SDL_SCANCODE_LEFT)==ACT_RS_LEFT);
    ButtonMap acts=key_map;
    assert(apply_button_map(&acts,"interact=E|Mouse X1,tilt_right=Q,ls_up=Up,rs_up=I",true)==4);
    assert(pressed_by(&acts,SDL_SCANCODE_V)==0 && pressed_by(&acts,MOUSE_X2)==0);
    assert(pressed_by(&acts,SDL_SCANCODE_E)==(BTN_SQUARE|ACT_INTERACT) && pressed_by(&acts,MOUSE_X1)==ACT_INTERACT);
    assert(pressed_by(&acts,SDL_SCANCODE_T)==0 && pressed_by(&acts,SDL_SCANCODE_Q)==(BTN_TRIANGLE|ACT_TILT_RIGHT));
    assert(pressed_by(&acts,SDL_SCANCODE_W)==0 && pressed_by(&acts,SDL_SCANCODE_UP)==ACT_LS_UP && pressed_by(&acts,SDL_SCANCODE_I)==(BTN_UP|ACT_RS_UP));
    /* A source the player binds to a button leaves its default host action: r1=Mouse X2 takes X2
     * away from interact, and V stays. */
    ButtonMap x2=key_map;
    assert(apply_button_map(&x2,"r1=Mouse X2",true)==1);
    assert(pressed_by(&x2,MOUSE_X2)==BTN_R1 && pressed_by(&x2,SDL_SCANCODE_V)==ACT_INTERACT);
    /* A button binding after an interact one still wins the source. */
    ButtonMap both=key_map;
    assert(apply_button_map(&both,"interact=V|Mouse X2,r1=Mouse X2",true)==2);
    assert(pressed_by(&both,MOUSE_X2)==BTN_R1);
    /* The gamepad takes interact too. */
    ButtonMap padi=pad_map;
    assert(apply_button_map(&padi,"interact=paddle1",false)==1);
    assert(pressed_by(&padi,SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1)==ACT_INTERACT);

    /* The menu keys (menu_map): Enter / left click select, Esc / right click go back, Q / E and
     * Shift+Tab / Tab are the pause tabs (L2 / R2), F / R triangle / square, arrows the d-pad. */
    static bool kb[SDL_SCANCODE_COUNT];
    const uint32_t mleft=1u<<(MOUSE_LEFT-MOUSE_SOURCE), mright=1u<<(MOUSE_RIGHT-MOUSE_SOURCE);
#define ONLY(code) (memset(kb,0,sizeof(kb)),kb[code]=true)
    ONLY(SDL_SCANCODE_RETURN); assert(menu_keys(kb,0)==BTN_CROSS && map_keys(kb,0)==BTN_OPTIONS);
    ONLY(SDL_SCANCODE_ESCAPE); assert(menu_keys(kb,0)==BTN_CIRCLE && map_keys(kb,0)==BTN_OPTIONS); /* pause in gameplay */
    ONLY(SDL_SCANCODE_Q); assert(menu_keys(kb,0)==BTN_L2 && map_keys(kb,0)==BTN_TRIANGLE);
    ONLY(SDL_SCANCODE_E); assert(menu_keys(kb,0)==BTN_R2);
    ONLY(SDL_SCANCODE_TAB); assert(menu_keys(kb,0)==BTN_R2 && map_keys(kb,0)==CLICK_LEFT);
    ONLY(SDL_SCANCODE_TAB); kb[SDL_SCANCODE_LSHIFT]=true; assert(menu_keys(kb,0)==BTN_L2); /* not circle (Shift) */
    ONLY(SDL_SCANCODE_F); assert(menu_keys(kb,0)==BTN_TRIANGLE && map_keys(kb,0)==BTN_R2);
    ONLY(SDL_SCANCODE_R); assert(menu_keys(kb,0)==BTN_SQUARE);
    ONLY(SDL_SCANCODE_LSHIFT); assert(menu_keys(kb,0)==0); /* circle moved to Esc */
    ONLY(SDL_SCANCODE_UP); assert(menu_keys(kb,0)==BTN_UP && map_keys(kb,0)==ACT_RS_UP);
    ONLY(SDL_SCANCODE_W); assert(menu_keys(kb,0)==ACT_LS_UP); /* untouched gameplay keys stay */
    ONLY(SDL_SCANCODE_1); assert(menu_keys(kb,0)==BTN_L1);
    ONLY(SDL_SCANCODE_P); assert(menu_keys(kb,0)==BTN_OPTIONS);
    memset(kb,0,sizeof(kb));
    assert(menu_keys(kb,mleft)==BTN_CROSS && menu_keys(kb,mright)==BTN_CIRCLE && map_keys(kb,mleft)==BTN_R2);
    assert(menu_keys(kb,1u<<(WHEEL_DOWN-MOUSE_SOURCE))==BTN_DOWN);
    /* Side to side with the mouse: the thumb buttons (X2 is not interact here). */
    assert(menu_keys(kb,1u<<(MOUSE_X1-MOUSE_SOURCE))==BTN_LEFT && menu_keys(kb,1u<<(MOUSE_X2-MOUSE_SOURCE))==BTN_RIGHT);
    /* Remapped: Shift+names parse; cross=Space only. */
    ButtonMap saved=menu_map;
    assert(apply_button_map(&menu_map,"cross=Space,l2=Shift+Q",true)==2);
    ONLY(SDL_SCANCODE_RETURN); assert(menu_keys(kb,0)==0);
    ONLY(SDL_SCANCODE_Q); assert(menu_keys(kb,0)==0); kb[SDL_SCANCODE_LSHIFT]=true; assert(menu_keys(kb,0)==BTN_L2);
    menu_map=saved;
    /* The native action keys: a native key leaves its PS4 button in gameplay (Space is jump, not
     * cross; Esc is pause, not OPTIONS); other gameplay keys stay (W, 1, V). */
    const uint32_t NJ=1u<<ISS_NA_JUMP, NM=1u<<ISS_NA_MELEE, NE=1u<<ISS_NA_EXECUTE;
    ONLY(SDL_SCANCODE_SPACE); assert(map_bits(&native_map,kb,0)==NJ && play_keys(kb,0,true)==0 && play_keys(kb,0,false)==BTN_CROSS);
    ONLY(SDL_SCANCODE_F); assert(map_bits(&native_map,kb,0)==NM && play_keys(kb,0,true)==0);
    ONLY(SDL_SCANCODE_B); assert(map_bits(&native_map,kb,0)==NE);
    ONLY(SDL_SCANCODE_LSHIFT); assert(map_bits(&native_map,kb,0)==(1u<<ISS_NA_DASH));
    ONLY(SDL_SCANCODE_LCTRL); assert(map_bits(&native_map,kb,0)==(1u<<ISS_NA_DROP));
    ONLY(SDL_SCANCODE_C); assert(map_bits(&native_map,kb,0)==(1u<<ISS_NA_DROP));
    ONLY(SDL_SCANCODE_E); assert(map_bits(&native_map,kb,0)==((1u<<ISS_NA_USE)|(1u<<ISS_NA_DRAIN)));
    ONLY(SDL_SCANCODE_ESCAPE); assert(map_bits(&native_map,kb,0)==(1u<<ISS_NA_PAUSE) && play_keys(kb,0,true)==0);
    ONLY(SDL_SCANCODE_T); assert(map_bits(&native_map,kb,0)==(1u<<ISS_NA_INVESTIGATE) && play_keys(kb,0,true)==0);
    ONLY(SDL_SCANCODE_W); assert(map_bits(&native_map,kb,0)==0 && play_keys(kb,0,true)==ACT_LS_UP);
    ONLY(SDL_SCANCODE_1); assert(play_keys(kb,0,true)==BTN_L1);
    ONLY(SDL_SCANCODE_V); assert(play_keys(kb,0,true)==ACT_INTERACT);
    memset(kb,0,sizeof(kb));
    assert(map_bits(&native_map,kb,mleft)==((1u<<ISS_NA_FIRE)|(1u<<ISS_NA_SPRAY)) && play_keys(kb,mleft,true)==0);
    assert(map_bits(&native_map,kb,mright)==(1u<<ISS_NA_AIM));
    /* Remapped by action name. */
    ButtonMap saved_native=native_map;
    assert(apply_native_map("execute=G,jump=Space|Mouse X1")==2);
    ONLY(SDL_SCANCODE_G); assert(map_bits(&native_map,kb,0)==NE);
    ONLY(SDL_SCANCODE_B); assert(map_bits(&native_map,kb,0)==0);
    memset(kb,0,sizeof(kb)); assert(map_bits(&native_map,kb,1u<<(MOUSE_X1-MOUSE_SOURCE))==NJ);
    assert(apply_native_map("execute=none")==1);
    ONLY(SDL_SCANCODE_G); assert(map_bits(&native_map,kb,0)==0);
    native_map=saved_native;
    /* Actions the game still reads as a PS4 button press that button too. */
    assert(native_raw_buttons(1u<<ISS_NA_PAUSE)==BTN_OPTIONS && native_raw_buttons(1u<<ISS_NA_SPRAY)==BTN_R2);
    assert(native_raw_buttons(1u<<ISS_NA_INVESTIGATE)==BTN_UP && native_raw_buttons(NM|NE|NJ)==0);
    /* The game hook takes the held actions and each press once. */
    atomic_store(&native_down,0); atomic_store(&native_latched,NJ);
    assert(runtime_pad_native_take()==NJ && runtime_pad_native_take()==0);
    /* A menu key held when the menu opens stays silent until released (the Enter that paused). */
    ONLY(SDL_SCANCODE_RETURN);
    menu_latch_update(false,kb,0); menu_latch_update(true,kb,mleft);
    assert(menu_latch.keys[SDL_SCANCODE_RETURN] && menu_latch.mouse==mleft);
    memset(kb,0,sizeof(kb)); menu_latch_update(true,kb,0);
    assert(!menu_latch.keys[SDL_SCANCODE_RETURN] && !menu_latch.mouse);
    ONLY(SDL_SCANCODE_W); menu_latch_update(false,kb,0); assert(!menu_latch.keys[SDL_SCANCODE_W]); /* gameplay-only keys */
#undef ONLY
    puts("PASS: pad and key maps");
}
