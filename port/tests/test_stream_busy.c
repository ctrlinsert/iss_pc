/* The skill tree preview stream fix: the stub sets busy on the dequeued stream, keeps the
 * depth store and the unlock-at-depth-0 tail of the original, and resumes after it. The stub
 * runs here for real (x86-64), with the unlock and resume targets pointed at test code. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "iss_stream_busy.h"

static void *unlocked_with;
static int unlocks;
static void unlock(void *mutex) { unlocked_with = mutex; ++unlocks; }

/* run(stub, S, mutex handle, stream, depth): the registers the site sees, then into the stub;
 * the stub's resume jump lands on stream_busy_resume, which returns to the caller. */
void stream_busy_run(const void *stub, void *s, void *mutex, void *stream, uint32_t depth);
void stream_busy_resume(void);
__asm__(".text\n.globl stream_busy_run\nstream_busy_run:\n"
        "push %rbx\npush %r13\npush %r14\n"
        "mov %rsi,%r13\nmov %rdx,%r14\nmov %rcx,%rbx\nmov %r8d,%eax\njmp *%rdi\n"
        ".globl stream_busy_resume\nstream_busy_resume:\n"
        "pop %r14\npop %r13\npop %rbx\nret\n");

int main(void) {
    /* Guard bytes: the original 16-byte tail and the lea of the stream lock. */
    static unsigned char image[ISS_STREAM_BUSY_SITE + 64];
    memcpy(image + ISS_STREAM_BUSY_SITE, iss_stream_busy_site_bytes, 16);
    memcpy(image + ISS_STREAM_BUSY_LOCK_LEA, iss_stream_busy_lea_bytes, 7);
    assert(iss_stream_busy_valid(image, sizeof image));
    assert(!iss_stream_busy_valid(image, ISS_STREAM_BUSY_SITE + 8));
    image[ISS_STREAM_BUSY_SITE + 11] = 0x90;
    assert(!iss_stream_busy_valid(image, sizeof image));
    /* The original call targets the unlock thunk and the jne skips exactly the call. */
    const unsigned char *b = iss_stream_busy_site_bytes;
    int32_t rel; memcpy(&rel, b + 12, 4);
    assert(ISS_STREAM_BUSY_SITE + 16 + rel == ISS_STREAM_BUSY_UNLOCK);
    assert(ISS_STREAM_BUSY_SITE + 8 + b[7] == ISS_STREAM_BUSY_RESUME);
    /* The lea is rip-relative to the lock object at 0x1d1faa0. */
    memcpy(&rel, iss_stream_busy_lea_bytes + 3, 4);
    assert(ISS_STREAM_BUSY_LOCK_LEA + 7 + rel == 0x1d1faa0);

    /* Site: absolute jump to the stub, padded to the 16 replaced bytes. */
    unsigned char site[16];
    iss_stream_busy_site(site, 0x1122334455667788u);
    const unsigned char jump[] = {0xff,0x25,0,0,0,0,0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11,0x90,0x90};
    assert(!memcmp(site, jump, 16));

    /* Stub addresses come from the image base. */
    unsigned char bytes[64];
    size_t n = iss_stream_busy_stub(bytes, 0x800000000u);
    assert(n == 49);
    uint64_t a; memcpy(&a, bytes + 0x19, 8); assert(a == 0x800000000u + ISS_STREAM_BUSY_UNLOCK);
    memcpy(&a, bytes + 0x29, 8); assert(a == 0x800000000u + ISS_STREAM_BUSY_RESUME);

    /* Run it with the targets pointed at the test's unlock and resume. */
    unsigned char *stub = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(stub != MAP_FAILED);
    memcpy(stub, bytes, n);
    uint64_t target = (uint64_t)(uintptr_t)unlock; memcpy(stub + 0x19, &target, 8);
    target = (uint64_t)(uintptr_t)stream_busy_resume; memcpy(stub + 0x29, &target, 8);

    unsigned char lock[0x20], stream[0x2900];
    void *mutex = lock + 0x10;
    /* Outermost exit with a stream: busy set, depth stored, mutex unlocked. */
    memset(lock, 0xaa, sizeof lock); memset(stream, 0, sizeof stream);
    stream_busy_run(stub, lock, mutex, stream, 0);
    assert(stream[ISS_STREAM_BUSY_FLAG] == 1 && stream[ISS_STREAM_BUSY_FLAG - 1] == 0 && stream[ISS_STREAM_BUSY_FLAG + 1] == 0);
    uint32_t depth; memcpy(&depth, lock + 0x18, 4); assert(depth == 0);
    assert(unlocks == 1 && unlocked_with == mutex);
    /* Nested exit: busy set, depth stored, no unlock. */
    memset(stream, 0, sizeof stream);
    stream_busy_run(stub, lock, mutex, stream, 2);
    memcpy(&depth, lock + 0x18, 4); assert(depth == 2);
    assert(stream[ISS_STREAM_BUSY_FLAG] == 1 && unlocks == 1);
    /* Nothing dequeued: no write through rbx, unlock as before. */
    stream_busy_run(stub, lock, mutex, NULL, 0);
    assert(unlocks == 2);
    puts("stream busy: ok");
    return 0;
}
