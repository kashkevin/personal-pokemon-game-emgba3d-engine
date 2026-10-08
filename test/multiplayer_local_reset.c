#include "global.h"
#include "multiplayer_local_reset.h"
#include "multiplayer_save.h"
#include "multiplayer_ownership.h"
#include "multiplayer.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "main.h"
#include "battle.h"
#include "overworld.h"
#include "fieldmap.h"
#include "script.h"
#include "sprite.h"
#include "palette.h"
#include "event_data.h"
#include "malloc.h"
#include "item.h"
#include "load_save.h"
#include "text.h"
#include "constants/maps.h"
#include "constants/map_groups.h"
#include "constants/vars.h"
#include "test/test.h"

struct ResetFixture {
    struct MultiplayerSaveControl command;
    struct BattleStruct *battle;
    MainCallback cb1, cb2;
    struct MapHeader header;
    struct PlayerAvatar avatar;
    struct ObjectEvent object;
    bool8 inBattle, spriteInUse;
    u8 battlerPositions[MAX_BATTLERS_COUNT];
    u32 generation;
};

static void Submit(struct ResetFixture *f, u8 opcode)
{
    f->command.commandSeq++; f->command.opcode = opcode;
    f->command.chunkCrc32 = MultiplayerSave_TestCrc32(0, f->command.data, f->command.length);
    f->generation += 2;
    gMultiplayerSaveMailbox.command = f->command;
    gMultiplayerSaveMailbox.commandGeneration = f->generation;
    MultiplayerSave_Tick();
    EXPECT_EQ(gMultiplayerSaveMailbox.response.status, MP_SAVE_OK);
}

static void Connection(u32 epoch, bool32 connected)
{
    gMultiplayerMailbox.inboundGeneration++;
    gMultiplayerMailbox.connectionFlags = connected ? MULTIPLAYER_CONNECTED : 0;
    gMultiplayerMailbox.rosterEpoch = epoch;
    gMultiplayerMailbox.heartbeat++;
    gMultiplayerMailbox.peerCount = 0; gMultiplayerMailbox.selfSlot = 0;
    gMultiplayerMailbox.inboundGeneration++;
    Multiplayer_Tick();
}

static void SetUp(struct ResetFixture *f)
{
    memset(f, 0, sizeof(*f));
    f->cb1 = gMain.callback1; f->cb2 = gMain.callback2; f->inBattle = gMain.inBattle;
    f->header = gMapHeader; f->avatar = gPlayerAvatar; f->object = gObjectEvents[0];
    f->spriteInUse = gSprites[0].inUse; f->battle = gBattleStruct;
    memcpy(f->battlerPositions, gBattlerPositions, sizeof(f->battlerPositions));
    gBattlerPositions[0] = B_POSITION_PLAYER_LEFT;
    gBattlerPositions[1] = B_POSITION_OPPONENT_LEFT;
    gBattlerPositions[2] = B_POSITION_PLAYER_RIGHT;
    gBattlerPositions[3] = B_POSITION_OPPONENT_RIGHT;
    memset(gSaveBlock1Ptr, 0, sizeof(*gSaveBlock1Ptr));
    memset(gSaveBlock2Ptr, 0, sizeof(*gSaveBlock2Ptr));
    memset(gSaveBlock3Ptr, 0, sizeof(*gSaveBlock3Ptr));
    memset(gPokemonStoragePtr, 0, sizeof(*gPokemonStoragePtr));
    SetBagItemsPointers();
    ZeroPlayerPartyMons();
    gSaveBlock1Ptr->location.mapGroup = MAP_GROUP(MAP_LITTLEROOT_TOWN);
    gSaveBlock1Ptr->location.mapNum = MAP_NUM(MAP_LITTLEROOT_TOWN);
    gSpecialVar_LastTalked = 3;
    CreateMon(&gParties[B_TRAINER_PLAYER][0], SPECIES_TORCHIC, 7, 0, OTID_STRUCT_PLAYER_ID);
    CalculateMonStats(&gParties[B_TRAINER_PLAYER][0]); CalculatePlayerPartyCount();
    gMain.callback1 = CB1_Overworld; gMain.callback2 = CB2_Overworld; gMain.inBattle = FALSE;
    gMapHeader.mapLayout = (const void *)1;
    memset(&gPlayerAvatar, 0, sizeof(gPlayerAvatar));
    memset(&gObjectEvents[0], 0, sizeof(gObjectEvents[0]));
    gObjectEvents[0].active = TRUE; gObjectEvents[0].isPlayer = TRUE;
    gSprites[0].inUse = TRUE; gPaletteFade.active = FALSE;
    ScriptContext_Init(); UnlockPlayerFieldControls();
    Multiplayer_Init(); Connection(1, TRUE); EXPECT(Multiplayer_IsConnected());
    MultiplayerSave_Init(); MultiplayerOwnership_Loaded();
    f->command.incarnation = 1; f->command.sessionEpoch = 1;
    f->command.requestId[0] = 1; f->command.totalSize = 131072;
    Submit(f, MP_SAVE_BIND);
    f->command.length = 60; f->command.data[0] = 1; f->command.data[16] = 2; f->command.data[32] = 3;
    f->command.data[48] = 1; f->command.data[52] = 33; f->command.data[56] = 1;
    Submit(f, MP_SAVE_GRANT_OWNERSHIP);
    EXPECT(MultiplayerOwnership_IsEnrolled());
    gBattleStruct = AllocZeroed(sizeof(*gBattleStruct)); EXPECT(gBattleStruct != NULL);
    gBattleResults.battleTurnCounter = 0; gBattleTypeFlags = BATTLE_TYPE_IS_MASTER;
    gMain.heldKeysRaw = gMain.newKeys = 0;
}

