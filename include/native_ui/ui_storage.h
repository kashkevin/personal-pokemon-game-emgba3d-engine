#ifndef GUARD_NATIVE_UI_STORAGE_H
#define GUARD_NATIVE_UI_STORAGE_H

/*
 * The PC storage screen's wallpapers (screens/ui_storage_walls.c): original
 * pixel art in the design's manner for each of the game's wallpapers.
 */
#include "global.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_kit.h"

#define UI_STORAGE_WALL_W 248
#define UI_STORAGE_WALL_H 208

/* the wallpaper (enum NativeWallpaper) as a cached piece; walda: Walda's two colours (BGR555) */
const struct UiPiece *UiStorage_Wall(int kind, const u16 walda[2]);
/* the box frame's ramp for a wallpaper */
struct UiRamp UiStorage_WallRamp(int kind, const u16 walda[2]);
/* its name (engine text) */
const u8 *UiStorage_WallName(int kind);

#endif // GUARD_NATIVE_UI_STORAGE_H
