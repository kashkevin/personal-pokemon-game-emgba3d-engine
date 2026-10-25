// Native PC build: self-test of the stat stages kept until a Pokemon faints or the battle ends, and of what a link
// battle's replaying game shows of them (platform/battle_stat_stages.c). Project-owned. Runs the game's own code on
// battle state built here, then exits.
//   --selftest-stat-stages
//  - switches through the game's own script commands (getswitchedmondata, switchindataupdate) with the game's own
//    controller answering (BtlController_HandleGetMonData): a Pokemon switched out keeps its stages and has them again
//    when it comes back, per party member and per party (the player's, the opponent's, an in-game partner's); a forced
//    switch (Roar's script, BS_TARGET) the same; a Pokemon that faints (FaintClearSetData) loses them; Baton Pass gives
//    the user's to the one coming in and the user comes back with none; a new battle starts with none;
//  - a battle between players: the master's command (every battler's stages) and its checks, the master's engine
//    waiting for it, and the replaying game (its gBattleMons empty) showing exactly the master's stages, also those of a
//    Pokemon switched back in;
//  - the tags as the screens print them, in Showdown's order.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "battle.h"
#include "battle_controllers.h"
#include "battle_main.h"
#include "battle_script_commands.h"
#include "battle_scripts.h"
#include "battle_util.h"
#include "battle_util2.h"
#include "load_save.h"
#include "malloc.h"
#include "multiplayer_battle_schema.h"
#include "pokemon.h"
#include "constants/battle_script_commands.h"
#include "constants/moves.h"
#include "constants/species.h"
#include "platform/battle_stat_stages.h"

static int sChecks, sFailures;

static void Expect(const char *what, bool32 ok)
{
    sChecks++;
    if (!ok)
        sFailures++;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    fflush(stdout);
}

static void ControllerDone(enum BattlerId battler)
{
    MarkBattleControllerIdleOnLocal(battler);
}

static void MakeMon(struct Pokemon *mon, enum Species species, u32 personality)
{
    CreateMon(mon, species, 40, personality, OTID_STRUCT_PLAYER_ID);
    CalculateMonStats(mon);
}

// One of the game's battle script commands with one battler argument (BS_ATTACKER, BS_TARGET...).
static void RunBattleScriptCommand(u8 opcode, u8 battlerArg)
{
    static u8 script[4];

    script[0] = opcode;
    script[1] = battlerArg;
    script[2] = 0;
    gBattlescriptCurrInstr = script;
    gBattleScriptingCommandsTable[opcode]();
}

// A switch as the game's switch scripts make it: getswitchedmondata, the battler's controller answering the data
// request, switchindataupdate.
static void Switch(enum BattlerId battler, u8 battlerArg, u32 partyIndex)
{
    gBattleStruct->monToSwitchIntoId[battler] = partyIndex;
    gBattleControllerExecFlags = 0;
    RunBattleScriptCommand(B_SCR_OP_GETSWITCHEDMONDATA, battlerArg);
    BtlController_HandleGetMonData(battler);
    gBattleControllerExecFlags = 0;
    RunBattleScriptCommand(B_SCR_OP_SWITCHINDATAUPDATE, battlerArg);
}

static s32 Stage(enum BattlerId battler, enum Stat stat)
{
    return gBattleMons[battler].statStages[stat] - DEFAULT_STAT_STAGE;
}

static bool32 Neutral(enum BattlerId battler)
{
    for (u32 i = 0; i < NUM_BATTLE_STATS; i++)
    {
        if (gBattleMons[battler].statStages[i] != DEFAULT_STAT_STAGE)
            return FALSE;
    }
    return TRUE;
}

static bool32 NothingStored(enum BattleTrainer trainer, u32 slot)
{
    for (u32 i = 0; i < NUM_BATTLE_STATS; i++)
    {
        if (StatStages_Stored(trainer, slot, i) != 0)
            return FALSE;
    }
    return TRUE;
}

