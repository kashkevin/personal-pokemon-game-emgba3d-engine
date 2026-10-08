#include "global.h"
#include "multiplayer_battle.h"
#include "multiplayer_battle_schema.h"
#include "multiplayer_save.h"
#include "multiplayer_ownership.h"
#include "battle.h"
#include "battle_bg.h"
#include "battle_gfx_sfx_util.h"
#include "battle_controllers.h"
#include "battle_util.h"
#include "battle_util2.h"
#include "battle_script_commands.h"
#include "battle_message.h"
#include "battle_scripts.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "main.h"
#include "malloc.h"
#include "overworld.h"
#include "load_save.h"
#include "save.h"
#include "script.h"
#include "palette.h"
#include "sprite.h"
#include "task.h"
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
#include "constants/characters.h"
#include "constants/items.h"
#include "constants/game_stat.h"
#include "test/test.h"

struct ChannelFixture {
    struct MultiplayerBattleControl command;
    struct MultiplayerBattleBinding binding;
    MainCallback cb1, cb2;
    MainCallback savedCallback;
    rng_value_t rng, rng2;
    u32 battleFlags;
    u32 controllerExecFlags;
    u16 linkType;
    struct LinkPlayer players[4];
    struct MapHeader header;
    struct PlayerAvatar avatar;
    struct ObjectEvent object;
    struct RfuLinkStatus *rfu;
    u32 generation;
    u32 lastSuccessfulSequence;
    u8 wireless, received;
    u8 attacker, target, effect, absent, outcome;
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
    if (gMultiplayerBattleMailbox.response.status == MP_BATTLE_OK && f->command.opcode != MP_BATTLE_QUERY)
        f->lastSuccessfulSequence = f->command.commandSeq;
    return gMultiplayerBattleMailbox.response.status;
}

