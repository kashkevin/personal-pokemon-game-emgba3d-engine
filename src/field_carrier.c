#include "global.h"
// The Pokemon carrying the player, drawn as itself (the owner's request of 2026-10-27, docs/gameplay-visual-updates-
// 20261027.md section 4 in the app repo). Project-owned. The original draws a generic blob for every Surf (and up a
// Waterfall), has the same blob drawn into the player's underwater picture, and flies everybody on a generic bird. Here
// each is the Pokemon that used the move: its own overworld picture (species and form, shininess, gender), its own
// animations (an idle loop, a paddle or a wing beat), and the rider sat on its back where the riding seats put a rider
// on that picture (MultiplayerRide_GetSeat). The surf blob's sprite and every rule of surfing are the original's; only
// its picture, place and drawing order change, so the jumps on and off, waterfalls, currents, fishing and Dive work as
// before. Native PC build only: the GBA ROM build compiles this file to nothing.
#ifdef PORTABLE
#include "field_carrier.h"
#include "character_customization.h"
#include "event_object_movement.h"
#include "field_effect.h"
#include "field_effect_helpers.h"
#include "field_move.h"
#include "field_player_avatar.h"
#include "graphics.h"
#include "multiplayer_ride.h"
#include "overworld.h"
#include "palette.h"
#include "party_menu.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "sprite.h"
#include "constants/event_object_movement.h"
#include "constants/event_objects.h"
#include "constants/field_effects.h"
#include "constants/maps.h"
#include "constants/moves.h"
#include "constants/rgb.h"
#include "constants/species.h"

STATIC_ASSERT(FIELD_CARRIER_GFX_UNDERWATER_RIDER >= CC_GFX_END && FIELD_CARRIER_GFX_UNDERWATER_RIDER < OBJ_EVENT_MON_FEMALE,
              FieldCarrierGraphicsIdFree);

struct Carrier
{
    u16 species;
    bool8 shiny;
    bool8 female;
    bool8 chosen;   // the move's user (else found again: a save loaded while surfing keeps no user)
};

enum
{
    OWNED_WATER,    // the surf sprite
    OWNED_FLY,      // the bird
    OWNED_REMOTE,   // other players' carriers, FIELD_CARRIER_REMOTES of them
    OWNED_COUNT = OWNED_REMOTE + FIELD_CARRIER_REMOTES,
};

// A carrier sprite made here: the game destroys it as it does the original (DestroySprite), so its palette is freed
// once the sprite is gone (FieldCarrier_Tick).
struct Owned
{
    const struct SpriteFrameImage *images;  // tells it from a sprite made later in the same slot
    u16 graphicsId;                         // the carrier's (the seats)
    u16 paletteTag;                         // the palette it was given (a map's load resets the palettes)
    u8 spriteId;
    u8 paletteNum;
    bool8 live;
};

static EWRAM_DATA struct Carrier sWater = {0};
static EWRAM_DATA struct Carrier sFlyer = {0};
static EWRAM_DATA struct Owned sOwned[OWNED_COUNT] = {0};
static EWRAM_DATA u16 sRiderSilhouette[16] = {0};
static EWRAM_DATA u16 sCarrierSilhouette[16] = {0};
static EWRAM_DATA struct ObjectEventGraphicsInfo sUnderwaterRider = {0};
static EWRAM_DATA u8 sRemoteAvatar[FIELD_CARRIER_REMOTES] = {0};    // each remote carrier's rider (object event id)

static void ReleaseIfGone(u32 which);

// ---------------------------------------------------------------------------
// Which Pokemon

static void SetFromBoxMon(struct Carrier *carrier, struct BoxPokemon *mon)
{
    carrier->species = GetBoxMonData(mon, MON_DATA_SPECIES);
    carrier->shiny = GetBoxMonData(mon, MON_DATA_IS_SHINY);
    carrier->female = GetBoxMonGender(mon) == MON_FEMALE;
    carrier->chosen = carrier->species != SPECIES_NONE && carrier->species < NUM_SPECIES
                   && !GetBoxMonData(mon, MON_DATA_IS_EGG);
}

