#include "global.h"
#include "multiplayer_battle.h"
#include "multiplayer_battle_schema.h"
#include "battle.h"
#include "battle_controllers.h"
#include "battle_message.h"
#include "constants/characters.h"
#include "test/test.h"

// Each test binds the two cores of a native 1v1 to the validator
// (MultiplayerBattle_TestSetCoreCount): since the co-op commit 761c9c27ff the
// validator counts cores from the binding, and an unbound one refuses every
// block. The tests predate that and failed on their first expectation.

static u16 Packet(u8 *wire, u8 buffer, u8 battler, const void *payload, u16 raw)
{
    u16 padded = raw - raw % 4 + 4;
    memset(wire, 0, 256);
    wire[0] = buffer; wire[1] = battler; wire[4] = padded;
    memcpy(wire + 8, payload, raw);
    return padded + 8;
}

static u16 Request(u8 *wire, u8 battler, u8 command)
{
    u8 payload[156] = {0};
    payload[0] = command;
    if (command == CONTROLLER_CHOOSEPOKEMON) payload[4] = 0x10, payload[5] = 0x32, payload[6] = 0x54;
    return Packet(wire, B_COMM_TO_CONTROLLER, battler, payload, MultiplayerBattle_ControllerRawSize(command, 0));
}

static u16 Done(u8 *wire, u8 battler, u8 source)
{
    u8 payload[4] = {source, 0, 0, 0};
    return Packet(wire, B_COMM_CONTROLLER_IS_DONE, battler, payload, 4);
}

TEST("Multiplayer native controller: roles padding sizes and unsafe commands cannot dispatch")
{
    u8 wire[256], bad[4] = {CONTROLLER_DMA3TRANSFER};
    u16 size;
    MultiplayerBattle_ResetControllerValidation(); MultiplayerBattle_TestSetCoreCount(2);
    size = Request(wire, 0, CONTROLLER_LOADMONSPRITE);
    EXPECT(MultiplayerBattle_ValidateController(wire, size, 0));
    EXPECT(!MultiplayerBattle_ValidateController(wire, size, 1));
    EXPECT(!MultiplayerBattle_ValidateController(wire, size - 1, 0));
    wire[size - 1] = 1; EXPECT(!MultiplayerBattle_ValidateController(wire, size, 0));
    wire[size - 1] = 0; wire[1] = 2; EXPECT(!MultiplayerBattle_ValidateController(wire, size, 0));
    wire[1] = 0; wire[2] = 2; EXPECT(!MultiplayerBattle_ValidateController(wire, size, 0));
    size = Packet(wire, B_COMM_TO_CONTROLLER, 0, bad, 4);
    EXPECT(!MultiplayerBattle_ValidateController(wire, size, 0));
    bad[0] = CONTROLLER_DEBUGMENU; Packet(wire, B_COMM_TO_CONTROLLER, 0, bad, 4);
    EXPECT(!MultiplayerBattle_ValidateController(wire, size, 0));
    bad[0] = CONTROLLER_GETRAWMONDATA; Packet(wire, B_COMM_TO_CONTROLLER, 0, bad, 4);
    EXPECT(!MultiplayerBattle_ValidateController(wire, size, 0));
    MultiplayerBattle_TestSetCoreCount(0);
}

