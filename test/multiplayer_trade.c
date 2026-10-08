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

struct TradeFixture {
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

static u8 Submit(struct TradeFixture *f)
{
    f->command.chunkCrc32 = TestCrc(0, f->command.data, f->command.length);
    f->generation += 2;
    gMultiplayerSaveMailbox.command = f->command;
    gMultiplayerSaveMailbox.commandGeneration = f->generation;
    MultiplayerSave_Tick();
    return gMultiplayerSaveMailbox.response.status;
}

static u8 Send(struct TradeFixture *f, u8 opcode, u32 offset)
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

static void SetUp(struct TradeFixture *f)
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

static void TearDown(struct TradeFixture *f)
{
    MultiplayerSave_Init();
    gMain.callback1 = f->cb1; gMain.callback2 = f->cb2;
    gMapHeader = f->header; gPlayerAvatar = f->avatar; gObjectEvents[0] = f->object;
    gRfuLinkStatus = f->rfu; gWirelessCommType = f->wireless;
    gReceivedRemoteLinkPlayers = f->received; gSprites[0].inUse = f->spriteInUse;
    gSoftResetDisabled = f->resetDisabled;
}

static void BeginTrade(struct TradeFixture *f)
{
    EXPECT_EQ(Send(f, MP_SAVE_BEGIN_TRADE_EXPORT, 0), MP_SAVE_OK);
    EXPECT_EQ(gMultiplayerSaveMailbox.response.length, 40);
    f->counter = Word(gMultiplayerSaveMailbox.response.data);
    f->rotation = Word(gMultiplayerSaveMailbox.response.data + 4);
    EXPECT_EQ(gMultiplayerSaveMailbox.state, MP_SAVE_EXPORTING);
    EXPECT(MultiplayerSave_IsHeld()); EXPECT(MultiplayerSave_PauseClocks());
    EXPECT(gSoftResetDisabled);
}

static u32 ReadAll(struct TradeFixture *f, u32 start)
{
    u32 i, crc = 0;
    for (i = start; i < 131072; i += 128)
    {
        u8 chunk[128];
        EXPECT_EQ(Send(f, MP_SAVE_READ, i), MP_SAVE_OK);
        EXPECT_EQ(gMultiplayerSaveMailbox.response.length, 128);
        EXPECT_EQ(gMultiplayerSaveMailbox.response.offset, i);
        memcpy(chunk, (const void *)gMultiplayerSaveMailbox.response.data, 128);
        crc = TestCrc(crc, chunk, 128);
    }
    return crc;
}

static void PrepareTrade(struct TradeFixture *f)
{
    BeginTrade(f);
    f->crc = ReadAll(f, 0);
    EXPECT_EQ(Send(f, MP_SAVE_END, 0), MP_SAVE_OK);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), f->crc);
    EXPECT_EQ(gMultiplayerSaveMailbox.state, MP_SAVE_TRADE_HELD);
    EXPECT(MultiplayerSave_PauseClocks()); EXPECT(gSoftResetDisabled);
}

TEST("Multiplayer save window: exact sectors and stream CRC match ordinary Flash")
{
    struct TradeFixture f;
    u8 expected[128];
    u32 offset, part, sectorCrc, crc = 0;
    SetUp(&f);
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_EXPORT, 0), MP_SAVE_OK);
    for (offset = 0; offset < 131072; offset += 4096)
    {
        EXPECT_EQ(Send(&f, MP_SAVE_READ_WINDOW, offset), MP_SAVE_OK);
        EXPECT_EQ(gMultiplayerSaveMailbox.response.offset, offset);
        EXPECT_EQ(gMultiplayerSaveMailbox.response.length, 4);
        sectorCrc = 0;
        for (part = 0; part < 4096; part += sizeof(expected))
        {
            ReadFlash(offset / 4096, part, expected, sizeof(expected));
            EXPECT_EQ(memcmp(expected, (u8 *)&gSaveDataBuffer + part, sizeof(expected)), 0);
            sectorCrc = TestCrc(sectorCrc, expected, sizeof(expected));
            crc = TestCrc(crc, expected, sizeof(expected));
        }
        EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), sectorCrc);
    }
    EXPECT_EQ(Send(&f, MP_SAVE_END, 0), MP_SAVE_OK);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), crc);
    EXPECT(!MultiplayerSave_IsHeld());
    TearDown(&f);
}

