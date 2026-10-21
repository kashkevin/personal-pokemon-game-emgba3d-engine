#ifndef GUARD_TOWER_LEGENDS_H
#define GUARD_TOWER_LEGENDS_H

// Battle Tower: Legends (native PC game only; the GBA ROM is unchanged). 100 floors of trainer battles at the floor's
// level, alone (a local trainer battle run by this game) or as a Duo (the native co-op battle, two players against the
// floor's two trainers). The host keeps the progress and pays the prizes; this game runs the battles, reports their
// results, keeps a mirror of its trainer's Tower record in the save and applies the host's Tower words. The contract
// with the app: docs/battle-tower-legends.md in the app repository.

#ifdef PORTABLE
#include "constants/tower_legends.h"
#include "data.h"
#include "pokemon.h"

#define MP_TOWER_ABI 1

// The two trainers a Tower battle fights: placeholders Emerald never battles. While a Tower battle is set up or runs,
// GetTrainerStructFromId (data.h) returns the floor's trainers for them; their trainer flags are never set.
#define TRAINER_TOWER_A TRAINER_RED
#define TRAINER_TOWER_B TRAINER_LEAF

#define TOWER_MODE_NONE 0
#define TOWER_MODE_SOLO 1
#define TOWER_MODE_DUO  2

// World action 7's result (the game -> the host).
#define TOWER_RESULT_STARTED 1
#define TOWER_RESULT_WON     2
#define TOWER_RESULT_LOST    3
#define TOWER_RESULT_REFUSED 4
#define TOWER_RESULT_DREW    5

// The Players menu: page 2 opens the Tower (START > PLAYER > TOWER); screen flags 4 start a solo Tower battle with the
// command below at screen offset 68; flags 5 open Hyper Training; bytes 238-239 name the native UI's Tower screen.
#define MP_MENU_PAGE_TOWER          2
#define MP_MENU_FLAG_TOWER_SOLO     4
#define MP_MENU_FLAG_HYPER_TRAINING 5
#define TOWER_COMMAND_MAGIC         0x31525754u // "TWR1"
#define TOWER_COMMAND_MODE_SOLO     1
// The native UI's Tower screen (byte 238; byte 239 its argument: the map's first floor shown, the previewed floor, the
// Duo's floor).
#define TOWER_STYLE_NONE     0
#define TOWER_STYLE_HOME     1
#define TOWER_STYLE_MAP      2
#define TOWER_STYLE_PREVIEW  3
#define TOWER_STYLE_SHOP     4
#define TOWER_STYLE_DUO      5
#define TOWER_STYLE_RECORDS  6
#define TOWER_STYLE_TRAINING 7
#define TOWER_STYLE_PRIZES   8
#define TOWER_STYLE_COUNT    9

struct TowerLegendsCommand
{
    u32 magic;
    u32 ticket;
    u32 tableHash;
    u8 floor;
    u8 mode;
    u8 reserved[18];
};

// Reward word kinds (the wallet stream, multiplayer_world.c).
#define MP_REWARD_TOWER_STATE 6
#define MP_REWARD_TOWER_MON   7
#define TOWER_STATE_OP_PROGRESS 0
#define TOWER_STATE_OP_CLAIMS   1

// Claim bits (the save record's claims, the host's CLAIMS word): the first-clear prize of each checkpoint floor, then
// the milestone Pokemon of 25, 50, 75 and 100 delivered.
#define TOWER_CLAIM_MON_SHIFT 12
#define TOWER_MILESTONE_COUNT 4

// The Duo battle code (the native co-op preparation's flags): the ladder layout with family 1 in bits 7-9.
#define TOWER_CODE_FAMILY      (1u << 7)
#define TOWER_CODE_FAMILY_MASK 0x380u

