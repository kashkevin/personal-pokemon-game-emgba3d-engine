#include "global.h"
#include "multiplayer_save.h"
#include "multiplayer_ownership.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "main.h"
#include "overworld.h"
#include "load_save.h"
#include "save.h"
#include "fieldmap.h"
#include "script.h"
#include "palette.h"
#include "sprite.h"
#include "random.h"
#include "link.h"
#include "link_rfu.h"
#include "librfu.h"
#include "trainer_hill.h"
#include "event_data.h"
#include "string_util.h"
#include "agb_flash.h"
#include "gba/flash_internal.h"
#include "constants/maps.h"
#include "constants/map_groups.h"
#include "constants/layouts.h"
#include "test/test.h"

static const u8 sTrainerName[] = _("TEST");

struct EmergencyFixture {
    struct MultiplayerSaveControl command;
    MainCallback cb1, cb2;
    struct MapHeader header;
    struct PlayerAvatar avatar;
    struct ObjectEvent object;
    struct RfuLinkStatus *rfu;
    u32 generation, counter, rotation, crc;
    u8 wireless, received;
    bool8 spriteInUse, resetDisabled;
};

static u32 TestCrc(u32 crc, const u8 *data, u32 size)
{
    u32 i;
    crc ^= 0xFFFFFFFF;
    while (size--)
    {
        crc ^= *data++;
        for (i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return crc ^ 0xFFFFFFFF;
}

static u32 Word(const volatile u8 *data)
{
    return data[0] | data[1] << 8 | data[2] << 16 | (u32)data[3] << 24;
}

static void PutWord(u8 *data, u32 value)
{
    data[0] = value; data[1] = value >> 8; data[2] = value >> 16; data[3] = value >> 24;
}

static u8 Submit(struct EmergencyFixture *f)
{
    f->command.chunkCrc32 = TestCrc(0, f->command.data, f->command.length);
    f->generation += 2;
    gMultiplayerSaveMailbox.command = f->command;
    gMultiplayerSaveMailbox.commandGeneration = f->generation;
    MultiplayerSave_Tick();
    return gMultiplayerSaveMailbox.response.status;
}

static u8 Send(struct EmergencyFixture *f, u8 opcode, u32 offset)
{
    f->command.commandSeq++;
    f->command.opcode = opcode; f->command.offset = offset;
    f->command.length = 0; memset(f->command.data, 0, 192);
    return Submit(f);
}

static void FieldCallbacks(void)
{
    gMain.callback1 = CB1_Overworld; gMain.callback2 = CB2_Overworld;
}

static void SetUp(struct EmergencyFixture *f)
{
    u32 i;
    memset(f, 0, sizeof(*f));
    f->cb1 = gMain.callback1; f->cb2 = gMain.callback2;
    f->header = gMapHeader; f->avatar = gPlayerAvatar; f->object = gObjectEvents[0];
    f->rfu = gRfuLinkStatus; f->wireless = gWirelessCommType;
    f->received = gReceivedRemoteLinkPlayers; f->spriteInUse = gSprites[0].inUse;
    f->resetDisabled = gSoftResetDisabled;
    ZeroPlayerPartyMons(); memset(gPokemonStoragePtr, 0, sizeof(*gPokemonStoragePtr));
    memset(gSaveBlock1Ptr, 0, sizeof(*gSaveBlock1Ptr));
    memset(gSaveBlock2Ptr, 0, sizeof(*gSaveBlock2Ptr));
    memset(gSaveBlock3Ptr, 0, sizeof(*gSaveBlock3Ptr));
    MultiplayerOwnership_Loaded();
    gSaveBlock1Ptr->location.mapGroup = MAP_GROUP(MAP_OLDALE_TOWN);
    gSaveBlock1Ptr->location.mapNum = MAP_NUM(MAP_OLDALE_TOWN);
    gSaveBlock1Ptr->mapLayoutId = LAYOUT_OLDALE_TOWN;
    StringCopy(gSaveBlock2Ptr->playerName, sTrainerName);
    gSaveBlock2Ptr->playerTrainerId[0] = 42;
    CreateMon(&gParties[B_TRAINER_PLAYER][0], SPECIES_TORCHIC, 7, 0, OTID_STRUCT_PLAYER_ID);
    CalculateMonStats(&gParties[B_TRAINER_PLAYER][0]);
    CalculatePlayerPartyCount();
    FieldCallbacks(); gMain.inBattle = FALSE;
    gMapHeader.mapLayout = (const void *)1; gMapHeader.mapLayoutId = LAYOUT_OLDALE_TOWN;
    memset(&gPlayerAvatar, 0, sizeof(gPlayerAvatar));
    memset(&gObjectEvents[0], 0, sizeof(gObjectEvents[0]));
    gObjectEvents[0].active = TRUE; gObjectEvents[0].isPlayer = TRUE;
    gSprites[0].inUse = TRUE; gPaletteFade.active = FALSE;
    ScriptContext_Init(); UnlockPlayerFieldControls();
    gTrainerHillVBlankCounter = NULL; FlagClear(FLAG_SYS_SAFARI_MODE);
    gWirelessCommType = 1; gReceivedRemoteLinkPlayers = FALSE;
    gRfu.callback = NULL; gRfu.playerExchangeActive = FALSE;
    gRfu.isShuttingDown = FALSE; gRfu.linkLossRecoveryState = 0; gRfuLinkStatus = NULL;
    gSoftResetDisabled = FALSE;
    CheckForFlashMemory(); EXPECT(gFlashMemoryPresent);
    for (i = 0; i < 32; i++)
        EXPECT_EQ(EraseFlashSector(i), 0);
    Save_ResetSaveCounters();
    MultiplayerSave_Init();
    f->command.incarnation = 11; f->command.sessionEpoch = 1;
    f->command.requestId[0] = 1; f->command.totalSize = 131072;
    EXPECT_EQ(Send(f, MP_SAVE_BIND, 0), MP_SAVE_OK);
    f->command.commandSeq++; f->command.opcode = MP_SAVE_GRANT_OWNERSHIP;
    f->command.length = 60; f->command.data[0] = 1;
    f->command.data[16] = 2; f->command.data[32] = 3;
    PutWord(f->command.data + 48, 1); PutWord(f->command.data + 52, 33);
    PutWord(f->command.data + 56, 1);
    EXPECT_EQ(Submit(f), MP_SAVE_OK);
    memset(f->command.requestId, 0, 16); f->command.requestId[0] = 9;
    f->command.revision = 7;
}

static void TearDown(struct EmergencyFixture *f)
{
    MultiplayerSave_Init();
    gMain.callback1 = f->cb1; gMain.callback2 = f->cb2;
    gMapHeader = f->header; gPlayerAvatar = f->avatar; gObjectEvents[0] = f->object;
    gRfuLinkStatus = f->rfu; gWirelessCommType = f->wireless;
    gReceivedRemoteLinkPlayers = f->received; gSprites[0].inUse = f->spriteInUse;
    gSoftResetDisabled = f->resetDisabled;
}

static void WaitPastDeadline(void)
{
    u32 i;
    for (i = 0; i < 1801; i++)
        MultiplayerSave_Tick();
}

static u32 ReadWindows(struct EmergencyFixture *f, u32 start, u32 crc)
{
    u32 offset, sectorCrc;
    for (offset = start; offset < 131072; offset += 4096)
    {
        EXPECT_EQ(Send(f, MP_SAVE_READ_WINDOW, offset), MP_SAVE_OK);
        EXPECT_EQ(gMultiplayerSaveMailbox.response.offset, offset);
        sectorCrc = TestCrc(0, (const u8 *)&gSaveDataBuffer, 4096);
        EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), sectorCrc);
        crc = TestCrc(crc, (const u8 *)&gSaveDataBuffer, 4096);
    }
    return crc;
}