TEST("Multiplayer native controller: GET ALL prevents multi-mon buffer overflow and requires owned reply")
{
    u8 wire[256], get[4] = {CONTROLLER_GETMONDATA, REQUEST_ALL_BATTLE, 3};
    u8 reply[144] = {CONTROLLER_DATATRANSFER, CONTROLLER_DATATRANSFER, 140};
    struct BattlePokemon mon = {0};
    u16 size;
    MultiplayerBattle_ResetControllerValidation(); MultiplayerBattle_TestSetCoreCount(2);
    size = Packet(wire, B_COMM_TO_CONTROLLER, 1, get, 4);
    EXPECT(!MultiplayerBattle_ValidateController(wire, size, 0));
    wire[10] = 1; EXPECT(MultiplayerBattle_ValidateController(wire, size, 0));
    MultiplayerBattle_NoteController(wire, 0);
    size = Done(wire, 1, 1); EXPECT(!MultiplayerBattle_ValidateController(wire, size, 1));
    mon.species = SPECIES_TORCHIC; mon.level = 7; mon.hp = mon.maxHP = 30;
    memset(mon.statStages, 6, sizeof(mon.statStages)); mon.nickname[0] = mon.otName[0] = EOS;
    memcpy(reply + 4, &mon, sizeof(mon));
    size = Packet(wire, B_COMM_TO_ENGINE, 1, reply, sizeof(reply));
    EXPECT_EQ(size, 156); EXPECT(!MultiplayerBattle_ValidateController(wire, size, 0));
    EXPECT(MultiplayerBattle_ValidateController(wire, size, 1));
    wire[8 + 4 + offsetof(struct BattlePokemon, level)] = 101;
    EXPECT(!MultiplayerBattle_ValidateController(wire, size, 1));
    wire[8 + 4 + offsetof(struct BattlePokemon, level)] = 7;
    MultiplayerBattle_NoteController(wire, 1);
    EXPECT(!MultiplayerBattle_ValidateController(wire, size, 1)); // A second reply cannot replace a choice.
    size = Done(wire, 1, 1); EXPECT(MultiplayerBattle_ValidateController(wire, size, 1));
    MultiplayerBattle_NoteController(wire, 1);
    EXPECT(!MultiplayerBattle_ValidateController(wire, size, 1));
    size = Done(wire, 1, 0); EXPECT(MultiplayerBattle_ValidateController(wire, size, 0));
    MultiplayerBattle_NoteController(wire, 0);
    size = Request(wire, 1, CONTROLLER_LOADMONSPRITE);
    EXPECT(MultiplayerBattle_ValidateController(wire, size, 0));
    MultiplayerBattle_TestSetCoreCount(0);
}

TEST("Multiplayer native controller: owner DONE waits for action move and party replies")
{
    static const u8 requests[] = {CONTROLLER_CHOOSEACTION, CONTROLLER_CHOOSEMOVE, CONTROLLER_CHOOSEPOKEMON};
    u8 wire[256], reply[6];
    u32 i;
    u16 size;
    for (i = 0; i < ARRAY_COUNT(requests); i++)
    {
        MultiplayerBattle_ResetControllerValidation(); MultiplayerBattle_TestSetCoreCount(2);
        size = Request(wire, 1, requests[i]);
        EXPECT(MultiplayerBattle_ValidateController(wire, size, 0));
        MultiplayerBattle_NoteController(wire, 0);
        size = Done(wire, 1, 1); EXPECT(!MultiplayerBattle_ValidateController(wire, size, 1));
        size = Done(wire, 1, 0); EXPECT(MultiplayerBattle_ValidateController(wire, size, 0));
        MultiplayerBattle_NoteController(wire, 0);
        memset(reply, 0, sizeof(reply));
        if (requests[i] == CONTROLLER_CHOOSEPOKEMON) reply[0] = CONTROLLER_CHOSENMONRETURNVALUE;
        else reply[0] = CONTROLLER_TWORETURNVALUES;
        if (requests[i] == CONTROLLER_CHOOSEMOVE) reply[1] = B_ACTION_EXEC_SCRIPT;
        size = Packet(wire, B_COMM_TO_ENGINE, 1, reply, requests[i] == CONTROLLER_CHOOSEPOKEMON ? 5 : 6);
        EXPECT(MultiplayerBattle_ValidateController(wire, size, 1));
        MultiplayerBattle_NoteController(wire, 1);
        size = Done(wire, 1, 1); EXPECT(MultiplayerBattle_ValidateController(wire, size, 1));
        MultiplayerBattle_NoteController(wire, 1);
        size = Request(wire, 1, CONTROLLER_LOADMONSPRITE);
        EXPECT(MultiplayerBattle_ValidateController(wire, size, 0));
    }
    MultiplayerBattle_TestSetCoreCount(0);
}

