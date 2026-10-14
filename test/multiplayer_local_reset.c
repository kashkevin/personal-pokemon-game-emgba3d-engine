#include "global.h"
#include "config/multiplayer_local_reset.h"
#include "multiplayer_local_reset.h"
#include "multiplayer_static_reset.h"
#include "multiplayer_reset_admission.h"
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
#include "string_util.h"
#include "random.h"
#include "script_pokemon_util.h"
#include "constants/event_objects.h"
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
    struct ObjectEvent object, pilotObject;
    bool8 inBattle, spriteInUse;
    u8 selectedObject;
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
    f->pilotObject = gObjectEvents[1];
    f->selectedObject = gSelectedObjectEvent;
    gSelectedObjectEvent = 0;
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
    gObjectEvents[0].localId = LOCALID_PLAYER;
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
    ScriptContext_Init(); UnlockPlayerFieldControls();
    MultiplayerSave_Init(); Multiplayer_Init();
    Free(gBattleStruct); gBattleStruct = f->battle;
    gMain.callback1 = f->cb1; gMain.callback2 = f->cb2; gMain.inBattle = f->inBattle;
    gMapHeader = f->header; gPlayerAvatar = f->avatar; gObjectEvents[0] = f->object;
    gObjectEvents[1] = f->pilotObject;
    gSelectedObjectEvent = f->selectedObject;
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

static void SetUpPilot(struct ResetFixture *f)
{
    SetUp(f);
    gSaveBlock1Ptr->location.mapGroup = MAP_GROUP(MAP_NEW_MAUVILLE_INSIDE);
    gSaveBlock1Ptr->location.mapNum = MAP_NUM(MAP_NEW_MAUVILLE_INSIDE);
    gSpecialVar_LastTalked = 6;
    memset(&gObjectEvents[1], 0, sizeof(gObjectEvents[1]));
    gObjectEvents[1].active = TRUE;
    gObjectEvents[1].localId = 6;
    gObjectEvents[1].mapGroup = MAP_GROUP(MAP_NEW_MAUVILLE_INSIDE);
    gObjectEvents[1].mapNum = MAP_NUM(MAP_NEW_MAUVILLE_INSIDE);
    gObjectEvents[1].currentCoords.x = 25 + MAP_OFFSET;
    gObjectEvents[1].currentCoords.y = 18 + MAP_OFFSET;
    VarSet(VAR_NEW_MAUVILLE_STATE, 1);
    VarSet(VAR_TEMP_1, 1);
    VarSet(VAR_TEMP_2, 0);
}

static void BeginPilot(void)
{
    EXPECT(MultiplayerLocalReset_TryBegin(MP_STATIC_RESET_VOLTORB_1));
    CreateScriptedWildMon(SPECIES_VOLTORB, 25, ITEM_NONE);
    gMain.inBattle = TRUE;
    EXPECT(MultiplayerLocalReset_TestOpenWindow());
}

TEST("Multiplayer static reset: exact New Mauville object and native availability are required")
{
    struct ResetFixture f; SetUpPilot(&f);
    EXPECT(!MultiplayerLocalReset_TryBegin(MP_STATIC_RESET_VOLTORB_1 + 1));
    gSaveBlock1Ptr->location.mapNum++;
    EXPECT(!MultiplayerLocalReset_TryBegin(MP_STATIC_RESET_VOLTORB_1));
    gSaveBlock1Ptr->location.mapNum--;
    gSpecialVar_LastTalked = 7; EXPECT(!MultiplayerLocalReset_TryBegin(MP_STATIC_RESET_VOLTORB_1));
    gSpecialVar_LastTalked = 6;
    gObjectEvents[1].currentCoords.x++; EXPECT(!MultiplayerLocalReset_TryBegin(MP_STATIC_RESET_VOLTORB_1));
    gObjectEvents[1].currentCoords.x--;
    gObjectEvents[1].active = FALSE; EXPECT(!MultiplayerLocalReset_TryBegin(MP_STATIC_RESET_VOLTORB_1));
    gObjectEvents[1].active = TRUE;
    FlagSet(FLAG_HIDE_NEW_MAUVILLE_VOLTORB_1); EXPECT(!MultiplayerLocalReset_TryBegin(MP_STATIC_RESET_VOLTORB_1));
    FlagClear(FLAG_HIDE_NEW_MAUVILLE_VOLTORB_1);
    FlagSet(FLAG_DEFEATED_VOLTORB_1_NEW_MAUVILLE); EXPECT(!MultiplayerLocalReset_TryBegin(MP_STATIC_RESET_VOLTORB_1));
    FlagClear(FLAG_DEFEATED_VOLTORB_1_NEW_MAUVILLE);
    VarSet(VAR_NEW_MAUVILLE_STATE, 0); EXPECT(!MultiplayerLocalReset_TryBegin(MP_STATIC_RESET_VOLTORB_1));
    VarSet(VAR_NEW_MAUVILLE_STATE, 3); EXPECT(!MultiplayerLocalReset_TryBegin(MP_STATIC_RESET_VOLTORB_1));
    EXPECT_EQ(MultiplayerStaticReset_Count(), 0);
    VarSet(VAR_NEW_MAUVILLE_STATE, 2); BeginPilot();
    EXPECT_EQ(MultiplayerStaticReset_Count(), 1);
    TearDown(&f);
}