TEST("Multiplayer save window: replay does not advance and skipped or mixed offsets refuse")
{
    struct TradeFixture f;
    struct MultiplayerSaveControl original;
    u32 crc;
    SetUp(&f); BeginTrade(&f);
    EXPECT_EQ(Send(&f, MP_SAVE_READ_WINDOW, 0), MP_SAVE_OK);
    original = f.command; crc = Word(gMultiplayerSaveMailbox.response.data);
    EXPECT_EQ(Submit(&f), MP_SAVE_OK);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), crc);
    EXPECT_EQ(TestCrc(0, (u8 *)&gSaveDataBuffer, 4096), crc);
    EXPECT_EQ(Send(&f, MP_SAVE_READ_WINDOW, 8192), MP_SAVE_ORDER);
    f.command = original;
    EXPECT_EQ(Submit(&f), MP_SAVE_ORDER);
    f.command.commandSeq += 2;
    EXPECT_EQ(Send(&f, MP_SAVE_READ, 4096), MP_SAVE_OK);
    EXPECT_EQ(Send(&f, MP_SAVE_READ_WINDOW, 4224), MP_SAVE_ORDER);
    EXPECT_EQ(Send(&f, MP_SAVE_ABORT_TRADE, 0), MP_SAVE_OK);
    TearDown(&f);
}

TEST("Multiplayer save window: bound and completed trade hold never expose sector buffer")
{
    struct TradeFixture f;
    SetUp(&f);
    EXPECT_EQ(Send(&f, MP_SAVE_READ_WINDOW, 0), MP_SAVE_ORDER);
    PrepareTrade(&f);
    EXPECT_EQ(Send(&f, MP_SAVE_READ_WINDOW, 0), MP_SAVE_ORDER);
    EXPECT_EQ(Send(&f, MP_SAVE_ABORT_TRADE, 0), MP_SAVE_OK);
    TearDown(&f);
}

TEST("Multiplayer save window: expired ordinary snapshot cannot replay released buffer")
{
    struct TradeFixture f;
    u32 i;
    SetUp(&f);
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_EXPORT, 0), MP_SAVE_OK);
    EXPECT_EQ(Send(&f, MP_SAVE_READ_WINDOW, 0), MP_SAVE_OK);
    for (i = 0; i < 602; i++) MultiplayerSave_Tick();
    EXPECT(!MultiplayerSave_IsHeld());
    EXPECT_EQ(Submit(&f), MP_SAVE_ORDER);
    TearDown(&f);
}

static u8 BeginImport(struct TradeFixture *f)
{
    f->command.revision = 8; f->command.commandSeq++;
    f->command.opcode = MP_SAVE_BEGIN_IMPORT; f->command.offset = 0;
    f->command.length = 16; memset(f->command.data, 0, 192);
    PutWord(f->command.data, f->crc); PutWord(f->command.data + 4, f->counter);
    PutWord(f->command.data + 8, f->rotation); PutWord(f->command.data + 12, 42);
    return Submit(f);
}

static u8 WriteSource(struct TradeFixture *f, u32 offset, bool32 corrupt)
{
    f->command.commandSeq++; f->command.opcode = MP_SAVE_WRITE;
    f->command.offset = offset; f->command.length = 128;
    memset(f->command.data, 0, 192);
    // First export is counter 1. Its old bank is blank; the active source bank
    // remains untouched throughout inactive-bank staging.
    if (offset < 14 * 4096)
        memset(f->command.data, 0xFF, 128);
    else
        ReadFlash(offset / 4096, offset & 4095, f->command.data, 128);
    if (corrupt)
        f->command.data[0] ^= 1;
    return Submit(f);
}

TEST("Multiplayer trade: unsafe admission refuses before freezing or assigning")
{
    struct TradeFixture f;
    SetUp(&f);
    f.command.revision = 0;
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_TRADE_EXPORT, 0), MP_SAVE_ORDER);
    f.command.revision = 0x7FFFFFFF;
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_TRADE_EXPORT, 0), MP_SAVE_ORDER);
    EXPECT(!MultiplayerSave_IsHeld()); EXPECT(!gSoftResetDisabled);
    f.command.revision = 7;
    gMain.inBattle = TRUE;
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_TRADE_EXPORT, 0), MP_SAVE_UNSAFE);
    EXPECT(!MultiplayerSave_IsHeld()); EXPECT(!gSoftResetDisabled);
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_MULTIPLAYER_ID), 0);
    gMain.inBattle = FALSE;
    TearDown(&f);
}