TEST("Multiplayer native controller: malformed text cannot index arrays or overrun expansion")
{
    u8 wire[256], payload[80] = {CONTROLLER_PRINTSTRING};
    struct BattleMsgData message = {0};
    u16 size;
    MultiplayerBattle_ResetControllerValidation(); MultiplayerBattle_TestSetCoreCount(2);
    message.textBuffs[0][0] = message.textBuffs[1][0] = message.textBuffs[2][0] = EOS;
    memcpy(payload + 4, &message, sizeof(message));
    size = Packet(wire, B_COMM_TO_CONTROLLER, 0, payload, sizeof(payload));
    EXPECT(MultiplayerBattle_ValidateController(wire, size, 0));
    memset(wire + 12 + offsetof(struct BattleMsgData, textBuffs), 0, TEXT_BUFF_ARRAY_COUNT);
    EXPECT(!MultiplayerBattle_ValidateController(wire, size, 0));
    memcpy(wire + 12, &message, sizeof(message));
    wire[12 + offsetof(struct BattleMsgData, scrActive)] = 2;
    EXPECT(!MultiplayerBattle_ValidateController(wire, size, 0));
    memcpy(wire + 12, &message, sizeof(message));
    wire[10] = STRINGID_TRAINERSLIDE;
    EXPECT(!MultiplayerBattle_ValidateController(wire, size, 0));
    wire[10] = 0;
    message.textBuffs[0][0] = B_BUFF_PLACEHOLDER_BEGIN;
    message.textBuffs[0][1] = B_BUFF_STRING;
    message.textBuffs[0][2] = STRINGID_BATTLERFAINTED;
    message.textBuffs[0][3] = 0;
    message.textBuffs[0][4] = B_BUFF_EOS;
    memcpy(wire + 12, &message, sizeof(message));
    EXPECT(MultiplayerBattle_ValidateController(wire, size, 0));
    wire[12 + offsetof(struct BattleMsgData, textBuffs) + 2] = 0xFF;
    wire[12 + offsetof(struct BattleMsgData, textBuffs) + 3] = 0xFF;
    EXPECT(!MultiplayerBattle_ValidateController(wire, size, 0));
    MultiplayerBattle_TestSetCoreCount(0);
}

TEST("Multiplayer native controller: forbidden setters and invalid selection metadata refuse")
{
    u8 wire[256], payload[44] = {CONTROLLER_SETMONDATA, REQUEST_PERSONALITY_BATTLE};
    struct ChooseMoveStruct moves = {0};
    u16 size;
    MultiplayerBattle_ResetControllerValidation(); MultiplayerBattle_TestSetCoreCount(2);
    size = Packet(wire, B_COMM_TO_CONTROLLER, 0, payload, 7);
    EXPECT(!MultiplayerBattle_ValidateController(wire, size, 0));
    memset(payload, 0, sizeof(payload)); payload[0] = CONTROLLER_CHOOSEMOVE;
    memcpy(payload + 4, &moves, sizeof(moves));
    size = Packet(wire, B_COMM_TO_CONTROLLER, 0, payload, sizeof(payload));
    EXPECT(MultiplayerBattle_ValidateController(wire, size, 0));
    moves.moves[0] = MOVES_COUNT_ALL; memcpy(wire + 12, &moves, sizeof(moves));
    EXPECT(!MultiplayerBattle_ValidateController(wire, size, 0));
    moves.moves[0] = 1; moves.currentPP[0] = 2; moves.maxPP[0] = 1;
    memcpy(wire + 12, &moves, sizeof(moves));
    EXPECT(!MultiplayerBattle_ValidateController(wire, size, 0));
    MultiplayerBattle_TestSetCoreCount(0);
}