static void CheckHeld(u32 state)
{
    EXPECT_EQ(gMultiplayerSaveMailbox.state, state);
    EXPECT(MultiplayerSave_IsHeld());
    EXPECT(MultiplayerSave_SkipCallbacks());
    EXPECT(MultiplayerSave_PauseClocks());
    EXPECT(gSoftResetDisabled);
}

TEST("Multiplayer emergency: exact export completion preserves retirement entries and never releases")
{
    struct EmergencyFixture f;
    u32 crc;
    SetUp(&f);
    // Establish an already acknowledged roster and a legitimate retirement.
    CreateMon(&gParties[B_TRAINER_PLAYER][1], SPECIES_POOCHYENA, 3, 1, OTID_STRUCT_PLAYER_ID);
    CalculatePlayerPartyCount();
    EXPECT_EQ(MultiplayerOwnership_PrepareExport(), MP_SAVE_OK);
    MultiplayerOwnership_Acknowledge(TRUE);
    EXPECT(MultiplayerOwnership_Retire(&gParties[B_TRAINER_PLAYER][0].box, MP_RETIRE_RELEASE));
    gParties[B_TRAINER_PLAYER][0] = gParties[B_TRAINER_PLAYER][1];
    memset(&gParties[B_TRAINER_PLAYER][1], 0, sizeof(struct Pokemon));
    CalculatePlayerPartyCount();
    EXPECT_EQ(gSaveBlock3Ptr->ownership.retirementCount, 1);
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_EMERGENCY_EXPORT, 0), MP_SAVE_OK);
    EXPECT_EQ(gMultiplayerSaveMailbox.response.length, 40);
    crc = ReadWindows(&f, 0, 0);
    EXPECT_EQ(Send(&f, MP_SAVE_PRESERVE_EMERGENCY, 131072), MP_SAVE_OK);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), crc);
    EXPECT_EQ(gSaveBlock3Ptr->ownership.retirementCount, 1);
    EXPECT_EQ(Send(&f, MP_SAVE_QUERY_EMERGENCY, 0), MP_SAVE_OK);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), 7);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data + 36), 131072);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data + 40), crc);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data + 44), MP_SAVE_OK);
    WaitPastDeadline(); CheckHeld(MP_SAVE_EMERGENCY_PRESERVED);
    EXPECT_EQ(gSaveBlock3Ptr->ownership.retirementCount, 1);
    TearDown(&f);
}