TEST("Multiplayer trade: expiration retains frozen stream and cached ACK until authoritative abort")
{
    struct TradeFixture f;
    u32 i;
    u8 chunk[128];
    rng_value_t rng, rng2;
    SetUp(&f); BeginTrade(&f);
    rng = gRngValue; rng2 = gRng2Value;
    EXPECT_EQ(Send(&f, MP_SAVE_READ, 0), MP_SAVE_OK);
    memcpy(chunk, (const void *)gMultiplayerSaveMailbox.response.data, 128);
    for (i = 0; i < 602; i++) MultiplayerSave_Tick();
    EXPECT(MultiplayerSave_PauseClocks()); EXPECT(gSoftResetDisabled);
    EXPECT_EQ(Submit(&f), MP_SAVE_OK); // exact lost-ACK replay, even after expiration
    EXPECT_EQ(memcmp(chunk, (const void *)gMultiplayerSaveMailbox.response.data, 128), 0);
    EXPECT_EQ(Send(&f, MP_SAVE_QUERY_TRADE, 0), MP_SAVE_OK);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), 3);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data + 36), 128);
    EXPECT_EQ(Send(&f, MP_SAVE_ABORT_TRADE, 0), MP_SAVE_TIMEOUT);
    EXPECT_EQ(Send(&f, MP_SAVE_KEEP_TRADE, 0), MP_SAVE_OK);
    EXPECT_EQ(Send(&f, MP_SAVE_CANCEL, 0), MP_SAVE_ORDER);
    f.command.requestId[0] = 8;
    EXPECT_EQ(Send(&f, MP_SAVE_ABORT_TRADE, 0), MP_SAVE_SESSION);
    f.command.requestId[0] = 9;
    EXPECT_EQ(Send(&f, MP_SAVE_ABORT_TRADE, 0), MP_SAVE_OK);
    EXPECT(!MultiplayerSave_IsHeld()); EXPECT(!gSoftResetDisabled);
    EXPECT_EQ(memcmp(&rng, &gRngValue, 16), 0); EXPECT_EQ(memcmp(&rng2, &gRng2Value, 16), 0);
    TearDown(&f);
}

TEST("Multiplayer trade: export acknowledgment stays held and newer lease rebind preserves exact transaction")
{
    struct TradeFixture f;
    struct MultiplayerSaveControl rebind;
    u32 i;
    SetUp(&f); PrepareTrade(&f);
    // A read-only recovery probe may use a newly authenticated lease and an
    // unknown revision, without fencing away the original binding or ACK.
    f.command.sessionEpoch = 2; f.command.revision = 0;
    EXPECT_EQ(Send(&f, MP_SAVE_QUERY_TRADE, 0), MP_SAVE_OK);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data + 24), 1);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data + 32), 7);
    f.command.sessionEpoch = 1; f.command.revision = 7;
    EXPECT_EQ(Send(&f, MP_SAVE_KEEP_TRADE, 0), MP_SAVE_OK);
    f.command.sessionEpoch = 2; f.command.commandSeq = 1;
    f.command.opcode = MP_SAVE_REBIND_TRADE;
    EXPECT_EQ(Submit(&f), MP_SAVE_OK);
    rebind = f.command;
    EXPECT_EQ(gMultiplayerSaveMailbox.response.length, 48);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), MP_SAVE_TRADE_HELD);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data + 4), 131072);
    EXPECT_EQ(Submit(&f), MP_SAVE_OK);
    f.command.incarnation++;
    EXPECT_EQ(Submit(&f), MP_SAVE_SESSION); f.command = rebind;
    f.command.sessionEpoch = 1;
    EXPECT_EQ(Submit(&f), MP_SAVE_SESSION); f.command = rebind;
    f.command.commandSeq = 2;
    EXPECT_EQ(Submit(&f), MP_SAVE_ORDER);
    EXPECT_EQ(Send(&f, MP_SAVE_QUERY_TRADE, 0), MP_SAVE_OK);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), 1);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data + 24), 2);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data + 28), 11);
    f.command.sessionEpoch = 3; f.command.revision = 0; f.command.commandSeq = 1;
    memset(f.command.requestId, 0, 16); f.command.requestId[0] = 4;
    f.command.opcode = MP_SAVE_BIND;
    EXPECT_EQ(Submit(&f), MP_SAVE_BUSY);
    f.command = rebind; f.command.commandSeq = 3;
    for (i = 0; i < 602; i++) MultiplayerSave_Tick();
    EXPECT_EQ(Send(&f, MP_SAVE_KEEP_TRADE, 0), MP_SAVE_OK);
    EXPECT_EQ(Send(&f, MP_SAVE_ABORT_TRADE, 0), MP_SAVE_OK);
    EXPECT(!MultiplayerSave_IsHeld()); EXPECT(!gSoftResetDisabled);
    TearDown(&f);
}