static void SetFromIndex(struct Carrier *carrier, u32 partyIndex)
{
    carrier->chosen = FALSE;
    if (partyIndex < PARTY_SIZE || partyIndex == FIELD_MOVE_BOX_USER)
        SetFromBoxMon(carrier, FieldMove_UserBoxMon(partyIndex));
}

void FieldCarrier_SetWater(u32 partyIndex)
{
    SetFromIndex(&sWater, partyIndex);
}

void FieldCarrier_SetFlyer(u32 partyIndex)
{
    SetFromIndex(&sFlyer, partyIndex);
}

// The move's user as the game would choose it (ScrCmd_checkfieldmove): the first party Pokemon knowing it, else one
// in the PC boxes (the HMs from the boxes, field_move.c).
static bool32 FindUser(struct Carrier *carrier, enum Move move)
{
    u32 i, box, position;

    for (i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][i];

        if (GetMonData(mon, MON_DATA_SPECIES) == SPECIES_NONE)
            break;
        if (!GetMonData(mon, MON_DATA_IS_EGG) && MonKnowsMove(mon, move))
        {
            SetFromBoxMon(carrier, &mon->box);
            return carrier->chosen;
        }
    }
    if (FieldMove_FindBoxMonWithMove(move, &box, &position))
    {
        SetFromBoxMon(carrier, &gPokemonStoragePtr->boxes[box][position]);
        return carrier->chosen;
    }
    return FALSE;
}

// A chosen carrier is still this save's: one of the player's Pokemon of that species knows the move (a carrier left
// from another save, or released since, is found again).
static bool32 StillOwned(const struct Carrier *carrier, enum Move move, enum Move otherMove)
{
    u32 i, box, slot;

    for (i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][i];

        if (GetMonData(mon, MON_DATA_SPECIES) == carrier->species && !GetMonData(mon, MON_DATA_IS_EGG)
         && (MonKnowsMove(mon, move) || (otherMove != MOVE_NONE && MonKnowsMove(mon, otherMove))))
            return TRUE;
    }
    if (gPokemonStoragePtr == NULL)
        return FALSE;
    for (box = 0; box < TOTAL_BOXES_COUNT; box++)
    {
        for (slot = 0; slot < IN_BOX_COUNT; slot++)
        {
            struct BoxPokemon *mon = &gPokemonStoragePtr->boxes[box][slot];

            if (GetBoxMonData(mon, MON_DATA_SPECIES) == carrier->species && !GetBoxMonData(mon, MON_DATA_IS_EGG)
             && (BoxMonKnowsMove(mon, move) || (otherMove != MOVE_NONE && BoxMonKnowsMove(mon, otherMove))))
                return TRUE;
        }
    }
    return FALSE;
}

// The water's carrier: the one that used Surf or Dive last, else the Surf (Dive underwater) user the game would choose.
static bool32 ResolveWater(struct Carrier *out, bool32 underwater)
{
    if (!sWater.chosen || !StillOwned(&sWater, MOVE_SURF, MOVE_DIVE))
    {
        if (!FindUser(&sWater, underwater ? MOVE_DIVE : MOVE_SURF) && !FindUser(&sWater, underwater ? MOVE_SURF : MOVE_DIVE))
            return FALSE;
    }
    *out = sWater;
    return TRUE;
}

bool32 FieldCarrier_GetLiveWater(u16 *species, bool8 *shiny, bool8 *female)
{
    ReleaseIfGone(OWNED_WATER);
    if (!sOwned[OWNED_WATER].live || !sWater.chosen)
        return FALSE;
    *species = sWater.species;
    *shiny = sWater.shiny;
    *female = sWater.female;
    return TRUE;
}

bool32 FieldCarrier_GetWater(u16 *species, bool8 *shiny, bool8 *female)
{
    struct Carrier carrier;

    if (!ResolveWater(&carrier, (gPlayerAvatar.flags & PLAYER_AVATAR_FLAG_UNDERWATER) != 0))
        return FALSE;
    *species = carrier.species;
    *shiny = carrier.shiny;
    *female = carrier.female;
    return TRUE;
}

