/* Game menu rows (src/iss_menu_script.h, src/runtime_game_menu.c) on a synthetic Options script:
 * no game data. Build: build.sh --test. */
#include "iss_menu_script.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CATEGORY 0x218u
#define CHOICE 0x248u
#define SLIDER 0x260u

static uint64_t list_storage[3];

/* category Video, choice Subtitles, slider Gamma, then "get value" and "value changed". */
static size_t script(uint8_t *b, size_t cap, size_t *gamma, size_t *get_nop, size_t *get_exit, size_t *set_nop) {
    ImsWriter w = {b, 0, cap, 0};
    ims_put32(&w, 0x00000008u); /* some other code first */
    ims_put_imm(&w, IMS_PUSH8, ims_text_hash("u_options_cat_language"));
    ims_put_call(&w, CATEGORY, IMS_DROP8);
    ims_put_imm(&w, IMS_PUSH8, ims_text_hash("u_options_opt_subtitles"));
    ims_put_imm(&w, IMS_PUSH_LIST, (uint64_t)(uintptr_t)list_storage);
    ims_put_call(&w, CHOICE, IMS_DROP16);
    ims_put_imm(&w, IMS_PUSH8, ims_text_hash("u_options_cat_video"));
    ims_put_call(&w, CATEGORY, IMS_DROP8);
    *gamma = w.at;
    ims_put_imm(&w, IMS_PUSH8, ims_text_hash("u_options_opt_gamma"));
    ims_put_imm(&w, IMS_PUSH8, 0x1234);
    ims_put_call(&w, SLIDER, IMS_DROP16);
    ims_put32(&w, 0x00000121u); /* what follows the rows */
    ims_put32(&w, 0x00000026u);
    /* get value */
    ims_put32(&w, 0x00100008u);
    ims_put32(&w, 0x07080803u);
    ims_put32(&w, 0);
    *get_nop = w.at;
    ims_put32(&w, 0);
    ims_put_imm(&w, IMS_PUSH8, UINT64_MAX);
    ims_put32(&w, 0x00000705u);
    ims_put32(&w, 8);
    *get_exit = w.at;
    ims_put32(&w, 0x00000821u);
    ims_put32(&w, 0x08000807u);
    ims_put32(&w, 0x00000008u);
    ims_put32(&w, 0x07081026u);
    /* value changed */
    ims_put32(&w, 0x00100008u);
    ims_put32(&w, 0x07100803u);
    ims_put32(&w, 0);
    ims_put32(&w, 0x07080803u);
    ims_put32(&w, 8);
    *set_nop = w.at;
    ims_put32(&w, 0);
    ims_put32(&w, 0x00001026u);
    assert(!w.overflow);
    return w.at;
}