static void LoadBattler(enum BattlerId battler, struct Pokemon *party, u32 slot)
{
    gBattlerPartyIndexes[battler] = slot;
    PokemonToBattleMon(&party[slot], &gBattleMons[battler]);
}

// The tags a battler shows, as one string ("ATK+2 SPE-1").
static void TagLine(enum BattlerId battler, char *out, size_t size)
{
    struct StatStageTag tags[NUM_BATTLE_STATS];
    u32 count = StatStages_Tags(battler, tags);

    out[0] = '\0';
    for (u32 i = 0; i < count; i++)
    {
        strncat(out, tags[i].text, size - strlen(out) - 1);
        if (i + 1 < count)
            strncat(out, " ", size - strlen(out) - 1);
    }
    printf("     battler %u shows \"%s\"\n", battler, out);
}

static void SinglesBattle(void)
{
    gBattleTypeFlags = BATTLE_TYPE_TRAINER | BATTLE_TYPE_IS_MASTER;
    gBattlersCount = 2;
    gBattlerPositions[0] = B_POSITION_PLAYER_LEFT;
    gBattlerPositions[1] = B_POSITION_OPPONENT_LEFT;
    gBattlerBattleController[0] = BATTLE_CONTROLLER_PLAYER;
    gBattlerBattleController[1] = BATTLE_CONTROLLER_OPPONENT;
    gBattlerControllerEndFuncs[0] = ControllerDone;
    gBattlerControllerEndFuncs[1] = ControllerDone;
    gAbsentBattlerFlags = 0;
    gBattleOutcome = 0;
}