TEST("Multiplayer static reset: versioned attempts never reinterpret development or unknown save data")
{
    struct ResetFixture f; SetUpPilot(&f);
    VarSet(MP_LOCAL_RESET_COUNTER_MAGIC, MP_LOCAL_RESET_MAGIC);
    VarSet(MP_LOCAL_RESET_COUNTER_LO, 42); VarSet(MP_LOCAL_RESET_CAPTURED, 1);
    VarSet(VAR_MP_VOLTORB_1_RESET_VERSION, 1);
    EXPECT(!MultiplayerLocalReset_TryBegin(MP_STATIC_RESET_VOLTORB_1));
    EXPECT_EQ(VarGet(VAR_MP_VOLTORB_1_RESET_VERSION), 1);
    VarSet(VAR_MP_VOLTORB_1_RESET_VERSION, 0); VarSet(VAR_MP_VOLTORB_1_RESET_COUNT_HI, 1);
    EXPECT(!MultiplayerLocalReset_TryBegin(MP_STATIC_RESET_VOLTORB_1));
    EXPECT_EQ(MultiplayerStaticReset_Count(), 0x10000);
    VarSet(VAR_MP_VOLTORB_1_RESET_VERSION, MP_STATIC_RESET_VERSION_1);
    VarSet(VAR_MP_VOLTORB_1_RESET_COUNT_LO, 0xFFFF); VarSet(VAR_MP_VOLTORB_1_RESET_COUNT_HI, 0xFFFF);
    EXPECT(!MultiplayerLocalReset_TryBegin(MP_STATIC_RESET_VOLTORB_1));
    EXPECT_EQ(MultiplayerStaticReset_Count(), 0xFFFFFFFF);
    VarSet(VAR_MP_VOLTORB_1_RESET_COUNT_LO, 0xFFFF); VarSet(VAR_MP_VOLTORB_1_RESET_COUNT_HI, 0);
    BeginPilot(); EXPECT_EQ(MultiplayerStaticReset_Count(), 0x10000);
    EXPECT_EQ(MultiplayerLocalReset_Count(), 42); EXPECT_EQ(VarGet(MP_LOCAL_RESET_CAPTURED), 1);
    TearDown(&f);
}

TEST("Multiplayer static reset: actual species level egg and extra opponents must match authored generation")
{
    struct ResetFixture f; SetUpPilot(&f);
    EXPECT(MultiplayerLocalReset_TryBegin(MP_STATIC_RESET_VOLTORB_1));
    gMain.inBattle = TRUE;
    CreateScriptedWildMon(SPECIES_ELECTRODE, 25, ITEM_NONE);
    EXPECT(!MultiplayerLocalReset_TestOpenWindow());
    CreateScriptedWildMon(SPECIES_VOLTORB, 24, ITEM_NONE);
    EXPECT(!MultiplayerLocalReset_TestOpenWindow());
    CreateScriptedWildMon(SPECIES_VOLTORB, 25, ITEM_NONE);
    gParties[B_TRAINER_OPPONENT_A][0].box.isEgg = TRUE;
    EXPECT(!MultiplayerLocalReset_TestOpenWindow());
    gParties[B_TRAINER_OPPONENT_A][0].box.isEgg = FALSE;
    CreateMon(&gParties[B_TRAINER_OPPONENT_A][1], SPECIES_VOLTORB, 25, 0, OTID_STRUCT_PLAYER_ID);
    EXPECT(!MultiplayerLocalReset_TestOpenWindow());
    CreateScriptedWildMon(SPECIES_VOLTORB, 25, ITEM_NONE);
    EXPECT(MultiplayerLocalReset_TestOpenWindow());
    gParties[B_TRAINER_OPPONENT_A][0].box.checksum++;
    struct Pokemon malformed = gParties[B_TRAINER_OPPONENT_A][0];
    EXPECT(!MultiplayerLocalReset_TestRequest());
    EXPECT(!memcmp(&malformed, &gParties[B_TRAINER_OPPONENT_A][0], sizeof(malformed)));
    TearDown(&f);
}

TEST("Multiplayer static reset: No keeps the roll and Yes does not rewind RNG or character data")
{
    struct ResetFixture f; SetUpPilot(&f); BeginPilot();
    struct Pokemon roll = gParties[B_TRAINER_OPPONENT_A][0];
    struct Pokemon party[PARTY_SIZE]; memcpy(party, gParties[B_TRAINER_PLAYER], sizeof(party));
    rng_value_t rng = gRngValue, rng2 = gRng2Value;
    EXPECT(MultiplayerLocalReset_TestRequest()); EXPECT(!MultiplayerLocalReset_TestConfirm(FALSE));
    EXPECT(!memcmp(&roll, &gParties[B_TRAINER_OPPONENT_A][0], sizeof(roll)));
    EXPECT_EQ(MultiplayerStaticReset_Count(), 1);
    EXPECT(MultiplayerLocalReset_TestRequest()); EXPECT(MultiplayerLocalReset_TestConfirm(TRUE));
    EXPECT(!memcmp(&rng, &gRngValue, sizeof(rng))); EXPECT(!memcmp(&rng2, &gRng2Value, sizeof(rng2)));
    EXPECT(!memcmp(party, gParties[B_TRAINER_PLAYER], sizeof(party)));
    EXPECT(!memcmp(&roll, &gParties[B_TRAINER_OPPONENT_A][0], sizeof(roll)));
    MultiplayerLocalReset_ConsumeResult(); EXPECT_EQ(gSpecialVar_Result, TRUE);
    gMain.inBattle = FALSE; BeginPilot(); EXPECT_EQ(MultiplayerStaticReset_Count(), 2);
    EXPECT(memcmp(&roll, &gParties[B_TRAINER_OPPONENT_A][0], sizeof(roll)) != 0);
    EXPECT_EQ(MultiplayerLocalReset_Count(), 0);
    TearDown(&f);
}

