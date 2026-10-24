/*
 * A shiny wild Pokemon twinkles on the field (PC only; the owner's request of 2026-10-24: "when seeing a shiny pokemon in
 * the wild, it should have mini pokemon shiny sparks so user knows which pokemon is the shiny").
 *
 * Every SPARKLE_PERIOD frames each shiny wild Pokemon on the field - the game's own overworld encounters
 * (wild_encounter_ow.c) and the shared spawns of a multiplayer world (multiplayer_spawns.c) - gets one small star, an 8x8
 * sprite in the general field-effect palette, at the next of SPARKLE_SPOTS places over its picture. The star follows the
 * Pokemon, grows, shines and shrinks in SPARKLE_FRAMES frames and is gone: one sprite at a time per shiny, none for
 * any other Pokemon (a player's follower, a script's Pokemon, a non-shiny one). A star whose Pokemon is gone (despawned,
 * caught, the map left) goes with it, and none are made off the field (a battle starts from the field: its sprites are
 * reset). The 3D view draws the stars over their Pokemon (OwShinySparkle_GetOwner; diorama/scene/scene_snapshot.c),
 * Classic 2D draws the sprites. Project-owned.
 */
#include "global.h"
#ifdef PORTABLE
#include "event_object_movement.h"
#include "field_effect.h"
#include "main.h"
#include "multiplayer_spawns.h"
#include "sprite.h"
#include "wild_encounter_ow.h"
#include "constants/event_objects.h"
#include "constants/field_effects.h"
#include "platform/ow_shiny_sparkle.h"

#define SPARKLE_PERIOD 40           // frames from one star of a Pokemon to its next
#define SPARKLE_FRAMES 18           // a star's life (sAnim_Sparkle)
#define SPARKLE_SPRITE_RESERVE 12   // free sprites always left to the field (its effects, weather, the other players)
#define SPARKLE_SPOTS 6

// The palette's colours (graphics/field_effects/palettes/general_0.pal): 4 white, 11-14 pale to deep gold.
static const u32 sSparkleTiles0[] = {0x00000000, 0x00000000, 0x0000B000, 0x000B4B00, 0x0000B000, 0x00000000, 0x00000000, 0x00000000};
static const u32 sSparkleTiles1[] = {0x00000000, 0x0000C000, 0x000B4B00, 0x00C444C0, 0x000B4B00, 0x0000C000, 0x00000000, 0x00000000};
static const u32 sSparkleTiles2[] = {0x0000D000, 0x00E0B0E0, 0x000B4B00, 0x0DB444BD, 0x000B4B00, 0x00E0B0E0, 0x0000D000, 0x00000000};

static const struct SpriteFrameImage sSparkleImages[] =
{
    obj_frame_tiles(sSparkleTiles0),
    obj_frame_tiles(sSparkleTiles1),
    obj_frame_tiles(sSparkleTiles2),
};

static const union AnimCmd sAnim_Sparkle[] =
{
    ANIMCMD_FRAME(0, 3),
    ANIMCMD_FRAME(1, 3),
    ANIMCMD_FRAME(2, 6),
    ANIMCMD_FRAME(1, 3),
    ANIMCMD_FRAME(0, 3),
    ANIMCMD_END,
};

static const union AnimCmd *const sAnims_Sparkle[] = {sAnim_Sparkle};

static const struct OamData sOam_Sparkle =
{
    .shape = SPRITE_SHAPE(8x8),
    .size = SPRITE_SIZE(8x8),
    .priority = 2,
};

static void SpriteCB_Sparkle(struct Sprite *sprite);

static const struct SpriteTemplate sSpriteTemplate_Sparkle =
{
    .tileTag = TAG_NONE,
    .paletteTag = FLDEFF_PAL_TAG_GENERAL_0,
    .oam = &sOam_Sparkle,
    .anims = sAnims_Sparkle,
    .images = sSparkleImages,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = SpriteCB_Sparkle,
};

// The places over the picture, in eighths of its half width and half height from its middle (up is negative).
static const s8 sSpots[SPARKLE_SPOTS][2] = {{-5, -4}, {4, -1}, {-1, 3}, {5, -5}, {-4, 1}, {1, -6}};

#define sLocalId    data[0]     // data[0..2]: the Pokemon's local id and map, as the trainer icons' (the 3D view's owner)
#define sMapNum     data[1]
#define sMapGroup   data[2]
#define sObjectId   data[3]
#define sOffsetX    data[4]
#define sOffsetY    data[5]

static u32 sFrame, sMade;

static bool32 IsShinyWild(struct ObjectEvent *objectEvent)
{
    if (!objectEvent->active || !OW_SHINY(objectEvent))
        return FALSE;
    return IsOverworldWildEncounter(objectEvent, OWE_ANY) || MultiplayerSpawns_IsSpawnObject(objectEvent);
}

static bool32 SpritesToSpare(void)
{
    u32 i, free = 0;

    for (i = 0; i < MAX_SPRITES; i++)
        if (!gSprites[i].inUse && ++free > SPARKLE_SPRITE_RESERVE)
            return TRUE;
    return FALSE;
}

