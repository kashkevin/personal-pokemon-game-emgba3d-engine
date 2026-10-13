#ifndef GUARD_MULTIPLAYER_SPAWNS_H
#define GUARD_MULTIPLAYER_SPAWNS_H

// Shared visible wild Pokemon (EmeraldTogether Phase 3, ABI EMV1 v1).
//
// While the game is connected to an EmeraldTogether world, the host chooses
// every visible land wild Pokemon of the map the player stands on (species,
// level, personality, shiny, tile) and walks it around; every player on that
// map sees the same Pokemon in the same place. The game only draws them:
// talking to one asks the host for it (the first claim wins), a granted claim
// starts an ordinary wild battle with the host's Pokemon, and the battle's
// result goes back to the host, which removes a caught or defeated Pokemon for
// everybody. Offline, nothing here runs and the game's own overworld wild
// encounters (wild_encounter_ow.c) work exactly as before.
//
// Native PC builds only: the GBA ROM has no such mailbox, so its bytes stay
// pinned. The mailbox sits next to the EMW1 world mailbox and is moved by the
// native bridge (mp_bridge.c frames 'w', 'g' and 'n'); the app repository's
// spawn_mailbox.py is its byte-for-byte counterpart.

#ifdef PORTABLE

struct ObjectEvent;

#define MP_SPAWN_MAGIC          0x31564D45u // EMV1
#define MP_SPAWN_ABI_VERSION    1
#define MP_SPAWN_MAILBOX_BYTES  384
#define MP_SPAWN_MAX_SLOTS      12
#define MP_SPAWN_SLOT_BYTES     20

// Local object IDs 224-235: below the Berry Blender players (236-240) and the
// multiplayer avatars, followers, World Event, mount and generated encounters
// (241-252); far above any map's own objects.
#define MP_SPAWN_LOCAL_ID_BASE  224

#define MP_SPAWN_FLAG_ENABLED   (1 << 0) // connected to a world with shared spawns

#define MP_SPAWN_SLOT_SHINY     (1 << 0)
#define MP_SPAWN_SLOT_MINE      (1 << 1) // the host granted this player's claim
#define MP_SPAWN_SLOT_BUSY      (1 << 2) // another player is battling it
#define MP_SPAWN_SLOT_OUTBREAK  (1 << 3)
#define MP_SPAWN_SLOT_FRESH     (1 << 4) // just appeared: play the appear animation

#define MP_SPAWN_ACTION_NONE    0
#define MP_SPAWN_ACTION_CLAIM   1
#define MP_SPAWN_ACTION_RESULT  2 // result: MP_WORLD_RESULT_*
#define MP_SPAWN_ACTION_RELEASE 3

struct MultiplayerSpawnAction
{
    u32 seq; // increments for every new action; acknowledged inbound
    u8 kind;
    u8 result;
    u16 species;
    u32 spawnId;
    u8 mapGroup;
    u8 mapNum;
    u8 reserved[2];
};

struct MultiplayerSpawnShown
{
    u32 spawnId;
    s16 x; // map tile coordinates without MAP_OFFSET
    s16 y;
};

struct MultiplayerSpawnLocal
{
    struct MultiplayerSpawnAction action;
    u8 mapGroup;   // the map the player stands on
    u8 mapNum;
    u8 weather;    // the overworld weather shown (the host weighs species by it)
    u8 shownCount; // the spawn objects drawn right now
    struct MultiplayerSpawnShown shown[MP_SPAWN_MAX_SLOTS];
};

struct MultiplayerSpawnSlot
{
    u32 spawnId; // zero means no Pokemon
    u32 personality;
    u16 species;
    u8 level;
    u8 flags;
    s16 x; // map tile coordinates without MAP_OFFSET
    s16 y;
    u8 facing;  // DIR_SOUTH..DIR_EAST
    u8 moveSeq; // changes whenever the host moves or turns it
    u16 reserved;
};

struct MultiplayerSpawnMailbox
{
    // Game-owned: odd localGeneration while writing.
    u32 magic;
    u16 abiVersion;
    u16 byteSize;
    u32 localGeneration;
    struct MultiplayerSpawnLocal local;
    // Bridge-owned from here on: odd inboundGeneration while writing.
    u32 inboundGeneration;
    u32 flags;
    u32 actionAck; // last action seq the host has answered
    u8 mapGroup;   // the map these slots belong to
    u8 mapNum;
    u8 count;
    u8 reserved;
    struct MultiplayerSpawnSlot slots[MP_SPAWN_MAX_SLOTS];
};

extern volatile struct MultiplayerSpawnMailbox gMultiplayerSpawnMailbox;
extern const u32 gMultiplayerSpawnAbiVersion;

void MultiplayerSpawns_Init(void);
void MultiplayerSpawns_Tick(bool32 fieldReady, bool32 connected);
void MultiplayerSpawns_LeaveField(void);
// Connected to a world that shares wild Pokemon: the game makes no land
// encounters of its own (surfing, fishing and Rock Smash stay personal).
bool32 MultiplayerSpawns_IsActive(void);
bool32 MultiplayerSpawns_IsSpawnObject(const struct ObjectEvent *objectEvent);

// The World Event script's specials (EventScript_MultiplayerWorldEvent) serve
// spawn objects too; multiplayer_world.c hands them over.
bool32 MultiplayerSpawns_TryClaimTalked(void); // FALSE: the talked object is no spawn
bool32 MultiplayerSpawns_InInteraction(void);
void MultiplayerSpawns_StartBattle(void);
u8 MultiplayerSpawns_TakeResult(void);
bool32 MultiplayerSpawns_OnScriptedBattleEnd(void); // TRUE when it was a shared spawn's battle

#endif // PORTABLE
#endif // GUARD_MULTIPLAYER_SPAWNS_H