TEST("Multiplayer static reset: changed generator barriers counter and capture state are refused without rollback")
{
    struct ResetFixture f; SetUpPilot(&f); BeginPilot();
    VarSet(VAR_NEW_MAUVILLE_STATE, 2); EXPECT(!MultiplayerLocalReset_TestRequest());
    EXPECT_EQ(VarGet(VAR_NEW_MAUVILLE_STATE), 2); VarSet(VAR_NEW_MAUVILLE_STATE, 1);
    VarSet(VAR_TEMP_1, 0); EXPECT(!MultiplayerLocalReset_TestRequest()); VarSet(VAR_TEMP_1, 1);
    VarSet(VAR_TEMP_2, 1); EXPECT(!MultiplayerLocalReset_TestRequest()); VarSet(VAR_TEMP_2, 0);
    VarSet(VAR_MP_VOLTORB_1_RESET_COUNT_LO, 2); EXPECT(!MultiplayerLocalReset_TestRequest());
    EXPECT_EQ(MultiplayerStaticReset_Count(), 2); VarSet(VAR_MP_VOLTORB_1_RESET_COUNT_LO, 1);
    EXPECT(MultiplayerLocalReset_TestRequest());
    FlagSet(FLAG_DEFEATED_VOLTORB_1_NEW_MAUVILLE);
    EXPECT(!MultiplayerLocalReset_TestConfirm(TRUE));
    EXPECT(FlagGet(FLAG_DEFEATED_VOLTORB_1_NEW_MAUVILLE));
    EXPECT(!MultiplayerLocalReset_WasAccepted());
    TearDown(&f);
}

TEST("Multiplayer static reset: common action ownership party and binding fences still apply")
{
    struct ResetFixture f; SetUpPilot(&f); BeginPilot();
    gSaveBlock1Ptr->money++; EXPECT(!MultiplayerLocalReset_TestRequest()); gSaveBlock1Ptr->money--;
    gSaveBlock3Ptr->ownership.next++; EXPECT(!MultiplayerLocalReset_TestRequest()); gSaveBlock3Ptr->ownership.next--;
    MultiplayerLocalReset_CloseAction(); EXPECT(!MultiplayerLocalReset_TestRequest());
    MultiplayerLocalReset_Finish(); gMain.inBattle = FALSE; BeginPilot();
    MultiplayerLocalReset_CloseCapture(); EXPECT(!MultiplayerLocalReset_TestRequest());
    MultiplayerLocalReset_Finish(); gMain.inBattle = FALSE; BeginPilot();
    gBattleTypeFlags |= BATTLE_TYPE_TRAINER; EXPECT(!MultiplayerLocalReset_TestRequest());
    gBattleTypeFlags = BATTLE_TYPE_IS_MASTER;
    Connection(2, TRUE); EXPECT(!MultiplayerLocalReset_TestRequest());
    EXPECT(!MultiplayerLocalReset_WasAccepted());
    TearDown(&f);
}

extern const u8 NewMauville_Inside_EventScript_Voltorb1Return[];

TEST("Multiplayer static reset: actual accepted return script preserves target and generator progression")
{
    struct ResetFixture f; SetUpPilot(&f); BeginPilot();
    EXPECT(MultiplayerLocalReset_TestRequest()); EXPECT(MultiplayerLocalReset_TestConfirm(TRUE));
    FlagSet(FLAG_SYS_CTRL_OBJ_DELETE);
    gBattleOutcome = B_OUTCOME_PLAYER_TELEPORTED;
    gMain.inBattle = FALSE;
    RunScriptImmediately(NewMauville_Inside_EventScript_Voltorb1Return);
    EXPECT(!FlagGet(FLAG_SYS_CTRL_OBJ_DELETE));
    EXPECT(!FlagGet(FLAG_DEFEATED_VOLTORB_1_NEW_MAUVILLE));
    EXPECT(!FlagGet(FLAG_HIDE_NEW_MAUVILLE_VOLTORB_1));
    EXPECT(gObjectEvents[1].active);
    EXPECT_EQ(VarGet(VAR_NEW_MAUVILLE_STATE), 1);
    EXPECT_EQ(VarGet(VAR_TEMP_1), 1); EXPECT_EQ(VarGet(VAR_TEMP_2), 0);
    EXPECT_EQ(MultiplayerStaticReset_Count(), 1);
    EXPECT(!MultiplayerLocalReset_WasAccepted());
    TearDown(&f);
}

TEST("Multiplayer static reset: ordinary caught return still records defeat and prevents rearming")
{
    struct ResetFixture f; SetUpPilot(&f); BeginPilot();
    MultiplayerLocalReset_CloseCapture();
    FlagSet(FLAG_SYS_CTRL_OBJ_DELETE);
    gBattleOutcome = B_OUTCOME_CAUGHT;
    gMain.inBattle = FALSE;
    RunScriptImmediately(NewMauville_Inside_EventScript_Voltorb1Return);
    EXPECT(!FlagGet(FLAG_SYS_CTRL_OBJ_DELETE));
    EXPECT(FlagGet(FLAG_DEFEATED_VOLTORB_1_NEW_MAUVILLE));
    EXPECT(!MultiplayerLocalReset_TryBegin(MP_STATIC_RESET_VOLTORB_1));
    EXPECT_EQ(MultiplayerStaticReset_Count(), 1);
    EXPECT_EQ(VarGet(VAR_NEW_MAUVILLE_STATE), 1);
    EXPECT_EQ(VarGet(VAR_TEMP_1), 1); EXPECT_EQ(VarGet(VAR_TEMP_2), 0);
    TearDown(&f);
}

