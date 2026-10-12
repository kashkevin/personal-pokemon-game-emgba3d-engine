#include "global.h"
// Character selection (include/character_customization.h). Native PC build
// only: the GBA ROM build compiles this file to nothing.
#ifdef PORTABLE
#include "character_customization.h"
#include "data.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "field_player_avatar.h"
#include "graphics.h"
#include "link.h"
#include "multiplayer_appearance.h"
#include "palette.h"
#include "pokemon.h"
#include "sprite.h"
#include "trainer.h"
#include "trainer_pokemon_sprites.h"
#include "window.h"
#include "constants/event_object_movement.h"
#include "constants/event_objects.h"
#include "constants/trainers.h"

#include "data/character_customization/overworld.h"

STATIC_ASSERT(CC_GFX_BASE > OBJ_EVENT_GFX_VAR_F, CharCustomGraphicsIdsAboveTheEngines);
STATIC_ASSERT(CC_GFX_END <= OBJ_EVENT_MON_FEMALE, CharCustomGraphicsIdsBelowFollowerBits);
STATIC_ASSERT(CC_CHARACTER_COUNT <= 256, CharCustomIdsFitTheLook);
STATIC_ASSERT(TRAINER_PIC_COUNT + CHARCUSTOM_TRAINER_PIC_COUNT <= 256, CharCustomTrainerPicsFitAByte);

// The save keeps the look (8 bytes) in four of the engine's unused variables.
// Byte 7 of a look is always 0: its place holds a marker and the "not yet
// confirmed by the host" flag, so a save without a look reads as none.
#define CC_VAR_LOOK_01   VAR_UNUSED_0x4083
#define CC_VAR_LOOK_23   VAR_UNUSED_0x408B
#define CC_VAR_LOOK_45   VAR_UNUSED_0x4091
#define CC_VAR_LOOK_67   VAR_UNUSED_0x409B
#define CC_SAVE_MARKER   0xA0
#define CC_SAVE_PENDING  0x01

// ---------------------------------------------------------------------------
// Roster

u32 CharCustom_Count(void)
{
    return CC_CHARACTER_COUNT;
}

bool32 CharCustom_IsValid(u32 character)
{
    return character != CC_CHARACTER_NONE && character < CC_CHARACTER_COUNT && sCharacters[character].name != NULL;
}

const struct CharCustomCharacter *CharCustom_Get(u32 character)
{
    return CharCustom_IsValid(character) ? &sCharacters[character] : NULL;
}

const u8 *CharCustom_GetName(u32 character)
{
    const struct CharCustomCharacter *ch = CharCustom_Get(character);
    return ch != NULL ? ch->name : NULL;
}

const u8 *CharCustom_GetVariant(u32 character)
{
    const struct CharCustomCharacter *ch = CharCustom_Get(character);
    return ch != NULL ? ch->variant : NULL;
}

// ---------------------------------------------------------------------------
// Looks: byte 0 = 1, the character id in the low nibbles of bytes 4 and 6
// (reserved "bottom" and "accent" colours, which no game draws).

u32 CharCustom_FromLook(const u8 *look)
{
    u32 character;

    if (look == NULL || look[0] != 1 || look[4] > 15 || look[6] > 15)
        return CC_CHARACTER_NONE;
    character = (look[4] << 4) | look[6];
    return CharCustom_IsValid(character) ? character : CC_CHARACTER_NONE;
}

void CharCustom_SetInLook(u8 *look, u32 character)
{
    if (!CharCustom_IsValid(character))
        character = CC_CHARACTER_NONE;
    look[4] = character >> 4;
    look[6] = character & 0xF;
    if (character != CC_CHARACTER_NONE)
    {
        // A character has its own colours: the default trainer's colour slots stay empty.
        look[0] = 1;
        look[1] = look[2] = look[3] = look[5] = 0;
    }
    else if (!look[1] && !look[2] && !look[3] && !look[5])
    {
        look[0] = 0;
    }
    look[7] = 0;
}