// The NPCs who fly (FLDEFF_NPCFLY_OUT, by the map: the effect names nobody) on a flier of their own team: the rival
// leaving Lilycove (Tropius, in each of their six teams there), Steven arriving on and leaving Route 128 and leaving
// Route 120 (Skarmory).
static bool32 NpcFlyer(struct Carrier *out)
{
    u16 map = (gSaveBlock1Ptr->location.mapGroup << 8) | gSaveBlock1Ptr->location.mapNum;

    memset(out, 0, sizeof(*out));
    if (map == MAP_LILYCOVE_CITY)
        out->species = SPECIES_TROPIUS;
    else if (map == MAP_ROUTE120 || map == MAP_ROUTE128)
        out->species = SPECIES_SKARMORY;
    else
        return FALSE;
    out->chosen = TRUE;
    return TRUE;
}

// The carrier's overworld picture; a form without one of its own shows its base form's (the same Pokemon). NULL when
// the species has none (OW_SUBSTITUTE_PLACEHOLDER would give the generic stand-in: the original is drawn instead).
static const struct ObjectEventGraphicsInfo *CarrierInfo(const struct Carrier *carrier, u16 *graphicsId)
{
    const struct ObjectEventGraphicsInfo *placeholder = &gSpeciesInfo[SPECIES_NONE].overworldData;
    u16 species = carrier->species;
    const struct ObjectEventGraphicsInfo *info;

    if (species == SPECIES_NONE || species >= NUM_SPECIES || !IsSpeciesEnabled(species))
        return NULL;
    info = SpeciesToGraphicsInfo(species, carrier->shiny, carrier->female);
    if (info == NULL || info == placeholder)
    {
        species = GET_BASE_SPECIES_ID(species);
        info = SpeciesToGraphicsInfo(species, carrier->shiny, carrier->female);
        if (info == NULL || info == placeholder)
            return NULL;
    }
    *graphicsId = GetGraphicsIdForMon(species, carrier->shiny, carrier->female);
    return info;
}

bool32 FieldCarrier_GetGraphicsId(u16 species, bool8 shiny, bool8 female, u16 *graphicsId)
{
    struct Carrier carrier = {species, shiny, female, TRUE};

    return CarrierInfo(&carrier, graphicsId) != NULL;
}

// ---------------------------------------------------------------------------
// The sprites

static bool32 SpritesAvailable(u32 needed)
{
    u32 i, free = 0;

    for (i = 0; i < MAX_SPRITES; i++)
        if (!gSprites[i].inUse && ++free >= needed)
            return TRUE;
    return FALSE;
}

static bool32 IsOwned(u32 which, const struct Sprite *sprite)
{
    const struct Owned *owned = &sOwned[which];

    return owned->live && sprite == &gSprites[owned->spriteId] && sprite->inUse && sprite->images == owned->images;
}

// The carrier sprite the game has destroyed (DestroySprite, or every sprite with a map's load): its palette freed if
// nothing else uses it and it is still the one it was given (after a map's load the slot may hold another's).
static void ReleaseIfGone(u32 which)
{
    struct Owned *owned = &sOwned[which];

    if (!owned->live || (gSprites[owned->spriteId].inUse && gSprites[owned->spriteId].images == owned->images))
        return;
    owned->live = FALSE;
    if (GetSpritePaletteTagByPaletteNum(owned->paletteNum) == owned->paletteTag)
        FieldEffectFreePaletteIfUnused(owned->paletteNum);
}

// A carrier made from its overworld picture, one hardware sprite as the original blob and bird are (the 3D view draws
// those whole, and the bird turns and grows from its Poke Ball by the sprite's matrix). atEnd: in the last free slot,
// as the original blob is (it reads the player's place after the player has moved in the frame); else the first, as
// the original bird is.
static u8 CreateCarrier(u32 which, u16 graphicsId, SpriteCallback callback, s16 x, s16 y, u8 subpriority, bool32 atEnd)
{
    struct Sprite *sprite;
    u8 spriteId;

    ReleaseIfGone(which);
    if (sOwned[which].live)
        return MAX_SPRITES;     // one at a time (the original never makes two)
    if (!SpritesAvailable(2))
        return MAX_SPRITES;
    if (atEnd)
        spriteId = CreateObjectGraphicsSpriteAtEnd(graphicsId, callback, x, y, subpriority);
    else
        spriteId = CreateObjectGraphicsSprite(graphicsId, callback, x, y, subpriority);
    if (spriteId >= MAX_SPRITES)
        return MAX_SPRITES;
    sprite = &gSprites[spriteId];
    sprite->subspriteTables = NULL;
    sprite->subspriteTableNum = 0;
    sprite->subspriteMode = SUBSPRITES_OFF;
    sOwned[which].images = sprite->images;
    sOwned[which].graphicsId = graphicsId;
    sOwned[which].spriteId = spriteId;
    sOwned[which].paletteNum = sprite->oam.paletteNum;
    sOwned[which].paletteTag = GetSpritePaletteTagByPaletteNum(sprite->oam.paletteNum);
    sOwned[which].live = TRUE;
    return spriteId;
}