TEST("Multiplayer static reset: generation waits for exact durable admission and counts once")
{
    struct ResetFixture f; SetUpPilot(&f);
    MultiplayerResetAdmission_Init();
    rng_value_t rng = gRngValue;
    MultiplayerResetAdmission_Begin(MP_STATIC_RESET_VOLTORB_1);
    EXPECT(MultiplayerResetAdmission_IsWaiting());
    EXPECT_EQ(MultiplayerStaticReset_Count(), 0);
    EXPECT(!memcmp(&rng, &gRngValue, sizeof(rng)));
    struct MultiplayerResetReply reply = {.sequence=1, .session=1, .incarnation=1,
        .encounter=MP_STATIC_RESET_VOLTORB_1, .attempt=3};
    reply.incarnation = 2;
    gMultiplayerResetMailbox.reply = reply; gMultiplayerResetMailbox.inboundGeneration = 2;
    MultiplayerResetAdmission_Tick(); EXPECT(MultiplayerResetAdmission_IsWaiting());
    EXPECT_EQ(MultiplayerStaticReset_Count(), 0);
    reply.incarnation = 1;
    gMultiplayerResetMailbox.reply = reply; gMultiplayerResetMailbox.inboundGeneration = 4;
    MultiplayerResetAdmission_Tick(); EXPECT(!MultiplayerResetAdmission_IsWaiting());
    EXPECT_EQ(gSpecialVar_Result, TRUE); EXPECT_EQ(MultiplayerStaticReset_Count(), 3);
    gMultiplayerResetMailbox.inboundGeneration = 6;
    MultiplayerResetAdmission_Tick(); EXPECT_EQ(MultiplayerStaticReset_Count(), 3);
    CreateScriptedWildMon(SPECIES_VOLTORB, 25, ITEM_NONE);
    gMain.inBattle = TRUE; EXPECT(MultiplayerLocalReset_TestOpenWindow());
    TearDown(&f);
}

TEST("Multiplayer static reset: refusal creates no roll and host loss cannot release pending admission")
{
    struct ResetFixture f; SetUpPilot(&f);
    MultiplayerResetAdmission_Init();
    MultiplayerResetAdmission_Begin(MP_STATIC_RESET_VOLTORB_1);
    struct MultiplayerResetReply reply = {.sequence=1, .session=1, .incarnation=1,
        .encounter=MP_STATIC_RESET_VOLTORB_1, .status=1};
    Connection(1, FALSE);
    gMultiplayerResetMailbox.reply = reply; gMultiplayerResetMailbox.inboundGeneration = 2;
    MultiplayerResetAdmission_Tick(); EXPECT(MultiplayerResetAdmission_IsWaiting());
    EXPECT_EQ(MultiplayerStaticReset_Count(), 0);
    Connection(1, TRUE);
    gMultiplayerResetMailbox.inboundGeneration = 4;
    MultiplayerResetAdmission_Tick(); EXPECT(!MultiplayerResetAdmission_IsWaiting());
    EXPECT_EQ(gSpecialVar_Result, FALSE); EXPECT_EQ(MultiplayerStaticReset_Count(), 0);
    TearDown(&f);
}

TEST("Multiplayer static reset: Gen6 new personal shiny rolls preserve the existing stored predicate")
{
    struct ResetFixture f; SetUpPilot(&f);
    EXPECT(!MultiplayerLocalReset_PersonalGeneration());
    // Every personal roll uses the Gen 6 rate since 80fef1a823 (owner-approved
    // "Gen 6 personal shiny rate"), not only a static reset's: 0-15 are shiny,
    // 16 is not. The stored predicate (SHINY_ODDS + shinyModifier) is checked below.
    EXPECT(ComputePlayerShinyOdds(8, 0));
    EXPECT(!ComputePlayerShinyOdds(16, 0));
    EXPECT(MultiplayerLocalReset_TryBegin(MP_STATIC_RESET_VOLTORB_1));
    EXPECT(MultiplayerLocalReset_PersonalGeneration());
    EXPECT(ComputePlayerShinyOdds(0, 0)); EXPECT(ComputePlayerShinyOdds(15, 0));
    EXPECT(!ComputePlayerShinyOdds(16, 0));
    CreateMon(&gParties[B_TRAINER_OPPONENT_A][0], SPECIES_VOLTORB, 25, 8, OTID_STRUCT_PLAYER_ID);
    EXPECT(GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_IS_SHINY));
    struct Pokemon owned = gParties[B_TRAINER_OPPONENT_A][0];
    MultiplayerLocalReset_Finish();
    EXPECT(!MultiplayerLocalReset_PersonalGeneration()); EXPECT(!ComputePlayerShinyOdds(8, 0));
    EXPECT(GetMonData(&owned, MON_DATA_IS_SHINY));
    EXPECT(!memcmp(&owned, &gParties[B_TRAINER_OPPONENT_A][0], sizeof(owned)));
    TearDown(&f);
}

