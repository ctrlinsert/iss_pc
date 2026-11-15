/* New rows in the game's own Options menu (prototype, ISS_GAME_MENU=1).
 *
 * src/iss_menu_script.h has the format. While core_common.sprig.xpps is read, the Options script
 * is patched in guest memory, never on disk:
 *   - the Video gamma row becomes a jump to rows written here (gamma again, then ours) that jump
 *     back after it;
 *   - "get value" and "value changed" get a hook word on their no-op, so our rows show and change
 *     their own values and the game's own rows go on as before.
 * probe.c (install_iss_game_menu) sends the script interpreter's error path to
 * runtime_game_menu_vm_op, so the words only work with that hook installed; without it nothing
 * is patched.
 *
 * Rows so far: Video > HUD (Show / Hide), stored here (runtime_game_menu_hud_shown) and logged.
 * The game's own text ids are used until new text can be added to the language package. */
#include "iss_menu_script.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_COPIES 4 /* the package loaded again (another world) keeps the older copy valid */
#define CODE_SIZE 256

typedef struct {
    uint64_t label;
    const char *name;
    ImsValueList list;
    uint64_t values[4];
    const char *value_names[4];
    unsigned count;
    atomic_uint current;
} Row;

typedef struct {
    const uint8_t *rows, *rows_resume, *get_hook, *get_exit, *set_hook, *code_end;
    uint8_t *code;
} Copy;

static atomic_int enabled;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static Copy copies[MAX_COPIES];
static unsigned next_copy;
static int menu_fd = -1;
static Row rows[1];
static int rows_ready;

static void setup_rows(void) {
    if (rows_ready) return;
    Row *r = &rows[0];
    r->name = "u_options_opt_hud";
    r->label = ims_text_hash(r->name);
    r->value_names[0] = "u_options_val_show";
    r->value_names[1] = "u_options_val_hide";
    r->count = 2;
    for (unsigned i = 0; i < r->count; ++i) r->values[i] = ims_text_hash(r->value_names[i]);
    r->list = (ImsValueList){(uint64_t)(uintptr_t)r->values, r->count, IMS_VALUE_TYPE_TEXT_ID};
    atomic_store(&r->current, 0);
    rows_ready = 1;
}

void runtime_game_menu_enable(void) {
    pthread_mutex_lock(&lock);
    setup_rows();
    pthread_mutex_unlock(&lock);
    atomic_store(&enabled, 1);
}
int runtime_game_menu_hud_shown(void) { return atomic_load(&rows[0].current) == 0; }

static Row *row_of(uint64_t label) {
    for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
        if (rows[i].label == label) return &rows[i];
    return NULL;
}

static int is_menu_package(const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    return !strcmp(base, "core_common.sprig.xpps");
}
void runtime_game_menu_open(int fd, const char *guest_path) {
    if (atomic_load(&enabled) && is_menu_package(guest_path)) {
        pthread_mutex_lock(&lock);
        menu_fd = fd;
        pthread_mutex_unlock(&lock);
    }
}
void runtime_game_menu_close(int fd) {
    pthread_mutex_lock(&lock);
    if (menu_fd == fd) menu_fd = -1;
    pthread_mutex_unlock(&lock);
}
int runtime_game_menu_file(int fd) {
    return atomic_load(&enabled) && fd >= 0 && fd == menu_fd;
}

/* Writes our rows: the replaced gamma row first, then one choice row per Row, then the hook
 * word that returns to the game's code. */
static int emit(uint8_t *code, const ImsSites *s) {
    ImsWriter w = {code, 0, CODE_SIZE, 0};
    ims_put_imm(&w, IMS_PUSH8, s->slider_label);
    ims_put_imm(&w, IMS_PUSH8, s->slider_extra);
    ims_put_call(&w, s->member_slider, IMS_DROP16);
    for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        ims_put_imm(&w, IMS_PUSH8, rows[i].label);
        ims_put_imm(&w, IMS_PUSH_LIST, (uint64_t)(uintptr_t)&rows[i].list);
        ims_put_call(&w, s->member_choice, IMS_DROP16);
    }
    ims_put32(&w, ISS_MENU_WORD(IMS_HOOK_ROWS_END));
    return w.overflow ? -1 : (int)w.at;
}