bool32 CharCustom_ReadSavedLook(u8 *look, bool8 *pending)
{
    u16 v01, v23, v45, v67;

    if (gSaveBlock1Ptr == NULL) // the save blocks are not set up yet (boot)
        return FALSE;
    v67 = VarGet(CC_VAR_LOOK_67);
    if ((v67 >> 8 & 0xFE) != CC_SAVE_MARKER)
        return FALSE;
    v01 = VarGet(CC_VAR_LOOK_01);
    v23 = VarGet(CC_VAR_LOOK_23);
    v45 = VarGet(CC_VAR_LOOK_45);
    look[0] = v01 & 0xFF;
    look[1] = v01 >> 8;
    look[2] = v23 & 0xFF;
    look[3] = v23 >> 8;
    look[4] = v45 & 0xFF;
    look[5] = v45 >> 8;
    look[6] = v67 & 0xFF;
    look[7] = 0;
    if (pending != NULL)
        *pending = (v67 >> 8) & CC_SAVE_PENDING;
    return TRUE;
}

void CharCustom_WriteSavedLook(const u8 *look, bool8 pending)
{
    u16 v67 = look[6] | ((CC_SAVE_MARKER | (pending ? CC_SAVE_PENDING : 0)) << 8);

    if (gSaveBlock1Ptr == NULL)
        return;
    if (VarGet(CC_VAR_LOOK_67) == v67 && VarGet(CC_VAR_LOOK_01) == (look[0] | (look[1] << 8))
     && VarGet(CC_VAR_LOOK_23) == (look[2] | (look[3] << 8)) && VarGet(CC_VAR_LOOK_45) == (look[4] | (look[5] << 8)))
        return;
    VarSet(CC_VAR_LOOK_01, look[0] | (look[1] << 8));
    VarSet(CC_VAR_LOOK_23, look[2] | (look[3] << 8));
    VarSet(CC_VAR_LOOK_45, look[4] | (look[5] << 8));
    VarSet(CC_VAR_LOOK_67, v67);
}

u32 CharCustom_LocalCharacter(void)
{
    return CharCustom_FromLook(MultiplayerAppearance_GetLocalLook());
}

// ---------------------------------------------------------------------------
// Avatar graphics

static const u8 sStateGroup[CC_GFX_STATES] =
{
    [PLAYER_AVATAR_STATE_NORMAL]     = CC_GROUP_NORMAL,
    [PLAYER_AVATAR_STATE_MACH_BIKE]  = CC_GROUP_MACH,
    [PLAYER_AVATAR_STATE_ACRO_BIKE]  = CC_GROUP_ACRO,
    [PLAYER_AVATAR_STATE_SURFING]    = CC_GROUP_SURF,
    [PLAYER_AVATAR_STATE_UNDERWATER] = CC_GROUP_COUNT, // the engine's own (a silhouette with its own palette)
    [PLAYER_AVATAR_STATE_FIELD_MOVE] = CC_GROUP_FIELD,
    [PLAYER_AVATAR_STATE_FISHING]    = CC_GROUP_FISH,
    [PLAYER_AVATAR_STATE_WATERING]   = CC_GROUP_WATERING,
    [PLAYER_AVATAR_STATE_VSSEEKER]   = CC_GROUP_FIELD,
    [CC_GFX_STATE_DECORATING]        = CC_GROUP_DECORATING,
};

static EWRAM_DATA struct ObjectEventGraphicsInfo sInfos[CC_CHARACTER_COUNT][CC_GFX_STATES] = {0};
static EWRAM_DATA bool8 sInfosBuilt[CC_CHARACTER_COUNT] = {0};

static u16 VanillaAvatarId(u32 state, u32 gender)
{
    if (state == CC_GFX_STATE_DECORATING)
        return gender == FEMALE ? OBJ_EVENT_GFX_MAY_DECORATING : OBJ_EVENT_GFX_BRENDAN_DECORATING;
    return GetPlayerAvatarGraphicsIdByStateIdAndGender(state, gender);
}

