/* inFAMOUS Second Son 1.07 (US CUSA00223 / EU CUSA00004): extra opcodes for the game's script
 * interpreter, for new rows in its own menus (src/iss_menu_script.h, runtime_game_menu.c).
 *
 * The interpreter (va 0x1aeae0) dispatches the low byte of each instruction word through a
 * table of 0xaf entries; a larger opcode jumps to its error path at ISS_MENU_VM_ERROR, with
 * r12 = the instruction, r15 = the interpreter state, its program counter already past the word.
 * The 8 bytes there (mov byte [r15+0xe0],1) become a jump to a stub that asks the host:
 *   handled -> store the returned program counter, resume at the normal next-instruction
 *              check (ISS_MENU_VM_NEXT) with rdi = the dispatch table, as the handlers leave it;
 *   not ours -> the original store, then the rest of the error path.
 * Only opcode 0xf0 reaches the host; everything else is untouched. */
#ifndef ISS_MENU_VM_HOOK_H
#define ISS_MENU_VM_HOOK_H
#include <stdint.h>
#include <string.h>

#define ISS_MENU_VM_DISPATCH 0x1aeb40u
#define ISS_MENU_VM_ERROR 0x1b1fc0u
#define ISS_MENU_VM_ERROR_REST 0x1b1fc8u
#define ISS_MENU_VM_NEXT 0x1b1db0u
#define ISS_MENU_VM_TABLE 0x99a528u
#define ISS_MENU_VM_STUB_SIZE 128u

/* Dispatch: pc = [r15+0x20]; [r15+0x20] = pc+4; op = byte [pc]; op > 0xae -> error path;
 * lea of the table at 0x1aeb1c. */
static const unsigned char iss_menu_vm_dispatch[] = {
    0x4d,0x8b,0x67,0x20, 0x4d,0x8d,0x74,0x24,0x04, 0x4d,0x89,0x77,0x20, 0x41,0x0f,0xb6,0x04,0x24,
    0x48,0x3d,0xae,0x00,0x00,0x00, 0x0f,0x87,0x62,0x34,0x00,0x00};
static const unsigned char iss_menu_vm_table_lea[] = {0x48,0x8d,0x3d,0x05,0xba,0x7e,0x00};
#define ISS_MENU_VM_TABLE_LEA 0x1aeb1cu
static const unsigned char iss_menu_vm_error[] = {0x41,0xc6,0x87,0xe0,0x00,0x00,0x00,0x01};
/* Normal next instruction: cmp byte [r15+0xe0],0 */
static const unsigned char iss_menu_vm_next[] = {0x41,0x80,0xbf,0xe0,0x00,0x00,0x00,0x00};

static inline int iss_menu_vm_signature(const unsigned char *image, uint64_t size) {
    return size > ISS_MENU_VM_TABLE + 0xaf * 4 &&
           !memcmp(image + ISS_MENU_VM_DISPATCH, iss_menu_vm_dispatch, sizeof(iss_menu_vm_dispatch)) &&
           !memcmp(image + ISS_MENU_VM_TABLE_LEA, iss_menu_vm_table_lea, sizeof(iss_menu_vm_table_lea)) &&
           !memcmp(image + ISS_MENU_VM_ERROR, iss_menu_vm_error, sizeof(iss_menu_vm_error)) &&
           !memcmp(image + ISS_MENU_VM_NEXT, iss_menu_vm_next, sizeof(iss_menu_vm_next));
}

static inline unsigned iss_menu_vm_put(unsigned char *p, unsigned n, const void *bytes, unsigned count) {
    memcpy(p + n, bytes, count);
    return n + count;
}
/* host(vm, instr) returns the next program counter or NULL; both argument registers are loaded
 * for the SysV and the Windows x64 conventions. */
static inline unsigned iss_menu_vm_build_stub(unsigned char *p, uintptr_t host, uintptr_t image) {
    unsigned n = 0;
    static const unsigned char head[] = {
        0x41,0x80,0x3c,0x24,0xf0,           /* cmp byte [r12],0xf0 */
        0x75,0x00,                          /* jne original (patched below) */
        0x48,0x81,0xec,0xa0,0x00,0x00,0x00, /* sub rsp,0xa0: red zone + shadow space, keeps alignment */
        0x4c,0x89,0xff, 0x4c,0x89,0xe6,     /* mov rdi,r15; mov rsi,r12 */
        0x4c,0x89,0xf9, 0x4c,0x89,0xe2,     /* mov rcx,r15; mov rdx,r12 */
        0x48,0xb8};                         /* mov rax,host */
    n = iss_menu_vm_put(p, n, head, sizeof(head));
    n = iss_menu_vm_put(p, n, &host, 8);
    static const unsigned char call[] = {
        0xff,0xd0,                          /* call rax */
        0x48,0x81,0xc4,0xa0,0x00,0x00,0x00, /* add rsp,0xa0 */
        0x48,0x85,0xc0,                     /* test rax,rax */
        0x74,0x00,                          /* jz original (patched below) */
        0x49,0x89,0x47,0x20,                /* mov [r15+0x20],rax */
        0x48,0xbf};                         /* mov rdi,table */
    const unsigned jz = n + 12;
    n = iss_menu_vm_put(p, n, call, sizeof(call));
    const uintptr_t table = image + ISS_MENU_VM_TABLE, next = image + ISS_MENU_VM_NEXT,
                    rest = image + ISS_MENU_VM_ERROR_REST;
    n = iss_menu_vm_put(p, n, &table, 8);
    static const unsigned char jump[] = {0xff,0x25,0,0,0,0};
    n = iss_menu_vm_put(p, n, jump, sizeof(jump));
    n = iss_menu_vm_put(p, n, &next, 8);
    const unsigned original = n;
    p[6] = (unsigned char)(original - 7);
    p[jz + 1] = (unsigned char)(original - (jz + 2));
    n = iss_menu_vm_put(p, n, iss_menu_vm_error, sizeof(iss_menu_vm_error));
    n = iss_menu_vm_put(p, n, jump, sizeof(jump));
    n = iss_menu_vm_put(p, n, &rest, 8);
    return n;
}
#endif