TEST("Multiplayer emergency: solo battle admission freezes without saving or minting")
{
    struct EmergencyFixture f;
    u32 counter;
    SetUp(&f); counter = gSaveCounter; gMain.inBattle = TRUE;
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_EMERGENCY_EXPORT, 0), MP_SAVE_UNSAFE);
    EXPECT_EQ(gSaveCounter, counter);
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_MULTIPLAYER_ID), 0);
    EXPECT_EQ(Send(&f, MP_SAVE_QUERY_EMERGENCY, 0), MP_SAVE_OK);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), 1);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data + 44), MP_SAVE_UNSAFE);
    EXPECT_EQ(Send(&f, MP_SAVE_READ, 0), MP_SAVE_ORDER);
    EXPECT_EQ(Send(&f, MP_SAVE_PRESERVE_EMERGENCY, 131072), MP_SAVE_ORDER);
    WaitPastDeadline(); CheckHeld(MP_SAVE_EMERGENCY_UNSAVED);
    gMain.inBattle = FALSE; TearDown(&f);
}

TEST("Multiplayer emergency: missing acknowledged grant retains an unsaved core")
{
    struct EmergencyFixture f;
    u32 counter;
    SetUp(&f); counter = gSaveCounter;
    MultiplayerOwnership_InvalidateGrant();
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_EMERGENCY_EXPORT, 0), MP_SAVE_OWNERSHIP);
    EXPECT_EQ(gSaveCounter, counter);
    EXPECT_EQ(Send(&f, MP_SAVE_QUERY_EMERGENCY, 0), MP_SAVE_OK);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), 1);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data + 36), 0);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data + 40), 0);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data + 44), MP_SAVE_OWNERSHIP);
    WaitPastDeadline(); CheckHeld(MP_SAVE_EMERGENCY_UNSAVED);
    TearDown(&f);
}

TEST("Multiplayer emergency: active field script stays unsaved and held")
{
    struct EmergencyFixture f;
    u32 counter;
    SetUp(&f); counter = gSaveCounter;
    ScriptContext_Enable();
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_EMERGENCY_EXPORT, 0), MP_SAVE_UNSAFE);
    EXPECT_EQ(gSaveCounter, counter);
    EXPECT_EQ(Send(&f, MP_SAVE_QUERY_EMERGENCY, 0), MP_SAVE_OK);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), 1);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data + 44), MP_SAVE_UNSAFE);
    WaitPastDeadline(); CheckHeld(MP_SAVE_EMERGENCY_UNSAVED);
    ScriptContext_Init(); TearDown(&f);
}