void FieldCarrier_GetSpriteIds(u8 *water, u8 *fly)
{
    *water = IsOwned(OWNED_WATER, &gSprites[sOwned[OWNED_WATER].spriteId]) ? sOwned[OWNED_WATER].spriteId : MAX_SPRITES;
    *fly = IsOwned(OWNED_FLY, &gSprites[sOwned[OWNED_FLY].spriteId]) ? sOwned[OWNED_FLY].spriteId : MAX_SPRITES;
}

void FieldCarrier_Tick(void)
{
    u32 i;

    for (i = 0; i < OWNED_COUNT; i++)
        ReleaseIfGone(i);
}

// ---------------------------------------------------------------------------
// Under the water: the original's player there is a silhouette in its own palette, with the blob drawn in. The rider
// and their Pokemon are drawn as that silhouette: each colour by its brightness on the original's four underwater blues.

void FieldCarrier_Silhouette(const u16 *source, u16 *out)
{
    // player_underwater.pal: the silhouette's blues, darkest to lightest (the original's colours 8, 7, 6, 5), and its
    // light blue (colour 9) for the brightest highlights
    static const u8 sRamp[5][3] = {{24, 41, 82}, {49, 65, 106}, {74, 90, 131}, {98, 123, 156}, {131, 164, 197}};
    u32 i;

    out[0] = source[0];
    for (i = 1; i < 16; i++)
    {
        u32 r = source[i] & 0x1F, g = (source[i] >> 5) & 0x1F, b = (source[i] >> 10) & 0x1F;
        u32 luma = (r * 77 + g * 150 + b * 29) >> 8;   // 0..31
        u32 step = luma * 4;                            // 0..124: between the ramp's colours, 31 steps each
        u32 low = step / 31, frac = step % 31;
        const u8 *a = sRamp[low > 4 ? 4 : low], *c = sRamp[low + 1 > 4 ? 4 : low + 1];
        u32 rr = (a[0] * (31 - frac) + c[0] * frac) / 31, gg = (a[1] * (31 - frac) + c[1] * frac) / 31,
            bb = (a[2] * (31 - frac) + c[2] * frac) / 31;

        out[i] = RGB(rr >> 3, gg >> 3, bb >> 3);
    }
}

// The player's surfing pose (their character's), the rider the seats are read for
static u16 RiderGraphicsId(void)
{
    return GetPlayerAvatarGraphicsIdByStateId(PLAYER_AVATAR_STATE_SURFING);
}

static const u16 *RiderPalette(const struct ObjectEventGraphicsInfo *rider)
{
    struct SpritePalette characterPalette;

    if (CharCustom_GetSpritePalette(rider->paletteTag, &characterPalette))
        return characterPalette.data;
    return rider->paletteTag == OBJ_EVENT_PAL_TAG_MAY ? gObjectEventPal_May : gObjectEventPal_Brendan;
}

bool32 FieldCarrier_GetSpritePalette(u16 tag, struct SpritePalette *out)
{
    if (tag == FIELD_CARRIER_PAL_TAG_UNDERWATER_RIDER)
    {
        FieldCarrier_Silhouette(RiderPalette(GetObjectEventGraphicsInfo(RiderGraphicsId())), sRiderSilhouette);
        out->data = sRiderSilhouette;
        out->tag = tag;
        return TRUE;
    }
    return FALSE;
}

