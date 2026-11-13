/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The mouse in the game's own menus. The PS4 UI has no pointer, so while a menu is up (title and
 * main menus, loading, pause; see runtime_menu_scene) the left button confirms (cross), the right
 * button goes back (circle) and the wheel moves the selection (d-pad up/down). Gameplay keeps the
 * normal bindings. TLG_MENU_MOUSE=0 keeps the normal bindings in menus too. */
#ifndef ISS_MENU_MOUSE_H
#define ISS_MENU_MOUSE_H
#include <stdint.h>
#include "iss_menu_aspect.h"

static inline int iss_menu_mouse_scene(int scene) {
    return scene==ISS_MENU_SCENE_STARTING || scene==ISS_MENU_SCENE_NO_HERO || scene==ISS_MENU_SCENE_PAUSED;
}
/* bits: the mouse source bits (bit left, bit right, bit wheel_up, bit wheel_down are positions).
 * Clears those four from *mouse and returns the PS4 buttons they press instead. */
static inline uint32_t iss_menu_mouse_buttons(uint32_t *mouse, unsigned left, unsigned right,
                                              unsigned wheel_up, unsigned wheel_down,
                                              uint32_t cross, uint32_t circle, uint32_t up, uint32_t down) {
    const uint32_t m=*mouse;
    uint32_t ps=0;
    if (m>>left & 1) ps|=cross;
    if (m>>right & 1) ps|=circle;
    if (m>>wheel_up & 1) ps|=up;
    if (m>>wheel_down & 1) ps|=down;
    *mouse=m & ~((1u<<left)|(1u<<right)|(1u<<wheel_up)|(1u<<wheel_down));
    return ps;
}
#endif