static void CheckSingles(void)
{
    struct Pokemon *player = gParties[B_TRAINER_PLAYER], *foe = gParties[B_TRAINER_OPPONENT_A];
    u16 noHp = 0, someHp = 30;

    ZeroPlayerPartyMons();
    ZeroEnemyPartyMons();
    MakeMon(&player[0], SPECIES_SNORLAX, 0x10);
    MakeMon(&player[1], SPECIES_ZIGZAGOON, 0x11);
    MakeMon(&player[2], SPECIES_PIKACHU, 0x12);
    MakeMon(&foe[0], SPECIES_POOCHYENA, 0x20);
    MakeMon(&foe[1], SPECIES_WURMPLE, 0x21);
    SinglesBattle();
    LoadBattler(0, player, 0);
    LoadBattler(1, foe, 0);
    gCurrentMove = MOVE_TACKLE;

    printf("a switch keeps the stages of the Pokemon that leaves\n");
    gBattleMons[0].statStages[STAT_ATK] += 2;   // Swords Dance
    gBattleMons[0].statStages[STAT_SPEED] -= 1; // a Speed drop
    gBattleMons[1].statStages[STAT_DEF] -= 2;   // Screech
    gBattlerAttacker = 0;
    Switch(0, BS_ATTACKER, 1);
    Expect("Zigzagoon came in for Snorlax", gBattlerPartyIndexes[0] == 1 && gBattleMons[0].species == SPECIES_ZIGZAGOON);
    Expect("... at stage 0 in every stat", Neutral(0));
    Expect("Snorlax keeps Attack +2 and Speed -1 in the party",
           StatStages_Stored(B_TRAINER_PLAYER, 0, STAT_ATK) == 2 && StatStages_Stored(B_TRAINER_PLAYER, 0, STAT_SPEED) == -1);
    Expect("the opponent's Poochyena on the field still at Defense -2", Stage(1, STAT_DEF) == -2);

    printf("... and has them again when it comes back in\n");
    gBattleMons[0].statStages[STAT_EVASION] += 1; // Zigzagoon: Double Team
    Switch(0, BS_ATTACKER, 0);
    Expect("Snorlax back: Attack +2, Speed -1, nothing else",
           gBattleMons[0].species == SPECIES_SNORLAX && Stage(0, STAT_ATK) == 2 && Stage(0, STAT_SPEED) == -1
           && Stage(0, STAT_DEF) == 0 && Stage(0, STAT_EVASION) == 0);
    Expect("... nothing kept for it any more (it is on the field)", NothingStored(B_TRAINER_PLAYER, 0));
    Expect("Zigzagoon keeps its evasion +1", StatStages_Stored(B_TRAINER_PLAYER, 1, STAT_EVASION) == 1);

    printf("each party keeps its own (the opponent's slot 0 is not the player's)\n");
    gBattlerAttacker = 1;
    Switch(1, BS_ATTACKER, 1);
    Expect("the opponent's Poochyena keeps Defense -2", StatStages_Stored(B_TRAINER_OPPONENT_A, 0, STAT_DEF) == -2);
    Expect("... the player's slot 0 has nothing kept", NothingStored(B_TRAINER_PLAYER, 0));
    Expect("the opponent's Wurmple comes in at 0", Neutral(1));
    Switch(1, BS_ATTACKER, 0);
    Expect("the opponent's Poochyena back at Defense -2", gBattleMons[1].species == SPECIES_POOCHYENA && Stage(1, STAT_DEF) == -2);

    printf("a forced switch (Roar's script: getswitchedmondata BS_TARGET) keeps them too\n");
    gCurrentMove = MOVE_ROAR;
    gBattlerAttacker = 1;
    gBattlerTarget = 0;
    Switch(0, BS_TARGET, 2);
    Expect("Pikachu dragged in at 0", gBattleMons[0].species == SPECIES_PIKACHU && Neutral(0));
    Expect("Snorlax, dragged out, keeps Attack +2 and Speed -1",
           StatStages_Stored(B_TRAINER_PLAYER, 0, STAT_ATK) == 2 && StatStages_Stored(B_TRAINER_PLAYER, 0, STAT_SPEED) == -1);
    Switch(0, BS_TARGET, 0);
    Expect("Snorlax dragged back in: Attack +2, Speed -1", Stage(0, STAT_ATK) == 2 && Stage(0, STAT_SPEED) == -1);
    gCurrentMove = MOVE_TACKLE;
    gBattlerAttacker = 0;

    printf("Baton Pass gives the user's stages to the one coming in; the user comes back with none\n");
    Expect("Zigzagoon (in the party) keeps evasion +1", StatStages_Stored(B_TRAINER_PLAYER, 1, STAT_EVASION) == 1);
    gCurrentMove = MOVE_BATON_PASS;
    Switch(0, BS_ATTACKER, 1);
    Expect("Zigzagoon has Snorlax's Attack +2 and Speed -1 (not its own evasion +1)",
           Stage(0, STAT_ATK) == 2 && Stage(0, STAT_SPEED) == -1 && Stage(0, STAT_EVASION) == 0);
    Expect("... Snorlax, which passed them, keeps nothing", NothingStored(B_TRAINER_PLAYER, 0));
    gCurrentMove = MOVE_TACKLE;
    Switch(0, BS_ATTACKER, 0);
    Expect("Snorlax back after its Baton Pass: stage 0 in every stat", Neutral(0));
    Expect("Zigzagoon keeps the passed Attack +2 and Speed -1",
           StatStages_Stored(B_TRAINER_PLAYER, 1, STAT_ATK) == 2 && StatStages_Stored(B_TRAINER_PLAYER, 1, STAT_SPEED) == -1
           && StatStages_Stored(B_TRAINER_PLAYER, 1, STAT_EVASION) == 0);

    printf("a Pokemon that faints loses them\n");
    Switch(0, BS_ATTACKER, 1); // Zigzagoon in with Attack +2, Speed -1
    Expect("Zigzagoon in again with Attack +2, Speed -1", Stage(0, STAT_ATK) == 2 && Stage(0, STAT_SPEED) == -1);
    gBattleMons[0].hp = 0;
    SetMonData(&player[1], MON_DATA_HP, &noHp);
    FaintClearSetData(0);
    Expect("fainted: stage 0 on the field", Neutral(0));
    Switch(0, BS_ATTACKER, 2); // its replacement
    Expect("the fainted Zigzagoon keeps nothing", NothingStored(B_TRAINER_PLAYER, 1));
    SetMonData(&player[1], MON_DATA_HP, &someHp); // revived (Revival Blessing, a Revive)
    Switch(0, BS_ATTACKER, 1);
    Expect("revived and sent in again: stage 0 in every stat", gBattleMons[0].species == SPECIES_ZIGZAGOON && Neutral(0));

    printf("the battle's end: nothing kept for the next one\n");
    Switch(0, BS_ATTACKER, 0); // Snorlax in, then out with Attack +1 kept
    gBattleMons[0].statStages[STAT_ATK] = DEFAULT_STAT_STAGE + 1;
    Switch(0, BS_ATTACKER, 2);
    Expect("Snorlax keeps Attack +1 as the battle ends", StatStages_Stored(B_TRAINER_PLAYER, 0, STAT_ATK) == 1);
    StatStages_BattleEnd();
    Expect("after the battle's end, nothing kept", NothingStored(B_TRAINER_PLAYER, 0));
}

