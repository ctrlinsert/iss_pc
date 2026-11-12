/* The framing-cone cap (src/iss_native_cone.h): signature guards, and the stub executed with a fake
 * tanf that returns its argument, so the angle the game would take the tangent of is visible. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include "../src/iss_native_cone.h"

static float __attribute__((sysv_abi, noinline)) fake_tanf(float x) { return x; }

int main(void) {
    unsigned char *image=calloc(1,ISS_NATIVE_CONE_TANF+16);
    assert(image && !iss_native_cone_signature(image,ISS_NATIVE_CONE_TANF+16));
    memcpy(image+ISS_NATIVE_CONE_SITE,iss_native_cone_site,sizeof(iss_native_cone_site));
    memcpy(image+ISS_NATIVE_CONE_TANF,iss_native_cone_tanf,2);
    assert(iss_native_cone_signature(image,ISS_NATIVE_CONE_TANF+16));
    for (unsigned i=0;i<sizeof(iss_native_cone_site);++i) {
        image[ISS_NATIVE_CONE_SITE+i]^=1; assert(!iss_native_cone_signature(image,ISS_NATIVE_CONE_TANF+16)); image[ISS_NATIVE_CONE_SITE+i]^=1;
    }
    int32_t rel; memcpy(&rel,iss_native_cone_site+25,4);
    assert(ISS_NATIVE_CONE_RESUME+rel==ISS_NATIVE_CONE_TANF);     /* the call really is tanf's thunk */
    free(image);

#ifdef _WIN32
    unsigned char *code=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE); assert(code);
#else
    unsigned char *code=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); assert(code!=MAP_FAILED);
#endif
    /* float run(float fov, void *camera): the camera pointer in rcx as at the game's site. */
    unsigned char *caller=code+1024, *site=caller+4, *resume=site+ISS_NATIVE_CONE_LENGTH;
    const unsigned char head[]={0x53,0x48,0x89,0xf9};             /* push rbx; mov rcx,rdi */
    const unsigned char tail[]={0x5b,0xc3};                       /* pop rbx; ret */
    memcpy(caller,head,sizeof(head)); memcpy(resume,tail,sizeof(tail));
    unsigned n=iss_native_cone_build_stub(code,(uintptr_t)fake_tanf,(uintptr_t)resume,ISS_NATIVE_CONE_MAX);
    assert(n<=ISS_NATIVE_CONE_MAX_STUB);
    iss_native_cone_build_jump(site,(uintptr_t)code);
#ifdef _WIN32
    DWORD old; assert(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old));
    FlushInstructionCache(GetCurrentProcess(),code,4096);
#else
    assert(!mprotect(code,4096,PROT_READ|PROT_EXEC));
#endif
    float (__attribute__((sysv_abi)) *run)(float,void *)=(float (__attribute__((sysv_abi)) *)(float,void *))caller;
    unsigned char camera[0x600]={0};
    const struct { float aspect, fov, want; } cases[]={
        {16.0f/9.0f, 1.0f, 0.5f*1.0f*(16.0f/9.0f)+0.1f},           /* unchanged */
        {3440.0f/1440.0f, 1.0f, 0.5f*1.0f*(3440.0f/1440.0f)+0.1f},  /* 21:9 unchanged */
        {7680.0f/2160.0f, 1.0f, ISS_NATIVE_CONE_MAX},               /* 32:9: 1.88 capped */
        {5120.0f/1440.0f, 0.6f, 0.5f*0.6f*(5120.0f/1440.0f)+0.1f},  /* narrow FOV on 32:9 fits */
    };
    for (unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        memcpy(camera+0x59c,&cases[i].aspect,4);
        const float got=run(cases[i].fov,camera);
        assert(fabsf(got-cases[i].want)<1e-6f);
        assert(tanf(got)>0.0f);
    }
    puts("PASS: native framing cone (signature, unchanged up to 21:9, 32:9 capped below 90 degrees)");
    return 0;
}