static void Next(struct ChannelFixture *f, u8 opcode)
{
    if (f->command.phase != MP_BATTLE_PHASE_DIAGNOSTIC)
        f->command.commandSeq = f->lastSuccessfulSequence + 1;
    else
        f->command.commandSeq++;
    f->command.opcode = opcode;
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
    f->savedCallback = gMain.savedCallback; f->rng = gRngValue; f->rng2 = gRng2Value;
    f->battleFlags = gBattleTypeFlags; f->linkType = gLinkType;
    f->controllerExecFlags = gBattleControllerExecFlags;
    f->attacker = gBattlerAttacker; f->target = gBattlerTarget; f->effect = gEffectBattler;
    f->absent = gAbsentBattlerFlags; f->outcome = gBattleOutcome;
    memcpy(f->players, gLinkPlayers, sizeof(f->players));
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
    gMain.savedCallback = f->savedCallback; gRngValue = f->rng; gRng2Value = f->rng2;
    gBattleTypeFlags = f->battleFlags; gLinkType = f->linkType; gMain.inBattle = FALSE;
    gBattleControllerExecFlags = f->controllerExecFlags;
    gBattlerAttacker = f->attacker; gBattlerTarget = f->target; gEffectBattler = f->effect;
    gAbsentBattlerFlags = f->absent; gBattleOutcome = f->outcome;
    memcpy(gLinkPlayers, f->players, sizeof(f->players));
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

static void NativeBinding(struct ChannelFixture *f)
{
    SetUp(f, 2, 0);
    CalculateMonStats(&gParties[B_TRAINER_PLAYER][0]);
    gSaveBlock2Ptr->playerName[0] = EOS;
    f->binding.mode = MP_BATTLE_PHASE_SINGLES; f->command.phase = MP_BATTLE_PHASE_SINGLES;
    memcpy(f->command.data, &f->binding, sizeof(f->binding));
    Bind(f);
}

static void Preparation(struct ChannelFixture *f)
{
    struct MultiplayerBattlePreparation p = {0};
    p.checkpointRevision = 1; p.nativeCounter = gSaveCounter;
    p.trainerId = READ_OTID_FROM_SAVE; p.partyCrc32 = Crc((const u8 *)gParties[B_TRAINER_PLAYER], 600);
    p.rng[0] = 1; p.rng[4] = 2; p.rng[8] = 3; p.rng[12] = 4;
    Next(f, MP_BATTLE_PREPARE_SINGLES); f->command.length = sizeof(p);
    memcpy(f->command.data, &p, sizeof(p));
}

static void TrainerAndAck(struct ChannelFixture *f)
{
    struct LinkPlayer peer = {0};
    u32 crc = Crc((const u8 *)gBlockSendBuffer, 28);
    Next(f, MP_BATTLE_READ_OUT); f->command.blockSeq = 1; f->command.totalSize = 28;
    f->command.kind = MP_BATTLE_KIND_TRAINER;
    EXPECT_EQ(Submit(f), MP_BATTLE_OK);
    Next(f, MP_BATTLE_ACK_OUT); f->command.blockSeq = 1; f->command.totalSize = 28;
    f->command.offset = 28; f->command.kind = MP_BATTLE_KIND_TRAINER; f->command.length = 8;
    PutWord(f->command.data, crc); PutWord(f->command.data + 4, 2);
    EXPECT_EQ(Submit(f), MP_BATTLE_OK);
    peer.version = VERSION_EMERALD + 0x4000; peer.id = 1; peer.linkType = LINKTYPE_BATTLE;
    peer.language = gGameLanguage; peer.name[0] = EOS;
    Next(f, MP_BATTLE_RECEIVE); f->command.sourceSlot = 1; f->command.blockSeq = 1;
    f->command.kind = MP_BATTLE_KIND_TRAINER; f->command.totalSize = sizeof(peer);
    f->command.length = sizeof(peer); memcpy(f->command.data, &peer, sizeof(peer));
    EXPECT_EQ(Submit(f), MP_BATTLE_OK);
}

static void AckNativeOutput(struct ChannelFixture *f, bool32 clearLocal)
{
    u32 sequence, size, crc, kind, offset;
    EXPECT_EQ(Send(f, MP_BATTLE_QUERY), MP_BATTLE_OK);
    sequence = Word(gMultiplayerBattleMailbox.response.data + 28);
    kind = Word(gMultiplayerBattleMailbox.response.data + 32);
    size = Word(gMultiplayerBattleMailbox.response.data + 36);
    crc = Word(gMultiplayerBattleMailbox.response.data + 40);
    for (offset = 0; offset < size; offset += 128)
    {
        Next(f, MP_BATTLE_READ_OUT); f->command.blockSeq = sequence;
        f->command.kind = kind; f->command.totalSize = size; f->command.offset = offset;
        EXPECT_EQ(Submit(f), MP_BATTLE_OK);
    }
    Next(f, MP_BATTLE_ACK_OUT); f->command.blockSeq = sequence;
    f->command.kind = kind; f->command.totalSize = size; f->command.offset = size;
    f->command.length = 8; PutWord(f->command.data, crc);
    PutWord(f->command.data + 4, ((1 << f->binding.playerCount) - 1) ^ (1 << f->binding.localSlot));
    EXPECT_EQ(Submit(f), MP_BATTLE_OK);
    if (clearLocal) ResetBlockReceivedFlag(f->binding.localSlot);
}

static void AcknowledgeNativeOutput(struct ChannelFixture *f) { AckNativeOutput(f, TRUE); }

static void FullMultiBinding(struct ChannelFixture *f, u8 local)
{
    SetUp(f, 4, local);
    for (u32 slot = 0; slot < PARTY_SIZE; slot++)
    {
        u32 serial = slot + 1;
        if (slot) CreateMon(&gParties[B_TRAINER_PLAYER][slot], SPECIES_WURMPLE, 3 + slot, 0, OTID_STRUCT_PLAYER_ID);
        SetMonData(&gParties[B_TRAINER_PLAYER][slot], MON_DATA_MULTIPLAYER_ID, &serial);
        CalculateMonStats(&gParties[B_TRAINER_PLAYER][slot]);
    }
    CalculatePlayerPartyCount();
    gSaveBlock2Ptr->playerName[0] = EOS;
    f->binding.mode = f->command.phase = MP_BATTLE_PHASE_FULL_MULTI;
    memcpy(f->command.data, &f->binding, sizeof(f->binding));
    Bind(f);
}

static void ReceiveFullBlock(struct ChannelFixture *f, u8 source, u8 kind, u32 sequence, const void *data, u16 size)
{
    for (u32 offset = 0; offset < size; offset += 128)
    {
        Next(f, MP_BATTLE_RECEIVE); f->command.sourceSlot = source;
        f->command.blockSeq = sequence; f->command.kind = kind;
        f->command.totalSize = size; f->command.offset = offset;
        f->command.length = min(128, size - offset);
        memcpy(f->command.data, (const u8 *)data + offset, f->command.length);
        EXPECT_EQ(Submit(f), MP_BATTLE_OK);
    }
}

static void StartFullMulti(struct ChannelFixture *f)
{
    struct LinkPlayer peer = {0};
    Preparation(f); EXPECT_EQ(Submit(f), MP_BATTLE_OK);
    EXPECT_EQ(gLinkPlayers[f->binding.localSlot].linkType, LINKTYPE_MULTI_BATTLE);
    AckNativeOutput(f, FALSE);
    EXPECT_EQ(Send(f, MP_BATTLE_START_SINGLES), MP_BATTLE_BUSY);
    for (u32 source = 0; source < 4; source++)
    {
        if (source == f->binding.localSlot) continue;
        peer.version = VERSION_EMERALD + 0x4000; peer.id = source;
        peer.linkType = LINKTYPE_MULTI_BATTLE; peer.language = gGameLanguage; peer.name[0] = EOS;
        ReceiveFullBlock(f, source, MP_BATTLE_KIND_TRAINER, 1, &peer, sizeof(peer));
    }
    EXPECT_EQ(GetBlockReceivedStatus(), 15);
    EXPECT_EQ(Send(f, MP_BATTLE_START_SINGLES), MP_BATTLE_OK);
    EXPECT_EQ(gLinkType, LINKTYPE_MULTI_BATTLE);
    EXPECT_EQ(gBattleTypeFlags & (BATTLE_TYPE_MULTI | BATTLE_TYPE_DOUBLE), BATTLE_TYPE_MULTI | BATTLE_TYPE_DOUBLE);
}

TEST("Multiplayer full Multi: VS preview retains six independent health icons from every owner's startup header on all four devices")
{
    static const u16 flags[4] = {0xE49, 0xB6C, 0xD92, 0x6E5};
    static const u8 status[4][PARTY_SIZE] = {
        {1, 2, 0, 1, 2, 3}, {0, 3, 2, 1, 3, 2},
        {2, 0, 1, 2, 1, 3}, {1, 1, 2, 3, 2, 1},
    };
    struct ChannelFixture f;
    struct LinkBattlerHeader header = {0};
    u16 tiles[PARTY_SIZE + 1];
    for (u32 local = 0; local < 4; local++)
    {
        FullMultiBinding(&f, local);
        StartFullMulti(&f);
        header.versionSignatureHi = 3; header.battleEnigmaBerry.name[0] = EOS;
        header.vsScreenHealthFlagsLo = flags[local];
        header.vsScreenHealthFlagsHi = flags[local] >> 8;
        EXPECT(MultiplayerBattle_SendBlock(&header, sizeof(header)));
        AckNativeOutput(&f, FALSE);
        for (u32 source = 0; source < 4; source++)
        {
            if (source == local) continue;
            header.vsScreenHealthFlagsLo = flags[source];
            header.vsScreenHealthFlagsHi = flags[source] >> 8;
            ReceiveFullBlock(&f, source, MP_BATTLE_KIND_START_HEADER, 2, &header, sizeof(header));
        }
        EXPECT_EQ(GetBlockReceivedStatus(), 15);
        u8 task = CreateTask(TaskDummy, 0);
        EXPECT(task < NUM_TASKS);
        gTasks[task].data[1] = 0x10E; gTasks[task].data[2] = 0x5A;
        // Poison the old half-party summaries: none may supply a native icon.
        gTasks[task].data[3] = gTasks[task].data[4] = 0xFFF;
        BufferNativeMultiBattleVsScreenHealth(task);
        ResetBlockReceivedFlags();
        // Later party blocks replace the receive buffers during this animation.
        memset(gBlockRecvBuffer, 0, sizeof(gBlockRecvBuffer));
        for (u32 source = 0; source < 4; source++)
        {
            for (u32 slot = 0; slot < ARRAY_COUNT(tiles); slot++) tiles[slot] = 0xAAAA;
            EXPECT_EQ(BattleBg_TestParticipantPokeballTiles(task, source, tiles), PARTY_SIZE);
            for (u32 slot = 0; slot < PARTY_SIZE; slot++)
                EXPECT_EQ(tiles[slot], 0x6001 + status[source][slot]);
            EXPECT_EQ(tiles[PARTY_SIZE], 0xAAAA);
        }
        EXPECT_EQ(BattleBg_TestParticipantPokeballTiles(task, 4, tiles), 0);
        EXPECT_EQ(gTasks[task].data[1], 0x10E); EXPECT_EQ(gTasks[task].data[2], 0x5A);
        EXPECT_EQ(gTasks[task].data[3], 0xFFF); EXPECT_EQ(gTasks[task].data[4], 0xFFF);
        DestroyTask(task);
        TearDown(&f);
    }
}

TEST("Multiplayer VS preview: legacy Multi still shows three icons per trainer with its existing end-screen opponent order")
{
    static const u8 opening[4][3] = {{1, 2, 0}, {0, 3, 2}, {1, 2, 3}, {1, 3, 2}};
    static const u8 ending[4][3] = {{1, 2, 0}, {1, 3, 2}, {1, 2, 3}, {0, 3, 2}};
    struct ChannelFixture f; SetUp(&f, 4, 0);
    u8 task = CreateTask(TaskDummy, 0);
    u16 tiles[PARTY_SIZE];
    gBattleTypeFlags = BATTLE_TYPE_LINK | BATTLE_TYPE_MULTI;
    gTasks[task].data[3] = 0xE49; gTasks[task].data[4] = 0xB6C;
    for (u32 slot = 6; slot < 10; slot++) gTasks[task].data[slot] = 0xFFF;
    BufferNativeMultiBattleVsScreenHealth(task);
    for (u32 slot = 6; slot < 10; slot++) EXPECT_EQ(gTasks[task].data[slot], 0xFFF);
    for (u32 end = 0; end < 2; end++)
    {
        gTasks[task].data[5] = end;
        for (u32 source = 0; source < 4; source++)
        {
            for (u32 slot = 0; slot < PARTY_SIZE; slot++) tiles[slot] = 0xAAAA;
            EXPECT_EQ(BattleBg_TestParticipantPokeballTiles(task, source, tiles), 3);
            for (u32 slot = 0; slot < 3; slot++)
                EXPECT_EQ(tiles[slot], 0x6001 + (end ? ending[source][slot] : opening[source][slot]));
            for (u32 slot = 3; slot < PARTY_SIZE; slot++) EXPECT_EQ(tiles[slot], 0xAAAA);
        }
    }
    DestroyTask(task); TearDown(&f);
}

TEST("Multiplayer VS preview: native singles retain six self and opponent icons without using full Multi task data")
{
    static const u8 status[2][PARTY_SIZE] = {{1, 2, 0, 1, 2, 3}, {0, 3, 2, 1, 3, 2}};
    struct ChannelFixture f; NativeBinding(&f);
    u8 task = CreateTask(TaskDummy, 0), previousId = gBattleScripting.multiplayerId;
    u16 tiles[PARTY_SIZE];
    gBattleTypeFlags = BATTLE_TYPE_LINK;
    gTasks[task].data[3] = 0xE49; gTasks[task].data[4] = 0xB6C;
    for (u32 slot = 6; slot < 10; slot++) gTasks[task].data[slot] = 0xFFF;
    BufferNativeMultiBattleVsScreenHealth(task);
    for (u32 slot = 6; slot < 10; slot++) EXPECT_EQ(gTasks[task].data[slot], 0xFFF);
    for (u32 local = 0; local < 2; local++)
    {
        gBattleScripting.multiplayerId = local;
        for (u32 source = 0; source < 2; source++)
        {
            EXPECT_EQ(BattleBg_TestParticipantPokeballTiles(task, source, tiles), PARTY_SIZE);
            for (u32 slot = 0; slot < PARTY_SIZE; slot++)
                EXPECT_EQ(tiles[slot], 0x6001 + status[source != local][slot]);
        }
    }
    gBattleScripting.multiplayerId = previousId;
    DestroyTask(task); TearDown(&f);
}

TEST("Multiplayer full Multi: four fixed owners retain six separate party slots through startup")
{
    struct ChannelFixture f;
    struct LinkBattlerHeader header = {0};
    // Keep the 3 KB comparison roster off the limited GBA system stack.
    struct Pokemon *original = Alloc(sizeof(struct Pokemon) * PARTY_SIZE);
    struct Pokemon (*peers)[PARTY_SIZE] = Alloc(sizeof(struct Pokemon) * PARTY_SIZE * 4);
    for (u32 local = 0; local < 4; local++)
    {
        FullMultiBinding(&f, local);
        EXPECT(MultiplayerBattle_IsFullMulti()); EXPECT_EQ(MultiplayerBattle_GetPlayerCount(), 4);
        memcpy(original, gParties[B_TRAINER_PLAYER], sizeof(struct Pokemon) * PARTY_SIZE);
        for (u32 source = 0; source < 4; source++)
            for (u32 slot = 0; slot < PARTY_SIZE; slot++)
            {
                u32 serial = 33 + source * PARTY_SIZE + slot;
                peers[source][slot] = original[slot];
                SetMonData(&peers[source][slot], MON_DATA_MULTIPLAYER_ID, &serial);
            }
        StartFullMulti(&f);
        EXPECT(AreMultiPartiesFullTeams());
        header.versionSignatureHi = 3; header.battleEnigmaBerry.name[0] = EOS;
        EXPECT(MultiplayerBattle_SendBlock(&header, sizeof(header))); AckNativeOutput(&f, FALSE);
        for (u32 source = 0; source < 4; source++)
            if (source != local) ReceiveFullBlock(&f, source, MP_BATTLE_KIND_START_HEADER, 2, &header, sizeof(header));
        ResetBlockReceivedFlags();
        for (u32 pair = 0; pair < 3; pair++)
        {
            EXPECT(MultiplayerBattle_SendPartyPair(pair * 2)); AckNativeOutput(&f, FALSE);
            EXPECT(!MultiplayerBattle_CopyFullMultiPartyPair(pair * 2));
            for (u32 source = 0; source < 4; source++)
                if (source != local) ReceiveFullBlock(&f, source, MP_BATTLE_KIND_PARTY_0 + pair, 3 + pair,
                                                     peers[source] + pair * 2, sizeof(struct Pokemon) * 2);
            EXPECT_EQ(GetBlockReceivedStatus(), 15);
            EXPECT(MultiplayerBattle_CopyFullMultiPartyPair(pair * 2));
            ResetBlockReceivedFlags();
        }
        EXPECT(!memcmp(original, gParties[B_TRAINER_PLAYER], sizeof(struct Pokemon) * PARTY_SIZE));
        for (u32 source = 0; source < 4; source++)
        {
            enum BattleTrainer trainer;
            if (source == local) continue;
            trainer = (source & 1) == (local & 1) ? B_TRAINER_PARTNER
                : source < 2 ? B_TRAINER_OPPONENT_A : B_TRAINER_OPPONENT_B;
            EXPECT_EQ(gPartiesCount[trainer], PARTY_SIZE);
            EXPECT(!memcmp(peers[source], gParties[trainer], sizeof(struct Pokemon) * PARTY_SIZE));
        }
        TearDown(&f);
    }
    Free(peers); Free(original);
}

TEST("Multiplayer full Multi: every device owns its controller, sixth lead and independent switch order")
{
    struct ChannelFixture f;
    u8 oldOrders[4][3];
    const u8 switched[3] = {0x51, 0x23, 0x40};
    for (u32 local = 0; local < 4; local++)
    {
        FullMultiBinding(&f, local);
        StartFullMulti(&f);
        if (local == 0) gBattleTypeFlags |= BATTLE_TYPE_IS_MASTER;
        gMain.inBattle = TRUE; // The native callback sets this before controller initialization.
        AllocateBattleResources();
        for (enum BattleTrainer trainer = 0; trainer < MAX_BATTLE_TRAINERS; trainer++)
        {
            if (trainer != B_TRAINER_PLAYER)
                memcpy(gParties[trainer], gParties[B_TRAINER_PLAYER], sizeof(gParties[trainer]));
            gPartiesCount[trainer] = PARTY_SIZE;
            for (u32 slot = 0; slot < PARTY_SIZE - 1; slot++)
            {
                u32 value = 0;
                SetMonData(&gParties[trainer][slot], MON_DATA_HP, &value);
            }
            // A healthy Egg in the first slot is not a legal lead either.
            u32 value = 1;
            SetMonData(&gParties[trainer][0], MON_DATA_IS_EGG, &value);
            SetMonData(&gParties[trainer][0], MON_DATA_HP, &value);
        }
        InitBattleControllers();
        EXPECT_EQ(gBattlersCount, 4);
        for (u32 battler = 0; battler < 4; battler++)
        {
            enum BattleTrainer expected = battler == local ? B_TRAINER_PLAYER
                : (battler & 1) == (local & 1) ? B_TRAINER_PARTNER
                : battler < 2 ? B_TRAINER_OPPONENT_A : B_TRAINER_OPPONENT_B;
            EXPECT_EQ(GetBattlerTrainer(battler), expected);
            EXPECT_EQ(BattlerIsPlayer(battler), battler == local);
            EXPECT_EQ(BattlerIsPartner(battler), expected == B_TRAINER_PARTNER);
            EXPECT_EQ(gBattlerPartyIndexes[battler], 5);
            EXPECT(!BattlersShareParty(battler, battler ^ 2));
            memcpy(oldOrders, gBattleStruct->battlerPartyOrders, sizeof(oldOrders));
            memcpy(&gBattleResources->bufferB[battler][2], switched, sizeof(switched));
            UpdateLinkBattlePartyOrder(battler);
            EXPECT(!memcmp(gBattleStruct->battlerPartyOrders[battler], switched, sizeof(switched)));
            for (u32 other = 0; other < 4; other++)
                if (other != battler)
                    EXPECT(!memcmp(gBattleStruct->battlerPartyOrders[other], oldOrders[other], sizeof(oldOrders[other])));
        }
        FreeBattleResources();
        TearDown(&f);
    }
}

TEST("Multiplayer native battles: temporary play cannot change recording flags or personal activity statistics")
{
    struct ChannelFixture f;
    u8 script[2] = {B_SCR_OP_INCREMENTGAMESTAT, GAME_STAT_USED_SPLASH};
    const u8 *savedScript = gBattlescriptCurrInstr;
    for (u32 mode = 2; mode <= 3; mode++)
    {
        if (mode == 2) NativeBinding(&f); else FullMultiBinding(&f, 0);
        gBattleTypeFlags = BATTLE_TYPE_LINK | BATTLE_TYPE_TRAINER;
        if (mode == 3) gBattleTypeFlags |= BATTLE_TYPE_MULTI | BATTLE_TYPE_DOUBLE;
        gBattlersCount = mode == 3 ? 4 : 2;
        for (u32 battler = 0; battler < gBattlersCount; battler++)
        {
            gBattlerPositions[battler] = battler;
            gChosenActionByBattler[battler] = B_ACTION_USE_MOVE;
        }
        for (u32 stat = GAME_STAT_USED_SPLASH; stat <= GAME_STAT_USED_STRUGGLE; stat++)
        {
            script[1] = stat;
            SetGameStat(stat, 7);
            for (u32 attacker = 0; attacker < gBattlersCount; attacker += 2)
            {
                gBattlerAttacker = attacker;
                gBattlescriptCurrInstr = script;
                gBattleScriptingCommandsTable[B_SCR_OP_INCREMENTGAMESTAT]();
                EXPECT_EQ(GetGameStat(stat), 7);
                EXPECT(gBattlescriptCurrInstr == script + 2);
            }
        }
        gSaveBlock2Ptr->frontier.disableRecordBattle = FALSE;
        gCurrentTurnActionNumber = 0;
        gBattlerByTurnOrder[0] = 0;
        gChosenActionByBattler[0] = B_ACTION_RUN;
        gBattleOutcome = 0;
        HandleAction_Run();
        EXPECT_EQ(gBattleOutcome, B_OUTCOME_LOST | B_OUTCOME_LINK_BATTLE_RAN);
        EXPECT(!gSaveBlock2Ptr->frontier.disableRecordBattle);
        TearDown(&f);
        // The established ordinary path continues to record activity.
        gBattlerPositions[0] = B_POSITION_PLAYER_LEFT;
        gBattlerAttacker = 0;
        gBattlescriptCurrInstr = script;
        gBattleScriptingCommandsTable[B_SCR_OP_INCREMENTGAMESTAT]();
        EXPECT_EQ(GetGameStat(GAME_STAT_USED_STRUGGLE), 8);
    }
    gBattlescriptCurrInstr = savedScript;
}

TEST("Multiplayer full Multi: forced switches can use sixth slot occupied only in a teammate's separate party")
{
    struct ChannelFixture f;
    const u8 script[5] = {B_SCR_OP_FORCERANDOMSWITCH, 0, 0, 0, 0};
    const u8 identity[3] = {0x01, 0x23, 0x45};
    const u8 switched[3] = {0x51, 0x23, 0x40};
    const u8 *savedScript = gBattlescriptCurrInstr;
    FullMultiBinding(&f, 0);
    StartFullMulti(&f);
    gBattleTypeFlags |= BATTLE_TYPE_IS_MASTER;
    gMain.inBattle = TRUE; // Exercise the actual battle path, not field party ordering.
    AllocateBattleResources();
    for (enum BattleTrainer trainer = 0; trainer < MAX_BATTLE_TRAINERS; trainer++)
    {
        if (trainer != B_TRAINER_PLAYER)
            memcpy(gParties[trainer], gParties[B_TRAINER_PLAYER], sizeof(gParties[trainer]));
        gPartiesCount[trainer] = PARTY_SIZE;
        for (u32 slot = 1; slot < PARTY_SIZE - 1; slot++)
        {
            u32 hp = 0;
            SetMonData(&gParties[trainer][slot], MON_DATA_HP, &hp);
        }
    }
    InitBattleControllers();
    for (u32 target = 0; target < 4; target++)
    {
        for (u32 battler = 0; battler < 4; battler++)
            memcpy(gBattleStruct->battlerPartyOrders[battler], identity, sizeof(identity));
        gBattlerTarget = target;
        gBattlerAttacker = target ^ 1;
        gBattlerPartyIndexes[target] = 0;
        gBattlerPartyIndexes[target ^ 2] = 5;
        gBattleScripting.switchCase = 0;
        gBattlescriptCurrInstr = script;
        gBattleScriptingCommandsTable[B_SCR_OP_FORCERANDOMSWITCH]();
        EXPECT(gBattlescriptCurrInstr == BattleScript_RoarSuccessSwitch);
        EXPECT_EQ(gBattleStruct->monToSwitchIntoId[target], 5);
        EXPECT(!memcmp(gBattleStruct->battlerPartyOrders[target], switched, sizeof(switched)));
        for (u32 other = 0; other < 4; other++)
            if (other != target)
                EXPECT(!memcmp(gBattleStruct->battlerPartyOrders[other], identity, sizeof(identity)));
    }
    FreeBattleResources();
    TearDown(&f);
    gBattlescriptCurrInstr = savedScript;
}

TEST("Multiplayer full Multi: owner choices and four renderer completions cannot cross battlers")
{
    struct ChannelFixture f;
    u8 request[16] = {B_COMM_TO_CONTROLLER, 0, 0, 3, 8, 0, 15, 3, CONTROLLER_CHOOSEACTION};
    u8 reply[16] = {B_COMM_TO_ENGINE, 0, 0, 0, 8, 0, 0, 0, CONTROLLER_TWORETURNVALUES};
    u8 done[16] = {B_COMM_CONTROLLER_IS_DONE, 0, 0, 0, 8};
    FullMultiBinding(&f, 0);
    for (u32 owner = 0; owner < 4; owner++)
    {
        request[1] = reply[1] = done[1] = owner;
        EXPECT(MultiplayerBattle_ValidateController(request, sizeof(request), 0));
        EXPECT(!MultiplayerBattle_ValidateController(request, sizeof(request), 1));
        MultiplayerBattle_NoteController(request, 0);
        done[8] = owner; EXPECT(!MultiplayerBattle_ValidateController(done, sizeof(done), owner));
        for (u32 source = 0; source < 4; source++)
            EXPECT_EQ(MultiplayerBattle_ValidateController(reply, sizeof(reply), source), source == owner);
        reply[9] = B_ACTION_CANCEL_PARTNER;
        EXPECT(!MultiplayerBattle_ValidateController(reply, sizeof(reply), owner));
        reply[9] = B_ACTION_USE_MOVE;
        MultiplayerBattle_NoteController(reply, owner);
        for (u32 source = 0; source < 4; source++)
        {
            EXPECT(!MultiplayerBattle_ValidateController(request, sizeof(request), 0));
            done[8] = source;
            EXPECT(MultiplayerBattle_ValidateController(done, sizeof(done), source));
            MultiplayerBattle_NoteController(done, source);
            EXPECT(!MultiplayerBattle_ValidateController(done, sizeof(done), source));
        }
        EXPECT(MultiplayerBattle_ValidateController(request, sizeof(request), 0));
    }
    TearDown(&f);
}

TEST("Multiplayer full Multi: move target three and owner party slots three through five are legal")
{
    struct ChannelFixture f;
    u8 move[56] = {B_COMM_TO_CONTROLLER, 3, 0, 3, 48, 0, 0, 0, CONTROLLER_CHOOSEMOVE, 1};
    u8 reply[16] = {B_COMM_TO_ENGINE, 3, 0, 0, 8, 0, 0, 0, CONTROLLER_TWORETURNVALUES, B_ACTION_EXEC_SCRIPT, 3, 3};
    u8 party[20] = {B_COMM_TO_CONTROLLER, 3, 0, 3, 12, 0, 0, 0, CONTROLLER_CHOOSEPOKEMON, 0, 6, 0, 0x10, 0x32, 0x54, 0, 3};
    u8 selected[16] = {B_COMM_TO_ENGINE, 3, 0, 0, 8, 0, 0, 0, CONTROLLER_CHOSENMONRETURNVALUE, 3, 0x10, 0x32, 0x54};
    FullMultiBinding(&f, 3);
    MultiplayerBattle_ResetControllerValidation();
    EXPECT(MultiplayerBattle_ValidateController(move, sizeof(move), 0));
    move[9] = 0; EXPECT(!MultiplayerBattle_ValidateController(move, sizeof(move), 0)); move[9] = 1;
    MultiplayerBattle_NoteController(move, 0);
    EXPECT(MultiplayerBattle_ValidateController(reply, sizeof(reply), 3));
    EXPECT(!MultiplayerBattle_ValidateController(reply, sizeof(reply), 2));
    reply[11] = 4; EXPECT(!MultiplayerBattle_ValidateController(reply, sizeof(reply), 3));
    MultiplayerBattle_ResetControllerValidation();
    EXPECT(MultiplayerBattle_ValidateController(party, sizeof(party), 0));
    MultiplayerBattle_NoteController(party, 0);
    for (u32 slot = 3; slot < 6; slot++)
    {
        selected[9] = slot;
        EXPECT(MultiplayerBattle_ValidateController(selected, sizeof(selected), 3));
    }
    selected[9] = 7; EXPECT(!MultiplayerBattle_ValidateController(selected, sizeof(selected), 3));
    TearDown(&f);
}

TEST("Multiplayer full Multi: exhausted local party waits for living partner")
{
    struct ChannelFixture f;
    u32 zero = 0;
    FullMultiBinding(&f, 0); StartFullMulti(&f);
    memcpy(gParties[B_TRAINER_PARTNER], gParties[B_TRAINER_PLAYER], sizeof(gParties[B_TRAINER_PLAYER]));
    gPartiesCount[B_TRAINER_PARTNER] = PARTY_SIZE;
    for (u32 slot = 0; slot < PARTY_SIZE; slot++)
        SetMonData(&gParties[B_TRAINER_PLAYER][slot], MON_DATA_HP, &zero);
    EXPECT(!NoAliveMonsForPlayer());
    for (u32 slot = 0; slot < PARTY_SIZE; slot++)
        SetMonData(&gParties[B_TRAINER_PARTNER][slot], MON_DATA_HP, &zero);
    EXPECT(NoAliveMonsForPlayer());
    TearDown(&f);
}

TEST("Multiplayer full Multi: exact personal return clears temporary partner without saving it")
{
    struct ChannelFixture f;
    struct LinkBattlerHeader header = {0};
    struct Pokemon original[6], empty[6] = {0};
    rng_value_t personal, issued;
    u8 end[16] = {B_COMM_TO_CONTROLLER, 0, 0, 0, 8, 0, 0, 0, CONTROLLER_ENDLINKBATTLE, B_OUTCOME_WON};
    FullMultiBinding(&f, 0); memcpy(original, gParties[B_TRAINER_PLAYER], sizeof(struct Pokemon) * PARTY_SIZE); personal = gRngValue;
    StartFullMulti(&f);
    MultiplayerBattle_InstallNativeRng(); issued = gRngValue;
    header.versionSignatureHi = 3; header.battleEnigmaBerry.name[0] = EOS;
    EXPECT(MultiplayerBattle_SendBlock(&header, sizeof(header))); AcknowledgeNativeOutput(&f);
    for (u32 pair = 0; pair < 3; pair++)
    {
        EXPECT(MultiplayerBattle_SendPartyPair(pair * 2)); AcknowledgeNativeOutput(&f);
    }
    EXPECT(MultiplayerBattle_SendBlock(&issued, sizeof(issued))); AcknowledgeNativeOutput(&f);
    EXPECT(MultiplayerBattle_SendBlock(end, sizeof(end))); AcknowledgeNativeOutput(&f);
    memcpy(gParties[B_TRAINER_PARTNER], original, sizeof(original)); gPartiesCount[B_TRAINER_PARTNER] = 6;
    gParties[B_TRAINER_PLAYER][5].hp = 1;
    gBattleOutcome = B_OUTCOME_WON; gBattleControllerExecFlags = 0;
    EXPECT(MultiplayerBattle_TryReturnNative());
    EXPECT_EQ(gPartiesCount[B_TRAINER_PARTNER], 0);
    EXPECT(!memcmp(gParties[B_TRAINER_PARTNER], empty, sizeof(empty)));
    EXPECT_EQ(gPartiesCount[B_TRAINER_PLAYER], 6);
    EXPECT(!memcmp(gParties[B_TRAINER_PLAYER], original, sizeof(original)));
    gMain.callback1 = CB1_Overworld; gMain.callback2 = CB2_Overworld; gMain.inBattle = FALSE;
    EXPECT_EQ(Send(&f, MP_BATTLE_QUERY), MP_BATTLE_OK);
    EXPECT_EQ(gMultiplayerBattleMailbox.state, MP_BATTLE_FIELD_READY_HELD);
    EXPECT(!memcmp(&gRngValue, &personal, sizeof(personal)));
    TearDown(&f);
}

TEST("Multiplayer native singles: canonical preparation mismatches refuse before party or RNG changes")
{
    struct ChannelFixture f;
    struct Pokemon original[6];
    rng_value_t rng, rng2;
    NativeBinding(&f);
    memcpy(original, gParties[B_TRAINER_PLAYER], sizeof(struct Pokemon) * PARTY_SIZE); rng = gRngValue; rng2 = gRng2Value;
    Preparation(&f); f.command.data[4] ^= 1;
    EXPECT_EQ(Submit(&f), MP_BATTLE_ORDER);
    Preparation(&f); f.command.data[8] ^= 1;
    EXPECT_EQ(Submit(&f), MP_BATTLE_ORDER);
    Preparation(&f); f.command.data[12] ^= 1;
    EXPECT_EQ(Submit(&f), MP_BATTLE_ORDER);
    EXPECT_EQ(gMultiplayerBattleMailbox.state, MP_BATTLE_CHANNEL);
    EXPECT(!memcmp(original, gParties[B_TRAINER_PLAYER], sizeof(struct Pokemon) * PARTY_SIZE));
    EXPECT(!memcmp(&rng, &gRngValue, sizeof(rng))); EXPECT(!memcmp(&rng2, &gRng2Value, sizeof(rng2)));
    EXPECT_EQ(Send(&f, MP_BATTLE_CLOSE), MP_BATTLE_UNSAFE);
    EXPECT_EQ(Send(&f, MP_BATTLE_ABORT_PRE_START), MP_BATTLE_OK);
    EXPECT(!MultiplayerBattle_IsActive());
    TearDown(&f);
}

TEST("Multiplayer native singles: prepare replay freezes originals and explicit abort restores exact empty tails")
{
    struct ChannelFixture f;
    struct Pokemon original[6];
    struct MultiplayerBattleControl prepared;
    rng_value_t rng, rng2;
    NativeBinding(&f);
    gParties[B_TRAINER_PLAYER][5].box.hpLost = 17;
    memcpy(original, gParties[B_TRAINER_PLAYER], sizeof(struct Pokemon) * PARTY_SIZE); rng = gRngValue; rng2 = gRng2Value;
    Preparation(&f); EXPECT_EQ(Submit(&f), MP_BATTLE_OK); prepared = f.command;
    EXPECT_EQ(gMultiplayerBattleMailbox.state, MP_BATTLE_PREPARED);
    EXPECT_EQ(gMultiplayerBattleMailbox.response.data[32], MP_BATTLE_KIND_TRAINER);
    EXPECT_EQ(Submit(&f), MP_BATTLE_OK); EXPECT(!memcmp(&prepared, &f.command, sizeof(prepared)));
    EXPECT(MultiplayerBattle_SkipCallbacks()); EXPECT(MultiplayerBattle_PauseClocks());
    EXPECT(!MultiplayerOwnership_CanSaveParty());
    EXPECT_EQ(Send(&f, MP_BATTLE_START_SINGLES), MP_BATTLE_BUSY);
    EXPECT_EQ(Send(&f, MP_BATTLE_CLEAR_RX), MP_BATTLE_KIND);
    gParties[B_TRAINER_PLAYER][0].hp = 1; gParties[B_TRAINER_PLAYER][5].box.hpLost = 0;
    Random(); Random2();
    EXPECT_EQ(Send(&f, MP_BATTLE_ABORT_PRE_START), MP_BATTLE_OK);
    EXPECT(!memcmp(original, gParties[B_TRAINER_PLAYER], sizeof(struct Pokemon) * PARTY_SIZE));
    EXPECT(!memcmp(&rng, &gRngValue, sizeof(rng))); EXPECT(!memcmp(&rng2, &gRng2Value, sizeof(rng2)));
    EXPECT_EQ(gPartiesCount[B_TRAINER_PLAYER], 1);
    TearDown(&f);
}

TEST("Multiplayer native singles: rejected next sequence preserves prior ACK and can be reused successfully")
{
    struct ChannelFixture f;
    struct MultiplayerBattleControl prepared, response;
    u32 nextSequence;
    NativeBinding(&f); Preparation(&f); EXPECT_EQ(Submit(&f), MP_BATTLE_OK);
    prepared = f.command; response = gMultiplayerBattleMailbox.response;
    nextSequence = prepared.commandSeq + 1;
    EXPECT_EQ(Send(&f, MP_BATTLE_START_SINGLES), MP_BATTLE_BUSY);
    EXPECT_EQ(f.command.commandSeq, nextSequence);
    f.command = prepared;
    EXPECT_EQ(Submit(&f), MP_BATTLE_OK);
    EXPECT(!memcmp((const void *)&gMultiplayerBattleMailbox.response, &response, sizeof(response)));
    EXPECT_EQ(Send(&f, MP_BATTLE_KEEP), MP_BATTLE_OK);
    EXPECT_EQ(f.command.commandSeq, nextSequence);
    EXPECT_EQ(Word(gMultiplayerBattleMailbox.response.data + 24), nextSequence);
    EXPECT_EQ(Send(&f, MP_BATTLE_ABORT_PRE_START), MP_BATTLE_OK);
    TearDown(&f);
}

TEST("Multiplayer native singles: startup requires descriptors then installs issued RNG exactly once")
{
    struct ChannelFixture f;
    rng_value_t issued, first, personal;
    NativeBinding(&f); Preparation(&f); memcpy(&issued, f.command.data + 16, 16); personal = gRngValue;
    EXPECT_EQ(Submit(&f), MP_BATTLE_OK);
    TrainerAndAck(&f); EXPECT_EQ(Send(&f, MP_BATTLE_START_SINGLES), MP_BATTLE_OK);
    EXPECT_EQ(gMultiplayerBattleMailbox.state, MP_BATTLE_RUNNING);
    EXPECT(!MultiplayerBattle_SkipCallbacks()); EXPECT(!MultiplayerBattle_PauseClocks());
    EXPECT(!memcmp(&personal, &gRngValue, 16));
    MultiplayerBattle_InstallNativeRng(); EXPECT(!memcmp(&issued, &gRngValue, 16));
    Random(); first = gRngValue; MultiplayerBattle_InstallNativeRng();
    EXPECT(!memcmp(&first, &gRngValue, 16));
    EXPECT_EQ(Send(&f, MP_BATTLE_ABORT_PRE_START), MP_BATTLE_UNSAFE);
    EXPECT_EQ(Send(&f, MP_BATTLE_RELEASE_SINGLES), MP_BATTLE_UNSAFE);
    TearDown(&f);
}

TEST("Multiplayer native singles: fresh action sentinel canonicalizes stale item without admitting invalid actions")
{
    struct ChannelFixture f;
    u8 payload[4] = {CONTROLLER_CHOOSEACTION, B_ACTION_NONE, CONTROLLER_DATATRANSFER, 140};
    u8 wire[256];
    u16 size;
    NativeBinding(&f); Preparation(&f); EXPECT_EQ(Submit(&f), MP_BATTLE_OK);
    TrainerAndAck(&f); EXPECT_EQ(Send(&f, MP_BATTLE_START_SINGLES), MP_BATTLE_OK);
    size = MultiplayerBattle_PackController(wire, 0, B_COMM_TO_CONTROLLER, sizeof(payload), payload);
    EXPECT_EQ(size, 16); EXPECT_EQ(wire[9], B_ACTION_NONE);
    EXPECT_EQ(wire[10], 0); EXPECT_EQ(wire[11], 0);
    // This boundary check uses canonical metadata independently of battle UI.
    wire[2] = wire[3] = wire[6] = wire[7] = 0;
    EXPECT(MultiplayerBattle_ValidateController(wire, size, 0));
    wire[9] = B_ACTION_RUN + 1;
    EXPECT(!MultiplayerBattle_ValidateController(wire, size, 0));
    wire[9] = B_ACTION_NONE; wire[10] = 1;
    EXPECT(!MultiplayerBattle_ValidateController(wire, size, 0));
    wire[9] = B_ACTION_USE_ITEM; wire[10] = ITEMS_COUNT & 0xFF; wire[11] = ITEMS_COUNT >> 8;
    EXPECT(!MultiplayerBattle_ValidateController(wire, size, 0));
    wire[10] = (ITEMS_COUNT - 1) & 0xFF; wire[11] = (ITEMS_COUNT - 1) >> 8;
    EXPECT(MultiplayerBattle_ValidateController(wire, size, 0));
    wire[9] = B_ACTION_NONE; wire[10] = wire[11] = 0;
    MultiplayerBattle_NoteController(wire, 0);
    wire[0] = B_COMM_TO_ENGINE; wire[8] = CONTROLLER_TWORETURNVALUES;
    EXPECT(!MultiplayerBattle_ValidateController(wire, size, 0)); // NONE is never a chosen action reply.
    TearDown(&f);
}

TEST("Multiplayer native singles: only unused effect metadata canonicalizes a native loop sentinel")
{
    struct ChannelFixture f;
    u8 payload[80] = {CONTROLLER_PRINTSTRING};
    u8 wire[256];
    u16 size;
    NativeBinding(&f); Preparation(&f); EXPECT_EQ(Submit(&f), MP_BATTLE_OK);
    TrainerAndAck(&f); EXPECT_EQ(Send(&f, MP_BATTLE_START_SINGLES), MP_BATTLE_OK);
    gBattlerAttacker = 1; gBattlerTarget = 0; gAbsentBattlerFlags = 0; gEffectBattler = 2;
    payload[2] = STRINGID_EMPTYSTRING3 & 0xFF; payload[3] = STRINGID_EMPTYSTRING3 >> 8;
    payload[4 + offsetof(struct BattleMsgData, textBuffs)] = EOS;
    payload[4 + offsetof(struct BattleMsgData, textBuffs) + 18] = EOS;
    payload[4 + offsetof(struct BattleMsgData, textBuffs) + 36] = EOS;
    size = MultiplayerBattle_PackController(wire, 1, B_COMM_TO_CONTROLLER, sizeof(payload), payload);
    EXPECT_EQ(wire[7], 0); EXPECT(MultiplayerBattle_ValidateController(wire, size, 0));
    payload[2] = STRINGID_PKMNFLINCHED & 0xFF; payload[3] = STRINGID_PKMNFLINCHED >> 8;
    size = MultiplayerBattle_PackController(wire, 1, B_COMM_TO_CONTROLLER, sizeof(payload), payload);
    EXPECT_EQ(wire[7], 2); EXPECT(!MultiplayerBattle_ValidateController(wire, size, 0));
    memset(payload, 0, sizeof(payload)); payload[0] = CONTROLLER_BATTLEANIMATION;
    size = MultiplayerBattle_PackController(wire, 1, B_COMM_TO_CONTROLLER, 12, payload);
    EXPECT_EQ(wire[7], 2); EXPECT(!MultiplayerBattle_ValidateController(wire, size, 0));
    memset(payload, 0, sizeof(payload)); payload[0] = CONTROLLER_CHOOSEACTION;
    size = MultiplayerBattle_PackController(wire, 0, B_COMM_TO_CONTROLLER, 4, payload);
    EXPECT_EQ(wire[7], 0); EXPECT(MultiplayerBattle_ValidateController(wire, size, 0));
    MultiplayerBattle_NoteController(wire, 0);
    payload[0] = CONTROLLER_TWORETURNVALUES;
    size = MultiplayerBattle_PackController(wire, 0, B_COMM_TO_ENGINE, 6, payload);
    EXPECT_EQ(wire[2], 0); EXPECT_EQ(wire[3], 0); EXPECT_EQ(wire[6], 0); EXPECT_EQ(wire[7], 0);
    EXPECT(MultiplayerBattle_ValidateController(wire, size, 0)); MultiplayerBattle_NoteController(wire, 0);
    payload[0] = 0;
    size = MultiplayerBattle_PackController(wire, 0, B_COMM_CONTROLLER_IS_DONE, 4, payload);
    EXPECT_EQ(wire[2], 0); EXPECT_EQ(wire[3], 0); EXPECT_EQ(wire[6], 0); EXPECT_EQ(wire[7], 0);
    EXPECT(MultiplayerBattle_ValidateController(wire, size, 0));
    TearDown(&f);
}

TEST("Multiplayer native singles: return publishes restoration only after field readiness and clears the block descriptor")
{
    struct ChannelFixture f;
    struct LinkBattlerHeader header = {0};
    struct Pokemon original[6];
    rng_value_t personal, issued;
    u8 end[16] = {B_COMM_TO_CONTROLLER, 0, 0, 0, 8, 0, 0, 0, CONTROLLER_ENDLINKBATTLE, B_OUTCOME_WON};
    u32 i;
    NativeBinding(&f); memcpy(original, gParties[B_TRAINER_PLAYER], sizeof(struct Pokemon) * PARTY_SIZE); personal = gRngValue;
    Preparation(&f); memcpy(&issued, f.command.data + 16, sizeof(issued)); EXPECT_EQ(Submit(&f), MP_BATTLE_OK);
    TrainerAndAck(&f); EXPECT_EQ(Send(&f, MP_BATTLE_START_SINGLES), MP_BATTLE_OK);
    MultiplayerBattle_InstallNativeRng();
    header.versionSignatureHi = 3; header.battleEnigmaBerry.name[0] = EOS;
    EXPECT(MultiplayerBattle_SendBlock(&header, sizeof(header))); AcknowledgeNativeOutput(&f);
    for (i = 0; i < 3; i++)
    {
        EXPECT(MultiplayerBattle_SendPartyPair(i * 2)); AcknowledgeNativeOutput(&f);
    }
    EXPECT(MultiplayerBattle_SendBlock(&issued, sizeof(issued))); AcknowledgeNativeOutput(&f);
    EXPECT(MultiplayerBattle_SendBlock(end, sizeof(end))); AcknowledgeNativeOutput(&f);
    gBattleOutcome = B_OUTCOME_WON; gBattleControllerExecFlags = 0;
    gParties[B_TRAINER_PLAYER][0].hp = 1;
    EXPECT(MultiplayerBattle_TryReturnNative());
    EXPECT_EQ(Send(&f, MP_BATTLE_QUERY), MP_BATTLE_OK);
    EXPECT_EQ(gMultiplayerBattleMailbox.state, MP_BATTLE_RETURNING);
    EXPECT(!(Word(gMultiplayerBattleMailbox.response.data) & 32));
    EXPECT(!memcmp(&issued, &gRngValue, sizeof(issued)));
    EXPECT(!memcmp(original, gParties[B_TRAINER_PLAYER], sizeof(struct Pokemon) * PARTY_SIZE));
    for (i = 28; i <= 44; i += 4) EXPECT_EQ(Word(gMultiplayerBattleMailbox.response.data + i), 0);
    gMain.callback1 = CB1_Overworld; gMain.callback2 = CB2_Overworld; gMain.inBattle = FALSE;
    EXPECT_EQ(Send(&f, MP_BATTLE_QUERY), MP_BATTLE_OK);
    EXPECT_EQ(gMultiplayerBattleMailbox.state, MP_BATTLE_FIELD_READY_HELD);
    EXPECT(Word(gMultiplayerBattleMailbox.response.data) & 32);
    EXPECT(!memcmp(&personal, &gRngValue, sizeof(personal)));
    TearDown(&f);
}

TEST("Multiplayer native singles: running expiry remains recovery-held and cannot close or rebind")
{
    struct ChannelFixture f;
    u32 i;
    NativeBinding(&f); Preparation(&f); EXPECT_EQ(Submit(&f), MP_BATTLE_OK);
    TrainerAndAck(&f); EXPECT_EQ(Send(&f, MP_BATTLE_START_SINGLES), MP_BATTLE_OK);
    for (i = 0; i < 600; i++) MultiplayerBattle_Tick();
    EXPECT_EQ(gMultiplayerBattleMailbox.state, MP_BATTLE_RECOVERY_REQUIRED);
    EXPECT(MultiplayerBattle_IsActive()); EXPECT(MultiplayerBattle_SkipCallbacks()); EXPECT(gSoftResetDisabled);
    EXPECT_EQ(Send(&f, MP_BATTLE_ABORT_PRE_START), MP_BATTLE_TIMEOUT);
    EXPECT_EQ(Send(&f, MP_BATTLE_CLOSE), MP_BATTLE_TIMEOUT);
    EXPECT_EQ(Send(&f, MP_BATTLE_KEEP), MP_BATTLE_TIMEOUT);
    Next(&f, MP_BATTLE_BIND); f.command.length = sizeof(f.binding);
    memcpy(f.command.data, &f.binding, sizeof(f.binding)); f.command.battleId[0]++;
    EXPECT_EQ(Submit(&f), MP_BATTLE_BUSY);
    EXPECT(!MultiplayerSave_FieldReady());
    TearDown(&f);
}

TEST("Multiplayer native singles: party fragments commit only after full validation and consumed retry stays consumed")
{
    struct ChannelFixture f;
    struct LinkBattlerHeader header = {0};
    struct Pokemon pair[2];
    NativeBinding(&f); Preparation(&f); EXPECT_EQ(Submit(&f), MP_BATTLE_OK);
    TrainerAndAck(&f); EXPECT_EQ(Send(&f, MP_BATTLE_START_SINGLES), MP_BATTLE_OK);
    header.versionSignatureHi = 3; header.battleEnigmaBerry.name[0] = EOS;
    Next(&f, MP_BATTLE_RECEIVE); f.command.sourceSlot = 1; f.command.blockSeq = 2;
    f.command.kind = MP_BATTLE_KIND_START_HEADER; f.command.totalSize = sizeof(header);
    f.command.length = sizeof(header); memcpy(f.command.data, &header, sizeof(header));
    EXPECT_EQ(Submit(&f), MP_BATTLE_OK); ResetBlockReceivedFlag(1);
    memcpy(pair, gParties[B_TRAINER_PLAYER], sizeof(pair));
    Next(&f, MP_BATTLE_RECEIVE); f.command.sourceSlot = 1; f.command.blockSeq = 3;
    f.command.kind = MP_BATTLE_KIND_PARTY_0; f.command.totalSize = sizeof(pair);
    f.command.length = 128; memcpy(f.command.data, pair, 128);
    EXPECT_EQ(Submit(&f), MP_BATTLE_OK); EXPECT_EQ(GetBlockReceivedStatus(), 0);
    EXPECT_EQ(Submit(&f), MP_BATTLE_OK); EXPECT_EQ(GetBlockReceivedStatus(), 0);
    Next(&f, MP_BATTLE_RECEIVE); f.command.sourceSlot = 1; f.command.blockSeq = 3;
    f.command.kind = MP_BATTLE_KIND_PARTY_0; f.command.totalSize = sizeof(pair);
    f.command.offset = 128; f.command.length = 72;
    memcpy(f.command.data, (const u8 *)pair + 128, 72);
    EXPECT_EQ(Submit(&f), MP_BATTLE_OK); EXPECT_EQ(GetBlockReceivedStatus(), 2);
    EXPECT(!memcmp(gBlockRecvBuffer[1], pair, sizeof(pair)));
    ResetBlockReceivedFlag(1);
    EXPECT_EQ(Submit(&f), MP_BATTLE_OK); EXPECT_EQ(GetBlockReceivedStatus(), 0);
    f.command.commandSeq++; f.command.data[0] ^= 1;
    EXPECT_EQ(Submit(&f), MP_BATTLE_ORDER); EXPECT_EQ(GetBlockReceivedStatus(), 0);
    TearDown(&f);
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
    EXPECT_EQ(Submit(&f), MP_BATTLE_SESSION); // ABI2 supports phase2, but cannot cross this binding.
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

TEST("Multiplayer full Multi: delayed send-outs wait for the previous ball and particles without starving the next trainer")
{
    struct ChannelFixture f;
    FullMultiBinding(&f, 0);
    AllocateBattleSpritesData();
    // Both send-outs may already be queued in the same frame. Waiting on
    // every battler's active flag would deadlock; only the admitted owner owns
    // the animation allowance until its ball and particles have completed.
    gBattleSpritesDataPtr->healthBoxesData[1].ballAnimActive = TRUE;
    gBattleSpritesDataPtr->healthBoxesData[3].ballAnimActive = TRUE;
    EXPECT(MultiplayerBattle_TrySendOutAnimation(1));
    EXPECT(!MultiplayerBattle_TrySendOutAnimation(3));
    gBattleSpritesDataPtr->healthBoxesData[1].ballAnimActive = FALSE;
    gBattleSpritesDataPtr->animationData->numBallParticles = 16;
    EXPECT(!MultiplayerBattle_TrySendOutAnimation(3));
    gBattleSpritesDataPtr->animationData->numBallParticles = 0;
    EXPECT(MultiplayerBattle_TrySendOutAnimation(3));
    EXPECT(!MultiplayerBattle_TrySendOutAnimation(1));
    FreeBattleSpritesData();
    TearDown(&f);
    // A fresh native battle cannot inherit the previous animation owner.
    FullMultiBinding(&f, 0);
    AllocateBattleSpritesData();
    gBattleSpritesDataPtr->healthBoxesData[3].ballAnimActive = TRUE;
    EXPECT(MultiplayerBattle_TrySendOutAnimation(1));
    FreeBattleSpritesData();
    TearDown(&f);
    EXPECT(MultiplayerBattle_TrySendOutAnimation(0));
}
