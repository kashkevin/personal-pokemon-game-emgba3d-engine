#include "global.h"
#ifdef PORTABLE
// Battle Tower: Legends (native PC game only; include/tower_legends.h). The contract with the app (the host) is
// docs/battle-tower-legends.md in the app repository:
//   * the floors: 100 floors of one trainer each (and a Duo pair), every Pokemon at the floor's level, compiled from
//     src/data/tower_legends.party (tools/tower_legends/generate.py);
//   * a solo floor: the host answers the Players menu's choice with a command (screen flags 4); once the field is calm
//     this game fights the floor's trainer in an ordinary trainer battle of its own (no link: nobody waits for it), heals
//     the party and gives back its held items afterwards, and never whites out, pays or takes money;
//   * the result is written into the save's Tower record the moment the battle ends and is sent to the host (world
//     action 7) until the host settles it: a reload or a reconnect after a save sends it again;
//   * the host is the authority on the progress and the prizes: it sends the record's new state and every prize on the
//     wallet stream (reward kinds 6 and 7, exactly once per save), applied here;
//   * a Duo floor is the native co-op battle with a Tower code (the ladder layout, family 1): every core builds the
//     floor's two trainers from its own table;
//   * experience in a Tower battle stops at the cap of this game's own badges (solo here, Duo in multiplayer_coop.c).
#include <stddef.h>
#include "tower_legends.h"
#include "battle.h"
#include "battle_setup.h"
#include "data.h"
#include "event_data.h"
#include "item.h"
#include "main.h"
#include "multiplayer_battle.h"
#include "multiplayer_ladder.h"
#include "multiplayer_menu.h"
#include "multiplayer_save.h"
#include "multiplayer_world.h"
#include "overworld.h"
#include "palette.h"
#include "pokeball.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "script.h"
#include "script_pokemon_util.h"
#include "string_util.h"
#include "strings.h"
#include "item_menu.h"
#include "task.h"
#include "field_screen_effect.h"
#include "field_weather.h"
#include "event_object_lock.h"
#include "trainer_pools.h"
#include "constants/abilities.h"
#include "constants/battle.h"
#include "constants/battle_ai.h"
#include "constants/field_weather.h"
#include "constants/flags.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "constants/opponents.h"
#include "constants/pokeball.h"
#include "constants/region_map_sections.h"
#include "constants/songs.h"
#include "constants/trainers.h"

extern const u8 EventScript_TowerLegendsSolo[];

const u32 gMultiplayerTowerAbiVersion = MP_TOWER_ABI;

const struct Trainer gTowerLegendsTrainers[DIFFICULTY_COUNT][TOWER_LEGENDS_TRAINER_COUNT] =
{
#include "data/tower_legends.h"
};

#include "data/tower_legends_floors.h"

_Static_assert(sizeof(struct TowerLegendsCommand) == 32, "Tower command ABI");
_Static_assert(sizeof(struct MPTowerRecord) == 64 && offsetof(struct MPTowerRecord, crc32) == 60, "Tower record ABI");

static const u8 sText_DefaultVictory[] = _("Better luck next time!");

// The checkpoints: a loss sends the climb back to the floor after the highest one reached.
static const u8 sCheckpoints[] = {10, 20, 25, 30, 40, 50, 60, 70, 75, 80, 90, 100};
// Badges 0..8 -> the Tower experience cap (a Champion: 100).
static const u8 sExpCaps[NUM_BADGES + 1] = {15, 20, 25, 30, 40, 45, 50, 55, 60};

#define REPOST_FRAMES (60 * 20)

static EWRAM_DATA struct
{
    u8 mode;             // the Tower battle set up or running (TOWER_MODE_*): the trainer hook answers for it
    u8 floor;
    u8 queued;           // a solo command waits for a calm field
    u8 hyperTrainingQueued; // the Tower page's Hyper Training waits for a calm field
    u8 startedPending;   // "started" for ticket waits to be posted
    u8 refusedPending;   // "refused" for refusedTicket waits to be posted
    u8 refusedFloor;
    u8 lastOutcome;      // the last solo battle, for its message on the field
    u8 lastFloor;
    u8 lastHighest;      // the record before that battle
    u8 lastProgress;
    u16 repostTimer;
    u32 ticket;
    u32 refusedTicket;
    struct TowerLegendsCommand command;
    u16 heldItems[PARTY_SIZE];
    u32 personalities[PARTY_SIZE];
    u16 species[PARTY_SIZE]; // before the battle: an evolution after it may have used its held item
} sTower = {0};

