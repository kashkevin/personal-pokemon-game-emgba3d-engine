#include "global.h"
#include "multiplayer_battle.h"
#include "multiplayer_save.h"
#include "multiplayer_ownership.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "main.h"
#include "overworld.h"
#include "load_save.h"
#include "save.h"
#include "script.h"
#include "palette.h"
#include "sprite.h"
#include "link.h"
#include "link_rfu.h"
#include "librfu.h"
#include "random.h"
#include "trainer_hill.h"
#include "event_data.h"
#include "agb_flash.h"
#include "gba/flash_internal.h"
#include "constants/maps.h"
#include "constants/map_groups.h"
#include "constants/layouts.h"
#include "test/test.h"

struct ChannelFixture {
    struct MultiplayerBattleControl command;
    struct MultiplayerBattleBinding binding;
    MainCallback cb1, cb2;
    struct MapHeader header;
    struct PlayerAvatar avatar;
    struct ObjectEvent object;
    struct RfuLinkStatus *rfu;
    u32 generation;
    u8 wireless, received;
    bool8 spriteInUse, resetDisabled;
};

static u32 Crc(const u8 *data, u32 size)
{
    u32 crc = 0xFFFFFFFF, i;
    while (size--)
    {
        crc ^= *data++;
        for (i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return crc ^ 0xFFFFFFFF;
}

static void PutWord(u8 *data, u32 value)
{
    data[0] = value; data[1] = value >> 8; data[2] = value >> 16; data[3] = value >> 24;
}

static u32 Word(const volatile u8 *data)
{
    return data[0] | data[1] << 8 | data[2] << 16 | (u32)data[3] << 24;
}

static u8 Submit(struct ChannelFixture *f)
{
    f->command.chunkCrc32 = Crc(f->command.data, f->command.length);
    gMultiplayerBattleMailbox.command = f->command;
    f->generation += 2; gMultiplayerBattleMailbox.commandGeneration = f->generation;
    MultiplayerBattle_Tick();
    EXPECT(!(gMultiplayerBattleMailbox.responseGeneration & 1));
    return gMultiplayerBattleMailbox.response.status;
}

static void Next(struct ChannelFixture *f, u8 opcode)
{
    f->command.commandSeq++; f->command.opcode = opcode;
    f->command.blockSeq = 0; f->command.offset = 0; f->command.totalSize = 0;
    f->command.sourceSlot = f->binding.localSlot; f->command.kind = 0;
    f->command.length = 0; memset(f->command.data, 0, 192);
}

static u8 Send(struct ChannelFixture *f, u8 opcode)
{
    Next(f, opcode); return Submit(f);
}

static void SetUp(struct ChannelFixture *f, u8 count, u8 local)
{
    u8 grant[60] = {0};
    u32 serial = 1, i;
    memset(f, 0, sizeof(*f));
    f->cb1 = gMain.callback1; f->cb2 = gMain.callback2;
    f->header = gMapHeader; f->avatar = gPlayerAvatar; f->object = gObjectEvents[0];
    f->rfu = gRfuLinkStatus; f->wireless = gWirelessCommType;
    f->received = gReceivedRemoteLinkPlayers; f->spriteInUse = gSprites[0].inUse;
    f->resetDisabled = gSoftResetDisabled;
    MultiplayerBattle_Init(); MultiplayerSave_Init();
    ZeroPlayerPartyMons(); memset(gPokemonStoragePtr, 0, sizeof(*gPokemonStoragePtr));
    memset(gSaveBlock1Ptr, 0, sizeof(*gSaveBlock1Ptr));
    memset(gSaveBlock2Ptr, 0, sizeof(*gSaveBlock2Ptr));
    memset(gSaveBlock3Ptr, 0, sizeof(*gSaveBlock3Ptr));
    MultiplayerOwnership_Loaded();
    grant[0] = 1; grant[16] = 2 + local; grant[32] = 3;
    PutWord(grant + 48, 1); PutWord(grant + 52, 33); PutWord(grant + 56, 1);
    EXPECT_EQ(MultiplayerOwnership_Grant(grant, 7 + local, 11 + local), MP_SAVE_OK);
    CreateMon(&gParties[B_TRAINER_PLAYER][0], SPECIES_TORCHIC, 7, 0, OTID_STRUCT_PLAYER_ID);
    SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_MULTIPLAYER_ID, &serial);
    CalculatePlayerPartyCount();
    gSaveBlock1Ptr->location.mapGroup = MAP_GROUP(MAP_OLDALE_TOWN);
    gSaveBlock1Ptr->location.mapNum = MAP_NUM(MAP_OLDALE_TOWN);
    gMain.callback1 = CB1_Overworld; gMain.callback2 = CB2_Overworld; gMain.inBattle = FALSE;
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
    f->binding.generation[0] = 99; f->binding.playerCount = count;
    f->binding.localSlot = local; f->binding.mode = MP_BATTLE_PHASE_DIAGNOSTIC;
    for (i = 0; i < count; i++)
    {
        f->binding.players[i][0] = 2 + i;
        f->binding.leases[i] = 7 + i; f->binding.incarnations[i] = 11 + i;
    }
    f->command.incarnation = 11 + local; f->command.sessionEpoch = 7 + local;
    f->command.battleId[0] = 77; f->command.phase = MP_BATTLE_PHASE_DIAGNOSTIC;
    Next(f, MP_BATTLE_BIND); f->command.length = sizeof(f->binding);
    memcpy(f->command.data, &f->binding, sizeof(f->binding));
}

static void Bind(struct ChannelFixture *f)
{
    EXPECT_EQ(Submit(f), MP_BATTLE_OK);
    EXPECT_EQ(gMultiplayerBattleMailbox.response.length, 64);
    EXPECT(MultiplayerBattle_IsActive()); EXPECT(MultiplayerBattle_PauseClocks());
    EXPECT(gSoftResetDisabled); EXPECT(IsNativeLinkSessionActive());
    EXPECT(!MultiplayerSave_FieldReady()); EXPECT(!MultiplayerOwnership_CanSaveParty());
}

static void TearDown(struct ChannelFixture *f)
{
    MultiplayerBattle_Init(); MultiplayerSave_Init();
    gMain.callback1 = f->cb1; gMain.callback2 = f->cb2;
    gMapHeader = f->header; gPlayerAvatar = f->avatar; gObjectEvents[0] = f->object;
    gRfuLinkStatus = f->rfu; gWirelessCommType = f->wireless;
    gReceivedRemoteLinkPlayers = f->received; gSprites[0].inUse = f->spriteInUse;
    gSoftResetDisabled = f->resetDisabled;
}

static void Emit(struct ChannelFixture *f, u32 token)
{
    Next(f, MP_BATTLE_EMIT_DIAGNOSTIC); f->command.length = 4;
    PutWord(f->command.data, token); EXPECT_EQ(Submit(f), MP_BATTLE_OK);
}

static void Receive(struct ChannelFixture *f, u8 source, u32 sequence, u32 token)
{
    struct MultiplayerBattleDiagnostic block = {0};
    Next(f, MP_BATTLE_RECEIVE); f->command.sourceSlot = source;
    f->command.blockSeq = sequence; f->command.kind = MP_BATTLE_KIND_DIAGNOSTIC;
    f->command.totalSize = sizeof(block); f->command.length = sizeof(block);
    block.magic = MULTIPLAYER_BATTLE_DIAGNOSTIC_MAGIC; block.version = 1;
    memcpy(block.generation, f->binding.generation, 16);
    block.sourceSlot = source; block.token = token;
    block.lease = f->binding.leases[source]; block.incarnation = f->binding.incarnations[source];
    memcpy(f->command.data, &block, sizeof(block));
}

TEST("Multiplayer battle channel: atomic binding refuses unsafe field and stale identities")
{
    struct ChannelFixture f;
    struct MultiplayerBattleControl original;
    u32 response;
    SetUp(&f, 2, 0);
    EXPECT_EQ(gMultiplayerBattleMailbox.byteSize, 512);
    response = gMultiplayerBattleMailbox.responseGeneration;
    gMultiplayerBattleMailbox.command = f.command;
    gMultiplayerBattleMailbox.commandGeneration = 1; MultiplayerBattle_Tick();
    EXPECT_EQ(gMultiplayerBattleMailbox.responseGeneration, response);
    EXPECT(!MultiplayerBattle_IsActive());
    gMain.inBattle = TRUE;
    EXPECT_EQ(Submit(&f), MP_BATTLE_UNSAFE); EXPECT(!MultiplayerBattle_IsActive());
    gMain.inBattle = FALSE; Bind(&f); original = f.command;
    EXPECT_EQ(Submit(&f), MP_BATTLE_OK);
    f.command.sessionEpoch--; EXPECT_EQ(Submit(&f), MP_BATTLE_SESSION);
    f.command = original; f.command.data[0] ^= 1;
    EXPECT_EQ(Submit(&f), MP_BATTLE_BUSY);
    f.command = original; EXPECT_EQ(Submit(&f), MP_BATTLE_OK);
    EXPECT_EQ(Send(&f, MP_BATTLE_CLOSE), MP_BATTLE_OK);
    Next(&f, MP_BATTLE_BIND); f.command.length = sizeof(f.binding);
    memcpy(f.command.data, &f.binding, sizeof(f.binding));
    EXPECT_EQ(Submit(&f), MP_BATTLE_ORDER); // Closed UUID cannot reopen.
    f.command.battleId[0] = 78; Bind(&f);
    TearDown(&f);
}

TEST("Multiplayer battle channel: native block seam includes loopback and waits for checked peer delivery")
{
    struct ChannelFixture f;
    u8 rejected[256] = {0};
    u32 crc;
    SetUp(&f, 4, 2); Bind(&f);
    EXPECT_EQ(GetMultiplayerId(), 2); EXPECT_EQ(GetLinkPlayerCount(), 4);
    EXPECT_EQ(GetLinkPlayerCount_2(), 4); EXPECT(!IsLinkMaster());
    EXPECT(!SendBlock(0, rejected, 256)); EXPECT(!SendBlock(0, rejected, 40));
    Emit(&f, 12345); crc = Word(gMultiplayerBattleMailbox.response.data + 40);
    EXPECT_EQ(GetBlockReceivedStatus(), 4); EXPECT(!IsLinkTaskFinished());
    EXPECT_EQ(((struct MultiplayerBattleDiagnostic *)gBlockRecvBuffer[2])->token, 12345);
    OpenLink(); CloseLink(); SetCloseLinkCallbackAndType(0);
    EXPECT(MultiplayerBattle_IsActive()); EXPECT_EQ(GetLinkPlayerCount(), 4);
    Next(&f, MP_BATTLE_READ_OUT); f.command.blockSeq = 1;
    f.command.kind = MP_BATTLE_KIND_DIAGNOSTIC; f.command.totalSize = 40;
    EXPECT_EQ(Submit(&f), MP_BATTLE_OK);
    EXPECT_EQ(gMultiplayerBattleMailbox.response.length, 40);
    EXPECT_EQ(gMultiplayerBattleMailbox.response.offset, 0);
    EXPECT_EQ(Crc((const void *)gMultiplayerBattleMailbox.response.data, 40), crc);
    EXPECT_EQ(Submit(&f), MP_BATTLE_OK); // Exact lost read ACK, cursor unchanged.
    Next(&f, MP_BATTLE_ACK_OUT); f.command.blockSeq = 1; f.command.totalSize = 40;
    f.command.offset = 40; f.command.kind = MP_BATTLE_KIND_DIAGNOSTIC; f.command.length = 8;
    PutWord(f.command.data, crc); PutWord(f.command.data + 4, 11);
    EXPECT_EQ(Submit(&f), MP_BATTLE_OK); EXPECT(IsLinkTaskFinished());
    ResetBlockReceivedFlag(2); EXPECT_EQ(GetBlockReceivedStatus(), 0);
    gWirelessCommType = 0; Emit(&f, 54321); EXPECT_EQ(GetMultiplayerId(), 2);
    EXPECT_EQ(Send(&f, MP_BATTLE_CLOSE), MP_BATTLE_OK);
    EXPECT(!MultiplayerBattle_IsActive()); EXPECT(!gSoftResetDisabled);
    TearDown(&f);
}

TEST("Multiplayer battle channel: receive fences roles generations and consumed duplicate blocks")
{
    struct ChannelFixture f;
    struct MultiplayerBattleDiagnostic before;
    SetUp(&f, 2, 0); Bind(&f);
    memset(&before, 0, sizeof(before));
    Receive(&f, 1, 1, 456); f.command.kind = 2;
    EXPECT_EQ(Submit(&f), MP_BATTLE_KIND);
    EXPECT(!memcmp(gBlockRecvBuffer[1], &before, sizeof(before)));
    Receive(&f, 1, 1, 456); f.command.data[8] ^= 1;
    EXPECT_EQ(Submit(&f), MP_BATTLE_ROLE);
    Receive(&f, 1, 1, 456); f.command.data[32] ^= 1;
    EXPECT_EQ(Submit(&f), MP_BATTLE_ROLE);
    Receive(&f, 0, 1, 456); EXPECT_EQ(Submit(&f), MP_BATTLE_ROLE);
    Receive(&f, 1, 2, 456); EXPECT_EQ(Submit(&f), MP_BATTLE_ORDER);
    Receive(&f, 1, 1, 456); EXPECT_EQ(Submit(&f), MP_BATTLE_OK);
    memcpy(&before, gBlockRecvBuffer[1], sizeof(before));
    EXPECT_EQ(GetBlockReceivedStatus(), 2);
    Receive(&f, 1, 2, 999); EXPECT_EQ(Submit(&f), MP_BATTLE_BUSY);
    EXPECT(!memcmp(gBlockRecvBuffer[1], &before, sizeof(before)));
    ResetBlockReceivedFlags(); EXPECT_EQ(GetBlockReceivedStatus(), 0);
    Receive(&f, 1, 1, 456); EXPECT_EQ(Submit(&f), MP_BATTLE_OK);
    EXPECT_EQ(GetBlockReceivedStatus(), 0); // A consumed retry cannot redispatch.
    Receive(&f, 1, 1, 999); EXPECT_EQ(Submit(&f), MP_BATTLE_ORDER);
    Receive(&f, 1, 2, 999); EXPECT_EQ(Submit(&f), MP_BATTLE_OK);
    EXPECT_EQ(((struct MultiplayerBattleDiagnostic *)gBlockRecvBuffer[1])->token, 999);
    Receive(&f, 1, 1, 456); EXPECT_EQ(Submit(&f), MP_BATTLE_ORDER);
    TearDown(&f);
}

TEST("Multiplayer battle channel: malformed commands cannot replace a cached emission acknowledgment")
{
    struct ChannelFixture f;
    struct MultiplayerBattleControl emitted;
    u8 buffer[40];
    SetUp(&f, 2, 0); Bind(&f); Emit(&f, 88); emitted = f.command;
    memcpy(buffer, gBlockSendBuffer, sizeof(buffer));
    Next(&f, MP_BATTLE_RECEIVE); f.command.length = 40; f.command.totalSize = 257;
    EXPECT_EQ(Submit(&f), MP_BATTLE_MALFORMED);
    f.command = emitted; f.command.reserved = 1;
    EXPECT_EQ(Submit(&f), MP_BATTLE_MALFORMED);
    f.command = emitted; f.command.data[191] = 1;
    EXPECT_EQ(Submit(&f), MP_BATTLE_MALFORMED);
    f.command = emitted; f.command.phase = 2;
    EXPECT_EQ(Submit(&f), MP_BATTLE_MALFORMED);
    f.command = emitted; f.command.opcode = 255;
    EXPECT_EQ(Submit(&f), MP_BATTLE_MALFORMED);
    f.command = emitted; f.command.chunkCrc32 ^= 1;
    gMultiplayerBattleMailbox.command = f.command;
    f.generation += 2; gMultiplayerBattleMailbox.commandGeneration = f.generation;
    MultiplayerBattle_Tick(); EXPECT_EQ(gMultiplayerBattleMailbox.response.status, MP_BATTLE_CRC);
    f.command = emitted; EXPECT_EQ(Submit(&f), MP_BATTLE_OK);
    EXPECT_EQ(Word(gMultiplayerBattleMailbox.response.data + 28), 1);
    EXPECT(!memcmp(buffer, gBlockSendBuffer, sizeof(buffer)));
    f.command.opcode = MP_BATTLE_QUERY; f.command.length = 0;
    memset(f.command.data, 0, 192); EXPECT_EQ(Submit(&f), MP_BATTLE_OK);
    f.command = emitted; EXPECT_EQ(Submit(&f), MP_BATTLE_OK);
    EXPECT(!memcmp(buffer, gBlockSendBuffer, sizeof(buffer)));
    TearDown(&f);
}

TEST("Multiplayer battle channel: checked ACK and receive clearing enforce bounded flow control")
{
    struct ChannelFixture f;
    u32 crc;
    SetUp(&f, 2, 0); Bind(&f); Emit(&f, 22);
    crc = Word(gMultiplayerBattleMailbox.response.data + 40);
    Next(&f, MP_BATTLE_ACK_OUT); f.command.blockSeq = 1; f.command.totalSize = 40;
    f.command.offset = 40; f.command.kind = MP_BATTLE_KIND_DIAGNOSTIC; f.command.length = 8;
    PutWord(f.command.data, crc); PutWord(f.command.data + 4, 2);
    EXPECT_EQ(Submit(&f), MP_BATTLE_ORDER); // No read yet.
    Next(&f, MP_BATTLE_READ_OUT); f.command.blockSeq = 1; f.command.totalSize = 40;
    f.command.kind = MP_BATTLE_KIND_DIAGNOSTIC;
    EXPECT_EQ(Submit(&f), MP_BATTLE_OK);
    Next(&f, MP_BATTLE_ACK_OUT); f.command.blockSeq = 1; f.command.totalSize = 40;
    f.command.offset = 40; f.command.kind = MP_BATTLE_KIND_DIAGNOSTIC; f.command.length = 8;
    PutWord(f.command.data, crc ^ 1); PutWord(f.command.data + 4, 2);
    EXPECT_EQ(Submit(&f), MP_BATTLE_CRC);
    f.command.commandSeq++; PutWord(f.command.data, crc); PutWord(f.command.data + 4, 3);
    EXPECT_EQ(Submit(&f), MP_BATTLE_ROLE); EXPECT(!IsLinkTaskFinished());
    f.command.commandSeq++; PutWord(f.command.data + 4, 2);
    EXPECT_EQ(Submit(&f), MP_BATTLE_OK); EXPECT(IsLinkTaskFinished());
    Next(&f, MP_BATTLE_EMIT_DIAGNOSTIC); f.command.length = 4;
    EXPECT_EQ(Submit(&f), MP_BATTLE_BUSY); // Local loopback not consumed.
    Next(&f, MP_BATTLE_CLEAR_RX); f.command.length = 4; PutWord(f.command.data, 4);
    EXPECT_EQ(Submit(&f), MP_BATTLE_ROLE);
    f.command.commandSeq++; PutWord(f.command.data, 1);
    EXPECT_EQ(Submit(&f), MP_BATTLE_OK); EXPECT_EQ(GetBlockReceivedStatus(), 0);
    Emit(&f, 23); EXPECT_EQ(Word(gMultiplayerBattleMailbox.response.data + 28), 2);
    TearDown(&f);
}

TEST("Multiplayer battle channel: expiry keeps callbacks clocks and reset held until explicit close")
{
    struct ChannelFixture f;
    rng_value_t rng, rng2;
    struct Pokemon party;
    struct SaveBlock3 ownership;
    u32 i, counter;
    SetUp(&f, 2, 0); Bind(&f); Emit(&f, 33);
    rng = gRngValue; rng2 = gRng2Value; counter = gSaveCounter;
    party = gParties[B_TRAINER_PLAYER][0]; ownership = *gSaveBlock3Ptr;
    for (i = 0; i < 600; i++)
        MultiplayerBattle_Tick();
    EXPECT_EQ(gMultiplayerBattleMailbox.state, MP_BATTLE_EXPIRED);
    EXPECT(MultiplayerBattle_IsActive()); EXPECT(MultiplayerBattle_PauseClocks());
    EXPECT(gSoftResetDisabled); EXPECT(!IsLinkTaskFinished());
    EXPECT(!memcmp(&rng, &gRngValue, sizeof(rng))); EXPECT(!memcmp(&rng2, &gRng2Value, sizeof(rng2)));
    EXPECT(!memcmp(&party, &gParties[B_TRAINER_PLAYER][0], sizeof(party)));
    EXPECT(!memcmp(&ownership, gSaveBlock3Ptr, sizeof(ownership))); EXPECT_EQ(counter, gSaveCounter);
    EXPECT_EQ(Send(&f, MP_BATTLE_KEEP), MP_BATTLE_TIMEOUT);
    EXPECT_EQ(Send(&f, MP_BATTLE_QUERY), MP_BATTLE_OK);
    EXPECT_EQ(Word(gMultiplayerBattleMailbox.response.data) & 3, 3);
    EXPECT_EQ(Word(gMultiplayerBattleMailbox.response.data + 52), 0);
    // QUERY did not consume the next command's sequence.
    f.command.commandSeq--;
    EXPECT_EQ(Send(&f, MP_BATTLE_CLOSE), MP_BATTLE_OK);
    EXPECT(!MultiplayerBattle_IsActive()); EXPECT(!MultiplayerBattle_PauseClocks());
    EXPECT(!gSoftResetDisabled); EXPECT(MultiplayerSave_FieldReady());
    TearDown(&f);
}

TEST("Multiplayer battle channel: save ownership and native trade controls cannot escape the dedicated hold")
{
    struct ChannelFixture f;
    struct MultiplayerSaveControl command = {0};
    u8 query[80], oldQuery[80];
    u32 i;
    static const u8 mutators[] = {MP_SAVE_BIND, MP_SAVE_BEGIN_EXPORT, MP_SAVE_CANCEL,
        MP_SAVE_BEGIN_IMPORT, MP_SAVE_GRANT_OWNERSHIP, MP_SAVE_QUERY_OWNERSHIP,
        MP_SAVE_BEGIN_TRADE_EXPORT, MP_SAVE_ABORT_TRADE, MP_SAVE_RELEASE_TRADE};
    SetUp(&f, 2, 0);
    command.incarnation = 11; command.sessionEpoch = 7; command.commandSeq = 1;
    command.requestId[0] = 5; command.totalSize = 131072;
    command.opcode = MP_SAVE_BIND;
    gMultiplayerSaveMailbox.command = command; gMultiplayerSaveMailbox.commandGeneration = 2;
    MultiplayerSave_Tick(); EXPECT_EQ(gMultiplayerSaveMailbox.response.status, MP_SAVE_OK);
    EXPECT_EQ(MultiplayerOwnership_Query(oldQuery), MP_SAVE_OK); Bind(&f);
    for (i = 0; i < ARRAY_COUNT(mutators); i++)
    {
        command.opcode = mutators[i];
        gMultiplayerSaveMailbox.command = command; gMultiplayerSaveMailbox.commandGeneration = 2 * (i + 2);
        MultiplayerSave_Tick(); EXPECT_EQ(gMultiplayerSaveMailbox.response.status, MP_SAVE_BUSY);
        EXPECT(MultiplayerBattle_IsActive()); EXPECT(gSoftResetDisabled);
    }
    EXPECT_EQ(MultiplayerOwnership_Query(query), MP_SAVE_OK);
    EXPECT(!memcmp(oldQuery, query, 80));
    EXPECT_EQ(HandleSavingData(SAVE_NORMAL), SAVE_STATUS_ERROR);
    EXPECT_EQ(Send(&f, MP_BATTLE_CLOSE), MP_BATTLE_OK);
    command.opcode = MP_SAVE_BIND;
    command.incarnation = 10;
    gMultiplayerSaveMailbox.command = command; gMultiplayerSaveMailbox.commandGeneration = 38;
    MultiplayerSave_Tick(); EXPECT_EQ(gMultiplayerSaveMailbox.response.status, MP_SAVE_SESSION);
    command.incarnation = 11;
    gMultiplayerSaveMailbox.command = command; gMultiplayerSaveMailbox.commandGeneration = 40;
    MultiplayerSave_Tick(); EXPECT_EQ(gMultiplayerSaveMailbox.response.status, MP_SAVE_OK);
    // Exact original BIND survived the intervening BUSY and stale refusals.
    EXPECT_EQ(MultiplayerOwnership_Query(query), MP_SAVE_OK);
    EXPECT_EQ(Word(query) & 2, 0); // Native save BIND works only after explicit close.
    command.incarnation = 12;
    gMultiplayerSaveMailbox.command = command; gMultiplayerSaveMailbox.commandGeneration = 42;
    MultiplayerSave_Tick(); EXPECT_EQ(gMultiplayerSaveMailbox.response.status, MP_SAVE_OK);
    Next(&f, MP_BATTLE_BIND); f.command.battleId[0] = 78;
    f.command.length = sizeof(f.binding); memcpy(f.command.data, &f.binding, sizeof(f.binding));
    EXPECT_EQ(Submit(&f), MP_BATTLE_SESSION); EXPECT(!MultiplayerBattle_IsActive());
    TearDown(&f);
}

TEST("Multiplayer battle channel: ordinary held save rejects diagnostic binding without consuming its request")
{
    struct ChannelFixture f;
    struct MultiplayerSaveControl command = {0};
    u8 grant[60] = {0};
    u32 i;
    SetUp(&f, 2, 0);
    CheckForFlashMemory(); EXPECT(gFlashMemoryPresent);
    for (i = 0; i < 32; i++)
        EXPECT_EQ(EraseFlashSector(i), 0);
    Save_ResetSaveCounters();
    command.incarnation = 11; command.sessionEpoch = 7; command.requestId[0] = 1;
    command.commandSeq = 1; command.totalSize = 131072; command.opcode = MP_SAVE_BIND;
    gMultiplayerSaveMailbox.command = command; gMultiplayerSaveMailbox.commandGeneration = 2;
    MultiplayerSave_Tick(); EXPECT_EQ(gMultiplayerSaveMailbox.response.status, MP_SAVE_OK);
    // A native BIND revokes mint authority; install a fresh ordinary grant.
    grant[0] = 1; grant[16] = 2; grant[32] = 4;
    PutWord(grant + 48, 33); PutWord(grant + 52, 65);
    EXPECT_EQ(MultiplayerOwnership_Grant(grant, 7, 11), MP_SAVE_OK);
    command.commandSeq++; command.opcode = MP_SAVE_BEGIN_EXPORT; command.revision = 1;
    gMultiplayerSaveMailbox.command = command; gMultiplayerSaveMailbox.commandGeneration = 4;
    MultiplayerSave_Tick(); EXPECT_EQ(gMultiplayerSaveMailbox.response.status, MP_SAVE_OK);
    EXPECT(MultiplayerSave_IsHeld());
    EXPECT_EQ(Submit(&f), MP_BATTLE_BUSY); EXPECT(!MultiplayerBattle_IsActive());
    command.commandSeq++; command.opcode = MP_SAVE_CANCEL;
    gMultiplayerSaveMailbox.command = command; gMultiplayerSaveMailbox.commandGeneration = 6;
    MultiplayerSave_Tick(); EXPECT_EQ(gMultiplayerSaveMailbox.response.status, MP_SAVE_CANCELED);
    EXPECT(!MultiplayerSave_IsHeld()); Bind(&f);
    TearDown(&f);
}
