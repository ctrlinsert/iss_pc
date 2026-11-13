#include "iss_hud_corners.h"
#include "iss_native_resolution.h"
#include "iss_menu_aspect.h"
#include "iss_power_wheel.h"
#include <string.h>
#include <stdatomic.h>
#include <stdio.h>
static uintptr_t image_base;
static atomic_uint announced;
static atomic_uint inspected;
static atomic_uint calls;
void runtime_hud_corner_attach(uintptr_t base){image_base=base;}
static uint64_t hud64(const unsigned char *p){uint64_t v;memcpy(&v,p,8);return v;}
static uint32_t hud32(const unsigned char *p){uint32_t v;memcpy(&v,p,4);return v;}
/* TLG_UI_LAYOUT_TRACE=1: log each distinct widget kind once (type, parent type, size, flags and its
 * first matrix), to find the menu roots that need the 16:9 treatment on ultrawide outputs. */
#include <stdlib.h>
static void ui_layout_trace(const unsigned char *p,const void *output){
    static int enabled=-1; static atomic_uint logged; static uint64_t seen[512];
    if(enabled<0){const char *e=getenv("TLG_UI_LAYOUT_TRACE");enabled=e && *e=='1';}
    if(!enabled)return;
    uintptr_t vt=(uintptr_t)hud64(p)-image_base,owner=(uintptr_t)hud64(p+0x38),owner_vt=owner?(uintptr_t)hud64((const unsigned char*)owner)-image_base:0;
    uint64_t key=((uint64_t)(vt&0xffffff)<<40)^((uint64_t)(owner_vt&0xffffff)<<16)^hud32(p+0x4ec)^((uint64_t)hud32(p+0x4f0)<<8)^((uint64_t)hud32(p+0x4f4)<<56);
    for(unsigned i=0;i<512;i++){if(seen[i]==key)return;if(!seen[i]){seen[i]=key;break;}}
    if(atomic_fetch_add(&logged,1)>=480)return;
    float m[16];memcpy(m,output,sizeof(m));
    fprintf(stderr,"UI_LAYOUT vt=+%#lx owner=%p owner_vt=+%#lx size=%ux%u flags=%u x=[%.4g %.4g %.4g] y=[%.4g %.4g %.4g] t=[%.5g %.5g]\n",
        (unsigned long)vt,(void*)owner,(unsigned long)owner_vt,hud32(p+0x4ec),hud32(p+0x4f0),hud32(p+0x4f4),
        (double)m[0],(double)m[4],(double)m[8],(double)m[1],(double)m[5],(double)m[9],(double)m[12],(double)m[13]);
}
void __attribute__((ms_abi)) runtime_hud_corner_node(const void *widget,void *output){
    if(!image_base || runtime_native_status()!=1)return;
    const IssNativePlan *plan=runtime_native_plan();
    if(atomic_fetch_add(&calls,1)==0)fprintf(stderr,"HUD_CORNER_CALL ui=%d canvas=%ux%u widget=%p matrix=%p\n",plan->ui_16_9,plan->ui_width,plan->ui_height,widget,output);
    if(plan->ui_16_9!=1 || plan->ui_width!=1920 || plan->ui_height!=1080)return;
    const unsigned char *p=widget;
    ui_layout_trace(p,output);
    uintptr_t vt=(uintptr_t)hud64(p),owner=(uintptr_t)hud64(p+0x38),owner_vt=0;
    // Inspect the parent only for the ordinary HUD group type. Other UI types
    // (including menu/map widgets) leave before touching parent data.
    if(vt==image_base+0xd74668u){
        if(!owner || owner<0x1000000000ull || owner>=0x10000000000ull)return;
        owner_vt=(uintptr_t)hud64((const unsigned char*)owner)-image_base;
    }else if(vt!=image_base+0xd795d0u || owner)return;
    int kind=iss_hud_corner_kind(vt-image_base,owner_vt,hud32(p+0x4ec),hud32(p+0x4f0),hud32(p+0x4f4));
    if(!kind)return;
    float matrix[16];memcpy(matrix,output,sizeof(matrix));
    unsigned inspect_bit=1u<<(unsigned)kind;
    if(!(atomic_fetch_or(&inspected,inspect_bit)&inspect_bit))fprintf(stderr,
        "HUD_CORNER_MATRIX kind=%d rows=[%.6g %.6g %.6g %.6g][%.6g %.6g %.6g %.6g][%.6g %.6g %.6g %.6g][%.6g %.6g %.6g %.6g]\n",
        kind,(double)matrix[0],(double)matrix[1],(double)matrix[2],(double)matrix[3],
        (double)matrix[4],(double)matrix[5],(double)matrix[6],(double)matrix[7],
        (double)matrix[8],(double)matrix[9],(double)matrix[10],(double)matrix[11],
        (double)matrix[12],(double)matrix[13],(double)matrix[14],(double)matrix[15]);
    if(!iss_hud_corner_transform(matrix,plan->output_width,plan->output_height,kind))return;
    memcpy(output,matrix,sizeof(matrix));
    unsigned bit=1u<<(unsigned)kind;
    if(!(atomic_fetch_or(&announced,bit)&bit))fprintf(stderr,"HUD_CORNERS kind=%d matched: original 16:9 proportions, corner anchored; menu canvas unchanged\n",kind);
}
