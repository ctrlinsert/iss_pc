/* Aim assist hook (src/iss_aim_assist.h): the stub runs natively against a fake assist function and
 * a frame laid out like the game's camera turn function; plus the mode and device rules. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include "../src/iss_aim_assist.h"

volatile int32_t runtime_aim_assist_disabled;

static float seen_arg, seen_yaw_before=-1;
static int calls;
/* The game's assist: x87-free System V, rates out through two pointers (r9 and the stack). */
static void __attribute__((sysv_abi, noinline)) fake_assist(float a, long p3, long p4, long p5, long p6,
                                                           long p7, float *yaw, float *pitch) {
    (void)p3; (void)p4; (void)p5; (void)p6; (void)p7;
    seen_arg=a; seen_yaw_before=*yaw; calls++;
    *yaw=1.5f; *pitch=-2.5f;
}

int main(void) {
    /* Signature: the site and the function prologue, at their RVAs. */
    unsigned char *image=calloc(1,0x150000);
    assert(image && !iss_aim_assist_signature(image,0x150000));
    memcpy(image+ISS_AIM_ASSIST_SITE,iss_aim_assist_site,sizeof(iss_aim_assist_site));
    memcpy(image+ISS_AIM_ASSIST_FUNCTION,iss_aim_assist_prologue,sizeof(iss_aim_assist_prologue));
    assert(iss_aim_assist_signature(image,0x150000));
    assert(!iss_aim_assist_signature(image,ISS_AIM_ASSIST_RESUME-1));
    for (unsigned i=0;i<sizeof(iss_aim_assist_site);++i) {
        image[ISS_AIM_ASSIST_SITE+i]^=1; assert(!iss_aim_assist_signature(image,0x150000)); image[ISS_AIM_ASSIST_SITE+i]^=1;
    }
    image[ISS_AIM_ASSIST_FUNCTION+5]^=1; assert(!iss_aim_assist_signature(image,0x150000));
    /* The site's call really targets the function: rel32 from the end of the site. */
    int32_t rel; memcpy(&rel,iss_aim_assist_site+20,4);
    assert(ISS_AIM_ASSIST_RESUME+rel==ISS_AIM_ASSIST_FUNCTION);
    free(image);

    /* Modes and devices. */
    assert(iss_aim_assist_mode(NULL)==ISS_AIM_ASSIST_AUTO && iss_aim_assist_mode("Auto")==ISS_AIM_ASSIST_AUTO);
    assert(iss_aim_assist_mode("ON")==ISS_AIM_ASSIST_ON && iss_aim_assist_mode("1")==ISS_AIM_ASSIST_ON);
    assert(iss_aim_assist_mode("off")==ISS_AIM_ASSIST_OFF && iss_aim_assist_mode("0")==ISS_AIM_ASSIST_OFF);
    assert(iss_aim_assist_mode("banana")==ISS_AIM_ASSIST_AUTO);
    int source=ISS_INPUT_KBM;
    assert(iss_aim_assist_off(ISS_AIM_ASSIST_AUTO,source));                     /* off by default */
    assert(iss_input_source(&source,0,0)==ISS_INPUT_KBM);                       /* nothing: stays */
    assert(iss_input_source(&source,1,0)==ISS_INPUT_PAD && !iss_aim_assist_off(ISS_AIM_ASSIST_AUTO,source));
    assert(iss_input_source(&source,0,0)==ISS_INPUT_PAD);                       /* idle pad keeps it */
    assert(iss_input_source(&source,1,1)==ISS_INPUT_PAD);                       /* both: the pad wins */
    assert(iss_input_source(&source,0,1)==ISS_INPUT_KBM && iss_aim_assist_off(ISS_AIM_ASSIST_AUTO,source));
    assert(!iss_aim_assist_off(ISS_AIM_ASSIST_ON,ISS_INPUT_KBM) && iss_aim_assist_off(ISS_AIM_ASSIST_OFF,ISS_INPUT_PAD));

    /* The stub, executed. The caller below sets up the frame the way the game's turn function
     * does (r9 = &[rsp+0xf0], [rsp] = &[rsp+0xf4], the arg float at [rsp+0x70]), leaves garbage in
     * the rates (the replayed site must zero them), then runs the patched 24-byte site. */
#ifdef _WIN32
    unsigned char *code=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE); assert(code);
#else
    unsigned char *code=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); assert(code!=MAP_FAILED);
#endif
    const unsigned char head[]={
        0x53,                                           /* push rbx */
        0x48,0x81,0xec,0x00,0x01,0x00,0x00,             /* sub rsp,0x100 */
        0xc7,0x44,0x24,0x70,0x00,0x00,0x40,0x40,        /* mov dword [rsp+0x70],3.0f */
        0x4c,0x8d,0x8c,0x24,0xf0,0,0,0,                 /* lea r9,[rsp+0xf0] */
        0x48,0x8d,0x84,0x24,0xf4,0,0,0,                 /* lea rax,[rsp+0xf4] */
        0x48,0x89,0x04,0x24,                            /* mov [rsp],rax */
        0xc7,0x84,0x24,0xf0,0,0,0,0x45,0x23,0x01,0x00}; /* mov dword [rsp+0xf0],garbage */
    const unsigned char tail[]={
        0x48,0x8b,0x84,0x24,0xf0,0,0,0,                 /* mov rax,[rsp+0xf0]: both rates */
        0x48,0x81,0xc4,0x00,0x01,0x00,0x00,             /* add rsp,0x100 */
        0x5b,0xc3};                                     /* pop rbx; ret */
    unsigned char *caller=code+1024, *site=caller+sizeof(head), *resume=site+ISS_AIM_ASSIST_LENGTH;
    memcpy(caller,head,sizeof(head));
    memcpy(resume,tail,sizeof(tail));
    unsigned n=iss_aim_assist_build_stub(code,(uintptr_t)fake_assist,(uintptr_t)&runtime_aim_assist_disabled,(uintptr_t)resume);
    assert(n<=ISS_AIM_ASSIST_MAX_STUB && n<1024);
    iss_aim_assist_build_jump(site,(uintptr_t)code);
#ifdef _WIN32
    DWORD old; assert(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old));
    FlushInstructionCache(GetCurrentProcess(),code,4096);
#else
    assert(!mprotect(code,4096,PROT_READ|PROT_EXEC));
#endif
    uint64_t (__attribute__((sysv_abi)) *run)(void)=(uint64_t (__attribute__((sysv_abi)) *)(void))caller;
    float rates[2];

    runtime_aim_assist_disabled=0;  /* controller: the game's rates pass through */
    uint64_t r=run(); memcpy(rates,&r,8);
    assert(calls==1 && seen_arg==3.0f && seen_yaw_before==0.0f);
    assert(rates[0]==1.5f && rates[1]==-2.5f);

    runtime_aim_assist_disabled=1;  /* keyboard and mouse: the assist still runs, its rates are dropped */
    seen_yaw_before=-1;
    r=run(); memcpy(rates,&r,8);
    assert(calls==2 && seen_yaw_before==0.0f);
    assert(rates[0]==0.0f && rates[1]==0.0f);

    puts("PASS: aim assist hook (signature, modes, device switching, executed stub on and off)");
    return 0;
}
