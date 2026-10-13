#ifdef ENABLE_DIORAMA

/*
 * Battle FX: the battlers as the game shows them (include/diorama/battle_fx_attach.h).
 *
 * With the animated battle sprites (ENABLE_BATTLE_SPRITES: platform/battle_sprites.h) this reads the
 * sprite system's renderer interface, the frame it draws in place of each OAM entry's tiles
 * (gBattleSpriteObjViews, set by BattleSprites_BeginFrame in DrawFrame); without them there is
 * nothing to read. Read only.
 */

#include <limits.h>
#include <stddef.h>

#include "diorama/battle_fx_attach.h"

#ifdef ENABLE_BATTLE_SPRITES
#include "platform/battle_sprites.h"
#endif

bool BattleFx_ShownFrame(int oamIndex, struct BattleFxShownFrame *out)
{
#ifdef ENABLE_BATTLE_SPRITES
    const struct BattleSpriteView *view;

    if (oamIndex < 0 || oamIndex >= 128 || out == NULL)
        return false;
    view = gBattleSpriteObjViews[oamIndex];
    if (view == NULL || view->pixels == NULL || view->colors == NULL || view->width <= 0 || view->height <= 0)
        return false;
    out->pixels = view->pixels;
    out->stride = view->stride;
    out->width = view->width;
    out->height = view->height;
    out->left = view->left;
    out->top = view->top;
    out->clipBottom = view->clipBottom >= INT16_MAX ? INT_MAX : view->clipBottom;
    out->colors = view->colors;
    return true;
#else
    (void)oamIndex;
    (void)out;
    return false;
#endif
}

bool BattleFx_ShownFramesBuilt(void)
{
#ifdef ENABLE_BATTLE_SPRITES
    return true;
#else
    return false;
#endif
}

#endif // ENABLE_DIORAMA