const struct ObjectEventGraphicsInfo *FieldCarrier_ResolveGraphics(u16 graphicsId)
{
    if (graphicsId != FIELD_CARRIER_GFX_UNDERWATER_RIDER)
        return NULL;
    sUnderwaterRider = *GetObjectEventGraphicsInfo(RiderGraphicsId());
    sUnderwaterRider.paletteTag = FIELD_CARRIER_PAL_TAG_UNDERWATER_RIDER;
    return &sUnderwaterRider;
}

// The carrier's palette as the silhouette, in a palette of its own (the species' own may be drawn elsewhere)
static void MakeCarrierSilhouette(struct Sprite *sprite)
{
    struct SpritePalette palette = {sCarrierSilhouette, FIELD_CARRIER_PAL_TAG_UNDERWATER_CARRIER};
    u32 old = sprite->oam.paletteNum, paletteNum;

    FieldCarrier_Silhouette(&gPlttBufferUnfaded[OBJ_PLTT_ID(old)], sCarrierSilhouette);
    paletteNum = IndexOfSpritePaletteTag(palette.tag);
    if (paletteNum < 16)
        LoadPalette(sCarrierSilhouette, OBJ_PLTT_ID(paletteNum), PLTT_SIZE_4BPP);   // another carrier's before
    else
        paletteNum = LoadSpritePalette(&palette);
    if (paletteNum >= 16)
        return;     // no palette free: its own colours
    sprite->oam.paletteNum = paletteNum;
    sOwned[OWNED_WATER].paletteNum = paletteNum;
    sOwned[OWNED_WATER].paletteTag = palette.tag;
    FieldEffectFreePaletteIfUnused(old);
}

// ---------------------------------------------------------------------------
// On and under the water

// The facing animation the original's blob takes for a direction (SynchronizeSurfAnim): the follower table's order
static u32 FaceAnim(u32 direction)
{
    switch (direction)
    {
    case DIR_NORTH:
        return ANIM_STD_FACE_NORTH;
    case DIR_WEST:
    case DIR_SOUTHWEST:
    case DIR_NORTHWEST:
        return ANIM_STD_FACE_WEST;
    case DIR_EAST:
    case DIR_SOUTHEAST:
    case DIR_NORTHEAST:
        return ANIM_STD_FACE_EAST;
    default:
        return ANIM_STD_FACE_SOUTH;
    }
}

static enum Direction SeatFacing(u32 anim)
{
    switch (anim)
    {
    case ANIM_STD_FACE_NORTH: return DIR_NORTH;
    case ANIM_STD_FACE_WEST:  return DIR_WEST;
    case ANIM_STD_FACE_EAST:  return DIR_EAST;
    default:                  return DIR_SOUTH;
    }
}

// The bottom row of a sprite drawn at its place (its centre plus half its height)
static s16 SpriteBottom(const struct Sprite *sprite)
{
    return sprite->y - sprite->centerToCornerVecY;
}

// Facing the camera the carrier is drawn over its rider: low enough that the rider's head and face show over it (a
// front picture standing higher than the back's seat: Wailord's body, Lapras' neck), MultiplayerRide_GetFrontClearance
#define FACE_VISIBLE_ROWS 12

#define FRONT_CACHE 4
static EWRAM_DATA struct { u16 carrier, rider; u8 clearance; } sFront[FRONT_CACHE] = {0};
static EWRAM_DATA u8 sFrontNext = 0;

void FieldCarrier_GetSeat(u16 carrierGraphicsId, u16 riderGraphicsId, u32 facing, u8 *lift, s8 *shift)
{
    u32 i;

    MultiplayerRide_GetSeat(carrierGraphicsId, riderGraphicsId, facing, lift, shift);
    if (facing != DIR_SOUTH || carrierGraphicsId == 0)
        return;
    for (i = 0; i < FRONT_CACHE; i++)
        if (sFront[i].carrier == carrierGraphicsId && sFront[i].rider == riderGraphicsId)
            break;
    if (i == FRONT_CACHE)
    {
        i = sFrontNext;
        sFrontNext = (sFrontNext + 1) % FRONT_CACHE;
        sFront[i].carrier = carrierGraphicsId;
        sFront[i].rider = riderGraphicsId;
        sFront[i].clearance = MultiplayerRide_GetFrontClearance(carrierGraphicsId, riderGraphicsId, FACE_VISIBLE_ROWS);
    }
    *lift = max(*lift, sFront[i].clearance);
}