TEST("Multiplayer emergency: ordinary release mutation and rebind cannot displace the held export")
{
    static const u8 refused[] = {MP_SAVE_END, MP_SAVE_CANCEL, MP_SAVE_BEGIN_IMPORT,
        MP_SAVE_GRANT_OWNERSHIP, MP_SAVE_BIND, MP_SAVE_REBIND_TRADE, MP_SAVE_ABORT_TRADE,
        MP_SAVE_RELEASE_TRADE, MP_SAVE_QUERY};
    struct EmergencyFixture f;
    struct MultiplayerSaveControl begin;
    u32 i, counter;
    SetUp(&f);
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_EMERGENCY_EXPORT, 0), MP_SAVE_OK);
    begin = f.command; counter = gSaveCounter;
    for (i = 0; i < ARRAY_COUNT(refused); i++)
    {
        EXPECT_EQ(Send(&f, refused[i], 0), MP_SAVE_BUSY);
        CheckHeld(MP_SAVE_EXPORTING);
        EXPECT_EQ(gSaveCounter, counter);
    }
    f.command = begin;
    EXPECT_EQ(Submit(&f), MP_SAVE_OK);
    EXPECT_EQ(gMultiplayerSaveMailbox.response.length, 40);
    EXPECT_EQ(gSaveCounter, counter);
    f.command.sessionEpoch++;
    EXPECT_EQ(Send(&f, MP_SAVE_BIND, 0), MP_SAVE_BUSY);
    CheckHeld(MP_SAVE_EXPORTING);
    TearDown(&f);
}

TEST("Multiplayer emergency: observation preserves exact read replay and deadline never resumes")
{
    struct EmergencyFixture f;
    struct MultiplayerSaveControl read;
    u32 crc, counter;
    SetUp(&f);
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_EMERGENCY_EXPORT, 0), MP_SAVE_OK);
    counter = gSaveCounter;
    EXPECT_EQ(Send(&f, MP_SAVE_READ_WINDOW, 0), MP_SAVE_OK);
    read = f.command; crc = TestCrc(0, (const u8 *)&gSaveDataBuffer, 4096);
    EXPECT_EQ(Send(&f, MP_SAVE_QUERY_EMERGENCY, 0), MP_SAVE_OK);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), 3);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data + 36), 4096);
    WaitPastDeadline(); CheckHeld(MP_SAVE_EXPORTING);
    f.command = read;
    EXPECT_EQ(Submit(&f), MP_SAVE_OK);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), crc);
    crc = ReadWindows(&f, 4096, crc);
    EXPECT_EQ(Send(&f, MP_SAVE_PRESERVE_EMERGENCY, 131072), MP_SAVE_OK);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), crc);
    EXPECT_EQ(gSaveCounter, counter);
    CheckHeld(MP_SAVE_EMERGENCY_PRESERVED);
    TearDown(&f);
}

TEST("Multiplayer emergency: incomplete read cannot produce a preservation receipt")
{
    struct EmergencyFixture f;
    SetUp(&f);
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_EMERGENCY_EXPORT, 0), MP_SAVE_OK);
    EXPECT_EQ(Send(&f, MP_SAVE_READ, 0), MP_SAVE_OK);
    EXPECT_EQ(Send(&f, MP_SAVE_PRESERVE_EMERGENCY, 131072), MP_SAVE_ORDER);
    EXPECT_EQ(Send(&f, MP_SAVE_QUERY_EMERGENCY, 0), MP_SAVE_OK);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), 3);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data + 36), 128);
    WaitPastDeadline(); CheckHeld(MP_SAVE_EXPORTING);
    TearDown(&f);
}

TEST("Multiplayer emergency: malformed and foreign admissions cannot freeze ordinary play")
{
    struct EmergencyFixture f;
    u32 i;
    SetUp(&f);
    f.command.revision = 0;
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_EMERGENCY_EXPORT, 0), MP_SAVE_MALFORMED);
    EXPECT(!MultiplayerSave_IsHeld());
    f.command.revision = 7; f.command.sessionEpoch++;
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_EMERGENCY_EXPORT, 0), MP_SAVE_SESSION);
    EXPECT(!MultiplayerSave_IsHeld());
    f.command.sessionEpoch--; f.command.incarnation++;
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_EMERGENCY_EXPORT, 0), MP_SAVE_SESSION);
    EXPECT(!MultiplayerSave_IsHeld());
    f.command.incarnation--;
    EXPECT_EQ(Send(&f, MP_SAVE_QUERY_EMERGENCY, 0), MP_SAVE_OK);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data + 4), MP_SAVE_BOUND);
    for (i = 0; i < 48; i++)
        if (i < 4 || i >= 8) EXPECT_EQ(gMultiplayerSaveMailbox.response.data[i], 0);
    TearDown(&f);
}