static void CheckPartner(void)
{
    struct Pokemon *player = gParties[B_TRAINER_PLAYER], *partner = gParties[B_TRAINER_PARTNER];

    printf("a multi battle: the in-game partner's party keeps its own\n");
    ZeroPlayerPartyMons();
    memset(partner, 0, sizeof(struct Pokemon) * PARTY_SIZE);
    MakeMon(&player[0], SPECIES_SNORLAX, 0x30);
    MakeMon(&player[1], SPECIES_PIKACHU, 0x31);
    MakeMon(&partner[0], SPECIES_METAGROSS, 0x40);
    MakeMon(&partner[1], SPECIES_SKARMORY, 0x41);
    gBattleTypeFlags = BATTLE_TYPE_TRAINER | BATTLE_TYPE_IS_MASTER | BATTLE_TYPE_DOUBLE | BATTLE_TYPE_MULTI
                     | BATTLE_TYPE_INGAME_PARTNER;
    gBattlersCount = 4;
    for (enum BattlerId b = 0; b < 4; b++)
    {
        gBattlerPositions[b] = b;
        gBattlerControllerEndFuncs[b] = ControllerDone;
    }
    gBattlerBattleController[0] = BATTLE_CONTROLLER_PLAYER;
    gBattlerBattleController[2] = BATTLE_CONTROLLER_PLAYER_PARTNER;
    LoadBattler(0, player, 0);
    LoadBattler(2, partner, 0);
    Expect("the partner's battler is the partner's party", GetBattlerTrainer(2) == B_TRAINER_PARTNER);
    gCurrentMove = MOVE_TACKLE;
    gBattleMons[2].statStages[STAT_SPATK] += 3;
    gBattlerAttacker = 2;
    Switch(2, BS_ATTACKER, 1);
    Expect("the partner's Metagross keeps Sp. Atk +3, in the partner's party",
           StatStages_Stored(B_TRAINER_PARTNER, 0, STAT_SPATK) == 3 && NothingStored(B_TRAINER_PLAYER, 0));
    Switch(2, BS_ATTACKER, 0);
    Expect("... and has it again", gBattleMons[2].species == SPECIES_METAGROSS && Stage(2, STAT_SPATK) == 3);
    StatStages_BattleEnd();
}

static void CheckTags(void)
{
    char line[96];

    printf("the tags, in Showdown's order\n");
    SinglesBattle();
    PokemonToBattleMon(&gParties[B_TRAINER_PLAYER][0], &gBattleMons[0]);
    gBattleMons[0].statStages[STAT_ATK] = 8;
    gBattleMons[0].statStages[STAT_DEF] = 5;
    gBattleMons[0].statStages[STAT_SPATK] = 7;
    gBattleMons[0].statStages[STAT_SPDEF] = 0;
    gBattleMons[0].statStages[STAT_SPEED] = 12;
    gBattleMons[0].statStages[STAT_ACC] = 5;
    gBattleMons[0].statStages[STAT_EVASION] = 7;
    TagLine(0, line, sizeof(line));
    Expect("all seven, Showdown's order", strcmp(line, "ATK+2 DEF-1 SPA+1 SPD-6 SPE+6 ACC-1 EVA+1") == 0);
    for (u32 i = 0; i < NUM_BATTLE_STATS; i++)
        gBattleMons[0].statStages[i] = DEFAULT_STAT_STAGE;
    TagLine(0, line, sizeof(line));
    Expect("no tag at stage 0", line[0] == '\0');
    gBattleTypeFlags |= BATTLE_TYPE_SAFARI;
    memset(&gBattleMons[0], 0, sizeof(gBattleMons[0])); // the Safari Zone's player battler is empty
    TagLine(0, line, sizeof(line));
    Expect("none in the Safari Zone", line[0] == '\0');
}