// The avatar state and gender a graphics id of the engine's own trainers shows,
// or FALSE for any other graphics.
static bool32 VanillaAvatarState(u16 graphicsId, u32 *state, u32 *gender)
{
    u32 s, g;

    if (graphicsId >= NUM_OBJ_EVENT_GFX)
        return FALSE;
    for (s = 0; s < CC_GFX_STATES; s++)
    {
        for (g = 0; g < GENDER_COUNT; g++)
        {
            if (VanillaAvatarId(s, g) == graphicsId)
            {
                *state = s;
                *gender = g;
                return TRUE;
            }
        }
    }
    return FALSE;
}

static void BuildInfos(u32 character)
{
    const struct CharCustomCharacter *ch = &sCharacters[character];
    const struct ObjectEventGraphicsInfo *walking = GetVanillaObjectEventGraphicsInfo(OBJ_EVENT_GFX_BRENDAN_NORMAL);
    const struct ObjectEventGraphicsInfo *square = GetVanillaObjectEventGraphicsInfo(OBJ_EVENT_GFX_BRENDAN_SURFING);
    u32 state;

    for (state = 0; state < CC_GFX_STATES; state++)
    {
        const struct ObjectEventGraphicsInfo *vanilla = GetVanillaObjectEventGraphicsInfo(VanillaAvatarId(state, MALE));
        struct ObjectEventGraphicsInfo *info = &sInfos[character][state];
        u32 group = sStateGroup[state];
        const struct CharCustomGfx *gfx;
        const struct ObjectEventGraphicsInfo *shape;

        *info = *vanilla;
        if (group >= CC_GROUP_COUNT || ch->gfx[group].images == NULL)
            continue;
        gfx = &ch->gfx[group];
        shape = gfx->width == 16 ? walking : square;
        info->tileTag = TAG_NONE;
        info->paletteTag = CC_PAL_TAG_BASE + character;
        info->size = max(vanilla->size, gfx->frameSize);
        info->width = gfx->width;
        info->height = gfx->height;
        info->compressed = FALSE;
        info->tracks = gfx->tracks;
        info->oam = shape->oam;
        info->subspriteTables = shape->subspriteTables;
        info->anims = gfx->anims;
        info->images = gfx->images;
    }
    sInfosBuilt[character] = TRUE;
}

static const struct ObjectEventGraphicsInfo *CharacterInfo(u32 character, u32 gender, u32 state, u16 *graphicsId)
{
    const struct CharCustomCharacter *ch = CharCustom_Get(character);

    if (ch == NULL || ch->vanillaAvatar || sStateGroup[state] >= CC_GROUP_COUNT
     || ch->gfx[sStateGroup[state]].images == NULL)
    {
        // The engine's own trainer: Brendan / May themselves, the default
        // trainer, and the underwater silhouette (of the character's gender).
        *graphicsId = VanillaAvatarId(state, ch != NULL ? ch->gender : gender);
        return NULL;
    }
    if (!sInfosBuilt[character])
        BuildInfos(character);
    return &sInfos[character][state];
}

const struct ObjectEventGraphicsInfo *CharCustom_ResolveGraphics(u16 *graphicsId)
{
    u32 id = *graphicsId, state, gender, character;

    if (id >= CC_GFX_BASE && id < CC_GFX_END)
    {
        // Another player's avatar: the character is in the id.
        id -= CC_GFX_BASE;
        state = id % CC_GFX_STATES;
        id /= CC_GFX_STATES;
        return CharacterInfo(id >> 1, id & 1, state, graphicsId);
    }
    if (!VanillaAvatarState(id, &state, &gender))
        return NULL;
    character = CharCustom_LocalCharacter();
    if (character == CC_CHARACTER_NONE)
        return NULL;
    return CharacterInfo(character, gender, state, graphicsId);
}