struct TowerLegendsFloor
{
    u8 kind;        // TOWER_FLOOR_*
    u8 checkpoint;
    u8 region;      // TOWER_REGION_*
    u8 difficulty;  // 1..5
    u16 solo, duoA, duoB; // gTowerLegendsTrainers indexes
    u16 battleMusic;
    const u8 *introText;
    const u8 *defeatText;
    const u8 *defeatTextB;
    const u8 *victoryText;
};

extern const struct TowerLegendsFloor gTowerLegendsFloors[TOWER_FLOOR_COUNT];
extern const struct Trainer gTowerLegendsTrainers[DIFFICULTY_COUNT][TOWER_LEGENDS_TRAINER_COUNT];
extern const u32 gMultiplayerTowerAbiVersion;

// GetTrainerStructFromId's hook: the floor's trainer for TRAINER_TOWER_A/B while a Tower battle is set up, else NULL.
const struct Trainer *TowerLegends_GetTrainer(u16 trainerId);
bool32 TowerLegends_IsBattleActive(void);
u32 TowerLegends_ActiveMode(void);
u32 TowerLegends_ActiveFloor(void);

// Experience: the cap of this game's badges, and an award clamped to it (none at or above the cap).
u32 TowerLegends_ExpCap(void);
u32 TowerLegends_ClampExp(struct Pokemon *mon, u32 reward, u32 cap);

// The save's record (SaveBlock3).
bool32 TowerLegends_RecordValid(void);
u32 TowerLegends_Highest(void);
u32 TowerLegends_Progress(void);
u32 TowerLegends_Claims(void);
bool32 TowerLegends_HasPendingResult(void);

// The floor table and the record as the native UI shows them (src/native_ui/screens/ui_tower.c): a floor (1..100, else
// NULL), one of its trainers (0 solo, 1 Duo A, 2 Duo B), the checkpoint a loss falls back to (the highest one <= the
// highest cleared floor, 0 when none) and the floor of each prize claim bit (0..11, else 0).
const struct TowerLegendsFloor *TowerLegends_Floor(u32 floor);
const struct Trainer *TowerLegends_FloorTrainer(u32 floor, u32 which);
u32 TowerLegends_Checkpoint(void);
u32 TowerLegends_PrizeFloor(u32 bit);

// The wallet words (multiplayer_world.c's MultiplayerWorld_ApplyRewardWord): the receipt's amount.
u32 TowerLegends_ApplyStateWord(u32 payload);
u32 TowerLegends_ApplyMonWord(u32 payload);

// The Players menu: a valid solo command (screen flags 4), and the style bytes.
bool32 TowerLegends_ValidCommand(const struct TowerLegendsCommand *command);
bool32 TowerLegends_ValidStyle(u32 style, u32 arg);
void TowerLegends_QueueSolo(const struct TowerLegendsCommand *command);
void TowerLegends_QueueHyperTraining(void);
void TowerLegends_OpenHyperTraining(void);

// The Duo code (multiplayer_ladder.c).
bool32 TowerLegends_IsTowerCode(u32 code);
bool32 TowerLegends_IsValidCode(u32 code);
u32 TowerLegends_EncodeDuo(u32 floor, bool32 awardExp);
u32 TowerLegends_CodeFloor(u32 code);
void TowerLegends_PrepareCoop(u32 code); // every core, as the battle starts: the floor's Duo trainers
void TowerLegends_EndCoop(void);
u16 TowerLegends_CoopMusic(u32 code);

// Every frame (multiplayer_ladder.c), and while connected (multiplayer_world.c: world actions).
void TowerLegends_Tick(void);
void TowerLegends_WorldTick(void);

// Specials (data/scripts/mp_tower_legends.inc).
void TowerLegends_StartSoloBattle(void);
void TowerLegends_BufferResult(void);

// The native self-tests and the test build's driver: a solo floor battle without a host (ticket 0 is never sent).
bool32 TowerLegends_TestStartSolo(u32 floor);
void TowerLegends_TestReset(void);
void TowerLegends_TestSetPending(u32 outcome, u32 ticket, u32 floor);
#endif // PORTABLE

#endif // GUARD_TOWER_LEGENDS_H