TEST("Multiplayer static reset: exact disconnected cancellation is neutral and waits for safe field")
{
    struct ResetFixture f; SetUpPilot(&f);
    MultiplayerResetAdmission_Init();
    rng_value_t rng = gRngValue;
    MultiplayerResetAdmission_Begin(MP_STATIC_RESET_VOLTORB_1);
    struct MultiplayerResetReply reply = {.sequence=1, .session=1, .incarnation=1,
        .encounter=MP_STATIC_RESET_VOLTORB_1, .status=2};
    gMultiplayerResetMailbox.reply = reply; gMultiplayerResetMailbox.inboundGeneration = 2;
    MultiplayerResetAdmission_Tick(); EXPECT(MultiplayerResetAdmission_IsWaiting());
    Connection(1, FALSE);
    reply.incarnation = 2;
    gMultiplayerResetMailbox.reply = reply; gMultiplayerResetMailbox.inboundGeneration = 4;
    MultiplayerResetAdmission_Tick(); EXPECT(MultiplayerResetAdmission_IsWaiting());
    reply.incarnation = 1;
    gMultiplayerResetMailbox.reply = reply; gMultiplayerResetMailbox.inboundGeneration = 6;
    MultiplayerResetAdmission_Tick(); EXPECT(!MultiplayerResetAdmission_IsWaiting());
    EXPECT_EQ(gSpecialVar_Result, FALSE); EXPECT_EQ(MultiplayerStaticReset_Count(), 0);
    EXPECT_EQ(gMultiplayerResetMailbox.receipt.state, MP_RESET_CANCELLED);
    EXPECT_EQ(gMultiplayerResetMailbox.receipt.fieldReady, FALSE);
    EXPECT(!memcmp(&rng, &gRngValue, sizeof(rng)));
    EXPECT(!FlagGet(FLAG_DEFEATED_VOLTORB_1_NEW_MAUVILLE));
    EXPECT(!FlagGet(FLAG_HIDE_NEW_MAUVILLE_VOLTORB_1));
    // This fixture models completion of the authored release/end commands.
    ScriptContext_Init(); UnlockPlayerFieldControls();
    MultiplayerResetAdmission_Tick();
    EXPECT_EQ(gMultiplayerResetMailbox.receipt.fieldReady, TRUE);
    gMultiplayerResetMailbox.inboundGeneration = 8;
    MultiplayerResetAdmission_Tick();
    EXPECT_EQ(gMultiplayerResetMailbox.receipt.state, MP_RESET_CANCELLED);
    EXPECT_EQ(MultiplayerStaticReset_Count(), 0);
    TearDown(&f);
}

TEST("Multiplayer static reset: cancellation cannot reverse an acknowledged attempt")
{
    struct ResetFixture f; SetUpPilot(&f);
    MultiplayerResetAdmission_Init();
    MultiplayerResetAdmission_Begin(MP_STATIC_RESET_VOLTORB_1);
    struct MultiplayerResetReply reply = {.sequence=1, .session=1, .incarnation=1,
        .encounter=MP_STATIC_RESET_VOLTORB_1, .attempt=1};
    gMultiplayerResetMailbox.reply = reply; gMultiplayerResetMailbox.inboundGeneration = 2;
    MultiplayerResetAdmission_Tick();
    EXPECT_EQ(gMultiplayerResetMailbox.receipt.state, MP_RESET_ADMITTED);
    EXPECT_EQ(MultiplayerStaticReset_Count(), 1);
    Connection(1, FALSE);
    reply.status = 2; reply.attempt = 0;
    gMultiplayerResetMailbox.reply = reply; gMultiplayerResetMailbox.inboundGeneration = 4;
    MultiplayerResetAdmission_Tick();
    EXPECT_EQ(gMultiplayerResetMailbox.receipt.state, MP_RESET_ADMITTED);
    EXPECT_EQ(gMultiplayerResetMailbox.receipt.attempt, 1);
    EXPECT_EQ(gMultiplayerResetMailbox.receipt.fieldReady, FALSE);
    EXPECT_EQ(MultiplayerStaticReset_Count(), 1);
    MultiplayerLocalReset_Finish();
    TearDown(&f);
}

extern const u8 NewMauville_Inside_EventScript_Voltorb2PersonalReturn[];
extern const u8 NewMauville_Inside_EventScript_Voltorb3PersonalReturn[];
extern const u8 AquaHideout_B1F_EventScript_Electrode1PersonalReturn[];
extern const u8 AquaHideout_B1F_EventScript_Electrode2PersonalReturn[];
extern const u8 AncientTomb_EventScript_RegisteelPersonalReturn[];
extern const u8 DesertRuins_EventScript_RegirockPersonalReturn[];
extern const u8 IslandCave_EventScript_RegicePersonalReturn[];
extern const u8 SkyPillar_Top_EventScript_RayquazaPersonalReturn[];
extern const u8 NewMauville_Inside_EventScript_Voltorb1Admission[];
extern const u8 NewMauville_Inside_EventScript_Voltorb2Admission[];
extern const u8 NewMauville_Inside_EventScript_Voltorb3Admission[];
extern const u8 AquaHideout_B1F_EventScript_Electrode1Admission[];
extern const u8 AquaHideout_B1F_EventScript_Electrode2Admission[];
extern const u8 AncientTomb_EventScript_RegisteelAdmission[];
extern const u8 DesertRuins_EventScript_RegirockAdmission[];
extern const u8 IslandCave_EventScript_RegiceAdmission[];
extern const u8 SkyPillar_Top_EventScript_RayquazaAdmission[];
static const u8 *const sAdmissionScripts[] = {
    NewMauville_Inside_EventScript_Voltorb1Admission,
    NewMauville_Inside_EventScript_Voltorb2Admission,
    NewMauville_Inside_EventScript_Voltorb3Admission,
    AquaHideout_B1F_EventScript_Electrode1Admission,
    AquaHideout_B1F_EventScript_Electrode2Admission,
    AncientTomb_EventScript_RegisteelAdmission,
    DesertRuins_EventScript_RegirockAdmission,
    IslandCave_EventScript_RegiceAdmission,
    SkyPillar_Top_EventScript_RayquazaAdmission,
};
struct CatalogueFixture {
    u16 id, species, hide, defeated, prerequisite;
    u8 group, map, local, x, y, level;
    const u8 *returnScript;
};
#define CATALOGUE(id, species, hide, defeated, prerequisite, map, local, x, y, level, script) \
    {id, species, hide, defeated, prerequisite, MAP_GROUP(map), MAP_NUM(map), local, x, y, level, script}
