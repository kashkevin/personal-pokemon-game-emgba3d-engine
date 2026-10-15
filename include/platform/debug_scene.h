#ifndef GUARD_PLATFORM_DEBUG_SCENE_H
#define GUARD_PLATFORM_DEBUG_SCENE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Development aid (PC build only; does nothing unless its environment variables
 * are set): open one screen without playing to it, press scripted keys and save
 * numbered PNGs of the game picture, so a screen can be looked at on a machine
 * with no display. Used to check the Town Map markers (docs/graphical-pokenav-map.md).
 *
 *   PKMN_DEBUG_SCENE=regionmap|pokenav   which screen
 *   PKMN_DEBUG_HERE=group,num,x,y,gender where this player stands
 *   PKMN_DEBUG_PEERS=g,n,x,y,gender,slot;...   up to three other trainers (slot 0-3)
 *   PKMN_DEBUG_EVENT=g,n,x,y             a visible World Event
 *   PKMN_DEBUG_OUTBREAK=g,n              a running outbreak
 *   PKMN_DEBUG_KEYS=frame:KEY+KEY,...    keys held for 4 frames (A B SELECT START UP DOWN LEFT RIGHT R L)
 *   PKMN_DEBUG_SHOTS=frame,frame,...     PNGs of those frames, then the program ends
 *   PKMN_DEBUG_STEM=path                 file name stem of the PNGs
 *   PKMN_DEBUG_GLSHOTS=frame,frame       3D builds: the window's picture (3D view and native interface) as F12 takes it
 */
void DebugScene_Start(void);                    /* game thread, once the game is initialised */
uint16_t DebugScene_FilterKeys(uint16_t keys);  /* once per game frame */
void DebugScene_Frame(const uint16_t *pixels);  /* once per drawn frame (240x160, BGR555) */
bool DebugScene_WantGlShot(void);               /* GL builds, once per presented frame: take the window's picture now */

#endif
