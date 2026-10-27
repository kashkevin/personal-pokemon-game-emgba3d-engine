#include "global.h"
#include "multiplayer_ride.h"
#include "multiplayer_world.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "event_scripts.h"
#include "field_player_avatar.h"
#include "fieldmap.h"
#include "follower_npc.h"
#include "overworld.h"
#include "pokemon.h"
#include "script.h"
#include "sound.h"
#include "sprite.h"
#include "string_util.h"
#include "task.h"
#include "constants/event_objects.h"
#include "constants/event_object_movement.h"
#include "constants/flags.h"
#include "constants/map_types.h"
#include "constants/songs.h"

_Static_assert(MP_RIDE_LOCAL_ID_MOUNT > MP_WORLD_LOCAL_ID_EVENT, "Mount ID follows the event ID");
_Static_assert(MP_RIDE_LOCAL_ID_MOUNT < LOCALID_OW_ENCOUNTER_END - OWE_SPAWNS_MAX + 1, "Mount ID outside generated encounters");

// Supported ride roster: large, physically appropriate Gen 1-6 Pokemon
// (every form of these species may carry a rider). Expand over time.
static const u16 sRideRoster[] =
{
    SPECIES_VENUSAUR, SPECIES_CHARIZARD, SPECIES_BLASTOISE, SPECIES_NINETALES, SPECIES_NIDOQUEEN,
    SPECIES_NIDOKING, SPECIES_ARCANINE, SPECIES_RAPIDASH, SPECIES_DODRIO, SPECIES_RHYHORN,
    SPECIES_RHYDON, SPECIES_TAUROS, SPECIES_AERODACTYL, SPECIES_DRAGONITE,
    SPECIES_MEGANIUM, SPECIES_TYPHLOSION, SPECIES_FERALIGATR, SPECIES_GIRAFARIG, SPECIES_STANTLER,
    SPECIES_DONPHAN, SPECIES_URSARING, SPECIES_PILOSWINE, SPECIES_HOUNDOOM, SPECIES_MILTANK,
    SPECIES_RAIKOU, SPECIES_ENTEI, SPECIES_SUICUNE, SPECIES_TYRANITAR,
    SPECIES_SWAMPERT, SPECIES_MIGHTYENA, SPECIES_MANECTRIC, SPECIES_CAMERUPT, SPECIES_FLYGON,
    SPECIES_TROPIUS, SPECIES_ABSOL, SPECIES_SALAMENCE, SPECIES_METAGROSS, SPECIES_AGGRON,
    SPECIES_TORTERRA, SPECIES_LUXRAY, SPECIES_RAMPARDOS, SPECIES_BASTIODON, SPECIES_GARCHOMP,
    SPECIES_HIPPOWDON, SPECIES_RHYPERIOR, SPECIES_MAMOSWINE,
    SPECIES_EMBOAR, SPECIES_SAMUROTT, SPECIES_STOUTLAND, SPECIES_LIEPARD, SPECIES_ZEBSTRIKA,
    SPECIES_KROOKODILE, SPECIES_SAWSBUCK, SPECIES_BOUFFALANT, SPECIES_HAXORUS, SPECIES_DRUDDIGON,
    SPECIES_HYDREIGON, SPECIES_COBALION, SPECIES_TERRAKION, SPECIES_VIRIZION,
    SPECIES_PYROAR, SPECIES_GOGOAT, SPECIES_FURFROU, SPECIES_TYRANTRUM, SPECIES_AURORUS,
    SPECIES_AVALUGG, SPECIES_NOIVERN,
};

struct RideState
{
    bool8 riding;   // lift, mirrored movement and the synchronized mount state
    bool8 busy;     // a mount/dismount hop is playing
    u8 mountObjectId;
    u8 mountFlags;  // MP_WORLD_FOLLOWER_SHINY / _FEMALE
    u16 species;
    u32 personality;
};

struct RiderLift
{
    bool8 active;
    u8 spriteId;
    s16 clean;   // y2 as the movement code left it
    s16 written; // y2 after the lift was applied
#ifdef PORTABLE
    s16 cleanX;   // x2 likewise (the seat's sideways place, MultiplayerRide_GetSeat)
    s16 writtenX;
#endif
#line 63 // gba-line
};

static EWRAM_DATA struct RideState sRide = {0};
static EWRAM_DATA struct RiderLift sLift[OBJECT_EVENTS_COUNT] = {0};

static void Task_RideTransition(u8 taskId);
static bool32 SpritesAvailable(u32 needed);
#ifdef PORTABLE
static u8 SeatLift(u16 mountGraphicsId);
static void ResetSeatState(void);
#endif
#line 70 // gba-line

void MultiplayerRide_Init(void)
{
    memset(&sRide, 0, sizeof(sRide));
    memset(sLift, 0, sizeof(sLift));
    sRide.mountObjectId = OBJECT_EVENTS_COUNT;
#ifdef PORTABLE
    ResetSeatState();
#endif
#line 76 // gba-line
}

bool32 MultiplayerRide_IsSpeciesRideable(u16 species)
{
    u32 i;
    if (species == SPECIES_NONE || species >= NUM_SPECIES)
        return FALSE;
    species = GetFormSpeciesId(species, 0);
    for (i = 0; i < ARRAY_COUNT(sRideRoster); i++)
    {
        if (sRideRoster[i] == species)
            return TRUE;
    }
    return FALSE;
}

bool32 MultiplayerRide_IsRiding(void)
{
    return sRide.riding;
}