TEST("Multiplayer trade: restarted export rereads the same frozen snapshot without saving or consuming RNG")
{
    struct TradeFixture f;
    rng_value_t rng, rng2;
    u32 crc;
    u8 first[128];
    SetUp(&f); BeginTrade(&f);
    rng = gRngValue; rng2 = gRng2Value;
    EXPECT_EQ(Send(&f, MP_SAVE_READ, 0), MP_SAVE_OK);
    memcpy(first, (const void *)gMultiplayerSaveMailbox.response.data, 128);
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_TRADE_EXPORT, 0), MP_SAVE_OK);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), f.counter);
    EXPECT_EQ(gSaveCounter, f.counter);
    EXPECT_EQ(Send(&f, MP_SAVE_READ, 0), MP_SAVE_OK);
    EXPECT_EQ(memcmp(first, (const void *)gMultiplayerSaveMailbox.response.data, 128), 0);
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_TRADE_EXPORT, 0), MP_SAVE_OK);
    crc = ReadAll(&f, 0);
    EXPECT_EQ(Send(&f, MP_SAVE_END, 0), MP_SAVE_OK);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), crc);
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_TRADE_EXPORT, 0), MP_SAVE_OK);
    EXPECT_EQ(ReadAll(&f, 0), crc);
    EXPECT_EQ(gSaveCounter, f.counter); EXPECT_EQ(gLastWrittenSector, f.rotation);
    EXPECT_EQ(memcmp(&rng, &gRngValue, 16), 0); EXPECT_EQ(memcmp(&rng2, &gRng2Value, 16), 0);
    EXPECT_EQ(Send(&f, MP_SAVE_END, 0), MP_SAVE_OK);
    EXPECT_EQ(Send(&f, MP_SAVE_ABORT_TRADE, 0), MP_SAVE_OK);
    TearDown(&f);
}

TEST("Multiplayer trade: staging failure stays held and exact postimage retry requires verified final release")
{
    struct TradeFixture f;
    u32 i, postCrc, identity;
    SetUp(&f); PrepareTrade(&f);
    identity = GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_MULTIPLAYER_ID);
    EXPECT_EQ(f.counter, 1);
    EXPECT_EQ(BeginImport(&f), MP_SAVE_OK);
    EXPECT_EQ(Send(&f, MP_SAVE_ABORT_TRADE, 0), MP_SAVE_ORDER);
    EXPECT_EQ(Send(&f, MP_SAVE_CANCEL, 0), MP_SAVE_ORDER);
    for (i = 0; i < 15 * 4096; i += 128)
    {
        u8 status = WriteSource(&f, i, i == 14 * 4096);
        EXPECT_EQ(status, i == 15 * 4096 - 128 ? MP_SAVE_VALIDATION : MP_SAVE_OK);
    }
    EXPECT_EQ(gMultiplayerSaveMailbox.state, MP_SAVE_TRADE_HELD);
    EXPECT(MultiplayerSave_PauseClocks()); EXPECT(gSoftResetDisabled);
    EXPECT_EQ(Send(&f, MP_SAVE_QUERY_TRADE, 0), MP_SAVE_OK);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), 5);
    EXPECT_EQ(Send(&f, MP_SAVE_ABORT_TRADE, 0), MP_SAVE_ORDER);
    EXPECT_EQ(BeginImport(&f), MP_SAVE_OK);
    for (i = 0; i < 131072; i += 128)
    {
        u8 status = WriteSource(&f, i, FALSE);
        if (status != MP_SAVE_OK)
            Test_MgbaPrintf("write offset %d status %d state %d", i, status, gMultiplayerSaveMailbox.state);
        EXPECT_EQ(status, MP_SAVE_OK);
    }
    Send(&f, MP_SAVE_COMMIT, 0);
    EXPECT_EQ(gMultiplayerSaveMailbox.state, MP_SAVE_FINALIZING);
    for (i = 0; i < 14; i++) MultiplayerSave_Tick();
    EXPECT_EQ(gMultiplayerSaveMailbox.response.status, MP_SAVE_OK);
    EXPECT_EQ(gMultiplayerSaveMailbox.state, MP_SAVE_COMMITTED);
    EXPECT(MultiplayerSave_PauseClocks()); EXPECT(gSoftResetDisabled);
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_MULTIPLAYER_ID), identity);
    EXPECT_EQ(Send(&f, MP_SAVE_RELEASE_TRADE, 0), MP_SAVE_ORDER);
    postCrc = ReadAll(&f, 0);
    EXPECT_EQ(Send(&f, MP_SAVE_END, 0), MP_SAVE_ORDER);
    EXPECT_EQ(Send(&f, MP_SAVE_CANCEL, 0), MP_SAVE_ORDER);
    Send(&f, MP_SAVE_RELEASE_TRADE, 0);
    EXPECT_EQ(gMultiplayerSaveMailbox.state, MP_SAVE_RELOADING);
    EXPECT(gMain.callback2 == CB2_ContinueSavedGame);
    EXPECT(MultiplayerSave_IsHeld()); EXPECT(gSoftResetDisabled);
    // Callback execution itself is covered by actual-core proof; the mailbox
    // must wait for normal field readiness before publishing its release ACK.
    FieldCallbacks(); MultiplayerSave_Tick();
    EXPECT_EQ(gMultiplayerSaveMailbox.response.status, MP_SAVE_OK);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), postCrc);
    EXPECT(!MultiplayerSave_IsHeld()); EXPECT(!gSoftResetDisabled);
    EXPECT_EQ(Submit(&f), MP_SAVE_OK); // final-release lost ACK is idempotent
    EXPECT(!MultiplayerSave_IsHeld());
    TearDown(&f);
}