static void put_word(uint8_t *at, uint32_t word) { memcpy(at, &word, 4); }

/* bytes [offset, offset+size) of core_common were just read into buffer (guest memory). */
void runtime_game_menu_part(int fd, void *buffer, int64_t offset, int64_t size) {
    if (!runtime_game_menu_file(fd) || size < 0x100) return;
    uint8_t *b = buffer;
    const uint64_t gamma = ims_text_hash("u_options_opt_gamma");
    size_t at = (size_t)(-(uintptr_t)b & 7); /* script immediates are 8-byte aligned */
    while (at + 8 <= (size_t)size && memcmp(b + at, &gamma, 8)) at += 8;
    if (at + 8 > (size_t)size) return; /* cheap check first: most parts hold no menu */
    ImsSites s;
    if (!ims_locate(b, (size_t)size, &s)) {
        printf("Game menu: Options script at file offset %lld not recognized; menu unchanged\n", (long long)offset);
        return;
    }
    uint8_t *code = malloc(CODE_SIZE); /* 16-byte aligned on 64-bit hosts */
    if (!code) return;
    const int length = emit(code, &s);
    if (length < 0) { free(code); return; }
    pthread_mutex_lock(&lock);
    Copy *c = &copies[next_copy++ % MAX_COPIES];
    *c = (Copy){b + s.rows, b + s.rows_end, b + s.get_hook, b + s.get_exit, b + s.set_hook, code + length - 4, code};
    put_word(b + s.rows, ISS_MENU_WORD(IMS_HOOK_ROWS));
    put_word(b + s.get_hook, ISS_MENU_WORD(IMS_HOOK_GET));
    put_word(b + s.set_hook, ISS_MENU_WORD(IMS_HOOK_SET));
    pthread_mutex_unlock(&lock);
    printf("Game menu: Options script patched (%u new row%s under Video)\n",
           (unsigned)(sizeof(rows) / sizeof(rows[0])), sizeof(rows) / sizeof(rows[0]) == 1 ? "" : "s");
}

static uint64_t *locals(uint8_t *vm) {
    uint8_t *frame;
    memcpy(&frame, vm + IMS_VM_FRAME, sizeof(frame));
    if (!frame) return NULL;
    uint64_t *data;
    memcpy(&data, frame + IMS_FRAME_LOCALS, sizeof(data));
    return data;
}

/* The interpreter met an undefined opcode at `instr` (its program counter is already past the
 * word). Returns the next program counter for our words, NULL for anything else (the game then
 * reports its own error). */
const uint8_t *runtime_game_menu_vm_op(uint8_t *vm, const uint8_t *instr) {
    if (!atomic_load(&enabled) || ims_u32(instr) != ISS_MENU_WORD(instr[1]) || instr[1] >= IMS_HOOKS) return NULL;
    Copy c = {0};
    pthread_mutex_lock(&lock);
    for (unsigned i = 0; i < MAX_COPIES; ++i)
        if (copies[i].code && (instr == copies[i].rows || instr == copies[i].code_end || instr == copies[i].get_hook ||
                               instr == copies[i].set_hook)) { c = copies[i]; break; }
    pthread_mutex_unlock(&lock);
    if (!c.code) return NULL;
    switch (instr[1]) {
    case IMS_HOOK_ROWS: return instr == c.rows ? c.code : NULL;
    case IMS_HOOK_ROWS_END: return instr == c.code_end ? c.rows_resume : NULL;
    case IMS_HOOK_GET: {
        if (instr != c.get_hook) return NULL;
        uint64_t *data = locals(vm);
        Row *r = data ? row_of(data[0]) : NULL;
        if (!r) return instr + 4;
        data[1] = r->values[atomic_load(&r->current) % r->count];
        return c.get_exit;
    }
    case IMS_HOOK_SET: {
        if (instr != c.set_hook) return NULL;
        uint64_t *data = locals(vm);
        Row *r = data ? row_of(data[0]) : NULL;
        if (r)
            for (unsigned i = 0; i < r->count; ++i)
                if (r->values[i] == data[1] && atomic_exchange(&r->current, i) != i)
                    printf("Game menu: %s = %s\n", r->name, r->value_names[i]);
        return instr + 4;
    }
    }
    return NULL;
}