static struct ObjectEvent *GetPlayer(void)
{
    return &gObjectEvents[gPlayerAvatar.objectEventId];
}

bool32 MultiplayerRide_IsMountObject(const struct ObjectEvent *objectEvent)
{
    u32 id = sRide.mountObjectId;
    return id < OBJECT_EVENTS_COUNT && objectEvent == &gObjectEvents[id]
        && objectEvent->active && objectEvent->localId == MP_RIDE_LOCAL_ID_MOUNT;
}

static struct ObjectEvent *GetMount(void)
{
    u32 id = sRide.mountObjectId;
    if (id < OBJECT_EVENTS_COUNT && MultiplayerRide_IsMountObject(&gObjectEvents[id]))
        return &gObjectEvents[id];
    return NULL;
}

static bool32 MapAllowsRiding(void)
{
    switch (gMapHeader.mapType)
    {
    case MAP_TYPE_TOWN:
    case MAP_TYPE_CITY:
    case MAP_TYPE_ROUTE:
    case MAP_TYPE_UNDERGROUND:
    case MAP_TYPE_OCEAN_ROUTE:
        return Overworld_IsBikingAllowed();
    default:
        return FALSE;
    }
}

static bool32 PlayerOnFoot(void)
{
    return !(gPlayerAvatar.flags & (PLAYER_AVATAR_FLAG_SURFING | PLAYER_AVATAR_FLAG_UNDERWATER
                                  | PLAYER_AVATAR_FLAG_MACH_BIKE | PLAYER_AVATAR_FLAG_ACRO_BIKE));
}

// The rider is always the lead (first healthy) Pokemon: the follower.
static bool32 GetLeadMount(u16 *species, u8 *flags, u32 *personality)
{
    struct Pokemon *mon = GetFirstLiveMon();
    enum Species leadSpecies;
    bool32 shiny, female;

    if (mon == NULL || !GetFollowerInfo(&leadSpecies, &shiny, &female))
        return FALSE;
    *species = leadSpecies;
    *flags = (shiny ? MP_WORLD_FOLLOWER_SHINY : 0) | (female ? MP_WORLD_FOLLOWER_FEMALE : 0);
    *personality = GetMonData(mon, MON_DATA_PERSONALITY);
    return TRUE;
}

static bool32 MountGraphicsAvailable(u16 species, u8 flags)
{
    return SpeciesToGraphicsInfo(species, (flags & MP_WORLD_FOLLOWER_SHINY) != 0, (flags & MP_WORLD_FOLLOWER_FEMALE) != 0) != NULL;
}

static bool32 RidingAllowedHere(void)
{
    return MapAllowsRiding() && PlayerOnFoot() && !PlayerHasFollowerNPC();
}

bool32 MultiplayerRide_CanMount(void)
{
    u16 species;
    u8 flags;
    u32 personality;
    return !sRide.riding && !sRide.busy && RidingAllowedHere()
        && GetLeadMount(&species, &flags, &personality) && MultiplayerRide_IsSpeciesRideable(species)
        && MountGraphicsAvailable(species, flags);
}

u8 MultiplayerRide_GetLiftForMount(u16 mountGraphicsId)
{
#ifdef PORTABLE
    if (mountGraphicsId != 0)
        return SeatLift(mountGraphicsId);
#endif
#line 175 // gba-line
    const struct ObjectEventGraphicsInfo *info = GetObjectEventGraphicsInfo(mountGraphicsId);
    if (info == NULL)
        return 0;
    return info->height >= 64 ? 14 : info->height >= 32 ? 7 : 4;
}

static u16 RiderGraphicsId(void)
{
    return GetPlayerAvatarGraphicsIdByStateId(PLAYER_AVATAR_STATE_SURFING);
}

static u16 WalkerGraphicsId(void)
{
    return GetPlayerAvatarGraphicsIdByStateId(PLAYER_AVATAR_STATE_NORMAL);
}
#ifdef PORTABLE

// ---------------------------------------------------------------------------
// The seat (the owner's request of 2026-10-27: riders floated over some mounts and sank into others; every mount had one
// lift, 7 px, as their pictures are all 32x32).
//
// Where a rider sits is read from the mount's own picture, once per mount and rider picture: in the side picture (it
// faces west: head on the left) the columns 45-75 % of the body's width from the head are the back; their median top row
// is the seat (a mane, a fin or a gap moves it little). Every object's picture stands on its tile's bottom, so the seat is
// (height - 1 - top) px above the ground line; the rider's picture (the surfing pose: a seated trainer) has its lowest
// body pixel some px above its own bottom; the rider sits MP_RIDE_SEAT_SINK px into the back. The height is the same for
// every facing (a back is as high seen from the front); seen from the side the rider also sits over the back, not over
// the middle. A picture that cannot be read keeps the old lift.
#define MP_RIDE_SEAT_SINK   3
#define MP_RIDE_SEAT_MAX_PERCENT 68   // of the side picture's drawn height
#define MP_RIDE_LIFT_MAX    30
#define MP_RIDE_SEAT_SLOTS  8

struct MPRideSeat
{
    u16 mountGraphicsId;
    u16 riderGraphicsId;
    u8 lift;
    s8 shiftWest;   // px the rider sits right of the mount's middle, facing west (its back: toward the tail)
    s8 shiftEast;
};

static EWRAM_DATA struct MPRideSeat sSeats[MP_RIDE_SEAT_SLOTS] = {0};
static EWRAM_DATA u8 sSeatNext = 0;
static EWRAM_DATA u8 sRamp = MP_RIDE_RAMP_FRAMES;   // frames since the local rider's lift started growing
static EWRAM_DATA u8 sDismountLift = 0;             // the lift still shown while the local rider hops off
static EWRAM_DATA u8 sDismountFrames = 0;
static EWRAM_DATA s8 sDismountShift = 0;