static const struct CatalogueFixture sCatalogueFixtures[] = {
    CATALOGUE(0x1001, SPECIES_VOLTORB, FLAG_HIDE_NEW_MAUVILLE_VOLTORB_1, FLAG_DEFEATED_VOLTORB_1_NEW_MAUVILLE, 0, MAP_NEW_MAUVILLE_INSIDE, 6, 25, 18, 25, NewMauville_Inside_EventScript_Voltorb1Return),
    CATALOGUE(0x1002, SPECIES_VOLTORB, FLAG_HIDE_NEW_MAUVILLE_VOLTORB_2, FLAG_DEFEATED_VOLTORB_2_NEW_MAUVILLE, 0, MAP_NEW_MAUVILLE_INSIDE, 7, 6, 11, 25, NewMauville_Inside_EventScript_Voltorb2PersonalReturn),
    CATALOGUE(0x1003, SPECIES_VOLTORB, FLAG_HIDE_NEW_MAUVILLE_VOLTORB_3, FLAG_DEFEATED_VOLTORB_3_NEW_MAUVILLE, 0, MAP_NEW_MAUVILLE_INSIDE, 8, 13, 10, 25, NewMauville_Inside_EventScript_Voltorb3PersonalReturn),
    CATALOGUE(0x1004, SPECIES_ELECTRODE, FLAG_HIDE_AQUA_HIDEOUT_B1F_ELECTRODE_1, FLAG_DEFEATED_ELECTRODE_1_AQUA_HIDEOUT, 0, MAP_AQUA_HIDEOUT_B1F, 6, 16, 9, 30, AquaHideout_B1F_EventScript_Electrode1PersonalReturn),
    CATALOGUE(0x1005, SPECIES_ELECTRODE, FLAG_HIDE_AQUA_HIDEOUT_B1F_ELECTRODE_2, FLAG_DEFEATED_ELECTRODE_2_AQUA_HIDEOUT, 0, MAP_AQUA_HIDEOUT_B1F, 8, 16, 10, 30, AquaHideout_B1F_EventScript_Electrode2PersonalReturn),
    CATALOGUE(0x1006, SPECIES_REGISTEEL, FLAG_HIDE_REGISTEEL, FLAG_DEFEATED_REGISTEEL, FLAG_SYS_REGISTEEL_PUZZLE_COMPLETED, MAP_ANCIENT_TOMB, 1, 8, 7, 40, AncientTomb_EventScript_RegisteelPersonalReturn),
    CATALOGUE(0x1007, SPECIES_REGIROCK, FLAG_HIDE_REGIROCK, FLAG_DEFEATED_REGIROCK, FLAG_SYS_REGIROCK_PUZZLE_COMPLETED, MAP_DESERT_RUINS, 1, 8, 7, 40, DesertRuins_EventScript_RegirockPersonalReturn),
    CATALOGUE(0x1008, SPECIES_REGICE, FLAG_HIDE_REGICE, FLAG_DEFEATED_REGICE, FLAG_SYS_BRAILLE_REGICE_COMPLETED, MAP_ISLAND_CAVE, 1, 8, 7, 40, IslandCave_EventScript_RegicePersonalReturn),
    CATALOGUE(0x1009, SPECIES_RAYQUAZA, FLAG_HIDE_SKY_PILLAR_TOP_RAYQUAZA_STILL, FLAG_DEFEATED_RAYQUAZA, 0, MAP_SKY_PILLAR_TOP, 2, 14, 6, 70, SkyPillar_Top_EventScript_RayquazaPersonalReturn),
};

static void SetUpCatalogue(struct ResetFixture *f, const struct CatalogueFixture *source)
{
    SetUpPilot(f);
    MultiplayerResetAdmission_Init();
    gSaveBlock1Ptr->location.mapGroup = source->group;
    gSaveBlock1Ptr->location.mapNum = source->map;
    gSpecialVar_LastTalked = source->local;
    gObjectEvents[1].localId = source->local;
    gObjectEvents[1].mapGroup = source->group;
    gObjectEvents[1].mapNum = source->map;
    gObjectEvents[1].currentCoords.x = source->x + MAP_OFFSET;
    gObjectEvents[1].currentCoords.y = source->y + MAP_OFFSET;
    if (source->prerequisite) FlagSet(source->prerequisite);
    VarSet(VAR_SKY_PILLAR_STATE, 2);
}

static void BeginCatalogue(const struct CatalogueFixture *source)
{
    EXPECT(MultiplayerLocalReset_TryBegin(source->id));
    EXPECT(MultiplayerLocalReset_PersonalGeneration());
    CreateScriptedWildMon(source->species, source->level, ITEM_NONE);
    gMain.inBattle = TRUE;
    gBattleTypeFlags = BATTLE_TYPE_IS_MASTER | (source->id >= MP_STATIC_RESET_REGISTEEL ? BATTLE_TYPE_LEGENDARY : 0);
    EXPECT(MultiplayerLocalReset_TestOpenWindow());
}