static void TearDown(struct ResetFixture *f)
{
    MultiplayerSave_Init(); Multiplayer_Init();
    Free(gBattleStruct); gBattleStruct = f->battle;
    gMain.callback1 = f->cb1; gMain.callback2 = f->cb2; gMain.inBattle = f->inBattle;
    gMapHeader = f->header; gPlayerAvatar = f->avatar; gObjectEvents[0] = f->object;
    gSprites[0].inUse = f->spriteInUse;
    memcpy(gBattlerPositions, f->battlerPositions, sizeof(f->battlerPositions));
    gMain.heldKeysRaw = gMain.newKeys = 0;
}

static void Begin(void)
{
    EXPECT(MultiplayerLocalReset_TryBegin(MP_LOCAL_RESET_DEVELOPMENT_ID));
    gMain.inBattle = TRUE;
    EXPECT(MultiplayerLocalReset_TestOpenWindow());
}

TEST("Multiplayer local reset: eligible start counts once and canceled confirmation preserves its attempt")
{
    struct ResetFixture f; SetUp(&f);
    Begin(); EXPECT_EQ(MultiplayerLocalReset_Count(), 1);
    EXPECT(!MultiplayerLocalReset_TryBegin(MP_LOCAL_RESET_DEVELOPMENT_ID));
    EXPECT_EQ(MultiplayerLocalReset_Count(), 1);
    EXPECT(MultiplayerLocalReset_TestRequest());
    EXPECT(!MultiplayerLocalReset_TestConfirm(FALSE));
    EXPECT(!MultiplayerLocalReset_WasAccepted()); EXPECT_EQ(MultiplayerLocalReset_Count(), 1);
    EXPECT(MultiplayerLocalReset_TestRequest()); EXPECT(MultiplayerLocalReset_TestConfirm(TRUE));
    EXPECT(MultiplayerLocalReset_WasAccepted());
    MultiplayerLocalReset_ConsumeResult(); EXPECT_EQ(gSpecialVar_Result, TRUE);
    EXPECT(!MultiplayerLocalReset_WasAccepted());
    gMain.inBattle = FALSE; Begin(); EXPECT_EQ(MultiplayerLocalReset_Count(), 2);
    TearDown(&f);
}