static void ResetSeatState(void)
{
    sRamp = MP_RIDE_RAMP_FRAMES;
    sDismountFrames = 0;
}

// The picture an object shows standing still facing `anim` (ANIM_STD_FACE_*): its frame and whether it is mirrored, as its
// own animation table says (a Pokemon's overworld picture uses frames 0, 2, 4 and a mirrored 4 or its own 6; a trainer's 0,
// 1, 2 and a mirrored 2).
static bool32 FacePicture(const struct ObjectEventGraphicsInfo *info, u32 anim, u32 *frame, bool32 *hFlip)
{
    const union AnimCmd *cmd;

    if (info == NULL || info->compressed || info->images == NULL || info->anims == NULL
     || info->width < 8 || info->height < 8 || info->width > 64 || info->height > 64
     || (info->width % 8) != 0 || (info->height % 8) != 0
     || (cmd = info->anims[anim]) == NULL || cmd->type < 0)
        return FALSE;
    *frame = cmd->frame.imageValue;
    *hFlip = cmd->frame.hFlip;
    if (info->images[0].relativeFrames)   // one sheet: frame n is n frames after the first (a Pokemon's overworld picture)
        return info->images[0].data != NULL && info->images[0].size >= info->width * info->height / 2 && *frame < 16;
    return info->images[*frame].data != NULL && info->images[*frame].size >= info->width * info->height / 2;
}

static const u8 *FrameTiles(const struct ObjectEventGraphicsInfo *info, u32 frame)
{
    if (info->images[0].relativeFrames)
        return (const u8 *)info->images[0].data + frame * info->images[0].size;
    return info->images[frame].data;
}

// Whether that pixel of the picture, as shown (mirrored when hFlip), is drawn
static bool32 FrameOpaque(const struct ObjectEventGraphicsInfo *info, u32 frame, bool32 hFlip, s32 x, s32 y)
{
    const u8 *tiles = FrameTiles(info, frame);
    s32 sx = hFlip ? info->width - 1 - x : x;
    u32 tile = (y / 8) * (info->width / 8) + sx / 8;
    u8 packed = tiles[tile * TILE_SIZE_4BPP + (y & 7) * 4 + (sx & 7) / 2];

    return ((sx & 1) ? packed >> 4 : packed & 0xF) != 0;
}

// The leftmost and rightmost drawn columns and the lowest drawn row of a shown picture; FALSE if it is empty
static bool32 PictureBounds(const struct ObjectEventGraphicsInfo *info, u32 frame, bool32 hFlip, s32 *left, s32 *right, s32 *bottom)
{
    s32 x, y;

    *left = info->width;
    *right = *bottom = -1;
    for (y = 0; y < info->height; y++)
    {
        for (x = 0; x < info->width; x++)
        {
            if (FrameOpaque(info, frame, hFlip, x, y))
            {
                *left = min(*left, x);
                *right = max(*right, x);
                *bottom = y;
            }
        }
    }
    return *bottom >= 0;
}

// The seat's row among the top rows of the columns start..end of a shown picture: the lower quartile of those tops (a
// raised wing, a neck, a mane or a fin standing above the back moves it little; the back's dip is where a rider sits)
static s32 SeatTop(const struct ObjectEventGraphicsInfo *info, u32 frame, bool32 hFlip, s32 start, s32 end)
{
    s32 x, y, tops[64], count = 0;

    for (x = start; x <= end && count < 64; x++)
    {
        for (y = 0; y < info->height; y++)
        {
            if (FrameOpaque(info, frame, hFlip, x, y))
            {
                tops[count++] = y;
                break;
            }
        }
    }
    if (count == 0)
        return -1;
    for (x = 1; x < count; x++)   // insertion sort
    {
        s32 top = tops[x];

        for (y = x - 1; y >= 0 && tops[y] > top; y--)
            tops[y + 1] = tops[y];
        tops[y + 1] = top;
    }
    return tops[count * 3 / 4];
}