int main(void) {
    assert(ims_text_hash("") == 0 && ims_text_hash("a") == 'a' && ims_text_hash("ab") == 'a' * 101 + 'b');
    uint8_t *b = calloc(1, 4096);
    size_t gamma, get_nop, get_exit, set_nop;
    const size_t n = script(b, 4096, &gamma, &get_nop, &get_exit, &set_nop);

    ImsSites s;
    assert(ims_locate(b, n, &s));
    assert(s.rows == gamma && s.member_category == CATEGORY && s.member_choice == CHOICE && s.member_slider == SLIDER);
    assert(s.slider_label == ims_text_hash("u_options_opt_gamma") && s.slider_extra == 0x1234);
    assert(s.get_hook == get_nop && s.get_exit == get_exit && s.set_hook == set_nop);
    uint8_t *broken = malloc(4096);
    memcpy(broken, b, n);
    broken[get_exit] = 0x22; /* a different function: not recognized, nothing patched */
    assert(!ims_locate(broken, n, &s));

    /* Disabled (no interpreter hook): the file is left alone. */
    uint8_t *original = malloc(4096);
    memcpy(original, b, n);
    runtime_game_menu_open(5, "/app0/art/cache/core_common.sprig.xpps");
    runtime_game_menu_part(5, b, 0, (int64_t)n);
    assert(!memcmp(b, original, n));
    runtime_game_menu_close(5);

    runtime_game_menu_enable();
    runtime_game_menu_open(6, "/app0/art/cache/lang_english_text.xpps");
    assert(!runtime_game_menu_file(6));
    runtime_game_menu_open(5, "/app0/art/cache/core_common.sprig.xpps");
    assert(runtime_game_menu_file(5));
    runtime_game_menu_part(5, b, 0, (int64_t)n);
    runtime_game_menu_close(5);
    assert(!runtime_game_menu_file(5));
    assert(ims_u32(b + gamma) == ISS_MENU_WORD(IMS_HOOK_ROWS));
    assert(ims_u32(b + get_nop) == ISS_MENU_WORD(IMS_HOOK_GET));
    assert(ims_u32(b + set_nop) == ISS_MENU_WORD(IMS_HOOK_SET));

    /* Interpreter state: +0x78 frame, frame +0x48 locals. */
    uint8_t vm[0x100] = {0}, frame[0x100] = {0};
    uint64_t locals[2] = {0};
    uint8_t *fp = frame;
    uint64_t *lp = locals;
    memcpy(vm + IMS_VM_FRAME, &fp, 8);
    memcpy(frame + IMS_FRAME_LOCALS, &lp, 8);

    /* Rows: gamma again, then HUD (show/hide), then back after gamma. */
    const uint8_t *code = runtime_game_menu_vm_op(vm, b + gamma);
    assert(code && code != b + gamma);
    uint64_t v;
    size_t at = 0, len;
    const size_t cap = 256;
    assert((len = ims_push_at(code, cap, at, IMS_PUSH8, &v)) && v == ims_text_hash("u_options_opt_gamma"));
    at += len;
    assert((len = ims_push_at(code, cap, at, IMS_PUSH8, &v)) && v == 0x1234);
    at += len;
    assert(ims_call_tail(code, cap, at, IMS_DROP16) == SLIDER);
    at += 24;
    assert((len = ims_push_at(code, cap, at, IMS_PUSH8, &v)) && v == ims_text_hash("u_options_opt_hud"));
    at += len;
    assert((len = ims_push_at(code, cap, at, IMS_PUSH_LIST, &v)));
    const ImsValueList *list = (const ImsValueList *)(uintptr_t)v;
    assert(list->count == 2 && list->type == IMS_VALUE_TYPE_TEXT_ID);
    const uint64_t *values = (const uint64_t *)(uintptr_t)list->items;
    assert(values[0] == ims_text_hash("u_options_val_show") && values[1] == ims_text_hash("u_options_val_hide"));
    at += len;
    assert(ims_call_tail(code, cap, at, IMS_DROP16) == CHOICE);
    at += 24;
    assert(ims_u32(code + at) == ISS_MENU_WORD(IMS_HOOK_ROWS_END));
    assert(runtime_game_menu_vm_op(vm, code + at) == b + s.rows_end);

    /* get value: ours answers and returns; the game's own rows go on. */
    locals[0] = ims_text_hash("u_options_opt_hud");
    locals[1] = 0;
    assert(runtime_game_menu_vm_op(vm, b + get_nop) == b + get_exit);
    assert(locals[1] == ims_text_hash("u_options_val_show"));
    locals[0] = ims_text_hash("u_options_opt_subtitles");
    locals[1] = 0;
    assert(runtime_game_menu_vm_op(vm, b + get_nop) == b + get_nop + 4 && locals[1] == 0);

    /* value changed */
    assert(runtime_game_menu_hud_shown());
    locals[0] = ims_text_hash("u_options_opt_hud");
    locals[1] = ims_text_hash("u_options_val_hide");
    assert(runtime_game_menu_vm_op(vm, b + set_nop) == b + set_nop + 4);
    assert(!runtime_game_menu_hud_shown());
    locals[0] = ims_text_hash("u_options_opt_hud");
    assert(runtime_game_menu_vm_op(vm, b + get_nop) == b + get_exit);
    assert(locals[1] == ims_text_hash("u_options_val_hide"));

    /* Anything else is the game's own error. */
    assert(!runtime_game_menu_vm_op(vm, b + gamma + 4));
    uint32_t stray = ISS_MENU_WORD(IMS_HOOK_GET);
    assert(!runtime_game_menu_vm_op(vm, (const uint8_t *)&stray));
    uint32_t other = 0x000000f1u;
    assert(!runtime_game_menu_vm_op(vm, (const uint8_t *)&other));
    puts("Game menu: locate, patch, rows, get/set hooks PASS");
    free(b);
    free(broken);
    free(original);
    return 0;
}