// The star's Pokemon, as it was when the star was made; NULL once it is gone.
static struct ObjectEvent *Owner(struct Sprite *sprite)
{
    struct ObjectEvent *objectEvent;

    if (sprite->sObjectId < 0 || sprite->sObjectId >= OBJECT_EVENTS_COUNT)
        return NULL;
    objectEvent = &gObjectEvents[sprite->sObjectId];
    if (!objectEvent->active || objectEvent->localId != sprite->sLocalId || objectEvent->mapNum != sprite->sMapNum
     || objectEvent->mapGroup != sprite->sMapGroup || objectEvent->spriteId >= MAX_SPRITES || !IsShinyWild(objectEvent))
        return NULL;
    return objectEvent;
}

static void Follow(struct Sprite *sprite, const struct Sprite *pokemon)
{
    sprite->x = pokemon->x + sprite->sOffsetX;
    sprite->y = pokemon->y + sprite->sOffsetY;
    sprite->x2 = pokemon->x2;
    sprite->y2 = pokemon->y2;
    sprite->coordOffsetEnabled = pokemon->coordOffsetEnabled;
    sprite->invisible = pokemon->invisible;
    sprite->oam.priority = pokemon->oam.priority;
    sprite->subpriority = pokemon->subpriority > 0 ? pokemon->subpriority - 1 : 0;   // in front of its Pokemon
}

static void SpriteCB_Sparkle(struct Sprite *sprite)
{
    struct ObjectEvent *objectEvent = Owner(sprite);

    if (objectEvent == NULL || sprite->animEnded)
    {
        FieldEffectFreeGraphicsResources(sprite);   // the palette too, when nothing else uses it
        return;
    }
    Follow(sprite, &gSprites[objectEvent->spriteId]);
}

static void MakeSparkle(u32 objectEventId, u32 spot)
{
    struct ObjectEvent *objectEvent = &gObjectEvents[objectEventId];
    const struct Sprite *pokemon = &gSprites[objectEvent->spriteId];
    const struct ObjectEventGraphicsInfo *info = GetObjectEventGraphicsInfo(objectEvent->graphicsId);
    s32 halfWidth = (info != NULL ? info->width : 32) / 2 - 4, halfHeight = (info != NULL ? info->height : 32) / 2 - 4;
    struct Sprite *sprite;
    u32 spriteId;

    if (IndexOfSpritePaletteTag(FLDEFF_PAL_TAG_GENERAL_0) == 0xFF)
    {
        if (IndexOfSpritePaletteTag(TAG_NONE) == 0xFF)
            return;                                 // no palette to spare: the Pokemon's own colours still show it
        LoadSpritePalette(&gSpritePalette_GeneralFieldEffect0);
    }
    // unchecked: with no sprite slot or no room in the sprite tiles left the star is skipped (the checked call stops the
    // game on the crash screen instead)
    spriteId = CreateSpriteAtEndUnchecked(&sSpriteTemplate_Sparkle, pokemon->x, pokemon->y, 0);
    if (spriteId >= MAX_SPRITES)
        return;
    sprite = &gSprites[spriteId];
    sprite->sLocalId = objectEvent->localId;
    sprite->sMapNum = objectEvent->mapNum;
    sprite->sMapGroup = objectEvent->mapGroup;
    sprite->sObjectId = objectEventId;
    sprite->sOffsetX = sSpots[spot][0] * halfWidth / 8;
    sprite->sOffsetY = sSpots[spot][1] * halfHeight / 8;
    Follow(sprite, pokemon);
    sMade++;
}

void OwShinySparkle_Update(void)
{
    u32 i;

    sFrame++;
    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        struct ObjectEvent *objectEvent = &gObjectEvents[i];

        // each Pokemon on a beat of its own, so two shinies do not twinkle as one
        if ((sFrame + i * 7) % SPARKLE_PERIOD != 0 || !IsShinyWild(objectEvent) || objectEvent->invisible
         || objectEvent->offScreen || objectEvent->spriteId >= MAX_SPRITES || gSprites[objectEvent->spriteId].invisible)
            continue;
        if (!SpritesToSpare())
            return;
        MakeSparkle(i, (sFrame / SPARKLE_PERIOD + i) % SPARKLE_SPOTS);
    }
}

bool8 OwShinySparkle_GetOwner(const struct Sprite *sprite, u8 *localId, u8 *mapNum, u8 *mapGroup)
{
    if (!sprite->inUse || sprite->callback != SpriteCB_Sparkle)
        return FALSE;
    *localId = sprite->sLocalId;
    *mapNum = sprite->sMapNum;
    *mapGroup = sprite->sMapGroup;
    return TRUE;
}

u32 OwShinySparkle_Count(void)
{
    u32 i, count = 0;

    for (i = 0; i < MAX_SPRITES; i++)
        if (gSprites[i].inUse && gSprites[i].callback == SpriteCB_Sparkle)
            count++;
    return count;
}

u32 OwShinySparkle_Made(void)
{
    return sMade;
}
#endif // PORTABLE