// The carrier under a rider drawn centred on riderX with its bottom row on riderBottom: as a mount seats its rider
// (multiplayer_ride.c), drawn lower by the seat's height instead of the rider raised, so the rider stays where the
// original's surfer sits and every jump on and off is the original's
static void PlaceUnder(struct Sprite *sprite, u16 carrierGraphicsId, u16 riderGraphicsId, u32 face, s16 riderX,
                       s16 riderBottom)
{
    u8 lift;
    s8 shift;

    FieldCarrier_GetSeat(carrierGraphicsId, riderGraphicsId, SeatFacing(face), &lift, &shift);
    sprite->x = riderX - shift;
    sprite->y = riderBottom + lift + sprite->centerToCornerVecY;
}

static void PlaceUnderRider(struct Sprite *sprite, u32 face, s16 riderX, s16 riderBottom)
{
    PlaceUnder(sprite, sOwned[OWNED_WATER].graphicsId, RiderGraphicsId(), face, riderX, riderBottom);
}

u8 FieldCarrier_CreateSurfSprite(s16 x, s16 y, u8 subpriority, bool32 underwater)
{
    struct ObjectEvent *player = &gObjectEvents[gPlayerAvatar.objectEventId];
    struct Carrier carrier;
    u16 graphicsId;
    u8 spriteId;

    if (!ResolveWater(&carrier, underwater) || CarrierInfo(&carrier, &graphicsId) == NULL)
        return MAX_SPRITES;
    spriteId = CreateCarrier(OWNED_WATER, graphicsId, UpdateSurfBlobFieldEffect, x, y, subpriority, TRUE);
    if (spriteId >= MAX_SPRITES)
        return MAX_SPRITES;
    // Where it will carry the rider: the original's blob is drawn 8 px below the surfer's centre (UpdateBobbingEffect),
    // here as on a jump onto it, before the rider lands
    PlaceUnderRider(&gSprites[spriteId], FaceAnim(player->movementDirection), x,
                    y - 8 + GetObjectEventGraphicsInfo(RiderGraphicsId())->height / 2);
    StartSpriteAnim(&gSprites[spriteId], FaceAnim(player->movementDirection) - ANIM_STD_FACE_SOUTH + ANIM_STD_GO_SOUTH);
    if (underwater)
        MakeCarrierSilhouette(&gSprites[spriteId]);
    return spriteId;
}

bool32 FieldCarrier_IsSurfSprite(const struct Sprite *sprite)
{
    return IsOwned(OWNED_WATER, sprite);
}

// The rider's own walking animations playing: moving (a step on the water, a current carrying them, a bump against the
// shore). Standing, an object shows its walking animation paused on its first picture (FaceDirection).
static bool32 RiderMoving(const struct Sprite *playerSprite)
{
    return !playerSprite->animPaused
        && playerSprite->animNum >= ANIM_STD_GO_SOUTH && playerSprite->animNum <= ANIM_STD_GO_FASTEST_EAST;
}

bool32 FieldCarrier_UpdateSurfSprite(struct ObjectEvent *player, struct Sprite *playerSprite, struct Sprite *sprite,
                                     bool32 followsRider)
{
    u32 face;

    if (!IsOwned(OWNED_WATER, sprite))
        return FALSE;
    // the way the player faces, as the original's blob turns (SynchronizeSurfAnim); treading the water (its walking
    // loop) while they stay put or are off its back, paddling (its fast loop) while they move on it
    face = FaceAnim(player->movementDirection);
    StartSpriteAnimIfDifferent(sprite, face - ANIM_STD_FACE_SOUTH
                                     + (followsRider && RiderMoving(playerSprite) ? ANIM_STD_GO_FAST_SOUTH : ANIM_STD_GO_SOUTH));
    // its back under the rider's seat while it carries them (the original puts the blob at the player then)
    if (followsRider)
        PlaceUnderRider(sprite, face, playerSprite->x, SpriteBottom(playerSprite));
    // facing the camera its head is drawn over the rider's legs; otherwise the rider is in front
    if (face == ANIM_STD_FACE_SOUTH)
        sprite->subpriority = playerSprite->subpriority > 0 ? playerSprite->subpriority - 1 : 0;
    else
        sprite->subpriority = playerSprite->subpriority < 0xFF ? playerSprite->subpriority + 1 : 0xFF;
    return TRUE;
}

