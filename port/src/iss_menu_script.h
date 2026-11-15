/* Game menu rows: the format side (no game data here; everything is found in the player's own
 * core_common.sprig.xpps as the game reads it).
 *
 * The Options menu is not a layout file. A script function builds it, one row per call, and the
 * game's script interpreter runs it (eboot va 0x1aeae0, US/EU 1.07). That interpreter is a stack
 * machine over 32-bit instruction words; the low byte is the opcode, 0x00..0xae are defined and
 * anything above takes the interpreter's error path. Instructions used here:
 *   00080801 [pad] <u64>   push an 8-byte immediate (the immediate is 8-byte aligned in memory)
 *   0000008b [pad] <ptr>   push a value list made from {u64 *items, u64 count, u64 7}
 *   00001821               reserve 24 bytes (a function value)
 *   08011807 <i32 member>  copy the 24-byte member function at <member> of the script object
 *   0000160d, 00001824     take the function value and call it
 *   00000820 / 00001020    drop the 8 or 16 argument bytes afterwards
 * A row is: push <label text id>; for a choice row also push its value list; then call the
 * category (1 argument) or choice (2 arguments) helper. Text ids are the gametext hashes
 * (h = h*101 + byte, see ims_text_hash).
 *
 * Two more script functions of the same object matter: "get value" (label -> value text id,
 * the menu calls it to show a row) and "value changed" (label, value). Both bind their arguments
 * to locals 0 and 8 and then have a no-op word, where a hook can run.
 *
 * Hooks are extra opcodes (ISS_MENU_OP): probe.c sends the interpreter's error path to
 * runtime_game_menu.c, which handles our words at the addresses it patched and leaves every
 * other error to the game. */
#ifndef ISS_MENU_SCRIPT_H
#define ISS_MENU_SCRIPT_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Our opcode word: 0xf0 (undefined in the game's interpreter), hook index in byte 1, "IS". */
#define ISS_MENU_OP 0xf0u
#define ISS_MENU_WORD(index) (ISS_MENU_OP | (uint32_t)(index) << 8 | 0x53490000u)
enum { IMS_HOOK_ROWS = 0, IMS_HOOK_ROWS_END = 1, IMS_HOOK_GET = 2, IMS_HOOK_SET = 3, IMS_HOOKS = 4 };

/* Interpreter state (eboot 1.07): +0x20 program counter, +0x78 current frame; frame +0x48 its
 * locals. */
#define IMS_VM_PC 0x20u
#define IMS_VM_FRAME 0x78u
#define IMS_FRAME_LOCALS 0x48u

#define IMS_PUSH8 0x00080801u
#define IMS_PUSH_LIST 0x0000008bu
#define IMS_RESERVE_FN 0x00001821u
#define IMS_LOAD_MEMBER_FN 0x08011807u
#define IMS_TAKE_FN 0x0000160du
#define IMS_CALL_FN 0x00001824u
#define IMS_DROP8 0x00000820u
#define IMS_DROP16 0x00001020u
#define IMS_VALUE_TYPE_TEXT_ID 7u

static inline uint64_t ims_text_hash(const char *s) {
    uint64_t h = 0;
    while (*s) h = h * 101 + (unsigned char)*s++;
    return h;
}
static inline uint32_t ims_u32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline uint64_t ims_u64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }
/* Where an 8-byte immediate goes for the instruction word at address `at`. */
static inline uintptr_t ims_imm_at(uintptr_t at) { return (at + 4 + 7) & ~(uintptr_t)7; }

/* Everything the patch needs, as offsets into the buffer that was searched. */
typedef struct {
    size_t rows;            /* first word of the row to replace (the Video gamma slider) */
    size_t rows_end;        /* first word after it */
    uint64_t slider_label, slider_extra; /* the replaced row, emitted again first */
    uint32_t member_category, member_choice, member_slider;
    size_t get_hook, get_exit;  /* "get value": the no-op word, and the code that returns local 8 */
    size_t set_hook;            /* "value changed": the no-op word */
} ImsSites;

/* A call tail at b+at: reserve, load member, take, call, drop. Returns the member, 0 if not. */
static inline uint32_t ims_call_tail(const uint8_t *b, size_t n, size_t at, uint32_t drop) {
    if (at + 24 > n) return 0;
    const uint8_t *p = b + at;
    if (ims_u32(p) != IMS_RESERVE_FN || ims_u32(p + 4) != IMS_LOAD_MEMBER_FN || ims_u32(p + 12) != IMS_TAKE_FN ||
        ims_u32(p + 16) != IMS_CALL_FN || ims_u32(p + 20) != drop) return 0;
    const uint32_t member = ims_u32(p + 8);
    return member && member < 0x10000 && !(member & 7) ? member : 0;
}
/* A push of `value` at b+at: the instruction length, 0 if not. */
static inline size_t ims_push_at(const uint8_t *b, size_t n, size_t at, uint32_t word, uint64_t *value) {
    if (at + 4 > n || ims_u32(b + at) != word) return 0;
    const size_t imm = (size_t)(ims_imm_at((uintptr_t)(b + at)) - (uintptr_t)b);
    if (imm + 8 > n) return 0;
    if (imm != at + 4 && ims_u32(b + at + 4)) return 0; /* the padding word is zero */
    *value = ims_u64(b + imm);
    return imm + 8 - at;
}
/* Finds the label push of a row whose label is `label`: offset of its word, or SIZE_MAX. */
static inline size_t ims_find_label(const uint8_t *b, size_t n, uint64_t label, size_t from) {
    for (size_t at = (from + 3) & ~(size_t)3; at + 16 <= n; at += 4) {
        uint64_t v;
        if (ims_u32(b + at) == IMS_PUSH8 && ims_push_at(b, n, at, IMS_PUSH8, &v) && v == label) return at;
    }
    return SIZE_MAX;
}
static inline size_t ims_find_words(const uint8_t *b, size_t n, size_t from, size_t to,
                                    const uint32_t *words, size_t count) {
    if (to > n) to = n;
    for (size_t at = (from + 3) & ~(size_t)3; at + 4 * count <= to; at += 4) {
        size_t i = 0;
        while (i < count && ims_u32(b + at + 4 * i) == words[i]) ++i;
        if (i == count) return at;
    }
    return SIZE_MAX;
}

