/* Experimental US 1.07 pause-menu geometry. No gameplay HUD types are selected. */
#pragma once
#include <stdint.h>
#include <math.h>
#include <string.h>
/* Verified native mode only; explicit zero or malformed overrides disable. */
static inline int iss_menu_presentation_option(const char *value){return !value||!strcmp(value,"1");}
typedef struct {uintptr_t vtable,owner;uint32_t width,height,flags;} IssMenuNode;
typedef int (*IssMenuRead)(uintptr_t,IssMenuNode*,void*);
/* Not-in-game also includes startup and loading. Never interpret that alone
 * as an open pause menu. Reset the observation across save-load boundaries. */
static inline int iss_menu_pause_gate(unsigned *seen_gameplay,int supported,int ready,int gameplay){
 if(!supported||!ready){*seen_gameplay=0;return 0;}
 if(gameplay){*seen_gameplay=1;return 0;}
 return *seen_gameplay!=0;
}
/* The scene the game is in, sampled on its camera thread (runtime_menu_scene):
 * UNSUPPORTED: unverified executable, never fit;
 * STARTING: no camera update yet (logos, title screen);
 * NO_HERO: no player character (title and main menus, loading);
 * GAMEPLAY: a player character and no pause; the Hor+ world fills the screen;
 * PAUSED: a player character and the game's pause counter set (pause menu, map);
 * MOVIE: a full-screen movie (1280x720 or larger) is decoding, whatever else is true. */
enum { ISS_MENU_SCENE_UNSUPPORTED, ISS_MENU_SCENE_STARTING, ISS_MENU_SCENE_NO_HERO,
       ISS_MENU_SCENE_GAMEPLAY, ISS_MENU_SCENE_PAUSED, ISS_MENU_SCENE_MOVIE };
/* The 2D menus are laid out on a 1920x1080 canvas: show them at 16:9 everywhere but gameplay. */
static inline int iss_menu_scene_fit(int scene){
 return scene==ISS_MENU_SCENE_STARTING||scene==ISS_MENU_SCENE_NO_HERO||scene==ISS_MENU_SCENE_PAUSED||
        scene==ISS_MENU_SCENE_MOVIE;
}
/* The UI canvas is 16:9 (iss_native_ui_16_9=1, 1920x1080): only then are the menus drawn for a
 * 16:9 screen and need the presenter's 16:9 fit. The real-width canvas (ui_16_9=0, 1080 lines at
 * the output's aspect) is laid out by the game's own edge anchors, and its movies pillarbox. */
static inline int iss_menu_canvas_16_9(uint32_t ui_width,uint32_t ui_height){
 return ui_width&&ui_height&&(uint64_t)ui_width*9==(uint64_t)ui_height*16;
}
static inline int iss_menu_presentation_fit(uint32_t width,uint32_t height,int paused){
 return paused&&width&&height&&(uint64_t)width*9>(uint64_t)height*16;
}
static inline int iss_menu_member(uintptr_t node,IssMenuRead read,void* user){
 uintptr_t seen[16];
 for(unsigned depth=0;depth<16&&node;depth++){
  for(unsigned i=0;i<depth;i++)if(seen[i]==node)return 0;
  seen[depth]=node;IssMenuNode n;if(!read(node,&n,user))return 0;
  if(n.vtable==0xcdfc10u)return !n.owner&&n.width==1870&&n.height==1020&&n.flags==0;
  if(n.vtable!=0xd74668u&&n.vtable!=0xd795d0u&&n.vtable!=0xd75d98u&&n.vtable!=0xd74ce0u)return 0;
  node=n.owner;
 }
 return 0;
}
static inline int iss_menu_transform(float m[16],uint32_t w,uint32_t h){
 if(!w||!h||(uint64_t)w*9<=(uint64_t)h*16)return 0;
 float ratio=(16.0f*h)/(9.0f*w);
 if(!isfinite(ratio)||ratio<0.2f||ratio>=1)return 0;
 for(unsigned i=0;i<16;i++)if(!isfinite(m[i]))return 0;
 if(fabsf(m[3])>0.0001f||fabsf(m[7])>0.0001f||fabsf(m[11])>0.0001f||
    (fabsf(m[15])>0.0001f&&fabsf(m[15]-1)>0.0001f))return 0;
 m[0]*=ratio;m[4]*=ratio;m[8]*=ratio;m[12]*=ratio;
 return 1;
}