bool32 FieldCarrier_IsUnderwaterAvatarGraphicsId(u16 graphicsId)
{
    return graphicsId == FIELD_CARRIER_GFX_UNDERWATER_RIDER;
}

// ---------------------------------------------------------------------------
// Other players on the water: the multiplayer record carries their carrier (multiplayer_world.c), drawn under their
// avatar as the player's own is (the 2026-10-27 relay of the real carriers). On the water only: under it another
// player keeps the original's underwater look.

// After the rider has moved this frame (the carrier is made in the last free slot): under their seat, turned and
// swimming as they move, in front of them facing the camera and behind otherwise
static void RemoteCarrierCallback(struct Sprite *sprite)
{
    u32 slot = (u16)sprite->data[0], which = OWNED_REMOTE + slot, face;
    struct ObjectEvent *avatar;
    struct Sprite *rider;

    if (slot >= FIELD_CARRIER_REMOTES || !IsOwned(which, sprite) || sRemoteAvatar[slot] >= OBJECT_EVENTS_COUNT)
        return;
    avatar = &gObjectEvents[sRemoteAvatar[slot]];
    if (!avatar->active || avatar->spriteId >= MAX_SPRITES || !gSprites[avatar->spriteId].inUse)
    {
        sprite->invisible = TRUE;
        return;
    }
    rider = &gSprites[avatar->spriteId];
    face = FaceAnim(avatar->facingDirection);
    StartSpriteAnimIfDifferent(sprite, face - ANIM_STD_FACE_SOUTH + (RiderMoving(rider) ? ANIM_STD_GO_FAST_SOUTH : ANIM_STD_GO_SOUTH));
    PlaceUnder(sprite, sOwned[which].graphicsId, avatar->graphicsId, face, rider->x, SpriteBottom(rider));
    sprite->x2 = rider->x2;
    sprite->y2 = rider->y2;
    sprite->invisible = rider->invisible;
    sprite->coordOffsetEnabled = rider->coordOffsetEnabled;
    sprite->oam.priority = rider->oam.priority;
    if (face == ANIM_STD_FACE_SOUTH)
        sprite->subpriority = rider->subpriority > 0 ? rider->subpriority - 1 : 0;
    else
        sprite->subpriority = rider->subpriority < 0xFF ? rider->subpriority + 1 : 0xFF;
}

static void RemoveRemote(u32 which)
{
    if (sOwned[which].live && IsOwned(which, &gSprites[sOwned[which].spriteId]))
        DestroySprite(&gSprites[sOwned[which].spriteId]);
    ReleaseIfGone(which);
}

bool8 FieldCarrier_GetRemoteOwner(const struct Sprite *sprite, u8 *localId, u8 *mapNum, u8 *mapGroup)
{
    u32 slot;

    for (slot = 0; slot < FIELD_CARRIER_REMOTES; slot++)
    {
        const struct ObjectEvent *avatar;

        if (!IsOwned(OWNED_REMOTE + slot, sprite) || sRemoteAvatar[slot] >= OBJECT_EVENTS_COUNT)
            continue;
        avatar = &gObjectEvents[sRemoteAvatar[slot]];
        if (!avatar->active)
            return FALSE;
        *localId = avatar->localId;
        *mapNum = avatar->mapNum;
        *mapGroup = avatar->mapGroup;
        return TRUE;
    }
    return FALSE;
}

