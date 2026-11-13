/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Native ultrawide, 1.07: keep the game's character-framing cone below 90 degrees.
 *
 * The world camera is Hor+: it stores the vertical FOV (+0x590), takes tan_y = tanf(fov/2) (+0x5a4)
 * and derives tan_x = tan_y * aspect (+0x5a0), so a wider screen only shows more at the sides.
 * One helper (RVA 0x138b80) instead approximates the horizontal half-angle as
 *     fov/2 * aspect + 0.1
 * and takes tanf of it, to fit a group of characters into a cone. With the default 1.0 rad
 * vertical FOV that is 0.99 rad at 16:9 and 1.29 at 21:9, but 1.88 at 32:9: past pi/2, where the
 * tangent turns negative and the cone inside out. The hook replays those instructions in a stub
 * and caps the angle at ISS_NATIVE_CONE_MAX before tanf; up to 21:9 nothing changes. */
#ifndef ISS_NATIVE_CONE_H
#define ISS_NATIVE_CONE_H
#include <stdint.h>
#include <string.h>

#define ISS_NATIVE_CONE_SITE 0x38dc8u
#define ISS_NATIVE_CONE_LENGTH 29u
#define ISS_NATIVE_CONE_RESUME (ISS_NATIVE_CONE_SITE+ISS_NATIVE_CONE_LENGTH)
#define ISS_NATIVE_CONE_TANF 0x984510u   /* tanf import thunk */
#define ISS_NATIVE_CONE_MAX 1.48f         /* rad, about 85 degrees (tangent about 11) */
#define ISS_NATIVE_CONE_MAX_STUB 96u

static const unsigned char iss_native_cone_site[ISS_NATIVE_CONE_LENGTH]={
    0xc5,0xfa,0x59,0x05,0xa8,0xfe,0x94,0x00,      /* vmulss xmm0,[rip -> 0.5f] */
    0xc5,0xfa,0x59,0x81,0x9c,0x05,0x00,0x00,      /* vmulss xmm0,[rcx+0x59c] (camera aspect) */
    0xc5,0xfa,0x58,0x05,0x9c,0xfe,0x94,0x00,      /* vaddss xmm0,[rip -> 0.1f] */
    0xe8,0x2b,0xb7,0x94,0x00};                    /* call tanf */
static const unsigned char iss_native_cone_tanf[]={0xff,0x25}; /* the thunk is an indirect jump */

static inline int iss_native_cone_signature(const unsigned char *image,uint64_t size) {
    return image && size>ISS_NATIVE_CONE_TANF+2 &&
        !memcmp(image+ISS_NATIVE_CONE_SITE,iss_native_cone_site,sizeof(iss_native_cone_site)) &&
        !memcmp(image+ISS_NATIVE_CONE_TANF,iss_native_cone_tanf,sizeof(iss_native_cone_tanf));
}
static inline unsigned iss_native_cone_stub_emit_float(unsigned char *p,float v,unsigned char op) {
    uint32_t bits; memcpy(&bits,&v,4);
    unsigned n=0;
    p[n++]=0xb8; memcpy(p+n,&bits,4); n+=4;                     /* mov eax,bits */
    p[n++]=0xc5; p[n++]=0xf9; p[n++]=0x6e; p[n++]=0xc8;         /* vmovd xmm1,eax */
    p[n++]=0xc5; p[n++]=0xfa; p[n++]=op; p[n++]=0xc1;           /* v{mul,add,min}ss xmm0,xmm0,xmm1 */
    return n;
}
/* rsp stays the game's (the site jumps here), so tanf sees the original call's alignment. xmm1 and
 * rax are free: tanf clobbers both, and the code after the call reloads them. */
static inline unsigned iss_native_cone_build_stub(unsigned char *p,uintptr_t tanf,uintptr_t resume,float max_angle) {
    unsigned n=0;
    n+=iss_native_cone_stub_emit_float(p+n,0.5f,0x59);
    memcpy(p+n,iss_native_cone_site+8,8); n+=8;                 /* vmulss xmm0,[rcx+0x59c] */
    n+=iss_native_cone_stub_emit_float(p+n,0.1f,0x58);
    n+=iss_native_cone_stub_emit_float(p+n,max_angle,0x5d);     /* the cap */
    p[n++]=0x48; p[n++]=0xb8; memcpy(p+n,&tanf,8); n+=8;        /* mov rax,tanf */
    p[n++]=0xff; p[n++]=0xd0;                                   /* call rax */
    p[n++]=0xff; p[n++]=0x25; memset(p+n,0,4); n+=4;            /* jmp [rip+0] */
    memcpy(p+n,&resume,8); n+=8;
    return n;
}
static inline void iss_native_cone_build_jump(unsigned char *site,uintptr_t stub) {
    static const unsigned char jump[]={0xff,0x25,0,0,0,0};
    memcpy(site,jump,6); memcpy(site+6,&stub,8);
    memset(site+14,0x90,ISS_NATIVE_CONE_LENGTH-14);
}
#endif