static void ComputeSeat(u16 mountGraphicsId, u16 riderGraphicsId, struct MPRideSeat *seat)
{
    const struct ObjectEventGraphicsInfo *mount = GetObjectEventGraphicsInfo(mountGraphicsId);
    const struct ObjectEventGraphicsInfo *rider = GetObjectEventGraphicsInfo(riderGraphicsId);
    u32 mountWest, mountEast, riderWest, riderEast;
    bool32 mountWestFlip, mountEastFlip, riderWestFlip, riderEastFlip;
    s32 left, right, bottom, riderLeft, riderRight, riderBottom, start, end, top, lift;

    seat->mountGraphicsId = mountGraphicsId;
    seat->riderGraphicsId = riderGraphicsId;
    seat->shiftWest = seat->shiftEast = 0;
    seat->lift = mount == NULL ? 0 : mount->height >= 64 ? 14 : mount->height >= 32 ? 7 : 4;   // the old lift
    if (!FacePicture(mount, ANIM_STD_FACE_WEST, &mountWest, &mountWestFlip)
     || !FacePicture(mount, ANIM_STD_FACE_EAST, &mountEast, &mountEastFlip)
     || !FacePicture(rider, ANIM_STD_FACE_WEST, &riderWest, &riderWestFlip)
     || !FacePicture(rider, ANIM_STD_FACE_EAST, &riderEast, &riderEastFlip)
     || !PictureBounds(rider, riderWest, riderWestFlip, &riderLeft, &riderRight, &riderBottom)
     || !PictureBounds(mount, mountWest, mountWestFlip, &left, &right, &bottom))
        return;
    // facing west the head is on the left: the back is 45-75 % of the body's width from it
    start = left + (right - left) * 45 / 100;
    end = left + (right - left) * 75 / 100;
    if ((top = SeatTop(mount, mountWest, mountWestFlip, start, end)) < 0)
        return;
    // A seat is never above 68 % of the picture's drawn height: a raised wing, a neck or antlers standing over the
    // back of a flyer, a bird or a deer (Noivern, Dodrio, Sawsbuck) otherwise lifted the rider onto them.
    {
        s32 pictureTop, column = right + 1, highestSeat;

        for (pictureTop = 0; pictureTop < mount->height && column > right; pictureTop++)
        {
            for (column = left; column <= right && !FrameOpaque(mount, mountWest, mountWestFlip, column, pictureTop); column++)
                ;
        }
        pictureTop--;   // the first row with a drawn pixel
        highestSeat = bottom - (bottom - pictureTop) * MP_RIDE_SEAT_MAX_PERCENT / 100;
        if (top < highestSeat)
            top = highestSeat;
    }
    lift = (mount->height - 1 - top) - MP_RIDE_SEAT_SINK - (rider->height - 1 - riderBottom);
    seat->lift = lift < 0 ? 0 : lift > MP_RIDE_LIFT_MAX ? MP_RIDE_LIFT_MAX : lift;
    seat->shiftWest = ((start + end) / 2 - mount->width / 2) - ((riderLeft + riderRight) / 2 - rider->width / 2);
    // facing east (a mirrored or its own picture) the head is on the right
    if (PictureBounds(mount, mountEast, mountEastFlip, &left, &right, &bottom)
     && PictureBounds(rider, riderEast, riderEastFlip, &riderLeft, &riderRight, &riderBottom))
    {
        start = right - (right - left) * 75 / 100;
        end = right - (right - left) * 45 / 100;
        seat->shiftEast = ((start + end) / 2 - mount->width / 2) - ((riderLeft + riderRight) / 2 - rider->width / 2);
    }
}

static const struct MPRideSeat *GetSeat(u16 mountGraphicsId, u16 riderGraphicsId)
{
    u32 i;

    for (i = 0; i < MP_RIDE_SEAT_SLOTS; i++)
    {
        if (sSeats[i].mountGraphicsId == mountGraphicsId && sSeats[i].riderGraphicsId == riderGraphicsId
         && mountGraphicsId != 0)
            return &sSeats[i];
    }
    i = sSeatNext++ % MP_RIDE_SEAT_SLOTS;
    ComputeSeat(mountGraphicsId, riderGraphicsId, &sSeats[i]);
    return &sSeats[i];
}

static u8 SeatLift(u16 mountGraphicsId)
{
    return GetSeat(mountGraphicsId, RiderGraphicsId())->lift;
}

// The seat of that rider picture on that mount picture, for a rider facing `facing`: px up, px right.
void MultiplayerRide_GetSeat(u16 mountGraphicsId, u16 riderGraphicsId, u32 facing, u8 *lift, s8 *shift)
{
    const struct MPRideSeat *seat = GetSeat(mountGraphicsId, riderGraphicsId);

    *lift = seat->lift;
    *shift = facing == DIR_WEST ? seat->shiftWest : facing == DIR_EAST ? seat->shiftEast : 0;
}

// The rider (the local player on its mount, or another player's avatar on theirs): no shadow of its own (the mount's is
// the one on the ground), field_effect_helpers.c UpdateShadowFieldEffect.
bool32 MultiplayerRide_IsRider(const struct ObjectEvent *objectEvent)
{
    return MultiplayerRide_GetMountOf(objectEvent) != NULL;
}

// The mount a rider sits on now (the local player's, or another player's avatar's), NULL for anybody else.
struct ObjectEvent *MultiplayerRide_GetMountOf(const struct ObjectEvent *objectEvent)
{
    u8 lift;
    struct ObjectEvent *mount = NULL;

    if (objectEvent->isPlayer)
        return sRide.riding ? GetMount() : NULL;
    return MultiplayerWorld_GetRemoteRider(objectEvent, &lift, &mount) ? mount : NULL;
}
#endif
#line 190 // gba-line

static void RemoveMount(void)
{
    u32 id = sRide.mountObjectId;
    if (id < OBJECT_EVENTS_COUNT)
    {
        struct ObjectEvent *objectEvent = &gObjectEvents[id];
        if (objectEvent->active && objectEvent->localId == MP_RIDE_LOCAL_ID_MOUNT)
            RemoveObjectEvent(objectEvent);
    }
    sRide.mountObjectId = OBJECT_EVENTS_COUNT;
}