TEST("Multiplayer local reset: descriptor map object capture and counter corruption are refused without reset")
{
    struct ResetFixture f; SetUp(&f);
    EXPECT(!MultiplayerLocalReset_TryBegin(0)); EXPECT(!MultiplayerLocalReset_TryBegin(0xE001));
    gSpecialVar_LastTalked = 2; EXPECT(!MultiplayerLocalReset_TryBegin(MP_LOCAL_RESET_DEVELOPMENT_ID));
    gSpecialVar_LastTalked = 3; gSaveBlock1Ptr->location.mapNum++;
    EXPECT(!MultiplayerLocalReset_TryBegin(MP_LOCAL_RESET_DEVELOPMENT_ID)); gSaveBlock1Ptr->location.mapNum--;
    VarSet(MP_LOCAL_RESET_CAPTURED, 1); EXPECT(!MultiplayerLocalReset_TryBegin(MP_LOCAL_RESET_DEVELOPMENT_ID));
    VarSet(MP_LOCAL_RESET_CAPTURED, 0); VarSet(MP_LOCAL_RESET_COUNTER_MAGIC, 17);
    EXPECT(!MultiplayerLocalReset_TryBegin(MP_LOCAL_RESET_DEVELOPMENT_ID));
    VarSet(MP_LOCAL_RESET_COUNTER_MAGIC, 0); VarSet(MP_LOCAL_RESET_COUNTER_LO, 1);
    EXPECT(!MultiplayerLocalReset_TryBegin(MP_LOCAL_RESET_DEVELOPMENT_ID));
    VarSet(MP_LOCAL_RESET_COUNTER_MAGIC, MP_LOCAL_RESET_MAGIC);
    VarSet(MP_LOCAL_RESET_COUNTER_LO, 0xFFFF); VarSet(MP_LOCAL_RESET_COUNTER_HI, 0xFFFF);
    EXPECT(!MultiplayerLocalReset_TryBegin(MP_LOCAL_RESET_DEVELOPMENT_ID)); EXPECT_EQ(MultiplayerLocalReset_Count(), 0xFFFFFFFF);
    TearDown(&f);
}

TEST("Multiplayer local reset: real party bag money storage and ownership mutations prevent reset and are not rolled back")
{
    struct ResetFixture f; SetUp(&f); Begin();
    u32 hp = GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_HP), changed = hp - 1;
    SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_HP, &changed);
    EXPECT(!MultiplayerLocalReset_TestRequest()); EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_HP), changed);
    SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_HP, &hp);
    gSaveBlock1Ptr->bag.items[0].quantity = 1; EXPECT(!MultiplayerLocalReset_TestRequest());
    EXPECT_EQ(gSaveBlock1Ptr->bag.items[0].quantity, 1); gSaveBlock1Ptr->bag.items[0].quantity = 0;
    gSaveBlock1Ptr->money++; EXPECT(!MultiplayerLocalReset_TestRequest()); gSaveBlock1Ptr->money--;
    gPokemonStoragePtr->boxes[0][0].personality = 1; EXPECT(!MultiplayerLocalReset_TestRequest());
    gPokemonStoragePtr->boxes[0][0].personality = 0;
    gSaveBlock3Ptr->ownership.next++; EXPECT(!MultiplayerLocalReset_TestRequest()); gSaveBlock3Ptr->ownership.next--;
    EXPECT(MultiplayerLocalReset_TestRequest());
    gSaveBlock1Ptr->dexCaught[0] = 1; EXPECT(!MultiplayerLocalReset_TestConfirm(TRUE));
    EXPECT_EQ(gSaveBlock1Ptr->dexCaught[0], 1); EXPECT(!MultiplayerLocalReset_WasAccepted());
    TearDown(&f);
}

TEST("Multiplayer local reset: action capture turn and other battle sources close the first-action boundary")
{
    struct ResetFixture f; SetUp(&f); Begin();
    MultiplayerLocalReset_CloseAction(); EXPECT(!MultiplayerLocalReset_TestOpenWindow());
    EXPECT(!MultiplayerLocalReset_TestRequest());
    MultiplayerLocalReset_Finish(); gMain.inBattle = FALSE; Begin();
    MultiplayerLocalReset_CloseCapture(); EXPECT(!MultiplayerLocalReset_TestRequest());
    MultiplayerLocalReset_Finish(); gMain.inBattle = FALSE; Begin();
    gBattleResults.battleTurnCounter = 1; EXPECT(!MultiplayerLocalReset_TestRequest());
    EXPECT(!MultiplayerLocalReset_TestConfirm(TRUE)); EXPECT(!MultiplayerLocalReset_WasAccepted());
    gBattleResults.battleTurnCounter = 0; gBattleTypeFlags = BATTLE_TYPE_TRAINER;
    EXPECT(!MultiplayerLocalReset_TestOpenWindow());
    TearDown(&f);
}

TEST("Multiplayer local reset: disconnect and changed roster refuse the exact old context")
{
    struct ResetFixture f; SetUp(&f); Begin();
    Connection(1, FALSE); EXPECT(!MultiplayerLocalReset_TestRequest());
    Connection(2, TRUE); EXPECT(!MultiplayerLocalReset_TestRequest());
    EXPECT(!MultiplayerLocalReset_WasAccepted()); EXPECT_EQ(MultiplayerLocalReset_Count(), 1);
    TearDown(&f);
}

