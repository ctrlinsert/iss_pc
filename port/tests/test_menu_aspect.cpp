#include "../src/iss_menu_aspect.h"
#include "../src/iss_hud_corners.h"
#include "../src/iss_menu_mouse.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>
static IssMenuNode nodes[4];
static int reader(uintptr_t at,IssMenuNode*out,void*){if(at<1||at>4)return 0;*out=nodes[at-1];return 1;}
int main(){
 unsigned seen=0;
 assert(iss_menu_presentation_option(nullptr));assert(iss_menu_presentation_option("1"));
 assert(!iss_menu_presentation_option("0"));assert(!iss_menu_presentation_option(""));
 assert(!iss_menu_presentation_option("yes"));assert(!iss_menu_presentation_option("10"));
 assert(iss_menu_presentation_fit(5120,1440,1));
 assert(iss_menu_presentation_fit(3440,1440,1));
 assert(!iss_menu_presentation_fit(5120,1440,0));
 assert(!iss_menu_presentation_fit(2560,1440,1));
 assert(!iss_menu_presentation_fit(3840,2160,1));
 assert(!iss_menu_presentation_fit(0,1440,1));
 assert(!iss_menu_pause_gate(&seen,0,0,0));
 assert(!iss_menu_pause_gate(&seen,1,0,0));
 assert(!iss_menu_pause_gate(&seen,1,1,0)); // ready during loading isn't gameplay
 assert(!iss_menu_pause_gate(&seen,1,1,1)&&seen==1);
 assert(iss_menu_pause_gate(&seen,1,1,0)); // now a pause transition is eligible
 assert(!iss_menu_pause_gate(&seen,1,0,0)&&seen==0); // save-load boundary
 assert(!iss_menu_pause_gate(&seen,1,1,0));
 assert(!iss_menu_pause_gate(&seen,1,1,1));assert(iss_menu_pause_gate(&seen,1,1,0));
 // Scenes: 16:9 menus everywhere but gameplay; an unverified executable never fits.
 assert(!iss_menu_scene_fit(ISS_MENU_SCENE_UNSUPPORTED)&&!iss_menu_scene_fit(ISS_MENU_SCENE_GAMEPLAY));
 assert(iss_menu_scene_fit(ISS_MENU_SCENE_STARTING)&&iss_menu_scene_fit(ISS_MENU_SCENE_NO_HERO)&&iss_menu_scene_fit(ISS_MENU_SCENE_PAUSED)&&iss_menu_scene_fit(ISS_MENU_SCENE_MOVIE));
 assert(iss_menu_presentation_fit(7680,2160,iss_menu_scene_fit(ISS_MENU_SCENE_NO_HERO)));
 assert(!iss_menu_presentation_fit(3840,2160,iss_menu_scene_fit(ISS_MENU_SCENE_PAUSED)));
 // Mouse in menus: left = cross, right = circle, wheel = up/down; other bits stay.
 assert(iss_menu_mouse_scene(ISS_MENU_SCENE_PAUSED)&&iss_menu_mouse_scene(ISS_MENU_SCENE_NO_HERO)&&iss_menu_mouse_scene(ISS_MENU_SCENE_STARTING));
 assert(!iss_menu_mouse_scene(ISS_MENU_SCENE_GAMEPLAY)&&!iss_menu_mouse_scene(ISS_MENU_SCENE_MOVIE)&&!iss_menu_mouse_scene(ISS_MENU_SCENE_UNSUPPORTED));
 {uint32_t m=(1u<<0)|(1u<<1)|(1u<<2)|(1u<<5)|(1u<<6);
  uint32_t ps=iss_menu_mouse_buttons(&m,0,2,5,6,0x4000,0x2000,0x10,0x40);
  assert(ps==(0x4000u|0x2000u|0x10u|0x40u)&&m==(1u<<1));
  m=1u<<6;assert(iss_menu_mouse_buttons(&m,0,2,5,6,0x4000,0x2000,0x10,0x40)==0x40u&&m==0);}
 // The presenter's 16:9 menu fit only applies to the 1920x1080 canvas; the real-width canvas
 // lays the menus out at the output's own aspect.
 assert(iss_menu_canvas_16_9(1920,1080)&&!iss_menu_canvas_16_9(3840,1080)&&!iss_menu_canvas_16_9(1728,1080)&&!iss_menu_canvas_16_9(0,0));
 nodes[0]={0xcdfc10,0,1870,1020,0};nodes[1]={0xd74668,1,0,0,0};nodes[2]={0xd795d0,2,0,0,0};
 assert(iss_menu_member(1,reader,nullptr));assert(iss_menu_member(3,reader,nullptr));
 auto original=nodes[0];for(unsigned f=0;f<4;f++){nodes[0]=original;if(f==0)nodes[0].vtable++;if(f==1)nodes[0].width++;if(f==2)nodes[0].height++;if(f==3)nodes[0].flags++;assert(!iss_menu_member(3,reader,nullptr));}nodes[0]=original;
 nodes[1].owner=3;assert(!iss_menu_member(3,reader,nullptr));nodes[1].owner=1;
 nodes[2].vtable=0xd76410;assert(!iss_menu_member(3,reader,nullptr));nodes[2].vtable=0xd795d0;
 nodes[2].owner=999;assert(!iss_menu_member(3,reader,nullptr));
 for(unsigned w:{3440u,5120u}){
  float m[16]={1,0,0,0,0,1,0,0,0,0,1,0,800,400,0,0};assert(iss_menu_transform(m,w,1440));
  assert(fabsf(m[0]*w/1920.0f-1440.0f/1080)<0.0001f);assert(m[5]==1&&m[13]==400&&m[15]==0);
  assert(fabsf(m[12]-800*m[0])<0.0001f);
 }
 float m[16]={1,0,0,0,0,1,0,0,0,0,1,0,800,400,0,1},copy[16];memcpy(copy,m,sizeof(m));
 assert(!iss_menu_transform(m,2560,1440));assert(!memcmp(copy,m,sizeof(m)));
 m[0]=NAN;memcpy(copy,m,sizeof(m));assert(!iss_menu_transform(m,5120,1440));assert(!memcmp(copy,m,sizeof(m)));
 assert(iss_hud_corner_kind(0xd74668,0xcdb7d0,140,200,5)==1);
 assert(!iss_hud_corner_kind(0xcdfc10,0,1870,1020,0));
 puts("Menu aspect: exact root, descendant filtering, cycles/unreadable/unknown rejection, centered 21:9/32:9 geometry and 16:9 no-op passed.");
}
