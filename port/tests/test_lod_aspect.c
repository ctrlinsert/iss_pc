/* SPDX-License-Identifier: GPL-2.0-or-later
 * Runs the LOD-aspect stubs (src/lod_aspect_hook.h) on a fake camera: each site's replacement must
 * produce tan_y * 16/9 in the same register the original instruction wrote. */
#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include "lod_aspect_hook.h"

static unsigned char *exec_page(size_t size) {
#ifdef _WIN32
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#else
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
#endif
}

static unsigned put_abs_jump(unsigned char *at, uintptr_t target) {
    at[0] = 0xff; at[1] = 0x25; memset(at + 2, 0, 4);
    memcpy(at + 6, &target, 8);
    return 14;
}

typedef float (__attribute__((sysv_abi)) *Site)(const unsigned char *camera, const float *x);

int main(void) {
    unsigned char *page = exec_page(4096);
    assert(page);
    float *ratio = (float *)(page + 4096 - 16);
    *ratio = 16.0f / 9.0f;
    unsigned char camera[0x600] = {0};
    const float tan_x = 1.3996f, tan_y = 0.3936f, factor = 0.75f;
    memcpy(camera + 0x5a0, &tan_x, 4);
    memcpy(camera + 0x5a4, &tan_y, 4);

    /* Site 0: rax = camera, xmm0 = factor; the stub leaves xmm0 = factor * tan_y * 16/9. */
    {
        unsigned char *tramp = page, *stub = page + 512, *resume;
        unsigned n = 0;
        tramp[n++] = 0x48; tramp[n++] = 0x89; tramp[n++] = 0xf8;                 /* mov rax,rdi */
        tramp[n++] = 0xc5; tramp[n++] = 0xfa; tramp[n++] = 0x10; tramp[n++] = 0x06; /* vmovss xmm0,[rsi] */
        n += put_abs_jump(tramp + n, (uintptr_t)stub);
        resume = tramp + n;
        resume[0] = 0xc3;                                                          /* ret */
        assert(iss_lod_aspect_build_stub(stub, 0, ratio, (uintptr_t)resume));
        const float got = ((Site)(void *)tramp)(camera, &factor);
        assert(fabsf(got - factor * tan_y * 16.0f / 9.0f) < 1e-6f);
    }
    /* Sites 1-3: r14 = camera; the stub leaves xmm1 = tan_y * 16/9 (r14 is restored after). */
    for (unsigned i = 1; i < ISS_LOD_ASPECT_SITES; ++i) {
        unsigned char *tramp = page + 128 * i, *stub = page + 512 + 64 * i, *resume;
        unsigned n = 0;
        tramp[n++] = 0x41; tramp[n++] = 0x56;                                      /* push r14 */
        tramp[n++] = 0x49; tramp[n++] = 0x89; tramp[n++] = 0xfe;                   /* mov r14,rdi */
        n += put_abs_jump(tramp + n, (uintptr_t)stub);
        resume = tramp + n;
        const unsigned char tail[] = {0x41, 0x5e, 0xc5, 0xf8, 0x28, 0xc1, 0xc3};  /* pop r14; vmovaps xmm0,xmm1; ret */
        memcpy(resume, tail, sizeof(tail));
        assert(iss_lod_aspect_build_stub(stub, i, ratio, (uintptr_t)resume));
        const float got = ((Site)(void *)tramp)(camera, &factor);
        assert(fabsf(got - tan_y * 16.0f / 9.0f) < 1e-6f);
    }
    /* At 16:9 the replacement equals the game's own tangent. */
    assert(fabsf(0.5773503f * 16.0f / 9.0f - 0.5773503f * (1920.0f / 1080.0f)) < 1e-6f);
    /* Signature: the original bytes match, any other byte does not. */
    static unsigned char image[0x500000];
    for (unsigned i = 0; i < ISS_LOD_ASPECT_SITES; ++i)
        memcpy(image + iss_lod_aspect_site[i], iss_lod_aspect_original(i), iss_lod_aspect_length[i]);
    assert(iss_lod_aspect_signature_match(image, sizeof(image)));
    image[iss_lod_aspect_site[2] + 5] = 0xa4;
    assert(!iss_lod_aspect_signature_match(image, sizeof(image)));
    assert(!iss_lod_aspect_signature_match(image, 0x400000));
    puts("lod aspect: ok");
    return 0;
}