static u32 Crc32(const u8 *data, u32 size)
{
    u32 crc = 0xFFFFFFFF;

    while (size--)
    {
        u32 i;

        crc ^= *data++;
        for (i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return crc ^ 0xFFFFFFFF;
}

static bool32 Zero(const u8 *bytes, u32 size)
{
    while (size--)
        if (*bytes++)
            return FALSE;
    return TRUE;
}

static const struct TowerLegendsFloor *Floor(u32 floor)
{
    return floor >= 1 && floor <= TOWER_FLOOR_COUNT ? &gTowerLegendsFloors[floor - 1] : NULL;
}

// ---------------------------------------------------------------------------------------------------------------------
// The trainers

const struct Trainer *TowerLegends_GetTrainer(u16 trainerId)
{
    const struct TowerLegendsFloor *floor;

    if (sTower.mode == TOWER_MODE_NONE || (trainerId != TRAINER_TOWER_A && trainerId != TRAINER_TOWER_B))
        return NULL;
    floor = Floor(sTower.floor);
    if (floor == NULL)
        return NULL;
    if (trainerId == TRAINER_TOWER_B)
        return &gTowerLegendsTrainers[DIFFICULTY_NORMAL][floor->duoB];
    return &gTowerLegendsTrainers[DIFFICULTY_NORMAL][sTower.mode == TOWER_MODE_SOLO ? floor->solo : floor->duoA];
}

const struct TowerLegendsFloor *TowerLegends_Floor(u32 floor)
{
    return Floor(floor);
}

const struct Trainer *TowerLegends_FloorTrainer(u32 floor, u32 which)
{
    const struct TowerLegendsFloor *entry = Floor(floor);

    if (entry == NULL || which > 2)
        return NULL;
    return &gTowerLegendsTrainers[DIFFICULTY_NORMAL][which == 0 ? entry->solo : which == 1 ? entry->duoA : entry->duoB];
}

bool32 TowerLegends_IsBattleActive(void) { return sTower.mode != TOWER_MODE_NONE; }
u32 TowerLegends_ActiveMode(void) { return sTower.mode; }
u32 TowerLegends_ActiveFloor(void) { return sTower.mode != TOWER_MODE_NONE ? sTower.floor : 0; }

// ---------------------------------------------------------------------------------------------------------------------
// Experience

u32 TowerLegends_ExpCap(void)
{
    u32 i, badges = 0;

    if (FlagGet(FLAG_SYS_GAME_CLEAR)) // the Champion: the Hall of Fame entered
        return MAX_LEVEL;
    for (i = 0; i < NUM_BADGES; i++)
        if (FlagGet(gBadgeFlags[i]))
            badges++;
    return sExpCaps[badges];
}

// The award, clamped so the Pokemon stops exactly at the cap's level; nothing at or above it.
u32 TowerLegends_ClampExp(struct Pokemon *mon, u32 reward, u32 cap)
{
    enum Species species = GetMonData(mon, MON_DATA_SPECIES);
    u32 exp = GetMonData(mon, MON_DATA_EXP), capExp;

    if (cap == 0 || cap > MAX_LEVEL)
        cap = MAX_LEVEL;
    if (species == SPECIES_NONE || GetMonData(mon, MON_DATA_LEVEL) >= cap)
        return 0;
    capExp = gExperienceTables[gSpeciesInfo[species].growthRate][cap];
    if (exp >= capExp)
        return 0;
    return reward > capExp - exp ? capExp - exp : reward;
}

// ---------------------------------------------------------------------------------------------------------------------
// The save's record

static struct MPTowerRecord *Record(void)
{
    return gSaveBlock3Ptr != NULL ? &gSaveBlock3Ptr->tower : NULL;
}

static u32 RecordCrc(const struct MPTowerRecord *record)
{
    return Crc32((const u8 *)record, offsetof(struct MPTowerRecord, crc32));
}

bool32 TowerLegends_RecordValid(void)
{
    const struct MPTowerRecord *record = Record();

    return record != NULL && record->magic == MP_TOWER_RECORD_MAGIC && record->version == MP_TOWER_RECORD_VERSION
        && record->highest <= TOWER_FLOOR_COUNT && record->progress >= 1 && record->progress <= TOWER_FLOOR_COUNT + 1
        && (record->pendingOutcome == 0 || record->pendingOutcome == TOWER_RESULT_WON
         || record->pendingOutcome == TOWER_RESULT_LOST || record->pendingOutcome == TOWER_RESULT_DREW)
        && record->crc32 == RecordCrc(record);
}

// The record to change: a missing (an older save) or damaged one starts again at floor 1 with nothing claimed.
static struct MPTowerRecord *RecordForWrite(void)
{
    struct MPTowerRecord *record = Record();

    if (record != NULL && !TowerLegends_RecordValid())
    {
        memset(record, 0, sizeof(*record));
        record->magic = MP_TOWER_RECORD_MAGIC;
        record->version = MP_TOWER_RECORD_VERSION;
        record->progress = 1;
    }
    return record;
}

static void RecordSeal(struct MPTowerRecord *record)
{
    record->crc32 = RecordCrc(record);
}

u32 TowerLegends_Highest(void) { return TowerLegends_RecordValid() ? Record()->highest : 0; }
u32 TowerLegends_Progress(void) { return TowerLegends_RecordValid() ? Record()->progress : 1; }
u32 TowerLegends_Claims(void) { return TowerLegends_RecordValid() ? Record()->claims : 0; }
bool32 TowerLegends_HasPendingResult(void) { return TowerLegends_RecordValid() && Record()->pendingOutcome != 0; }

static u32 CheckpointBelow(u32 highest)
{
    u32 i, checkpoint = 0;

    for (i = 0; i < ARRAY_COUNT(sCheckpoints); i++)
        if (sCheckpoints[i] <= highest)
            checkpoint = sCheckpoints[i];
    return checkpoint;
}

u32 TowerLegends_Checkpoint(void) { return CheckpointBelow(TowerLegends_Highest()); }
u32 TowerLegends_PrizeFloor(u32 bit) { return bit < ARRAY_COUNT(sCheckpoints) ? sCheckpoints[bit] : 0; }

static void BumpRevision(struct MPTowerRecord *record)
{
    if (++record->revision == 0)
        record->revision = 1; // the receipt's amount says what moved: zero means nothing
}

// Kind 6. Op 0: the host's progress (H, P) as it is, and the settled ticket's low byte clears the unsettled result.
// Op 1: claim bits, never cleared. The receipt's amount is the record's revision (zero: refused).
u32 TowerLegends_ApplyStateWord(u32 payload)
{
    u32 op = (payload >> 22) & 3;
    struct MPTowerRecord *record;

    if (op == TOWER_STATE_OP_PROGRESS)
    {
        u32 highest = (payload >> 15) & 0x7F, progress = (payload >> 8) & 0x7F, ticketByte = payload & 0xFF;

        if (highest > TOWER_FLOOR_COUNT || progress < 1 || progress > TOWER_FLOOR_COUNT + 1
         || (record = RecordForWrite()) == NULL)
            return 0;
        record->highest = highest;
        record->progress = progress;
        if (ticketByte != 0 && record->pendingOutcome != 0 && (record->pendingTicket & 0xFF) == ticketByte)
        {
            record->pendingOutcome = 0;
            record->pendingTicket = 0;
            record->pendingFloor = 0;
        }
    }
    else if (op == TOWER_STATE_OP_CLAIMS)
    {
        if ((payload & 0x3F0000) != 0 || (record = RecordForWrite()) == NULL)
            return 0;
        record->claims |= payload & 0xFFFF;
    }
    else
    {
        return 0;
    }
    BumpRevision(record);
    RecordSeal(record);
    return record->revision;
}

// ---------------------------------------------------------------------------------------------------------------------
// The milestone Pokemon (kind 7). The host chose the species; the Pokemon comes from a seed of (milestone, species, this
// trainer's ID), so a resend, a reload or a retry after a full party and PC makes the very same Pokemon.

static u32 NextSeed(u32 *seed)
{
    u32 x = *seed;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *seed = x != 0 ? x : 0x6D2B79F5;
    return *seed;
}

static EWRAM_DATA struct Pokemon sMilestoneMon = {0};

// The save's record marks the milestone delivered with the Pokemon (both reach the save together): the same word again
// makes nothing.
static void MarkDelivered(u32 milestone)
{
    struct MPTowerRecord *record = RecordForWrite();

    if (record == NULL)
        return;
    record->claims |= 1u << (TOWER_CLAIM_MON_SHIFT + milestone);
    RecordSeal(record);
}

// The receipt's amount: 1 made (party, else PC), 2 already delivered to this save, 0 nothing made.
u32 TowerLegends_ApplyMonWord(u32 payload)
{
    u32 milestone = (payload >> 22) & 3, species = (payload >> 11) & 0x7FF, level = (payload >> 4) & 0x7F;
    u32 otId = READ_OTID_FROM_SAVE, seed, personality, i, value;
    u8 seedBytes[12];
    bool32 shiny;
    struct Pokemon *mon = &sMilestoneMon;

    if ((payload & 0xF) != 0 || species == SPECIES_NONE || species >= NUM_SPECIES || !IsSpeciesEnabled(species)
     || species == SPECIES_EGG || level < 1 || level > MAX_LEVEL)
        return 0;
    if (TowerLegends_Claims() & (1u << (TOWER_CLAIM_MON_SHIFT + milestone)))
        return 2;
    seedBytes[0] = 'T'; seedBytes[1] = 'W'; seedBytes[2] = 'R'; seedBytes[3] = '7';
    seedBytes[4] = milestone; seedBytes[5] = species & 0xFF; seedBytes[6] = species >> 8; seedBytes[7] = level;
    seedBytes[8] = otId; seedBytes[9] = otId >> 8; seedBytes[10] = otId >> 16; seedBytes[11] = otId >> 24;
    seed = Crc32(seedBytes, sizeof(seedBytes));
    if (seed == 0)
        seed = 0x6D2B79F5;
    personality = NextSeed(&seed);
    CreateMon(mon, species, level, personality, OTID_STRUCT_PLAYER_ID);
    // 25, 75 and 100 are shiny; 50 follows this game's wild odds (1 in 4096, Update 30) for this personality.
    shiny = milestone == 1 ? GET_SHINY_VALUE(otId, personality) < MP_GEN6_SHINY_ODDS : TRUE;
    SetMonData(mon, MON_DATA_IS_SHINY, &shiny);
    for (i = 0; i < NUM_STATS; i++)
    {
        value = milestone == 0 ? NextSeed(&seed) % (MAX_PER_STAT_IVS + 1) : MAX_PER_STAT_IVS;
        SetMonData(mon, MON_DATA_HP_IV + i, &value);
    }
    value = BALL_CHERISH;
    SetMonData(mon, MON_DATA_POKEBALL, &value);
    value = MAPSEC_BATTLE_FRONTIER;
    SetMonData(mon, MON_DATA_MET_LOCATION, &value);
    SetMonData(mon, MON_DATA_MET_LEVEL, &level);
    CalculateMonStats(mon);
    GiveMonInitialMoveset(mon);
    value = GetMonData(mon, MON_DATA_MAX_HP);
    SetMonData(mon, MON_DATA_HP, &value);
    switch (GiveScriptedMonToPlayer(mon, PARTY_SIZE))
    {
    case MON_GIVEN_TO_PARTY:
        MarkDelivered(milestone);
        MultiplayerWorld_ShowLocalNotice("BATTLE TOWER PRIZE!|A Pokemon joined your party.");
        return 1;
    case MON_GIVEN_TO_PC:
        MarkDelivered(milestone);
        MultiplayerWorld_ShowLocalNotice("BATTLE TOWER PRIZE!|A Pokemon was sent to your PC.");
        return 1;
    default:
        MultiplayerWorld_ShowLocalNotice("BATTLE TOWER PRIZE waiting:|make room in your party or PC.");
        return 0;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The Players menu's command and style

bool32 TowerLegends_ValidCommand(const struct TowerLegendsCommand *command)
{
    return command->magic == TOWER_COMMAND_MAGIC && command->ticket != 0 && (command->ticket & 0xFF) != 0
        && command->floor >= 1 && command->floor <= TOWER_FLOOR_COUNT && command->mode == TOWER_COMMAND_MODE_SOLO
        && Zero(command->reserved, sizeof(command->reserved));
}

bool32 TowerLegends_ValidStyle(u32 style, u32 arg)
{
    return style < TOWER_STYLE_COUNT && arg <= TOWER_FLOOR_COUNT && (style != 0 || arg == 0);
}

void TowerLegends_QueueSolo(const struct TowerLegendsCommand *command)
{
    sTower.command = *command;
    sTower.queued = TRUE;
}

// ---------------------------------------------------------------------------------------------------------------------
// A solo floor

static bool32 HasAbleMon(void)
{
    u32 i;

    for (i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][i];

        if (GetMonData(mon, MON_DATA_SPECIES) != SPECIES_NONE && !GetMonData(mon, MON_DATA_IS_EGG)
         && GetMonData(mon, MON_DATA_HP) != 0)
            return TRUE;
    }
    return FALSE;
}

static void Refuse(u32 ticket, u32 floor, const char *why)
{
    sTower.refusedTicket = ticket;
    sTower.refusedFloor = floor;
    sTower.refusedPending = ticket != 0;
    MultiplayerWorld_ShowLocalNotice(why);
}

static bool32 FieldCalm(void)
{
    return gMain.callback2 == CB2_Overworld && !ScriptContext_IsEnabled() && !ArePlayerFieldControlsLocked()
        && !gPaletteFade.active && !MultiplayerBattle_IsActive() && !MultiplayerMenu_IsActive();
}

void TowerLegends_Tick(void)
{
    // A Duo battle's trainers answer only while its native battle runs (an abandoned one leaves nothing behind).
    if (sTower.mode == TOWER_MODE_DUO && !MultiplayerBattle_IsActive())
        sTower.mode = TOWER_MODE_NONE;
    if (sTower.hyperTrainingQueued && FieldCalm())
    {
        sTower.hyperTrainingQueued = FALSE;
        TowerLegends_OpenHyperTraining();
        return;
    }
    if (!sTower.queued || !FieldCalm())
        return;
    sTower.queued = FALSE;
    if (gSaveBlock3Ptr == NULL || sTower.command.tableHash != TOWER_LEGENDS_TABLE_HASH)
        Refuse(sTower.command.ticket, sTower.command.floor, "BATTLE TOWER: this game's floors|differ from the host's.");
    else if (TowerLegends_HasPendingResult())
        Refuse(sTower.command.ticket, sTower.command.floor, "BATTLE TOWER: your last result|is still being sent.");
    else if (!HasAbleMon())
        Refuse(sTower.command.ticket, sTower.command.floor, "BATTLE TOWER: no POKeMON|can battle.");
    else
    {
        // The floor begins here (its intro, then its battle): "started" goes to the host at once, so the host's window
        // for an issued battle never waits on how long the player reads the intro.
        sTower.ticket = sTower.command.ticket;
        sTower.floor = sTower.command.floor;
        sTower.startedPending = sTower.ticket != 0;
        ScriptContext_SetupScript(EventScript_TowerLegendsSolo);
    }
}

// Held items come back after the battle (a Berry eaten, an item knocked off or stolen), matched by personality so a
// changed party order or an evolution keeps each Pokemon's own item.
static void SaveHeldItems(void)
{
    u32 i;

    for (i = 0; i < PARTY_SIZE; i++)
    {
        sTower.heldItems[i] = GetMonData(&gParties[B_TRAINER_PLAYER][i], MON_DATA_HELD_ITEM);
        sTower.personalities[i] = GetMonData(&gParties[B_TRAINER_PLAYER][i], MON_DATA_PERSONALITY);
        sTower.species[i] = GetMonData(&gParties[B_TRAINER_PLAYER][i], MON_DATA_SPECIES);
    }
}

// The evolution from `from` to `to` needs `item` held, and so took it (IF_HOLD_ITEM: Gligar's Razor Fang at night).
static bool32 EvolutionTookItem(u32 from, u32 to, u32 item)
{
    const struct Evolution *evolutions = GetSpeciesEvolutions(from);
    u32 i, j;

    if (evolutions == NULL || item == ITEM_NONE)
        return FALSE;
    for (i = 0; evolutions[i].method != EVOLUTIONS_END; i++)
    {
        if (evolutions[i].targetSpecies != to || evolutions[i].params == NULL)
            continue;
        for (j = 0; evolutions[i].params[j].condition != CONDITIONS_END; j++)
        {
            if (evolutions[i].params[j].condition == IF_HOLD_ITEM && evolutions[i].params[j].arg1 == item)
                return TRUE;
        }
    }
    return FALSE;
}

// Each saved item comes back once, to the first Pokemon in party order with its personality: a Shedinja made by the
// battle's evolution of a Nincada shares the Nincada's personality, follows the Ninjask in the party and holds nothing.
// An item a post-battle evolution used stays used.
static void RestoreHeldItems(void)
{
    bool8 used[PARTY_SIZE] = {FALSE};
    u32 i, j;

    for (i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][i];
        u32 personality = GetMonData(mon, MON_DATA_PERSONALITY);
        u32 species = GetMonData(mon, MON_DATA_SPECIES);

        if (species == SPECIES_NONE)
            continue;
        for (j = 0; j < PARTY_SIZE; j++)
        {
            if (used[j] || sTower.species[j] == SPECIES_NONE || sTower.personalities[j] != personality)
                continue;
            used[j] = TRUE;
            if (species == sTower.species[j] || !EvolutionTookItem(sTower.species[j], species, sTower.heldItems[j]))
                SetMonData(mon, MON_DATA_HELD_ITEM, &sTower.heldItems[j]);
            break;
        }
    }
}

static void CB2_EndTowerLegendsBattle(void)
{
    u32 outcome = gBattleOutcome == B_OUTCOME_WON ? TOWER_RESULT_WON
                : gBattleOutcome == B_OUTCOME_DREW ? TOWER_RESULT_DREW : TOWER_RESULT_LOST;
    struct MPTowerRecord *record;

    RestoreHeldItems();
    HealPlayerParty();
    sTower.lastOutcome = outcome;
    sTower.lastFloor = sTower.floor;
    sTower.lastHighest = TowerLegends_Highest();
    sTower.lastProgress = TowerLegends_Progress();
    if (sTower.ticket != 0 && (record = RecordForWrite()) != NULL)
    {
        record->pendingOutcome = outcome;
        record->pendingTicket = sTower.ticket;
        record->pendingFloor = sTower.floor;
        RecordSeal(record);
        sTower.repostTimer = 0; // sent at once
    }
    sTower.mode = TOWER_MODE_NONE;
    gIsDebugBattle = FALSE;
    SetMainCallback2(CB2_ReturnToFieldContinueScriptPlayMapMusic);
}

// Special (EventScript_TowerLegendsSolo): the floor's intro in gStringVar4.
void TowerLegends_BufferIntro(void)
{
    const struct TowerLegendsFloor *floor = Floor(sTower.floor);

    StringExpandPlaceholders(gStringVar4, floor != NULL && floor->introText != NULL ? floor->introText : gText_EmptyString2);
}

// Special (EventScript_TowerLegendsSolo, then waitstate): the battle against the floor's trainer.
void TowerLegends_StartSoloBattle(void)
{
    const struct TowerLegendsFloor *floor = Floor(sTower.floor);

    if (floor == NULL)
    {
        ScriptContext_Enable();
        return;
    }
    sTower.mode = TOWER_MODE_SOLO;
    SaveHeldItems();
    memset(&gTrainerBattleParameter, 0, sizeof(gTrainerBattleParameter));
    TRAINER_BATTLE_PARAM.opponentA = TRAINER_TOWER_A;
    TRAINER_BATTLE_PARAM.opponentB = TRAINER_NONE;
    TRAINER_BATTLE_PARAM.defeatTextA = (u8 *)floor->defeatText;
    TRAINER_BATTLE_PARAM.victoryText = (u8 *)(floor->victoryText != NULL ? floor->victoryText : sText_DefaultVictory);
    BattleSetup_StartTowerLegendsBattle(TRAINER_TOWER_A, floor->battleMusic, CB2_EndTowerLegendsBattle);
}

static u8 *AppendNumber(u8 *end, u32 value)
{
    return ConvertIntToDecimalStringN(end, value, STR_CONV_MODE_LEFT_ALIGN, 3);
}

static const u8 sText_FloorWon1[] = _("FLOOR ");
static const u8 sText_FloorWon2[] = _(" cleared!\nNext: floor ");
static const u8 sText_FloorWonTop[] = _(" cleared!\nThe Tower's top! Congratulations!");
static const u8 sText_ReplayWon[] = _(" replayed: you won!");
static const u8 sText_FloorLost1[] = _("You lost on floor ");
static const u8 sText_FloorLost2[] = _(".\nYour climb goes on at floor ");
static const u8 sText_ReplayLost[] = _(".\nA replay: your climb is unchanged.");
static const u8 sText_Period[] = _(".");

// Special: the last solo battle's result in gStringVar4 (the host's word confirms the progress and pays the prizes).
void TowerLegends_BufferResult(void)
{
    u8 *end = gStringVar4;
    bool32 progress = sTower.lastFloor == sTower.lastProgress;

    if (sTower.lastOutcome == TOWER_RESULT_WON)
    {
        end = StringCopy(end, sText_FloorWon1);
        end = AppendNumber(end, sTower.lastFloor);
        if (!progress)
            end = StringCopy(end, sText_ReplayWon);
        else if (sTower.lastFloor >= TOWER_FLOOR_COUNT)
            end = StringCopy(end, sText_FloorWonTop);
        else
        {
            end = StringCopy(end, sText_FloorWon2);
            end = AppendNumber(end, sTower.lastFloor + 1);
            end = StringCopy(end, sText_Period);
        }
    }
    else
    {
        end = StringCopy(end, sText_FloorLost1);
        end = AppendNumber(end, sTower.lastFloor);
        if (!progress)
            end = StringCopy(end, sText_ReplayLost);
        else
        {
            end = StringCopy(end, sText_FloorLost2);
            end = AppendNumber(end, CheckpointBelow(sTower.lastHighest) + 1);
            end = StringCopy(end, sText_Period);
        }
    }
}

// While connected: a refusal or "started" first, then the unsettled result (again every 20 s until settled).
void TowerLegends_WorldTick(void)
{
    const struct MPTowerRecord *record;

    if (sTower.refusedPending)
    {
        if (MultiplayerWorld_PostTowerAction(TOWER_RESULT_REFUSED, sTower.refusedTicket, sTower.refusedFloor))
            sTower.refusedPending = FALSE;
        return;
    }
    if (sTower.startedPending)
    {
        if (MultiplayerWorld_PostTowerAction(TOWER_RESULT_STARTED, sTower.ticket, sTower.floor))
            sTower.startedPending = FALSE;
        return;
    }
    if (!TowerLegends_HasPendingResult() || sTower.mode != TOWER_MODE_NONE)
        return;
    if (sTower.repostTimer != 0)
    {
        sTower.repostTimer--;
        return;
    }
    record = Record();
    if (MultiplayerWorld_PostTowerAction(record->pendingOutcome, record->pendingTicket, record->pendingFloor))
        sTower.repostTimer = REPOST_FRAMES;
}

void TowerLegends_QueueHyperTraining(void)
{
    sTower.hyperTrainingQueued = TRUE;
}

static void Task_OpenHyperTrainingBag(u8 taskId)
{
    if (gPaletteFade.active)
        return;
    CleanupOverworldWindowsAndTilemaps();
    GoToBagMenu(ITEMMENULOCATION_FIELD, POCKET_ITEMS, CB2_ReturnToField);
    DestroyTask(taskId);
}

// The Tower page's Hyper Training: the Bag on its Items pocket, where a Bottle Cap or a Gold Bottle Cap is used on a
// Pokemon (party_menu.c: the stat list shows each stat's natural IV and whether it is trained).
void TowerLegends_OpenHyperTraining(void)
{
    if (!CheckBagHasItem(ITEM_BOTTLE_CAP, 1) && !CheckBagHasItem(ITEM_GOLD_BOTTLE_CAP, 1))
    {
        MultiplayerWorld_ShowLocalNotice("HYPER TRAINING: you need a|Bottle Cap or Gold Bottle Cap.");
        return;
    }
    LockPlayerFieldControls();
    FadeScreen(FADE_TO_BLACK, 0);
    CreateTask(Task_OpenHyperTrainingBag, 10);
}

// ---------------------------------------------------------------------------------------------------------------------
// A Duo floor (the native co-op battle)

bool32 TowerLegends_IsTowerCode(u32 code)
{
    return (code & MP_LADDER_FLAG) != 0 && (code & TOWER_CODE_FAMILY_MASK) == TOWER_CODE_FAMILY;
}

u32 TowerLegends_CodeFloor(u32 code)
{
    return (code >> 20) & 0x3FF;
}

bool32 TowerLegends_IsValidCode(u32 code)
{
    u32 floor = TowerLegends_CodeFloor(code);

    return TowerLegends_IsTowerCode(code) && floor >= 1 && floor <= TOWER_FLOOR_COUNT
        && ((code >> 10) & 0x3FF) == 0 && (code & 0x7F) == floor;
}

u32 TowerLegends_EncodeDuo(u32 floor, bool32 awardExp)
{
    return MP_LADDER_FLAG | (awardExp ? MP_LADDER_EXP : 0) | (floor << 20) | TOWER_CODE_FAMILY | floor;
}

void TowerLegends_PrepareCoop(u32 code)
{
    const struct TowerLegendsFloor *floor;

    if (!TowerLegends_IsValidCode(code))
        return;
    floor = Floor(TowerLegends_CodeFloor(code));
    sTower.mode = TOWER_MODE_DUO;
    sTower.floor = TowerLegends_CodeFloor(code);
    TRAINER_BATTLE_PARAM.defeatTextA = (u8 *)floor->defeatText;
    TRAINER_BATTLE_PARAM.defeatTextB = (u8 *)floor->defeatTextB;
    TRAINER_BATTLE_PARAM.victoryText = (u8 *)(floor->victoryText != NULL ? floor->victoryText : sText_DefaultVictory);
}

void TowerLegends_EndCoop(void)
{
    if (sTower.mode == TOWER_MODE_DUO)
        sTower.mode = TOWER_MODE_NONE;
}

u16 TowerLegends_CoopMusic(u32 code)
{
    return TowerLegends_IsValidCode(code) ? Floor(TowerLegends_CodeFloor(code))->battleMusic : 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Tests (the native self-tests and the test build's driver): a solo floor without a host.

bool32 TowerLegends_TestStartSolo(u32 floor)
{
    struct MPTowerRecord *record;

    if (Floor(floor) == NULL)
        return FALSE;
    if ((record = RecordForWrite()) != NULL && record->pendingOutcome != 0)
    {
        record->pendingOutcome = 0;
        record->pendingTicket = 0;
        record->pendingFloor = 0;
        RecordSeal(record);
    }
    sTower.ticket = 0x01000000u | (floor << 8) | 0x7E; // never sent: no world in a test
    sTower.floor = floor;
    ScriptContext_SetupScript(EventScript_TowerLegendsSolo);
    return TRUE;
}

void TowerLegends_TestReset(void)
{
    memset(&sTower, 0, sizeof(sTower));
}

// The held items a solo floor saves before its battle and gives back after it (the native self-tests).
void TowerLegends_TestSaveHeldItems(void)
{
    SaveHeldItems();
}

void TowerLegends_TestRestoreHeldItems(void)
{
    RestoreHeldItems();
}

// A test fixture (the test build's PKMN_TEST_TOWER_RECORD with save:1): the record as a climb that cleared `highest`
// and goes on at `progress` left it (nothing claimed, nothing waiting).
void TowerLegends_TestSetProgress(u32 highest, u32 progress)
{
    struct MPTowerRecord *record = RecordForWrite();

    if (record == NULL || highest > TOWER_FLOOR_COUNT || progress < 1 || progress > TOWER_FLOOR_COUNT + 1)
        return;
    record->highest = highest;
    record->progress = progress;
    BumpRevision(record);
    RecordSeal(record);
}

void TowerLegends_TestSetPending(u32 outcome, u32 ticket, u32 floor)
{
    struct MPTowerRecord *record = RecordForWrite();

    record->pendingOutcome = outcome;
    record->pendingTicket = ticket;
    record->pendingFloor = floor;
    RecordSeal(record);
}
#endif // PORTABLE