TEST("Multiplayer static catalogue: nine exact sources reject altered object and native prerequisites")
{
    for (u32 i = 0; i < ARRAY_COUNT(sCatalogueFixtures); i++)
    {
        const struct CatalogueFixture *source = &sCatalogueFixtures[i];
        struct ResetFixture f; SetUpCatalogue(&f, source);
        EXPECT(MultiplayerStaticReset_EntryReady(source->id));
        gObjectEvents[1].currentCoords.x++; EXPECT(!MultiplayerStaticReset_EntryReady(source->id));
        gObjectEvents[1].currentCoords.x--;
        gSpecialVar_LastTalked++; EXPECT(!MultiplayerStaticReset_EntryReady(source->id));
        gSpecialVar_LastTalked--;
        FlagSet(source->hide); EXPECT(!MultiplayerStaticReset_EntryReady(source->id)); FlagClear(source->hide);
        FlagSet(source->defeated); EXPECT(!MultiplayerStaticReset_EntryReady(source->id)); FlagClear(source->defeated);
        if (source->prerequisite)
        {
            FlagClear(source->prerequisite); EXPECT(!MultiplayerStaticReset_EntryReady(source->id));
            FlagSet(source->prerequisite);
        }
        if (source->id == MP_STATIC_RESET_RAYQUAZA)
        {
            VarSet(VAR_SKY_PILLAR_STATE, 1); EXPECT(!MultiplayerStaticReset_EntryReady(source->id));
            VarSet(VAR_SKY_PILLAR_STATE, 2);
        }
        EXPECT(!MultiplayerStaticReset_EntryReady(0xE001)); // Shared event sources never opt in.
        BeginCatalogue(source);
        EXPECT_EQ(MultiplayerStaticReset_GetCount(source->id), 1);
        gBattleTypeFlags |= BATTLE_TYPE_TRAINER;
        EXPECT(!MultiplayerLocalReset_TestRequest());
        gBattleTypeFlags &= ~BATTLE_TYPE_TRAINER;
        MultiplayerLocalReset_CloseCapture();
        EXPECT(!MultiplayerLocalReset_TestRequest());
        MultiplayerLocalReset_Finish();
        TearDown(&f);
    }
}

TEST("Multiplayer static catalogue: accepted returns preserve all sources while catches keep original defeat branches")
{
    for (u32 i = 0; i < ARRAY_COUNT(sCatalogueFixtures); i++)
    for (u32 reset = 0; reset < 2; reset++)
    {
        const struct CatalogueFixture *source = &sCatalogueFixtures[i];
        struct ResetFixture f; SetUpCatalogue(&f, source); BeginCatalogue(source);
        if (reset)
        {
            EXPECT(MultiplayerLocalReset_TestRequest()); EXPECT(MultiplayerLocalReset_TestConfirm(TRUE));
            gBattleOutcome = B_OUTCOME_PLAYER_TELEPORTED;
        }
        else { MultiplayerLocalReset_CloseCapture(); gBattleOutcome = B_OUTCOME_CAUGHT; }
        FlagSet(FLAG_SYS_CTRL_OBJ_DELETE);
        gMain.inBattle = FALSE;
        RunScriptImmediately(source->returnScript);
        EXPECT_EQ(FlagGet(source->defeated), !reset);
        EXPECT(!FlagGet(source->hide));
        EXPECT(!FlagGet(FLAG_SYS_CTRL_OBJ_DELETE));
        EXPECT_EQ(MultiplayerStaticReset_GetCount(source->id), 1);
        EXPECT_EQ(VarGet(VAR_NEW_MAUVILLE_STATE), 1);
        EXPECT_EQ(VarGet(VAR_SKY_PILLAR_STATE), 2);
        if (source->prerequisite) EXPECT(FlagGet(source->prerequisite));
        EXPECT(!MultiplayerLocalReset_WasAccepted());
        TearDown(&f);
    }
}

TEST("Multiplayer static catalogue: host count appears unsigned and cannot cross descriptor or action boundaries")
{
    const struct CatalogueFixture *source = &sCatalogueFixtures[8];
    struct ResetFixture f; SetUpCatalogue(&f, source);
    EXPECT(MultiplayerStaticReset_SetAdmission(source->id, 0xFFFFFFFEu));
    BeginCatalogue(source);
    MultiplayerStaticReset_SetAdmission(0, 0);
    EXPECT_EQ(MultiplayerStaticReset_GetCount(MP_STATIC_RESET_VOLTORB_1), 0);
    EXPECT_EQ(MultiplayerStaticReset_GetCount(MP_STATIC_RESET_REGISTEEL), 0);
    const u8 *prompt = MultiplayerLocalReset_GetActionPrompt(0);
    static const u8 expectedPrompt[] = _("Attempt: 4294967294\nSELECT: Reset");
    EXPECT(prompt != NULL);
    EXPECT_EQ(StringCompare(prompt, expectedPrompt), 0);
    gBattleTypeFlags &= ~BATTLE_TYPE_LEGENDARY;
    EXPECT(!MultiplayerLocalReset_TestRequest());
    gBattleTypeFlags |= BATTLE_TYPE_LEGENDARY;
    EXPECT(MultiplayerLocalReset_TestRequest());
    EXPECT(!MultiplayerLocalReset_TestConfirm(FALSE));
    MultiplayerLocalReset_CloseAction();
    EXPECT(MultiplayerLocalReset_GetActionPrompt(0) == NULL);
    MultiplayerLocalReset_Finish();
    TearDown(&f);
}