static void CheckLink(void)
{
    struct Pokemon *player = gParties[B_TRAINER_PLAYER], *foe = gParties[B_TRAINER_OPPONENT_A];
    u8 command[PC_STAT_STAGES_RAW_SIZE];
    char line[96];
    enum BattlerId target;

    printf("a battle between players: the master tells every game\n");
    StatStages_BattleStart();
    ZeroPlayerPartyMons();
    ZeroEnemyPartyMons();
    MakeMon(&player[0], SPECIES_SNORLAX, 0x50);
    MakeMon(&player[1], SPECIES_ZIGZAGOON, 0x51);
    MakeMon(&foe[0], SPECIES_POOCHYENA, 0x60);
    SinglesBattle();
    LoadBattler(0, player, 0);
    LoadBattler(1, foe, 0);
    gBattleTypeFlags = BATTLE_TYPE_LINK | BATTLE_TYPE_TRAINER | BATTLE_TYPE_IS_MASTER;
    target = StatStages_BuildCommand(command);
    Expect("the master: nothing to tell at the battle's start", target == MAX_BATTLERS_COUNT);
    gBattleMons[0].statStages[STAT_ATK] = 8;   // Swords Dance
    gBattleMons[1].statStages[STAT_SPEED] = 4; // Scary Face
    target = StatStages_BuildCommand(command);
    Expect("... Swords Dance and Scary Face: a command, to battler 0", target == 0);
    Expect("... the command's bytes: id, battler count, the stages",
           command[0] == CONTROLLER_PC_STAT_STAGES && command[1] == 2 && command[2] == 0 && command[3] == 0
           && command[4 + 0 * NUM_BATTLE_STATS + STAT_ATK] == 8 && command[4 + 1 * NUM_BATTLE_STATS + STAT_SPEED] == 4
           && command[4 + 2 * NUM_BATTLE_STATS] == DEFAULT_STAT_STAGE);
    Expect("... its size is the one the multiplayer check knows",
           MultiplayerBattle_ControllerRawSize(CONTROLLER_PC_STAT_STAGES, command[1]) == PC_STAT_STAGES_RAW_SIZE);
    Expect("... and it passes the check", StatStages_CommandValid(command, 2));
    command[5] = 13;
    Expect("a stage past 12 fails the check", !StatStages_CommandValid(command, 2));
    command[5] = DEFAULT_STAT_STAGE;
    Expect("another battler count fails the check", !StatStages_CommandValid(command, 4));

    printf("the master's engine waits for every game to take it in\n");
    gBattleControllerExecFlags = 0;
    Expect("not during the battle's intro", !StatStages_SyncTick(FALSE) && gBattleControllerExecFlags == 0);
    Expect("sent once the battle is under way: the engine waits this frame",
           StatStages_SyncTick(TRUE) && (gBattleControllerExecFlags & (1u << (28 + 0))));
    Expect("... and every frame until every game has handled it", StatStages_SyncTick(TRUE));
    gBattleControllerExecFlags = 0; // every game said it is done
    Expect("... then the engine goes on; nothing new to tell", !StatStages_SyncTick(TRUE) && gBattleControllerExecFlags == 0);
    gBattleOutcome = B_OUTCOME_WON;
    gBattleMons[1].statStages[STAT_SPEED] = 3;
    Expect("nothing sent once the battle's outcome is known", !StatStages_SyncTick(TRUE));
    gBattleOutcome = 0;
    gBattleMons[1].statStages[STAT_SPEED] = 4;

    printf("the replaying game (it never runs the engine; its gBattleMons stay empty) shows the master's stages\n");
    target = StatStages_BuildCommand(command);
    gBattleTypeFlags = BATTLE_TYPE_LINK | BATTLE_TYPE_TRAINER;
    memset(gBattleMons, 0, sizeof(gBattleMons));
    StatStages_BattleStart();
    TagLine(0, line, sizeof(line));
    Expect("before the command: no tags", line[0] == '\0');
    memcpy(gBattleResources->bufferA[0], command, sizeof(command));
    MarkBattleControllerActiveOnLocal(0);
    Expect("the command is handled there (not by the game's controller tables)", StatStages_RunCommand(0));
    Expect("... and the controller completed", !IsBattleControllerActiveOnLocal(0));
    TagLine(0, line, sizeof(line));
    Expect("its own Pokemon: ATK+2, as on the master", strcmp(line, "ATK+2") == 0);
    TagLine(1, line, sizeof(line));
    Expect("the opponent: SPE-2, as on the master", strcmp(line, "SPE-2") == 0);
    command[4 + STAT_DEF] = 20; // a corrupt command changes nothing
    memcpy(gBattleResources->bufferA[0], command, sizeof(command));
    MarkBattleControllerActiveOnLocal(0);
    StatStages_RunCommand(0);
    TagLine(0, line, sizeof(line));
    Expect("a command that fails the check changes nothing shown", strcmp(line, "ATK+2") == 0);

    printf("a Pokemon switched back in on the master: the replaying game shows the stages it kept\n");
    gBattleTypeFlags = BATTLE_TYPE_TRAINER | BATTLE_TYPE_IS_MASTER;
    LoadBattler(0, player, 0);
    LoadBattler(1, foe, 0);
    gBattleMons[0].statStages[STAT_ATK] = 8;
    gCurrentMove = MOVE_TACKLE;
    gBattlerAttacker = 0;
    Switch(0, BS_ATTACKER, 1);
    Switch(0, BS_ATTACKER, 0); // Snorlax back with Attack +2 (the master's engine)
    gBattleTypeFlags = BATTLE_TYPE_LINK | BATTLE_TYPE_TRAINER | BATTLE_TYPE_IS_MASTER;
    memset(command, 0, sizeof(command));
    StatStages_BuildCommand(command);
    gBattleTypeFlags = BATTLE_TYPE_LINK | BATTLE_TYPE_TRAINER;
    memset(gBattleMons, 0, sizeof(gBattleMons));
    memcpy(gBattleResources->bufferA[0], command, sizeof(command));
    MarkBattleControllerActiveOnLocal(0);
    StatStages_RunCommand(0);
    TagLine(0, line, sizeof(line));
    Expect("Snorlax back in: the replaying game shows ATK+2", strcmp(line, "ATK+2") == 0);
    gBattleTypeFlags = 0;
}

int NativeSelfTest_StatStages(void)
{
    InitHeap(gHeap, HEAP_SIZE);
    SetSaveBlocksPointers(0);
    memset(gSaveBlock2Ptr, 0, sizeof(*gSaveBlock2Ptr));
    gSaveBlock2Ptr->playerTrainerId[0] = 0x39;
    gSaveBlock2Ptr->playerTrainerId[1] = 0x30;
    gBattleTypeFlags = BATTLE_TYPE_TRAINER | BATTLE_TYPE_IS_MASTER;
    AllocateBattleResources(); // the battle's memory, as a battle starts (its stat stages cleared there too)
    gMain.inBattle = TRUE;

    CheckSingles();
    CheckPartner();
    CheckTags();
    CheckLink();

    gMain.inBattle = FALSE;
    FreeBattleResources();
    printf("selftest-stat-stages: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures ? 1 : 0;
}
