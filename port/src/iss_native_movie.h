/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Full-screen movies (US 1.07 movie draw 0x4575b0): the picture is drawn on a constant
 * full-screen quad, and its texture rectangle selects a centred band of k = 16·H / (9·W) of the
 * picture's height (W, H: the UI canvas). That is a cover fit: on the real-width canvas
 * (iss_native_ui_16_9=0, 3840x1080 at 32:9) the movie filled the width and lost its top and
 * bottom. These three single-use constants make it a contain fit instead: 16 and 9 become the
 * canvas size, so k = 1 and the whole picture is sampled, and the quad narrows to the centred
 * 16:9 box (-q..q, q = 16·H / (9·W)), so the sides are bars. At a 16:9 canvas the bytes are
 * unchanged in effect (k = q = 1); at 1920x1080 they are the original bytes. Each constant has
 * exactly one reader, checked below. */
#ifndef ISS_NATIVE_MOVIE_H
#define ISS_NATIVE_MOVIE_H
#include <stdint.h>
#include <string.h>
enum { ISS_MOVIE_SITE_COUNT = 3 };
typedef struct { uint32_t offset; unsigned length; unsigned char before[16]; } IssMovieSite;
static const IssMovieSite iss_movie_sites[ISS_MOVIE_SITE_COUNT] = {
    {0xaadbe0, 4, {0x00,0x00,0x80,0x41}},                       /* 16.0f */
    {0xaadbe4, 4, {0x00,0x00,0x10,0x41}},                       /* 9.0f */
    {0xaadd60,16, {0x00,0x00,0x80,0xbf,0x00,0x00,0x80,0xbf,     /* quad (-1,-1,1,1) */
                   0x00,0x00,0x80,0x3f,0x00,0x00,0x80,0x3f}},
};
/* The readers, checked but never written. */
typedef struct { uint32_t offset; unsigned char bytes[8]; } IssMovieReader;
static const IssMovieReader iss_movie_readers[ISS_MOVIE_SITE_COUNT] = {
    {0x4576a2,{0xc5,0xfa,0x59,0x05,0x36,0x65,0x65,0x00}},
    {0x4576ae,{0xc5,0xf2,0x59,0x0d,0x2e,0x65,0x65,0x00}},
    {0x4576ba,{0xc5,0xf8,0x10,0x0d,0x9e,0x66,0x65,0x00}},
};
static inline void iss_movie_after(unsigned i,uint32_t ui_width,uint32_t ui_height,unsigned char out[16]){
    const IssMovieSite *s=iss_movie_sites+i;
    memcpy(out,s->before,s->length);
    if(ui_width*9u==ui_height*16u && ui_width==1920u)return; /* the original canvas */
    if(i==0){float f=(float)ui_width;memcpy(out,&f,4);}
    if(i==1){float f=(float)ui_height;memcpy(out,&f,4);}
    if(i==2){
        float q=(16.0f*(float)ui_height)/(9.0f*(float)ui_width);
        if(q>1.0f)q=1.0f; /* narrower than 16:9: keep the full width (the band covers it) */
        const float quad[4]={-q,-1.0f,q,1.0f};memcpy(out,quad,16);
    }
}
static inline int iss_movie_check(const unsigned char *image,uint64_t size){
    for(unsigned i=0;i<ISS_MOVIE_SITE_COUNT;i++){
        const IssMovieReader *r=iss_movie_readers+i;const IssMovieSite *s=iss_movie_sites+i;
        if(r->offset>size || 8>size-r->offset || memcmp(image+r->offset,r->bytes,8))return 0;
        if(s->offset>size || s->length>size-s->offset || memcmp(image+s->offset,s->before,s->length))return 0;
    }
    return 1;
}
static inline void iss_movie_write(unsigned char *image,uint32_t ui_width,uint32_t ui_height){
    unsigned char after[16];
    for(unsigned i=0;i<ISS_MOVIE_SITE_COUNT;i++){
        iss_movie_after(i,ui_width,ui_height,after);
        memcpy(image+iss_movie_sites[i].offset,after,iss_movie_sites[i].length);
    }
}
#endif