static bool32 SpawnMount(s16 x, s16 y, enum Direction direction)
{
    struct ObjectEventTemplate template = {0};
    struct ObjectEvent *player = GetPlayer();
    u16 graphicsId = GetGraphicsIdForMon(sRide.species, sRide.mountFlags & MP_WORLD_FOLLOWER_SHINY,
                                         sRide.mountFlags & MP_WORLD_FOLLOWER_FEMALE);
    u32 id;

    RemoveMount();
    if (GetObjectEventIdByLocalId(MP_RIDE_LOCAL_ID_MOUNT) < OBJECT_EVENTS_COUNT
     || GetFirstInactiveObjectEventId() >= OBJECT_EVENTS_COUNT || !SpritesAvailable(4))
        return FALSE;
    template.localId = MP_RIDE_LOCAL_ID_MOUNT;
    template.graphicsId = graphicsId;
    template.kind = OBJ_KIND_NORMAL;
    template.x = x - MAP_OFFSET;
    template.y = y - MAP_OFFSET;
    template.elevation = player->currentElevation;
    template.movementType = MOVEMENT_TYPE_NONE;
    id = SpawnSpecialObjectEvent(&template);
    if (id >= OBJECT_EVENTS_COUNT)
        return FALSE;
    sRide.mountObjectId = id;
    gObjectEvents[id].hideReflection = TRUE;
    gObjectEvents[id].fixedPriority = TRUE;
    MoveObjectEventToMapCoords(&gObjectEvents[id], x, y);
    ObjectEventTurn(&gObjectEvents[id], direction);
    if (gObjectEvents[id].spriteId < MAX_SPRITES && player->spriteId < MAX_SPRITES)
    {
        gSprites[gObjectEvents[id].spriteId].subpriority = gSprites[player->spriteId].subpriority + 1;
        gSprites[gObjectEvents[id].spriteId].oam.priority = gSprites[player->spriteId].oam.priority;
    }
    return TRUE;
}

static bool32 SpritesAvailable(u32 needed)
{
    u32 i, free = 0;
    for (i = 0; i < MAX_SPRITES; i++)
        if (!gSprites[i].inUse && ++free >= needed)
            return TRUE;
    return FALSE;
}

static void PublishMount(void)
{
    MultiplayerWorld_SetLocalMount(sRide.species, sRide.mountFlags, sRide.riding);
}

// Leave the mount at once (map rules, Surf, a bike, a changed lead...).
static void ForceDismount(bool32 restoreWalker)
{
    struct ObjectEvent *player = GetPlayer();

    RemoveMount();
    sRide.riding = FALSE;
    sRide.busy = FALSE;
#ifdef PORTABLE
    sDismountFrames = 0;
#endif
#line 260 // gba-line
    PublishMount();
    if (restoreWalker && player->active && player->graphicsId == RiderGraphicsId() && PlayerOnFoot())
    {
        ObjectEventSetGraphicsId(player, WalkerGraphicsId());
        ObjectEventTurn(player, player->facingDirection);
    }
    FlagClear(FLAG_TEMP_HIDE_FOLLOWER);
}

// ---------------------------------------------------------------------------
// Movement and drawing

void MultiplayerRide_MirrorPlayerMovement(u8 movementActionId)
{
    struct ObjectEvent *mount;

    if (!sRide.riding || sRide.busy || (mount = GetMount()) == NULL)
        return;
    mount->currentElevation = GetPlayer()->currentElevation;
    mount->previousElevation = GetPlayer()->previousElevation;
    ObjectEventForceSetHeldMovement(mount, movementActionId);
}

void MultiplayerRide_BeginObjectUpdate(struct ObjectEvent *objectEvent, struct Sprite *sprite)
{
    u32 id = objectEvent - gObjectEvents;
    if (id >= OBJECT_EVENTS_COUNT || !sLift[id].active)
        return;
    // Hand the movement code its own y2 (jump arcs keep working); a value
    // somebody else wrote since the last frame is kept as it is.
    if (sLift[id].spriteId == objectEvent->spriteId && sprite->y2 == sLift[id].written)
        sprite->y2 = sLift[id].clean;
#ifdef PORTABLE
    if (sLift[id].spriteId == objectEvent->spriteId && sprite->x2 == sLift[id].writtenX)
        sprite->x2 = sLift[id].cleanX;
#endif
#line 292 // gba-line
    sLift[id].active = FALSE;
}

static bool32 LocalRiderLift(u8 *lift, struct ObjectEvent **mount)
{
    if (!sRide.riding || (*mount = GetMount()) == NULL)
        return FALSE;
    *lift = MultiplayerRide_GetLiftForMount((*mount)->graphicsId);
    return TRUE;
}

