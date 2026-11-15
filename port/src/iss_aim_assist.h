/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Aim assist follows the input device (1.07: CUSA00223 US / CUSA00004 EU, same executable).
 *
 * The game has no aim assist option. Each camera update, its turn function (RVA 0x14d6f0) calls
 * the aim assist (RVA 0x14a810, the function that reads the AIM_MAP_* / AIM_BOOST_* / AIM_INIT_*
 * tuning). That call returns two angular rates in the caller's frame, [rsp+0xf0] (yaw) and
 * [rsp+0xf4] (pitch), which are zeroed right before it and then added to the stick's turn rate.
 * The hook replays the 24 bytes at the call site in a stub, calls the assist as before, and
 * zeroes those two rates while `*off` is not 0. The assist's own state still updates, so turning it
 * back on mid-fight resumes cleanly.
 *
 * TLG_AIM_ASSIST (or ISS_AIM_ASSIST): auto (default) = off while the keyboard and mouse are in
 * use, on from the first controller input until the keyboard or mouse is used again; on = always
 * (the original game); off = never. */
#ifndef ISS_AIM_ASSIST_H
#define ISS_AIM_ASSIST_H
#include <stdint.h>
#include <string.h>

#define ISS_AIM_ASSIST_SITE 0x14d98au   /* vxorps; vmovups [rsp+0xf0]; vmovss xmm0,[rsp+0x70]; call */
#define ISS_AIM_ASSIST_LENGTH 24u
#define ISS_AIM_ASSIST_RESUME (ISS_AIM_ASSIST_SITE+ISS_AIM_ASSIST_LENGTH)
#define ISS_AIM_ASSIST_FUNCTION 0x14a810u
#define ISS_AIM_ASSIST_MAX_STUB 96u

static const unsigned char iss_aim_assist_site[ISS_AIM_ASSIST_LENGTH]={
    0xc5,0xf8,0x57,0xc0,                          /* vxorps xmm0,xmm0,xmm0 */
    0xc5,0xf8,0x11,0x84,0x24,0xf0,0,0,0,          /* vmovups [rsp+0xf0],xmm0: the rates start at 0 */
    0xc5,0xfa,0x10,0x44,0x24,0x70,                /* vmovss xmm0,[rsp+0x70] */
    0xe8,0x6e,0xce,0xff,0xff};                    /* call 0x14a810 (rel32 from 0x14d9a2) */
static const unsigned char iss_aim_assist_prologue[]={
    0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,
    0x48,0x81,0xec,0xb8,0x02,0x00,0x00};          /* push rbp ... sub rsp,0x2b8 */

static inline int iss_aim_assist_signature(const unsigned char *image, uint64_t size) {
    return image && size>=ISS_AIM_ASSIST_RESUME &&
        !memcmp(image+ISS_AIM_ASSIST_SITE,iss_aim_assist_site,sizeof(iss_aim_assist_site)) &&
        !memcmp(image+ISS_AIM_ASSIST_FUNCTION,iss_aim_assist_prologue,sizeof(iss_aim_assist_prologue));
}

/* The stub (System V, the game's ABI): the replayed site with an absolute call, the flag test, and
 * an absolute jump back. rsp is the game's own all along (the site jumps here, it does not call),
 * so the stack argument and the alignment are what the original call had. Returns its length. */
static inline unsigned iss_aim_assist_build_stub(unsigned char *p, uintptr_t function,
                                                 uintptr_t off_flag, uintptr_t resume) {
    unsigned n=0;
    memcpy(p+n,iss_aim_assist_site,19); n+=19;               /* everything but the call */
    p[n++]=0x48; p[n++]=0xb8; memcpy(p+n,&function,8); n+=8; /* mov rax,function */
    p[n++]=0xff; p[n++]=0xd0;                                /* call rax */
    p[n++]=0x48; p[n++]=0xb8; memcpy(p+n,&off_flag,8); n+=8; /* mov rax,off_flag */
    p[n++]=0x83; p[n++]=0x38; p[n++]=0x00;                   /* cmp dword [rax],0 */
    p[n++]=0x74; p[n++]=22;                                  /* je resume_jump */
    static const unsigned char zero_yaw[]={0xc7,0x84,0x24,0xf0,0,0,0,0,0,0,0};
    static const unsigned char zero_pitch[]={0xc7,0x84,0x24,0xf4,0,0,0,0,0,0,0};
    memcpy(p+n,zero_yaw,11); n+=11;                          /* mov dword [rsp+0xf0],0 */
    memcpy(p+n,zero_pitch,11); n+=11;                        /* mov dword [rsp+0xf4],0 */
    p[n++]=0xff; p[n++]=0x25; memset(p+n,0,4); n+=4;         /* jmp [rip+0] */
    memcpy(p+n,&resume,8); n+=8;
    return n;
}
/* The site: an absolute jump to the stub, the rest int3-free nops. */
static inline void iss_aim_assist_build_jump(unsigned char *site, uintptr_t stub) {
    static const unsigned char jump[]={0xff,0x25,0,0,0,0};
    memcpy(site,jump,6); memcpy(site+6,&stub,8);
    memset(site+14,0x90,ISS_AIM_ASSIST_LENGTH-14);
}

/* ---- which device is in use ------------------------------------------------------------------ */
enum { ISS_AIM_ASSIST_AUTO, ISS_AIM_ASSIST_ON, ISS_AIM_ASSIST_OFF };
enum { ISS_INPUT_KBM, ISS_INPUT_PAD };

/* "auto"/"on"/"off" (any case, also 1/0); anything else or nothing is auto. */
static inline int iss_aim_assist_mode(const char *text) {
    if (!text) return ISS_AIM_ASSIST_AUTO;
    char s[8]={0};
    for (int i=0;i<7 && text[i];++i) s[i]=(char)(text[i]>='A' && text[i]<='Z' ? text[i]+32 : text[i]);
    if (!strcmp(s,"on") || !strcmp(s,"1")) return ISS_AIM_ASSIST_ON;
    if (!strcmp(s,"off") || !strcmp(s,"0")) return ISS_AIM_ASSIST_OFF;
    return ISS_AIM_ASSIST_AUTO;
}
/* One sample: the device that sent input last wins; with neither, the last one stays. When both
 * send input in one sample (a key held while the stick moves), the controller wins, so holding a
 * key never fights a player who has picked up the pad. Returns the device now in use. */
static inline int iss_input_source(int *last, int pad_input, int kbm_input) {
    if (pad_input) *last=ISS_INPUT_PAD;
    else if (kbm_input) *last=ISS_INPUT_KBM;
    return *last;
}
/* 1 = zero the assist's rates. */
static inline int iss_aim_assist_off(int mode, int source) {
    return mode==ISS_AIM_ASSIST_OFF || (mode==ISS_AIM_ASSIST_AUTO && source!=ISS_INPUT_PAD);
}

#ifdef __cplusplus
extern "C" {
#endif
/* Read by the stub each camera update; written by the pad sampler. */
extern volatile int32_t runtime_aim_assist_disabled;
#ifdef __cplusplus
}
#endif
#endif