TEST("Multiplayer local reset: enrolled unsafe chord is consumed without a BIOS reset or new attempt")
{
    struct ResetFixture f; SetUp(&f);
    gMain.heldKeysRaw = A_BUTTON | B_BUTTON | START_BUTTON | SELECT_BUTTON;
    EXPECT(MultiplayerLocalReset_HandleChord()); EXPECT_EQ(MultiplayerLocalReset_Count(), 0);
    EXPECT(MultiplayerLocalReset_HandleChord()); EXPECT_EQ(MultiplayerLocalReset_Count(), 0);
    gMain.heldKeysRaw = 0; EXPECT(!MultiplayerLocalReset_HandleChord());
    TearDown(&f);
}

TEST("Multiplayer local reset: native encryption key rotation preserves logical bag money and coins guards")
{
    struct ResetFixture f; SetUp(&f); Begin();
    u32 key = 0xD1E2F3A4;
    ApplyNewEncryptionKeyToBagItems(key);
    ApplyNewEncryptionKeyToWord(&gSaveBlock1Ptr->money, key);
    ApplyNewEncryptionKeyToHword(&gSaveBlock1Ptr->coins, key);
    gSaveBlock2Ptr->encryptionKey = key;
    EXPECT(MultiplayerLocalReset_TestRequest());
    EXPECT(MultiplayerLocalReset_TestConfirm(TRUE));
    EXPECT(MultiplayerLocalReset_WasAccepted());
    TearDown(&f);
}

TEST("Multiplayer local reset: visible Select action requests the same guarded confirmation and cancel preserves the encounter")
{
    struct ResetFixture f; SetUp(&f);
    EXPECT(MultiplayerLocalReset_GetActionPrompt(0) == NULL);
    EXPECT(MultiplayerLocalReset_TryBegin(MP_LOCAL_RESET_DEVELOPMENT_ID));
    gMain.inBattle = TRUE;
    const u8 *prompt = MultiplayerLocalReset_GetActionPrompt(0);
    EXPECT(prompt != NULL);
    // The ordinary action prompt is 14 tiles wide, with one pixel of padding.
    EXPECT(GetStringWidth(FONT_NORMAL, prompt, 0) <= 14 * 8 - 1);
    EXPECT(MultiplayerLocalReset_GetActionPrompt(1) == NULL);
    EXPECT(MultiplayerLocalReset_GetActionPrompt(2) == NULL);
    gMain.newKeys = gMain.heldKeysRaw = SELECT_BUTTON;
    EXPECT(!MultiplayerLocalReset_TestSelect(1));
    EXPECT(MultiplayerLocalReset_TestSelect(0));
    EXPECT(MultiplayerLocalReset_GetActionPrompt(0) == NULL);
    EXPECT(!MultiplayerLocalReset_TestConfirm(FALSE));
    EXPECT(!MultiplayerLocalReset_WasAccepted());
    EXPECT_EQ(MultiplayerLocalReset_Count(), 1);
    EXPECT(MultiplayerLocalReset_GetActionPrompt(0) != NULL);
    EXPECT(MultiplayerLocalReset_TestSelect(0));
    EXPECT(MultiplayerLocalReset_TestConfirm(TRUE));
    EXPECT(MultiplayerLocalReset_WasAccepted());
    EXPECT(MultiplayerLocalReset_GetActionPrompt(0) == NULL);
    EXPECT_EQ(MultiplayerLocalReset_Count(), 1);
    TearDown(&f);
}