void MultiplayerRide_EndObjectUpdate(struct ObjectEvent *objectEvent, struct Sprite *sprite)
{
#ifdef PORTABLE
    u32 id = objectEvent - gObjectEvents;
    struct ObjectEvent *mount = NULL;
    u8 lift = 0;
    s8 shift = 0;
    bool32 hoppingOff = FALSE;

    if (id >= OBJECT_EVENTS_COUNT)
        return;
    if (sRide.riding && MultiplayerRide_IsMountObject(objectEvent) && GetPlayer()->invisible)
        sprite->invisible = TRUE;   // gone through a door with its rider: in the same frame (MultiplayerRide_Tick)
    if (objectEvent->isPlayer && !sRide.riding && sDismountFrames != 0)
    {
        // hopping off: the seat's lift shrinks over the hop instead of dropping at once
        sDismountFrames--;
        lift = sDismountLift * sDismountFrames / MP_RIDE_RAMP_FRAMES;
        shift = sDismountShift * sDismountFrames / MP_RIDE_RAMP_FRAMES;
    }
    else if (objectEvent->isPlayer)
    {
        if (!LocalRiderLift(&lift, &mount))
            return;
        MultiplayerRide_GetSeat(mount->graphicsId, objectEvent->graphicsId, objectEvent->facingDirection, &lift, &shift);
        if (sRamp < MP_RIDE_RAMP_FRAMES)
        {
            // hopping on: the lift grows over the hop
            sRamp++;
            lift = lift * sRamp / MP_RIDE_RAMP_FRAMES;
            shift = shift * sRamp / MP_RIDE_RAMP_FRAMES;
        }
    }
    else
    {
        // another player: the seat rises after their hop on and comes down over their hop off (multiplayer_world.c)
        u32 seat = MultiplayerWorld_GetRemoteSeat(objectEvent, &mount);

        if (seat == 0)
            return;
        MultiplayerRide_GetSeat(mount->graphicsId, objectEvent->graphicsId, objectEvent->facingDirection, &lift, &shift);
        lift = lift * seat / MP_RIDE_RAMP_FRAMES;
        shift = shift * (s32)seat / MP_RIDE_RAMP_FRAMES;
        hoppingOff = !MultiplayerRide_IsRider(objectEvent);
    }
    sLift[id].clean = sprite->y2;
    sprite->y2 -= lift;
    sLift[id].written = sprite->y2;
    sLift[id].cleanX = sprite->x2;
    sprite->x2 += shift;
    sLift[id].writtenX = sprite->x2;
    sLift[id].spriteId = objectEvent->spriteId;
    sLift[id].active = TRUE;
    if (mount != NULL && mount->active && mount->spriteId < MAX_SPRITES)
    {
        struct Sprite *mountSprite = &gSprites[mount->spriteId];

        mount->fixedPriority = TRUE;
        // Facing the camera, the rider sits behind the mount's head: the mount is drawn in front. Otherwise the rider is
        // drawn in front (its back, or its near leg, over the mount), and always while another player hops off (their
        // Pokemon steps back from under them).
        if (objectEvent->facingDirection == DIR_SOUTH && !hoppingOff)
            mountSprite->subpriority = sprite->subpriority > 0 ? sprite->subpriority - 1 : 0;
        else
            mountSprite->subpriority = sprite->subpriority < 0xFF ? sprite->subpriority + 1 : 0xFF;
        mountSprite->oam.priority = sprite->oam.priority;
    }
#else
#line 305 // gba-line
    u32 id = objectEvent - gObjectEvents;
    struct ObjectEvent *mount = NULL;
    u8 lift = 0;

    if (id >= OBJECT_EVENTS_COUNT)
        return;
    if (objectEvent->isPlayer)
    {
        if (!LocalRiderLift(&lift, &mount))
            return;
    }
    else if (!MultiplayerWorld_GetRemoteRider(objectEvent, &lift, &mount))
    {
        return;
    }
    sLift[id].clean = sprite->y2;
    sprite->y2 -= lift;
    sLift[id].written = sprite->y2;
    sLift[id].spriteId = objectEvent->spriteId;
    sLift[id].active = TRUE;
    if (mount != NULL && mount->active && mount->spriteId < MAX_SPRITES)
    {
        struct Sprite *mountSprite = &gSprites[mount->spriteId];
        mount->fixedPriority = TRUE;
        mountSprite->subpriority = sprite->subpriority < 0xFF ? sprite->subpriority + 1 : 0xFF;
        mountSprite->oam.priority = sprite->oam.priority;
    }
#endif
#line 332 // gba-line
}

// ---------------------------------------------------------------------------
// Mount / dismount hops

#define tState      data[0]
#define tMounting   data[1]
#define tFromScript data[2]

static bool32 MovementDone(struct ObjectEvent *objectEvent)
{
    return objectEvent == NULL || ObjectEventClearHeldMovementIfFinished(objectEvent) != 0;
}

static void FinishTransition(u8 taskId)
{
    bool32 fromScript = gTasks[taskId].tFromScript;
    DestroyTask(taskId);
    sRide.busy = FALSE;
    if (fromScript)
        ScriptContext_Enable();
    else
        UnlockPlayerFieldControls();
}

static enum Direction DirectionBetween(s16 fromX, s16 fromY, s16 toX, s16 toY)
{
    if (toX > fromX)
        return DIR_EAST;
    if (toX < fromX)
        return DIR_WEST;
    if (toY > fromY)
        return DIR_SOUTH;
    if (toY < fromY)
        return DIR_NORTH;
    return DIR_NONE;
}

