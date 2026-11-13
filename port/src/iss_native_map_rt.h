/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The map render target (HUD minimap and pause map share one renderer and target), US 1.07.
 * Its sizing function 0x5a6100 assumes a 1920x1080 UI canvas: the target width is the UI width
 * times a literal 1920.0, capped at 0x780 x 0x438, with a 1920x1080 default when no map widget is
 * up. The sampler 0x5a7f30 then reads it at (canvas width x fraction) / (allocated width), so on
 * the real-width canvas (iss_native_ui_16_9=0, for example 3840x1080 at 32:9) the map came out
 * half size with smeared edges and its icons too far out. These sites put the canvas size in
 * place of 1920x1080; the sampler reads the allocated size back and needs nothing. At a
 * 1920x1080 canvas every site keeps its original bytes.
 * The float at 0xae5c9c has exactly one reader (0x5a614f, inside 0x5a6100). */
#ifndef ISS_NATIVE_MAP_RT_H
#define ISS_NATIVE_MAP_RT_H
#include <stdint.h>
#include <string.h>
enum { ISS_MAP_RT_SITE_COUNT = 6 };
typedef struct { uint32_t offset; unsigned length, imm, imm_size; unsigned char before[10]; } IssMapRtSite;
static const IssMapRtSite iss_map_rt_sites[ISS_MAP_RT_SITE_COUNT] = {
    {0x5a6119,10,2,8,{0x48,0xb9,0x80,0x07,0x00,0x00,0x38,0x04,0x00,0x00}}, /* default size, imm64 */
    {0x5a614a, 5,1,4,{0xbe,0x80,0x07,0x00,0x00}},                          /* width cap */
    {0xae5c9c, 4,0,4,{0x00,0x00,0xf0,0x44}},                               /* 1920.0f */
    {0x5a617f, 6,2,4,{0x81,0xf9,0x81,0x07,0x00,0x00}},                     /* width cap + 1 */
    {0x5a6188, 5,1,4,{0xb9,0x38,0x04,0x00,0x00}},                          /* height cap */
    {0x5a61a3, 5,1,4,{0x3d,0x39,0x04,0x00,0x00}},                          /* height cap + 1 */
};
/* The reader of the float, checked but never written. */
static const unsigned char iss_map_rt_float_load[8]={0xc5,0xfa,0x59,0x05,0x45,0xfb,0x53,0x00};
#define ISS_MAP_RT_FLOAT_LOAD 0x5a614fu
/* The bytes site i becomes for a ui_width x ui_height canvas. */
static inline void iss_map_rt_after(unsigned i,uint32_t ui_width,uint32_t ui_height,unsigned char out[10]){
    const IssMapRtSite *s=iss_map_rt_sites+i;
    memcpy(out,s->before,s->length);
    switch(i){
    case 0:{uint64_t v=((uint64_t)ui_height<<32)|ui_width;memcpy(out+s->imm,&v,8);break;}
    case 1:memcpy(out+s->imm,&ui_width,4);break;
    case 2:{float f=(float)ui_width;memcpy(out,&f,4);break;}
    case 3:{uint32_t v=ui_width+1;memcpy(out+s->imm,&v,4);break;}
    case 4:memcpy(out+s->imm,&ui_height,4);break;
    case 5:{uint32_t v=ui_height+1;memcpy(out+s->imm,&v,4);break;}
    }
}
/* Preflight: every site and the float's reader hold the original US 1.07 bytes. */
static inline int iss_map_rt_check(const unsigned char *image,uint64_t size){
    if(size<ISS_MAP_RT_FLOAT_LOAD+sizeof(iss_map_rt_float_load) ||
       memcmp(image+ISS_MAP_RT_FLOAT_LOAD,iss_map_rt_float_load,sizeof(iss_map_rt_float_load)))return 0;
    for(unsigned i=0;i<ISS_MAP_RT_SITE_COUNT;i++){
        const IssMapRtSite *s=iss_map_rt_sites+i;
        if(s->offset>size || s->length>size-s->offset || memcmp(image+s->offset,s->before,s->length))return 0;
    }
    return 1;
}
static inline void iss_map_rt_write(unsigned char *image,uint32_t ui_width,uint32_t ui_height){
    unsigned char after[10];
    for(unsigned i=0;i<ISS_MAP_RT_SITE_COUNT;i++){
        iss_map_rt_after(i,ui_width,ui_height,after);
        memcpy(image+iss_map_rt_sites[i].offset,after,iss_map_rt_sites[i].length);
    }
}
#endif