bool32 CharCustom_GetSpritePalette(u16 tag, struct SpritePalette *out)
{
    const struct CharCustomCharacter *ch;

    if (tag <= CC_PAL_TAG_BASE || tag >= CC_PAL_TAG_BASE + CC_CHARACTER_COUNT)
        return FALSE;
    ch = CharCustom_Get(tag - CC_PAL_TAG_BASE);
    if (ch == NULL || ch->palette == NULL)
        return FALSE;
    out->data = ch->palette;
    out->tag = tag;
    return TRUE;
}

const struct StepAnimTable *CharCustom_GetStepAnimTable(const union AnimCmd *const *anims)
{
    static struct StepAnimTable sTable;
    u32 i;

    for (i = 0; i < ARRAY_COUNT(sCCAnimTables); i++)
    {
        if (sCCAnimTables[i] == anims)
        {
            // Every player table of the engine alternates its steps this way.
            sTable.anims = anims;
            sTable.animPos[0] = 1;
            sTable.animPos[1] = 3;
            sTable.animPos[2] = 0;
            sTable.animPos[3] = 2;
            return &sTable;
        }
    }
    return NULL;
}

void CharCustom_GetFieldSprite(u32 character, u32 gender, struct CharCustomFieldSprite *out)
{
    const struct CharCustomCharacter *ch = CharCustom_Get(character);
    const struct ObjectEventGraphicsInfo *info;
    u16 graphicsId;

    if (ch != NULL)
        gender = ch->gender;
    info = CharacterInfo(ch != NULL ? character : CC_CHARACTER_NONE, gender, PLAYER_AVATAR_STATE_NORMAL, &graphicsId);
    out->palette = ch != NULL ? ch->palette : NULL;
    if (info == NULL)
    {
        // The engine's own trainer (Brendan and May themselves: their own colours)
        info = GetVanillaObjectEventGraphicsInfo(graphicsId);
        if (ch != NULL)
            out->palette = gender == FEMALE ? gObjectEventPal_May : gObjectEventPal_Brendan;
    }
    out->images = info->images;
    out->anims = info->anims;
    out->width = info->width;
    out->height = info->height;
}

u16 CharCustom_RemoteGraphicsId(const u8 *look, u16 vanillaGraphicsId)
{
    u32 state, gender;

    if (!VanillaAvatarState(vanillaGraphicsId, &state, &gender))
        return vanillaGraphicsId;
    return CC_GFX_ID(CharCustom_FromLook(look), gender, state);
}

// Shows the local player's character at once (style editor preview, a look the
// host returned): the player's graphics id stays the engine's own, so the sprite
// is set up again only when it shows other graphics than that id now names.
void CharCustom_RefreshLocalAvatar(void)
{
    struct ObjectEvent *player;
    const struct ObjectEventGraphicsInfo *info;
    struct Sprite *sprite;

    if (gPlayerAvatar.objectEventId >= OBJECT_EVENTS_COUNT)
        return;
    player = &gObjectEvents[gPlayerAvatar.objectEventId];
    if (!player->active || player->spriteId >= MAX_SPRITES || !gSprites[player->spriteId].inUse)
        return;
    sprite = &gSprites[player->spriteId];
    info = GetObjectEventGraphicsInfo(player->graphicsId);
    if (sprite->images == info->images && sprite->anims == info->anims)
        return;
    if (player->heldMovementActive && !player->heldMovementFinished)
        return; // mid-step: on a later frame
    ObjectEventSetGraphicsId(player, player->graphicsId);
    ObjectEventTurn(player, player->facingDirection);
}

// ---------------------------------------------------------------------------
// Trainer pictures (battle intro and throw, Trainer Card, opponent view)

static const struct CharCustomCharacter *LocalCharacter(void)
{
    return CharCustom_Get(CharCustom_LocalCharacter());
}

enum TrainerPicID CharCustom_LocalBackTrainerPic(enum Gender gender, enum GameVersion version)
{
    const struct CharCustomCharacter *ch = LocalCharacter();
    return ch != NULL ? ch->backPic : GetPlayerTrainerPic(gender, version);
}