void FieldCarrier_SetRemote(u32 slot, u16 species, u8 flags, u32 avatarObjectEventId)
{
    struct Carrier carrier = {species, (flags & 1) != 0, (flags & 2) != 0, TRUE};   // MP_WORLD_FOLLOWER_SHINY, _FEMALE
    u32 which = OWNED_REMOTE + slot;
    const struct ObjectEventGraphicsInfo *info;
    struct ObjectEvent *avatar;
    u16 graphicsId = 0;
    u8 spriteId;

    if (slot >= FIELD_CARRIER_REMOTES)
        return;
    ReleaseIfGone(which);
    info = species != SPECIES_NONE && avatarObjectEventId < OBJECT_EVENTS_COUNT ? CarrierInfo(&carrier, &graphicsId) : NULL;
    if (info == NULL)
    {
        RemoveRemote(which);
        return;
    }
    sRemoteAvatar[slot] = avatarObjectEventId;
    if (sOwned[which].live)
    {
        if (sOwned[which].graphicsId == graphicsId)
            return;     // already this one
        RemoveRemote(which);
    }
    avatar = &gObjectEvents[avatarObjectEventId];
    if (!avatar->active || avatar->spriteId >= MAX_SPRITES)
        return;
    // as the multiplayer layer makes other players' objects: never a palette without a free slot, and sprites left for
    // the weather and the field's effects
    if (IndexOfSpritePaletteTag(info->paletteTag) == 0xFF && IndexOfSpritePaletteTag(TAG_NONE) == 0xFF)
        return;
    if (!SpritesAvailable(8))
        return;
    spriteId = CreateCarrier(which, graphicsId, RemoteCarrierCallback, gSprites[avatar->spriteId].x,
                             gSprites[avatar->spriteId].y, gSprites[avatar->spriteId].subpriority, TRUE);
    if (spriteId >= MAX_SPRITES)
        return;
    gSprites[spriteId].data[0] = slot;
    RemoteCarrierCallback(&gSprites[spriteId]);
}

// ---------------------------------------------------------------------------
// In the air

u8 FieldCarrier_CreateFlySprite(u32 who, s16 x, s16 y, u8 subpriority)
{
    struct Carrier carrier;
    u16 graphicsId;
    u8 spriteId;

    if (who == FIELD_CARRIER_NPC)
    {
        if (!NpcFlyer(&carrier))
            return MAX_SPRITES;
    }
    else
    {
        if ((!sFlyer.chosen || !StillOwned(&sFlyer, MOVE_FLY, MOVE_NONE)) && !FindUser(&sFlyer, MOVE_FLY))
            return MAX_SPRITES;
        carrier = sFlyer;
    }
    if (CarrierInfo(&carrier, &graphicsId) == NULL)
        return MAX_SPRITES;
    spriteId = CreateCarrier(OWNED_FLY, graphicsId, SpriteCallbackDummy, x, y, subpriority, FALSE);
    if (spriteId < MAX_SPRITES)
        StartSpriteAnim(&gSprites[spriteId], ANIM_STD_GO_FAST_WEST);   // a fast wing beat: the bird flies west
    return spriteId;
}

void FieldCarrier_ShiftFlySprite(struct Sprite *bird, const struct Sprite *rider)
{
    const struct ObjectEventGraphicsInfo *riderInfo = GetObjectEventGraphicsInfo(RiderGraphicsId());
    u8 lift;
    s8 shift;

    if (!IsOwned(OWNED_FLY, bird))
        return;
    // facing west, the two stand on one line (their bottoms) with the rider raised by the seat: the rider's centre is the
    // flyer's plus (shift, half its height - lift - half the rider's). The flyer's half height is taken unscaled (its
    // matrix grows it only out of and into the Poke Ball, where no rider sits on it).
    MultiplayerRide_GetSeat(sOwned[OWNED_FLY].graphicsId, RiderGraphicsId(), DIR_WEST, &lift, &shift);
    bird->x2 -= shift;
    bird->y2 += -8 - (GetObjectEventGraphicsInfo(sOwned[OWNED_FLY].graphicsId)->height / 2 - lift - riderInfo->height / 2);
    // facing west the rider is drawn in front of it, as on a mount (multiplayer_ride.c); alone, over the map as the
    // original's bird is (CreateFlyBirdSprite)
    if (rider != NULL)
    {
        bird->oam.priority = rider->oam.priority;
        bird->subpriority = rider->subpriority < 0xFF ? rider->subpriority + 1 : 0xFF;
    }
    else
    {
        bird->oam.priority = 1;
        bird->subpriority = 1;
    }
}

#endif // PORTABLE