static void Task_RideTransition(u8 taskId)
{
    s16 *data = gTasks[taskId].data;
    struct ObjectEvent *player = GetPlayer();
    struct ObjectEvent *mount = GetMount();

    switch (tState)
    {
    case 0:
        if (!MovementDone(player))
            return;
        if (tMounting)
        {
            struct ObjectEvent *follower = GetFollowerObject();
            s16 x = player->currentCoords.x, y = player->currentCoords.y;
            enum Direction toPlayer = DIR_NONE;

            // The visible follower walks under its trainer and becomes the mount.
            if (follower != NULL && !follower->invisible
             && abs(follower->currentCoords.x - x) + abs(follower->currentCoords.y - y) == 1)
            {
                toPlayer = DirectionBetween(follower->currentCoords.x, follower->currentCoords.y, x, y);
                x = follower->currentCoords.x;
                y = follower->currentCoords.y;
            }
            FlagSet(FLAG_TEMP_HIDE_FOLLOWER);
            RemoveFollowingPokemon();
            if (!SpawnMount(x, y, toPlayer != DIR_NONE ? toPlayer : player->facingDirection))
            {
                FlagClear(FLAG_TEMP_HIDE_FOLLOWER);
                FinishTransition(taskId);
                return;
            }
            if (toPlayer != DIR_NONE)
                ObjectEventSetHeldMovement(GetMount(), GetWalkNormalMovementAction(toPlayer));
            tState = 1;
        }
        else
        {
            enum Direction back = GetOppositeDirection(player->facingDirection);
            s16 x = player->currentCoords.x, y = player->currentCoords.y;

#ifdef PORTABLE
            if (mount != NULL)
            {
                u8 lift;
                s8 shift;

                MultiplayerRide_GetSeat(mount->graphicsId, player->graphicsId, player->facingDirection, &lift, &shift);
                sDismountLift = lift;
                sDismountShift = shift;
                sDismountFrames = MP_RIDE_RAMP_FRAMES;
            }
#endif
#line 412 // gba-line
            sRide.riding = FALSE;
            PublishMount();
            ObjectEventSetGraphicsId(player, WalkerGraphicsId());
            ObjectEventTurn(player, player->facingDirection);
            ObjectEventForceSetHeldMovement(player, GetJumpInPlaceMovementAction(player->facingDirection));
            PlaySE(SE_LEDGE);
            MoveCoords(back, &x, &y);
            // The mount steps back to where a follower walks, when there is room.
            if (mount != NULL && GetCollisionAtCoords(mount, x, y, back) == COLLISION_NONE)
            {
                ObjectEventClearHeldMovementIfActive(mount);
                mount->fixedPriority = FALSE;
                ObjectEventSetHeldMovement(mount, GetWalkNormalMovementAction(back));
            }
            tState = 10;
        }
        break;
    case 1:
        if (mount == NULL)
        {
            FlagClear(FLAG_TEMP_HIDE_FOLLOWER);
            FinishTransition(taskId);
            return;
        }
        if (!MovementDone(mount))
            return;
        ObjectEventTurn(mount, player->facingDirection);
        sRide.riding = TRUE; // the hop lands on the mount's back
#ifdef PORTABLE
        sRamp = 0;
        sDismountFrames = 0;
        PublishMount();     // the other players see the hop on as it starts (their copy walks under the rider first)
#endif
#line 440 // gba-line
        ObjectEventSetGraphicsId(player, RiderGraphicsId());
        ObjectEventTurn(player, player->facingDirection);
        ObjectEventForceSetHeldMovement(player, GetJumpInPlaceMovementAction(player->facingDirection));
        PlaySE(SE_LEDGE);
        tState = 2;
        break;
    case 2:
        if (!MovementDone(player))
            return;
        PlayCry_Normal(sRide.species, 0);
        PublishMount();
        FinishTransition(taskId);
        break;
    case 10:
        if (!MovementDone(player) || (mount != NULL && !MovementDone(mount)))
            return;
        {
            s16 x = mount != NULL ? mount->currentCoords.x : player->currentCoords.x;
            s16 y = mount != NULL ? mount->currentCoords.y : player->currentCoords.y;
            enum Direction facing = mount != NULL ? mount->facingDirection : player->facingDirection;
            struct ObjectEvent *follower;

            RemoveMount();
            FlagClear(FLAG_TEMP_HIDE_FOLLOWER);
            UpdateFollowingPokemon();
            follower = GetFollowerObject();
            if (follower != NULL)
            {
                // The mount simply becomes the follower where it stands.
                MoveObjectEventToMapCoords(follower, x, y);
                ObjectEventTurn(follower, facing);
                follower->invisible = FALSE;
            }
        }
        FinishTransition(taskId);
        break;
    }
}

static bool32 StartTransition(bool32 mounting, bool32 fromScript)
{
    u8 taskId;

    if (sRide.busy)
        return FALSE;
    if (mounting)
    {
        if (!MultiplayerRide_CanMount() || !GetLeadMount(&sRide.species, &sRide.mountFlags, &sRide.personality))
            return FALSE;
    }
    else if (!sRide.riding)
    {
        return FALSE;
    }
    taskId = CreateTask(Task_RideTransition, 80);
    if (taskId == TASK_NONE)
        return FALSE;
    sRide.busy = TRUE;
    gTasks[taskId].tState = 0;
    gTasks[taskId].tMounting = mounting;
    gTasks[taskId].tFromScript = fromScript;
    if (!fromScript)
        LockPlayerFieldControls();
    return TRUE;
}

#undef tState
#undef tMounting
#undef tFromScript

bool32 MultiplayerRide_TryToggleFromField(void)
{
    u16 species;
    u8 flags;
    u32 personality;

    if (sRide.busy)
        return TRUE;
    if (sRide.riding)
        return StartTransition(FALSE, FALSE);
    if (MultiplayerRide_CanMount())
        return StartTransition(TRUE, FALSE);
    // Explain why not, instead of silently ignoring the button.
    if (!RidingAllowedHere())
    {
        ScriptContext_SetupScript(EventScript_MultiplayerRideNotHere);
        return TRUE;
    }
    if (GetLeadMount(&species, &flags, &personality))
    {
        StringCopy(gStringVar1, GetSpeciesName(species));
        ScriptContext_SetupScript(EventScript_MultiplayerRideNotAble);
        return TRUE;
    }
    return FALSE;
}

void MultiplayerRide_StartFromMenu(void)
{
    if (sRide.riding)
        StartTransition(FALSE, FALSE);
    else
        StartTransition(TRUE, FALSE);
}

void MultiplayerRide_CanRideFollower(void)
{
    gSpecialVar_Result = MultiplayerRide_CanMount();
}

static void Task_EnableScript(u8 taskId)
{
    DestroyTask(taskId);
    ScriptContext_Enable();
}

void MultiplayerRide_MountFromScript(void)
{
    // The script waits (waitstate) after this special: always resume it from
    // a task, never synchronously.
    if (!StartTransition(TRUE, TRUE))
        CreateTask(Task_EnableScript, 80);
}