TEST("Multiplayer emergency: active trade retains its original identity and hold")
{
    struct EmergencyFixture f;
    u32 counter;
    SetUp(&f);
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_TRADE_EXPORT, 0), MP_SAVE_OK);
    counter = gSaveCounter;
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_EMERGENCY_EXPORT, 0), MP_SAVE_BUSY);
    EXPECT_EQ(Send(&f, MP_SAVE_QUERY_EMERGENCY, 0), MP_SAVE_OK);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), 0);
    EXPECT_EQ(Send(&f, MP_SAVE_QUERY_TRADE, 0), MP_SAVE_OK);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), 1);
    EXPECT_EQ(gSaveCounter, counter);
    CheckHeld(MP_SAVE_EXPORTING);
    EXPECT_EQ(Send(&f, MP_SAVE_ABORT_TRADE, 0), MP_SAVE_OK);
    EXPECT(!MultiplayerSave_IsHeld());
    TearDown(&f);
}

TEST("Multiplayer emergency: ordinary export watchdog behavior remains unchanged")
{
    struct EmergencyFixture f;
    SetUp(&f);
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_EXPORT, 0), MP_SAVE_OK);
    WaitPastDeadline();
    EXPECT(!MultiplayerSave_IsHeld());
    EXPECT_EQ(gMultiplayerSaveMailbox.state, MP_SAVE_BOUND);
    EXPECT_EQ(gMultiplayerSaveMailbox.response.status, MP_SAVE_TIMEOUT);
    TearDown(&f);
}


TEST("Multiplayer emergency: promotion preserves the ordinary snapshot and rewinds only its read cursor")
{
    struct EmergencyFixture f;
    u32 crc, counter;
    SetUp(&f);
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_EXPORT, 0), MP_SAVE_OK);
    counter = gSaveCounter;
    EXPECT_EQ(Send(&f, MP_SAVE_READ_WINDOW, 0), MP_SAVE_OK);
    EXPECT_EQ(Send(&f, MP_SAVE_PROMOTE_EMERGENCY_EXPORT, 0), MP_SAVE_OK);
    EXPECT_EQ(gSaveCounter, counter);
    EXPECT_EQ(gMultiplayerSaveMailbox.response.offset, 0);
    WaitPastDeadline();
    CheckHeld(MP_SAVE_EXPORTING);
    crc = ReadWindows(&f, 0, 0);
    EXPECT_EQ(Send(&f, MP_SAVE_PRESERVE_EMERGENCY, 131072), MP_SAVE_OK);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), crc);
    CheckHeld(MP_SAVE_EMERGENCY_PRESERVED);
    TearDown(&f);
}

TEST("Multiplayer emergency: expired ordinary export cannot create different bytes under the same UUID")
{
    struct EmergencyFixture f;
    u32 counter;
    SetUp(&f);
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_EXPORT, 0), MP_SAVE_OK);
    counter = gSaveCounter;
    WaitPastDeadline();
    EXPECT_EQ(gMultiplayerSaveMailbox.state, MP_SAVE_BOUND);
    EXPECT_EQ(Send(&f, MP_SAVE_PROMOTE_EMERGENCY_EXPORT, 0), MP_SAVE_ORDER);
    EXPECT_EQ(gSaveCounter, counter);
    CheckHeld(MP_SAVE_EMERGENCY_UNSAVED);
    WaitPastDeadline();
    CheckHeld(MP_SAVE_EMERGENCY_UNSAVED);
    EXPECT_EQ(Send(&f, MP_SAVE_READ_WINDOW, 0), MP_SAVE_ORDER);
    TearDown(&f);
}

TEST("Multiplayer emergency: promotion cannot steal a foreign ordinary or trade export")
{
    struct EmergencyFixture f;
    SetUp(&f);
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_EXPORT, 0), MP_SAVE_OK);
    f.command.requestId[0]++;
    EXPECT_EQ(Send(&f, MP_SAVE_PROMOTE_EMERGENCY_EXPORT, 0), MP_SAVE_SESSION);
    f.command.requestId[0]--;
    EXPECT_EQ(Send(&f, MP_SAVE_CANCEL, 0), MP_SAVE_CANCELED);
    FieldCallbacks();
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_TRADE_EXPORT, 0), MP_SAVE_OK);
    EXPECT_EQ(Send(&f, MP_SAVE_PROMOTE_EMERGENCY_EXPORT, 0), MP_SAVE_BUSY);
    EXPECT_EQ(Send(&f, MP_SAVE_QUERY_EMERGENCY, 0), MP_SAVE_OK);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), 0);
    TearDown(&f);
}