TEST("Multiplayer authored admission: all nine scripts yield before generation then exit neutral on exact denial or cancellation")
{
    for (u32 i = 0; i < ARRAY_COUNT(sCatalogueFixtures); i++)
    for (u32 status = 1; status <= 2; status++)
    {
        const struct CatalogueFixture *source = &sCatalogueFixtures[i];
        struct ResetFixture f; SetUpCatalogue(&f, source);
        memset(gParties[B_TRAINER_OPPONENT_A], 0, sizeof(gParties[B_TRAINER_OPPONENT_A]));
        rng_value_t rng = gRngValue;
        // Enter the actual authored bytecode immediately after its movement/cry
        // prelude. Both its special and waitstate execute in the real interpreter.
        ScriptContext_SetupScript(sAdmissionScripts[i]);
        EXPECT(ScriptContext_RunScript());
        EXPECT(MultiplayerResetAdmission_IsWaiting());
        EXPECT_EQ(gSpecialVar_Result, MP_LOCAL_RESET_ADMISSION_PENDING);
        EXPECT(!ScriptContext_IsEnabled());
        EXPECT(!ScriptContext_RunScript());
        EXPECT_EQ(MultiplayerStaticReset_GetCount(source->id), 0);
        EXPECT_EQ(GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_SPECIES), SPECIES_NONE);
        EXPECT(!memcmp(&rng, &gRngValue, sizeof(rng)));
        struct MultiplayerResetReply reply = {.sequence=1, .session=1, .incarnation=1,
            .encounter=source->id, .status=status};
        if (status == 2) Connection(1, FALSE);
        gMultiplayerResetMailbox.reply = reply;
        gMultiplayerResetMailbox.inboundGeneration = 2;
        MultiplayerResetAdmission_Tick();
        EXPECT(!MultiplayerResetAdmission_IsWaiting());
        EXPECT(ScriptContext_IsEnabled());
        EXPECT_EQ(gMultiplayerResetMailbox.receipt.fieldReady, FALSE);
        EXPECT(!ScriptContext_RunScript()); // Actual authored release/end, no fake cleanup.
        MultiplayerResetAdmission_Tick();
        EXPECT_EQ(gMultiplayerResetMailbox.receipt.fieldReady, TRUE);
        EXPECT_EQ(gMultiplayerResetMailbox.receipt.state, status == 1 ? MP_RESET_DENIED : MP_RESET_CANCELLED);
        EXPECT_EQ(MultiplayerStaticReset_GetCount(source->id), 0);
        EXPECT_EQ(GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_SPECIES), SPECIES_NONE);
        EXPECT(!memcmp(&rng, &gRngValue, sizeof(rng)));
        EXPECT(!FlagGet(source->hide)); EXPECT(!FlagGet(source->defeated));
        TearDown(&f);
    }
}

TEST("Multiplayer authored admission: all nine immediate refusals exit without a wait or roll")
{
    for (u32 i = 0; i < ARRAY_COUNT(sCatalogueFixtures); i++)
    {
        const struct CatalogueFixture *source = &sCatalogueFixtures[i];
        struct ResetFixture f; SetUpCatalogue(&f, source);
        FlagSet(source->hide);
        rng_value_t rng = gRngValue;
        ScriptContext_SetupScript(sAdmissionScripts[i]);
        EXPECT(!ScriptContext_RunScript());
        EXPECT(!ScriptContext_IsEnabled());
        EXPECT(!MultiplayerResetAdmission_IsWaiting());
        EXPECT_EQ(gMultiplayerResetMailbox.request.sequence, 0);
        EXPECT_EQ(MultiplayerStaticReset_GetCount(source->id), 0);
        EXPECT(!memcmp(&rng, &gRngValue, sizeof(rng)));
        EXPECT(FlagGet(source->hide)); EXPECT(!FlagGet(source->defeated));
        TearDown(&f);
    }
}

TEST("Multiplayer authored admission: Voltorb and Electrode generate only after the exact receipt resumes bytecode")
{
    for (u32 i = 0; i < 5; i++)
    {
        const struct CatalogueFixture *source = &sCatalogueFixtures[i];
        struct ResetFixture f; SetUpCatalogue(&f, source);
        memset(gParties[B_TRAINER_OPPONENT_A], 0, sizeof(gParties[B_TRAINER_OPPONENT_A]));
        ScriptContext_SetupScript(sAdmissionScripts[i]);
        EXPECT(ScriptContext_RunScript());
        struct MultiplayerResetReply reply = {.sequence=1, .session=1, .incarnation=2,
            .encounter=source->id, .attempt=3};
        gMultiplayerResetMailbox.reply = reply;
        gMultiplayerResetMailbox.inboundGeneration = 2;
        MultiplayerResetAdmission_Tick();
        EXPECT(!ScriptContext_RunScript());
        EXPECT_EQ(MultiplayerStaticReset_GetCount(source->id), 0);
        EXPECT_EQ(GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_SPECIES), SPECIES_NONE);
        reply.incarnation = 1;
        gMultiplayerResetMailbox.reply = reply;
        gMultiplayerResetMailbox.inboundGeneration = 4;
        MultiplayerResetAdmission_Tick();
        EXPECT_EQ(MultiplayerStaticReset_GetCount(source->id), 3);
        EXPECT_EQ(GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_SPECIES), SPECIES_NONE);
        EXPECT(ScriptContext_RunScript()); // Resumes through setwildbattle to its native waitse.
        EXPECT_EQ(GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_SPECIES), source->species);
        EXPECT_EQ(GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_LEVEL), source->level);
        gMain.inBattle = TRUE;
        EXPECT(MultiplayerLocalReset_TestOpenWindow());
        MultiplayerLocalReset_Finish();
        TearDown(&f);
    }
}