TEST("Multiplayer trade: ordinary checkpoint export and timeout retain their existing behavior")
{
    struct TradeFixture f;
    u32 i;
    SetUp(&f);
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_EXPORT, 0), MP_SAVE_OK);
    ReadAll(&f, 0);
    EXPECT_EQ(Send(&f, MP_SAVE_END, 0), MP_SAVE_OK);
    EXPECT(!MultiplayerSave_IsHeld()); EXPECT(!gSoftResetDisabled);
    // A regular export still times out to normal gameplay.
    f.command.requestId[0] = 10; f.command.revision = 8;
    EXPECT_EQ(Send(&f, MP_SAVE_BEGIN_EXPORT, 0), MP_SAVE_OK);
    for (i = 0; i < 602; i++) MultiplayerSave_Tick();
    EXPECT(!MultiplayerSave_IsHeld()); EXPECT(!gSoftResetDisabled);
    EXPECT_EQ(gMultiplayerSaveMailbox.response.status, MP_SAVE_TIMEOUT);
    TearDown(&f);
}

TEST("Multiplayer save window: staging refuses and committed import readback releases exact CRC")
{
    struct TradeFixture f;
    u32 i, crc = 0, identity;
    SetUp(&f); PrepareTrade(&f);
    identity = GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_MULTIPLAYER_ID);
    EXPECT_EQ(BeginImport(&f), MP_SAVE_OK);
    EXPECT_EQ(Send(&f, MP_SAVE_READ_WINDOW, 0), MP_SAVE_ORDER);
    for (i = 0; i < 131072; i += 128)
        EXPECT_EQ(WriteSource(&f, i, FALSE), MP_SAVE_OK);
    Send(&f, MP_SAVE_COMMIT, 0);
    for (i = 0; i < 14; i++) MultiplayerSave_Tick();
    EXPECT_EQ(gMultiplayerSaveMailbox.response.status, MP_SAVE_OK);
    EXPECT_EQ(gMultiplayerSaveMailbox.state, MP_SAVE_COMMITTED);
    for (i = 0; i < 131072; i += 4096)
    {
        EXPECT_EQ(Send(&f, MP_SAVE_READ_WINDOW, i), MP_SAVE_OK);
        EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), TestCrc(0, (u8 *)&gSaveDataBuffer, 4096));
        crc = TestCrc(crc, (u8 *)&gSaveDataBuffer, 4096);
    }
    Send(&f, MP_SAVE_RELEASE_TRADE, 0);
    EXPECT_EQ(gMultiplayerSaveMailbox.state, MP_SAVE_RELOADING);
    FieldCallbacks(); MultiplayerSave_Tick();
    EXPECT_EQ(gMultiplayerSaveMailbox.response.status, MP_SAVE_OK);
    EXPECT_EQ(Word(gMultiplayerSaveMailbox.response.data), crc);
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_MULTIPLAYER_ID), identity);
    EXPECT(!MultiplayerSave_IsHeld());
    TearDown(&f);
}
