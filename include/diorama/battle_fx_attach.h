#ifndef GUARD_DIORAMA_BATTLE_FX_ATTACH_H
#define GUARD_DIORAMA_BATTLE_FX_ATTACH_H

/*
 * Battle FX: what the game shows of a battler when something else than its GBA picture is drawn in
 * the picture's place - the animated battle sprites (platform/battle_sprites.h, ENABLE_BATTLE_SPRITES):
 * an OAM entry showing a battler's picture is drawn from the sprite pack, a frame of its own size
 * placed in the entry's 64x64 box. The HD layer reads it to attach its effects to the battler as it
 * is shown (src/diorama/battlefx/README.md, "The battlers as the game shows them"): the battler's
 * glow, flash, light and depth take the frame's shape, and the effects near a battler are placed and
 * sized for the shown body instead of the GBA picture's.
 *
 * Read only: the sprite system's own renderer interface (gBattleSpriteObjViews), nothing of it is
 * changed. Without the animated sprites (or for an entry they leave to the GBA picture) there is
 * nothing to read, and the HD layer draws exactly as for the GBA picture.
 *
 * Main thread, after the picture was drawn (gba_easy_draw.c DrawFrame) and before the next one.
 */

#include <stdbool.h>
#include <stdint.h>

struct BattleFxShownFrame
{
    const uint8_t *pixels;  /* palette indices, row-major, index 0 see-through */
    uint32_t stride;        /* bytes per row of pixels */
    int width, height;      /* the frame's size */
    int left, top;          /* its top-left in the coordinates of the entry's 64x64 box (texture space) */
    int clipBottom;         /* screen row from which nothing of it is drawn (a fainting foe sinks); large: none */
    const uint16_t *colors; /* RGB555 per palette index, as drawn (faded as the battler's palette is) */
};

/* The frame OAM entry `oamIndex` is drawn as in the picture just drawn; false: its own GBA tiles. */
bool BattleFx_ShownFrame(int oamIndex, struct BattleFxShownFrame *out);
/* Whether the copy of a battler a move put on BG layer `bg` (1, 2: the scripts' "monbg") is drawn as such a frame
 * in the picture just drawn. */
bool BattleFx_ShownBgCopy(int bg);

/* Whether this build can show other frames at all (built with the animated battle sprites). */
bool BattleFx_ShownFramesBuilt(void);

#endif // GUARD_DIORAMA_BATTLE_FX_ATTACH_H