enum TrainerPicID CharCustom_LocalFrontTrainerPic(enum Gender gender)
{
    const struct CharCustomCharacter *ch = LocalCharacter();
    return ch != NULL ? ch->frontPic : PlayerGenderToFrontTrainerPicId(gender);
}

// Link battles: the local player's own trainer is the chosen character. The
// other players' choices do not travel with the battle (the host checks each
// trainer record against the save byte for byte), so they keep the engine's
// trainer of their gender.
enum TrainerPicID CharCustom_LinkBackTrainerPic(u32 linkPlayer)
{
    const struct CharCustomCharacter *ch = NULL;

    if (linkPlayer < MAX_LINK_PLAYERS && linkPlayer == GetMultiplayerId())
        ch = LocalCharacter();
    return ch != NULL ? ch->backPic : TRAINER_PIC_NONE;
}

// The Trainer Card's picture window is 72 x 80: a front bigger than the
// battle's 64 x 64 is shown whole there.
bool32 CharCustom_DrawTrainerCardPic(u8 windowId, u8 paletteSlot, u8 x, u8 y)
{
    const struct CharCustomCharacter *ch = LocalCharacter();

    if (ch == NULL)
        return FALSE;
    if (ch->cardPic != NULL)
    {
        BlitBitmapRectToWindow(windowId, (const u8 *)ch->cardPic, 0, 0, ch->cardWidth, ch->cardHeight,
                               0, 0, ch->cardWidth, ch->cardHeight);
        LoadPalette(ch->cardPal, PLTT_ID(paletteSlot), PLTT_SIZE_4BPP);
        return TRUE;
    }
    CreateTrainerCardTrainerPicSprite(ch->frontPic, TRUE, x, y, paletteSlot, windowId);
    return TRUE;
}

// 80 x 80 battle backs (the DS throws): six OAM pieces around the 64 x 64 one,
// the picture's bottom edge where a 64 x 64 back's is.
static const struct Subsprite sBack80Subsprites[] =
{
    {.x = -40, .y = -48, .shape = SPRITE_SHAPE(64x64), .size = SPRITE_SIZE(64x64), .tileOffset = 0,  .priority = 1},
    {.x =  24, .y = -48, .shape = SPRITE_SHAPE(16x32), .size = SPRITE_SIZE(16x32), .tileOffset = 64, .priority = 1},
    {.x =  24, .y = -16, .shape = SPRITE_SHAPE(16x32), .size = SPRITE_SIZE(16x32), .tileOffset = 72, .priority = 1},
    {.x = -40, .y =  16, .shape = SPRITE_SHAPE(32x16), .size = SPRITE_SIZE(32x16), .tileOffset = 80, .priority = 1},
    {.x =  -8, .y =  16, .shape = SPRITE_SHAPE(32x16), .size = SPRITE_SIZE(32x16), .tileOffset = 88, .priority = 1},
    {.x =  24, .y =  16, .shape = SPRITE_SHAPE(16x16), .size = SPRITE_SIZE(16x16), .tileOffset = 96, .priority = 1},
};

static const struct SubspriteTable sBack80SubspriteTables[] =
{
    {ARRAY_COUNT(sBack80Subsprites), sBack80Subsprites},
};

void CharCustom_SetupTrainerBackSprite(u8 spriteId, u16 trainerPicId)
{
    u32 i = trainerPicId - TRAINER_PIC_COUNT;

    if (spriteId >= MAX_SPRITES || trainerPicId < TRAINER_PIC_COUNT || i >= CHARCUSTOM_TRAINER_PIC_COUNT
     || !sCCLargeBackPic[i])
        return;
    SetSubspriteTables(&gSprites[spriteId], sBack80SubspriteTables);
    gSprites[spriteId].subspriteMode = SUBSPRITES_IGNORE_PRIORITY; // the battle sets the sprite's own priority
}

#endif // PORTABLE