// ---------------------------------------------------------------------------

void MultiplayerRide_Tick(bool32 fieldReady)
{
    struct ObjectEvent *player, *mount;
    u16 species;
    u8 flags;
    u32 personality;

#ifdef PORTABLE
    // Back on the field after a battle or a warp (rebuilt in the walking look, MultiplayerRide_BeforePlayerRespawn): the
    // rider's look and its mount come back on the first overworld frame the player is there, while the screen still
    // fades in, not once the field is calm (that showed the trainer walking on the ground, then popping onto the mount).
    // Not where riding is not allowed (into a building: the walking look stays, the ride ends once the field is calm).
    if (sRide.riding && !sRide.busy && !fieldReady && gMain.callback2 == CB2_Overworld && gMapHeader.mapLayout != NULL
     && gPlayerAvatar.objectEventId < OBJECT_EVENTS_COUNT && gObjectEvents[gPlayerAvatar.objectEventId].active
     && gObjectEvents[gPlayerAvatar.objectEventId].isPlayer && gObjectEvents[gPlayerAvatar.objectEventId].spriteId < MAX_SPRITES
     && gSprites[gObjectEvents[gPlayerAvatar.objectEventId].spriteId].inUse && PlayerOnFoot() && MapAllowsRiding())
    {
        player = GetPlayer();
        if (!FlagGet(FLAG_TEMP_HIDE_FOLLOWER))
            FlagSet(FLAG_TEMP_HIDE_FOLLOWER);
        if (GetFollowerObject() != NULL)
            RemoveFollowingPokemon();
        if (player->graphicsId == WalkerGraphicsId())
        {
            ObjectEventSetGraphicsId(player, RiderGraphicsId());
            ObjectEventTurn(player, player->facingDirection);
        }
        if (GetMount() == NULL)
            SpawnMount(player->currentCoords.x, player->currentCoords.y, player->facingDirection);
        // A script's steps (a door's step in, a cutscene's moves) are not mirrored as the player's own are: the mount
        // takes each with its rider, from where the step starts, and hides and shows with it (gone through a door; it
        // was left standing outside).
        if ((mount = GetMount()) != NULL)
        {
            if (player->heldMovementActive && !player->heldMovementFinished
             && (player->currentCoords.x != player->previousCoords.x || player->currentCoords.y != player->previousCoords.y)
             && !(mount->heldMovementActive && !mount->heldMovementFinished && mount->movementActionId == player->movementActionId))
            {
                if (mount->currentCoords.x != player->previousCoords.x || mount->currentCoords.y != player->previousCoords.y)
                    MoveObjectEventToMapCoords(mount, player->previousCoords.x, player->previousCoords.y);
                ObjectEventForceSetHeldMovement(mount, player->movementActionId);
            }
            mount->invisible = player->invisible;
        }
        return;
    }
#endif
#line 573 // gba-line
    if (!sRide.riding || sRide.busy || !fieldReady || gPlayerAvatar.objectEventId >= OBJECT_EVENTS_COUNT)
        return;
    player = GetPlayer();
    if (!player->active)
        return;
    // Riding stops where it is not allowed or when the lead Pokemon changed.
    if (!RidingAllowedHere() || !GetLeadMount(&species, &flags, &personality)
     || species != sRide.species || personality != sRide.personality)
    {
        ForceDismount(TRUE);
        UpdateFollowingPokemon();
        return;
    }
    // Temporary flags are cleared by every map load.
    if (!FlagGet(FLAG_TEMP_HIDE_FOLLOWER))
        FlagSet(FLAG_TEMP_HIDE_FOLLOWER);
    if (GetFollowerObject() != NULL)
        RemoveFollowingPokemon();
    if (player->graphicsId != RiderGraphicsId())
    {
        ObjectEventSetGraphicsId(player, RiderGraphicsId());
        ObjectEventTurn(player, player->facingDirection);
    }
    mount = GetMount();
    if (mount == NULL)
    {
        if (!SpawnMount(player->currentCoords.x, player->currentCoords.y, player->facingDirection))
            return;
        mount = GetMount();
    }
    // Scripted moves of the player are not mirrored: keep the mount under
    // its rider whenever both stand still.
    if (!(player->heldMovementActive && !player->heldMovementFinished)
     && !(mount->heldMovementActive && !mount->heldMovementFinished))
    {
        if (mount->currentCoords.x != player->currentCoords.x || mount->currentCoords.y != player->currentCoords.y)
            MoveObjectEventToMapCoords(mount, player->currentCoords.x, player->currentCoords.y);
        if (mount->facingDirection != player->facingDirection)
            ObjectEventTurn(mount, player->facingDirection);
        mount->currentElevation = player->currentElevation;
        mount->previousElevation = player->previousElevation;
    }
}

#ifdef PORTABLE
// The owner's report (2026-10-23): after a battle (or any screen that rebuilds the field) the rider sat on a Surf blob
// and the mount was gone. The field rebuilds the player's object from its graphics, and the rider's are the surfing
// pose (RiderGraphicsId), so the player was taken for surfing (SetPlayerAvatarExtraStateTransition: the Surf blob, the
// surfing flag, and MultiplayerRide_Tick then ended the ride, the player still in that pose). Rebuilt in the walking
// look, the player is on foot; the mount object is rebuilt with the field and the next tick puts the rider back on it.
void MultiplayerRide_BeforePlayerRespawn(struct ObjectEvent *player)
{
    if (sRide.riding && player->graphicsId == RiderGraphicsId())
        player->graphicsId = WalkerGraphicsId();
}
#endif
