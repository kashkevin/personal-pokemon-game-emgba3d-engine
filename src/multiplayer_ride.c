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
};

static EWRAM_DATA struct RideState sRide = {0};
static EWRAM_DATA struct RiderLift sLift[OBJECT_EVENTS_COUNT] = {0};

static void Task_RideTransition(u8 taskId);

void MultiplayerRide_Init(void)
{
    memset(&sRide, 0, sizeof(sRide));
    memset(sLift, 0, sizeof(sLift));
    sRide.mountObjectId = OBJECT_EVENTS_COUNT;
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
     || GetFirstInactiveObjectEventId() >= OBJECT_EVENTS_COUNT)
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
