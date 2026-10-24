#ifndef GUARD_PLATFORM_OW_SHINY_SPARKLE_H
#define GUARD_PLATFORM_OW_SHINY_SPARKLE_H

// A shiny wild Pokemon twinkles on the field (PC only; the owner's request of 2026-10-24): src/platform/ow_shiny_sparkle.c.

void OwShinySparkle_Update(void);   // once a field frame (overworld.c, OverworldBasic)
// The 3D view (diorama/scene/scene_snapshot.c): whether the sprite is a twinkle, and the Pokemon it twinkles on.
bool8 OwShinySparkle_GetOwner(const struct Sprite *sprite, u8 *localId, u8 *mapNum, u8 *mapGroup);
u32 OwShinySparkle_Count(void);     // twinkles on the field now
u32 OwShinySparkle_Made(void);      // twinkles made since the game started

#endif // GUARD_PLATFORM_OW_SHINY_SPARKLE_H