TEST("Multiplayer local reset: Select shortcut does not reinterpret held keys or the filtered reset chord")
{
    struct ResetFixture f; SetUp(&f); Begin();
    gMain.heldKeysRaw = SELECT_BUTTON;
    EXPECT(!MultiplayerLocalReset_TestSelect(0));
    gMain.newKeys = SELECT_BUTTON;
    gMain.heldKeysRaw = SELECT_BUTTON | A_BUTTON;
    EXPECT(!MultiplayerLocalReset_TestSelect(0));
    gMain.heldKeysRaw = SELECT_BUTTON | B_BUTTON;
    EXPECT(!MultiplayerLocalReset_TestSelect(0));
    gMain.heldKeysRaw = SELECT_BUTTON | START_BUTTON;
    EXPECT(!MultiplayerLocalReset_TestSelect(0));
    // The existing Windows filter removes Start from the four-button chord.
    gMain.newKeys = gMain.heldKeysRaw = SELECT_BUTTON | A_BUTTON | B_BUTTON;
    EXPECT(!MultiplayerLocalReset_TestSelect(0));
    EXPECT_EQ(MultiplayerLocalReset_HandlePlayerInput(0), MP_LOCAL_RESET_INPUT_PASS);
    EXPECT_EQ(gMain.newKeys, SELECT_BUTTON | A_BUTTON | B_BUTTON);
    EXPECT(MultiplayerLocalReset_GetActionPrompt(0) == NULL);
    EXPECT(!MultiplayerLocalReset_WasAccepted());
    TearDown(&f);
}

TEST("Multiplayer local reset: ineligible Select leaves the existing debug action input untouched")
{
    struct ResetFixture f; SetUp(&f);
    gMain.newKeys = gMain.heldKeysRaw = SELECT_BUTTON;
    EXPECT(MultiplayerLocalReset_GetActionPrompt(0) == NULL);
    EXPECT_EQ(MultiplayerLocalReset_HandlePlayerInput(0), MP_LOCAL_RESET_INPUT_PASS);
    EXPECT_EQ(gMain.newKeys, SELECT_BUTTON);
    Begin();
    gSaveBlock1Ptr->money++;
    EXPECT(MultiplayerLocalReset_GetActionPrompt(0) == NULL);
    EXPECT_EQ(MultiplayerLocalReset_HandlePlayerInput(0), MP_LOCAL_RESET_INPUT_PASS);
    EXPECT_EQ(gMain.newKeys, SELECT_BUTTON);
    EXPECT(!MultiplayerLocalReset_TestConfirm(TRUE));
    gSaveBlock1Ptr->money--;
    gBattleResults.battleTurnCounter = 1;
    EXPECT(MultiplayerLocalReset_GetActionPrompt(0) == NULL);
    EXPECT_EQ(MultiplayerLocalReset_HandlePlayerInput(0), MP_LOCAL_RESET_INPUT_PASS);
    gBattleResults.battleTurnCounter = 0;
    gBattleTypeFlags |= BATTLE_TYPE_TRAINER;
    EXPECT(MultiplayerLocalReset_GetActionPrompt(0) == NULL);
    EXPECT_EQ(MultiplayerLocalReset_HandlePlayerInput(0), MP_LOCAL_RESET_INPUT_PASS);
    gBattleTypeFlags = BATTLE_TYPE_IS_MASTER;
    MultiplayerLocalReset_CloseAction();
    EXPECT(MultiplayerLocalReset_GetActionPrompt(0) == NULL);
    EXPECT_EQ(MultiplayerLocalReset_HandlePlayerInput(0), MP_LOCAL_RESET_INPUT_PASS);
    EXPECT_EQ(gMain.newKeys, SELECT_BUTTON);
    EXPECT(!MultiplayerLocalReset_WasAccepted());
    TearDown(&f);
}

TEST("Multiplayer local reset: Select confirmation rechecks connection and protected native state")
{
    struct ResetFixture f; SetUp(&f); Begin();
    gMain.newKeys = gMain.heldKeysRaw = SELECT_BUTTON;
    EXPECT(MultiplayerLocalReset_TestSelect(0));
    gSaveBlock1Ptr->money++;
    EXPECT(!MultiplayerLocalReset_TestConfirm(TRUE));
    EXPECT(!MultiplayerLocalReset_WasAccepted());
    EXPECT(MultiplayerLocalReset_GetActionPrompt(0) == NULL);
    EXPECT_EQ(gSaveBlock1Ptr->money, 1);
    gSaveBlock1Ptr->money--;
    EXPECT(MultiplayerLocalReset_GetActionPrompt(0) != NULL);
    Connection(1, FALSE);
    EXPECT(MultiplayerLocalReset_GetActionPrompt(0) == NULL);
    EXPECT_EQ(MultiplayerLocalReset_HandlePlayerInput(0), MP_LOCAL_RESET_INPUT_PASS);
    EXPECT_EQ(gMain.newKeys, SELECT_BUTTON);
    EXPECT(!MultiplayerLocalReset_WasAccepted());
    TearDown(&f);
}