/* Searches one buffer (b is where it is in memory: alignment matters). 1 when every site was
 * found, each one exactly as expected. */
static inline int ims_locate(const uint8_t *b, size_t n, ImsSites *out) {
    memset(out, 0, sizeof(*out));
    const uint64_t gamma = ims_text_hash("u_options_opt_gamma");
    const uint64_t video = ims_text_hash("u_options_cat_video");
    const uint64_t subtitles = ims_text_hash("u_options_opt_subtitles");
    /* Category row: push label, call. */
    size_t at = ims_find_label(b, n, video, 0);
    if (at == SIZE_MAX) return 0;
    uint64_t v;
    size_t len = ims_push_at(b, n, at, IMS_PUSH8, &v);
    if (!(out->member_category = ims_call_tail(b, n, at + len, IMS_DROP8))) return 0;
    /* The Video category has one row, the gamma slider: push label, push extra, call. */
    at = ims_find_label(b, n, gamma, at);
    if (at == SIZE_MAX) return 0;
    out->rows = at;
    at += ims_push_at(b, n, at, IMS_PUSH8, &out->slider_label);
    if (!(len = ims_push_at(b, n, at, IMS_PUSH8, &out->slider_extra))) return 0;
    at += len;
    if (!(out->member_slider = ims_call_tail(b, n, at, IMS_DROP16))) return 0;
    out->rows_end = at + 24;
    /* A choice row (subtitles): push label, push value list, call. Written before gamma. */
    at = ims_find_label(b, n, subtitles, out->rows > 0x2000 ? out->rows - 0x2000 : 0);
    if (at == SIZE_MAX || at > out->rows) return 0;
    at += ims_push_at(b, n, at, IMS_PUSH8, &v);
    if (!(len = ims_push_at(b, n, at, IMS_PUSH_LIST, &v))) return 0;
    if (!(out->member_choice = ims_call_tail(b, n, at + len, IMS_DROP16))) return 0;
    if (out->member_choice == out->member_category || out->member_choice == out->member_slider) return 0;
    /* Handlers follow the menu function in the same script. "get value": bind label to local 0,
     * no-op, local 8 = -1 (no value); returns local 8. */
    const size_t window = out->rows_end + 0x4000;
    static const uint32_t get_entry[] = {0x07080803u, 0, 0};
    for (at = out->rows_end;; at += 4) {
        at = ims_find_words(b, n, at, window, get_entry, 3);
        if (at == SIZE_MAX) return 0;
        if (ims_push_at(b, n, at + 12, IMS_PUSH8, &v) && v == UINT64_MAX) break;
    }
    out->get_hook = at + 8;
    static const uint32_t get_return[] = {0x00000821u, 0x08000807u, 0x00000008u, 0x07081026u};
    out->get_exit = ims_find_words(b, n, out->get_hook, window, get_return, 4);
    if (out->get_exit == SIZE_MAX) return 0;
    /* "value changed": bind label to local 0 and value to local 8, no-op. */
    static const uint32_t set_entry[] = {0x07100803u, 0, 0x07080803u, 8, 0};
    at = ims_find_words(b, n, out->get_exit, window, set_entry, 5);
    if (at == SIZE_MAX) return 0;
    out->set_hook = at + 16;
    return 1;
}

/* Code writer: emits at the real address (alignment of immediates depends on it). */
typedef struct { uint8_t *code; size_t at, cap; int overflow; } ImsWriter;
static inline void ims_put32(ImsWriter *w, uint32_t v) {
    if (w->at + 4 > w->cap) { w->overflow = 1; return; }
    memcpy(w->code + w->at, &v, 4);
    w->at += 4;
}
static inline void ims_put_imm(ImsWriter *w, uint32_t word, uint64_t imm) {
    ims_put32(w, word);
    if (((uintptr_t)(w->code + w->at)) & 7) ims_put32(w, 0);
    ims_put32(w, (uint32_t)imm);
    ims_put32(w, (uint32_t)(imm >> 32));
}
static inline void ims_put_call(ImsWriter *w, uint32_t member, uint32_t drop) {
    ims_put32(w, IMS_RESERVE_FN);
    ims_put32(w, IMS_LOAD_MEMBER_FN);
    ims_put32(w, member);
    ims_put32(w, IMS_TAKE_FN);
    ims_put32(w, IMS_CALL_FN);
    ims_put32(w, drop);
}
/* A value list as the push-list instruction reads it. */
typedef struct { uint64_t items, count, type; } ImsValueList;

/* src/runtime_game_menu.c */
void runtime_game_menu_enable(void);
void runtime_game_menu_open(int fd, const char *guest_path);
void runtime_game_menu_part(int fd, void *buffer, int64_t offset, int64_t size);
void runtime_game_menu_close(int fd);
int runtime_game_menu_file(int fd);
const uint8_t *runtime_game_menu_vm_op(uint8_t *vm, const uint8_t *instr);
int runtime_game_menu_hud_shown(void);
#endif
