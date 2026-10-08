#include "global.h"
#include "multiplayer_ownership.h"
#include "multiplayer_save.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "random.h"
#include "load_save.h"
#include "save.h"
#include "agb_flash.h"
#include "gba/flash_internal.h"
#include "trade.h"
#include "hall_of_fame.h"
#include "test/test.h"

static void PutWord(u8 *data, u32 value)
{
    data[0] = value; data[1] = value >> 8; data[2] = value >> 16; data[3] = value >> 24;
}

static void ResetOwnership(void)
{
    ZeroPlayerPartyMons();
    memset(gPokemonStoragePtr, 0, sizeof(*gPokemonStoragePtr));
    memset(&gSaveBlock1Ptr->daycare, 0, sizeof(gSaveBlock1Ptr->daycare));
    memset(gSaveBlock3Ptr, 0, sizeof(*gSaveBlock3Ptr));
    MultiplayerOwnership_Loaded();
}

static void MakeGrant(u8 *data, u32 start, u32 end, u32 flags)
{
    memset(data, 0, 60);
    data[0] = 1; data[16] = 2; data[32] = 3;
    PutWord(data + 48, start); PutWord(data + 52, end); PutWord(data + 56, flags);
}

TEST("Multiplayer ownership: encrypted identity survives every personality order and field updates")
{
    u32 i;
    for (i = 0; i < 24; i++)
    {
        struct Pokemon mon, restored;
        u32 identity = 0xDABCD, changed = ITEM_ORAN_BERRY, personality = i;
        CreateMon(&mon, SPECIES_WURMPLE, 5, personality, OTID_STRUCT_PLAYER_ID);
        SetMonData(&mon, MON_DATA_MULTIPLAYER_ID, &identity);
        EXPECT_EQ(GetMonData(&mon, MON_DATA_MULTIPLAYER_ID), identity);
        SetMonData(&mon, MON_DATA_HELD_ITEM, &changed);
        changed = MOVE_TACKLE;
        SetMonData(&mon, MON_DATA_MOVE3, &changed);
        changed = MOVE_STRING_SHOT;
        SetMonData(&mon, MON_DATA_MOVE4, &changed);
        BoxMonToMon(&mon.box, &restored);
        UpdateMonPersonality(&restored.box, (i + 7) % 24);
        EXPECT_EQ(GetMonData(&restored, MON_DATA_MULTIPLAYER_ID), identity);
        EXPECT_EQ(GetMonData(&restored, MON_DATA_SPECIES), SPECIES_WURMPLE);
        EXPECT_EQ(GetMonData(&restored, MON_DATA_HELD_ITEM), ITEM_ORAN_BERRY);
        EXPECT_EQ(GetMonData(&restored, MON_DATA_MOVE3), MOVE_TACKLE);
        EXPECT_EQ(GetMonData(&restored, MON_DATA_MOVE4), MOVE_STRING_SHOT);
    }
}

TEST("Multiplayer ownership: serial uses only the six specified encrypted spare fields")
{
    struct Pokemon mon;
    struct BoxPokemon plaintext;
    u32 identity = 0xFFFFF, i;
    CreateMon(&mon, SPECIES_TORCHIC, 7, 0, OTID_STRUCT_PLAYER_ID);
    SetMonData(&mon, MON_DATA_MULTIPLAYER_ID, &identity);
    plaintext = mon.box;
    for (i = 0; i < 12; i++)
        plaintext.secure.raw[i] ^= plaintext.otId ^ plaintext.personality;
    EXPECT_EQ((plaintext.secure.substructs[0].raw[1] >> 10) & 63, 63);
    EXPECT_EQ((plaintext.secure.raw[1] >> 29) & 7, 7);
    EXPECT_EQ((plaintext.secure.substructs[0].raw[5] >> 14) & 3, 3);
    EXPECT_EQ((plaintext.secure.substructs[1].raw[2] >> 11) & 31, 31);
    EXPECT_EQ((plaintext.secure.substructs[1].raw[3] >> 11) & 7, 7);
    EXPECT_EQ((plaintext.secure.raw[11] >> 28) & 1, 1);
}

TEST("Multiplayer ownership: malformed creature inspection does not mark or alter it")
{
    struct Pokemon mon;
    struct BoxPokemon original;
    u32 identity;
    bool32 occupied;
    CreateMon(&mon, SPECIES_TORCHIC, 7, 0, OTID_STRUCT_PLAYER_ID);
    mon.box.secure.raw[0] ^= 1;
    original = mon.box;
    EXPECT(!InspectBoxMonIdentity(&mon.box, &identity, &occupied));
    EXPECT_EQ(memcmp(&original, &mon.box, 80), 0);
}

TEST("Multiplayer ownership: all retained containers receive unique IDs without RNG consumption")
{
    u8 grant[60], query[80];
    struct Pokemon mon;
    rng_value_t rng, rng2;
    ResetOwnership();
    MakeGrant(grant, 10, 15, 1);
    EXPECT_EQ(MultiplayerOwnership_Grant(grant, 1, 1), MP_SAVE_OK);
    CreateMon(&mon, SPECIES_TORCHIC, 7, 0, OTID_STRUCT_PLAYER_ID);
    gParties[B_TRAINER_PLAYER][0] = mon;
    gPokemonStoragePtr->boxes[13][29] = mon.box;
    gSaveBlock1Ptr->daycare.mons[1].mon = mon.box;
    gPokemonStoragePtr->fusions[3] = mon;
    rng = gRngValue; rng2 = gRng2Value;
    EXPECT_EQ(MultiplayerOwnership_PrepareExport(), MP_SAVE_OK);
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_MULTIPLAYER_ID), 10);
    EXPECT_EQ(GetBoxMonData(&gPokemonStoragePtr->boxes[13][29], MON_DATA_MULTIPLAYER_ID), 11);
    EXPECT_EQ(GetBoxMonData(&gSaveBlock1Ptr->daycare.mons[1].mon, MON_DATA_MULTIPLAYER_ID), 12);
    EXPECT_EQ(GetMonData(&gPokemonStoragePtr->fusions[3], MON_DATA_MULTIPLAYER_ID), 13);
    EXPECT_EQ(memcmp(&rng, &gRngValue, 16), 0);
    EXPECT_EQ(memcmp(&rng2, &gRng2Value, 16), 0);
    EXPECT_EQ(MultiplayerOwnership_Query(query), MP_SAVE_OK);
    EXPECT_EQ(query[0], 3);
    EXPECT_EQ(query[4], 0);
    EXPECT_EQ(query[8], 1);
    EXPECT(MultiplayerOwnership_ValidateSave((const void *)gSaveBlock3Ptr, FALSE));
}

TEST("Multiplayer ownership: exhausted ranges and duplicate IDs refuse without minting")
{
    u8 grant[60];
    struct SaveBlock3 before;
    u32 identity = 1;
    ResetOwnership();
    MakeGrant(grant, 1, 2, 1);
    EXPECT_EQ(MultiplayerOwnership_Grant(grant, 1, 1), MP_SAVE_OK);
    CreateMon(&gParties[B_TRAINER_PLAYER][0], SPECIES_TORCHIC, 7, 0, OTID_STRUCT_PLAYER_ID);
    gPokemonStoragePtr->boxes[0][0] = gParties[B_TRAINER_PLAYER][0].box;
    before = *gSaveBlock3Ptr;
    EXPECT_EQ(MultiplayerOwnership_PrepareExport(), MP_SAVE_OWNERSHIP);
    EXPECT_EQ(memcmp(&before, gSaveBlock3Ptr, 600), 0);
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_MULTIPLAYER_ID), 0);
    SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_MULTIPLAYER_ID, &identity);
    SetBoxMonData(&gPokemonStoragePtr->boxes[0][0], MON_DATA_MULTIPLAYER_ID, &identity);
    EXPECT_EQ(MultiplayerOwnership_PrepareExport(), MP_SAVE_OWNERSHIP);
    EXPECT_EQ(memcmp(&before, gSaveBlock3Ptr, 600), 0);
}

TEST("Multiplayer ownership: grant replay does not rewind and invalidation requires fresh grant")
{
    u8 grant[60], query[80];
    ResetOwnership();
    MakeGrant(grant, 1, 33, 1);
    EXPECT_EQ(MultiplayerOwnership_Grant(grant, 1, 1), MP_SAVE_OK);
    CreateMon(&gParties[B_TRAINER_PLAYER][0], SPECIES_TORCHIC, 7, 0, OTID_STRUCT_PLAYER_ID);
    EXPECT_EQ(MultiplayerOwnership_PrepareExport(), MP_SAVE_OK);
    EXPECT_EQ(MultiplayerOwnership_Grant(grant, 1, 1), MP_SAVE_OK);
    EXPECT_EQ(gSaveBlock3Ptr->ownership.next, 2);
    MultiplayerOwnership_InvalidateGrant();
    EXPECT_EQ(MultiplayerOwnership_Query(query), MP_SAVE_OK);
    EXPECT_EQ(query[0], 1);
    EXPECT_EQ(query[8], 0);
    EXPECT_EQ(MultiplayerOwnership_Grant(grant, 1, 1), MP_SAVE_OWNERSHIP);
    grant[32] = 4; PutWord(grant + 48, 33); PutWord(grant + 52, 65); PutWord(grant + 56, 0);
    EXPECT_EQ(MultiplayerOwnership_Grant(grant, 2, 1), MP_SAVE_OK);
    EXPECT_EQ(GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_MULTIPLAYER_ID), 1);
}

TEST("Multiplayer ownership: retirement overflow refuses before mutation and acknowledgement clears RAM")
{
    u8 grant[60];
    struct Pokemon mon;
    struct SaveBlock3 before;
    u32 i;
    ResetOwnership();
    MakeGrant(grant, 1, 257, 1);
    EXPECT_EQ(MultiplayerOwnership_Grant(grant, 1, 1), MP_SAVE_OK);
    CreateMon(&mon, SPECIES_WURMPLE, 3, 0, OTID_STRUCT_PLAYER_ID);
    MultiplayerOwnership_Acknowledge(TRUE);
    for (i = 1; i <= 128; i++)
    {
        SetMonData(&mon, MON_DATA_MULTIPLAYER_ID, &i);
        EXPECT(MultiplayerOwnership_Retire(&mon.box, MP_RETIRE_RELEASE));
    }
    before = *gSaveBlock3Ptr;
    SetMonData(&mon, MON_DATA_MULTIPLAYER_ID, &i);
    EXPECT(!MultiplayerOwnership_Retire(&mon.box, MP_RETIRE_RELEASE));
    EXPECT_EQ(memcmp(&before, gSaveBlock3Ptr, 600), 0);
    EXPECT(MultiplayerOwnership_ValidateSave((const void *)gSaveBlock3Ptr, FALSE));
    MultiplayerOwnership_Acknowledge(TRUE);
    EXPECT_EQ(gSaveBlock3Ptr->ownership.retirementCount, 0);
    EXPECT(MultiplayerOwnership_ValidateSave((const void *)gSaveBlock3Ptr, FALSE));
}

TEST("Multiplayer ownership: corrupt metadata cannot be repaired by ordinary checksum refresh")
{
    u8 grant[60];
    u32 original;
    ResetOwnership();
    MakeGrant(grant, 1, 33, 1);
    EXPECT_EQ(MultiplayerOwnership_Grant(grant, 1, 1), MP_SAVE_OK);
    gSaveBlock3Ptr->ownership.checksum ^= 1;
    original = gSaveBlock3Ptr->ownership.checksum;
    MultiplayerOwnership_Loaded();
    MultiplayerOwnership_RefreshChecksum();
    EXPECT_EQ(gSaveBlock3Ptr->ownership.checksum, original);
    EXPECT_EQ(MultiplayerOwnership_PrepareExport(), MP_SAVE_OWNERSHIP);
}

TEST("Multiplayer ownership: ordinary and formerly partial saves persist all six ownership chunks")
{
    u8 grant[60], chunk[116];
    struct SaveBlock3 expected;
    u32 saveType, section;
    ResetOwnership();
    CheckForFlashMemory();
    EXPECT(gFlashMemoryPresent);
    Save_ResetSaveCounters();
    MakeGrant(grant, 1, 33, 1);
    EXPECT_EQ(MultiplayerOwnership_Grant(grant, 1, 1), MP_SAVE_OK);
    expected = *gSaveBlock3Ptr;
    for (saveType = SAVE_NORMAL; saveType <= SAVE_EREADER; saveType++)
    {
        u32 counter = gSaveCounter;
        EXPECT_NE(HandleSavingData(saveType), SAVE_STATUS_ERROR);
        EXPECT_EQ(gDamagedSaveSectors, 0);
        EXPECT_EQ(gSaveCounter, counter + 1);
        for (section = 0; section < 6; section++)
        {
            u32 physical = (section + gLastWrittenSector) % 14 + 14 * (gSaveCounter & 1);
            u32 size = section < 5 ? 116 : 20;
            ReadFlash(physical, 3968, chunk, size);
            EXPECT_EQ(memcmp(chunk, (const u8 *)&expected + section * 116, size), 0);
        }
    }
    EXPECT(!WriteSaveBlock2());
    EXPECT(WriteSaveBlock1Sector());
    ReadFlash((5 + gLastWrittenSector) % 14 + 14 * (gSaveCounter & 1), 3968, chunk, 20);
    EXPECT_EQ(memcmp(chunk, (const u8 *)&expected + 580, 20), 0);
}

TEST("Multiplayer ownership: temporary Frontier party serialization refuses without Flash mutation")
{
    u8 grant[60];
    u32 counter;
    ResetOwnership();
    MakeGrant(grant, 1, 33, 1);
    EXPECT_EQ(MultiplayerOwnership_Grant(grant, 1, 1), MP_SAVE_OK);
    counter = gSaveCounter;
    gSaveBlock2Ptr->frontier.challengeStatus = 1;
    EXPECT_EQ(HandleSavingData(SAVE_LINK), SAVE_STATUS_ERROR);
    EXPECT_EQ(gSaveCounter, counter);
    EXPECT(WriteSaveBlock2());
    EXPECT_EQ(gSaveCounter, counter);
    EXPECT(WriteSaveBlock1Sector());
}

TEST("Multiplayer ownership: native trade admission refuses enrolled inventory")
{
    u8 grant[60];
    struct RfuGameCompatibilityData compatibility = {0};
    ResetOwnership();
    compatibility.hasNationalDex = TRUE;
    EXPECT_EQ(CanRegisterMonForTradingBoard(compatibility, SPECIES_TORCHIC, SPECIES_TORCHIC, FALSE), CAN_REGISTER_MON);
    MakeGrant(grant, 1, 33, 1);
    EXPECT_EQ(MultiplayerOwnership_Grant(grant, 1, 1), MP_SAVE_OK);
    EXPECT_EQ(CanRegisterMonForTradingBoard(compatibility, SPECIES_TORCHIC, SPECIES_TORCHIC, FALSE), CANT_REGISTER_MON);
    EXPECT_EQ(GetUnionRoomTradeMessageId(compatibility, compatibility, SPECIES_TORCHIC, SPECIES_TORCHIC, TYPE_FIRE, SPECIES_TORCHIC, FALSE), UR_TRADE_MSG_MON_CANT_BE_TRADED);
    EXPECT_EQ(CanSpinTradeMon(NULL, 0), CANT_TRADE_NATIONAL);
}

TEST("Multiplayer ownership: Hall of Fame reads invalidate grant without mistrusting live DexNav updates")
{
    u8 grant[60], query[80];
    ResetOwnership();
    CheckForFlashMemory();
    EXPECT(gFlashMemoryPresent);
    MakeGrant(grant, 1, 33, 1);
    EXPECT_EQ(MultiplayerOwnership_Grant(grant, 1, 1), MP_SAVE_OK);
    ((u8 *)gSaveBlock3Ptr)[0] ^= 1;
    LoadGameSave(SAVE_HALL_OF_FAME);
    EXPECT_EQ(MultiplayerOwnership_Query(query), MP_SAVE_OK);
    EXPECT_EQ(query[0], 1);
    EXPECT_EQ(query[8], 0);
    gSaveBlock3Ptr->ownership.checksum ^= 1;
    MultiplayerOwnership_Loaded();
    LoadGameSave(SAVE_HALL_OF_FAME);
    EXPECT_EQ(MultiplayerOwnership_Query(query), MP_SAVE_VALIDATION);
}

TEST("Multiplayer ownership: assigned releases require durable acknowledgment of the loaded roster")
{
    u8 grant[60];
    struct Pokemon unassigned;
    ResetOwnership();
    MakeGrant(grant, 1, 33, 1);
    EXPECT_EQ(MultiplayerOwnership_Grant(grant, 1, 1), MP_SAVE_OK);
    CreateMon(&unassigned, SPECIES_WURMPLE, 3, 0, OTID_STRUCT_PLAYER_ID);
    EXPECT(MultiplayerOwnership_Retire(&unassigned.box, MP_RETIRE_RELEASE));
    gParties[B_TRAINER_PLAYER][0] = unassigned;
    EXPECT_EQ(MultiplayerOwnership_PrepareExport(), MP_SAVE_OK);
    EXPECT(!MultiplayerOwnership_Retire(&gParties[B_TRAINER_PLAYER][0].box, MP_RETIRE_RELEASE));
    EXPECT_EQ(gSaveBlock3Ptr->ownership.retirementCount, 0);
    MultiplayerOwnership_Acknowledge(TRUE);
    EXPECT(MultiplayerOwnership_Retire(&gParties[B_TRAINER_PLAYER][0].box, MP_RETIRE_RELEASE));
    MultiplayerOwnership_Acknowledge(TRUE);
    MultiplayerOwnership_InvalidateGrant();
    EXPECT(!MultiplayerOwnership_Retire(&gParties[B_TRAINER_PLAYER][0].box, MP_RETIRE_RELEASE));
    MultiplayerOwnership_Acknowledge(FALSE);
    EXPECT(!MultiplayerOwnership_Retire(&gParties[B_TRAINER_PLAYER][0].box, MP_RETIRE_RELEASE));
    grant[32] = 4; PutWord(grant + 48, 33); PutWord(grant + 52, 65); PutWord(grant + 56, 0);
    EXPECT_EQ(MultiplayerOwnership_Grant(grant, 2, 1), MP_SAVE_OK);
    MultiplayerOwnership_Acknowledge(TRUE);
    gParties[B_TRAINER_PLAYER][1] = unassigned;
    EXPECT_EQ(MultiplayerOwnership_PrepareExport(), MP_SAVE_OK);
    EXPECT(!MultiplayerOwnership_Retire(&gParties[B_TRAINER_PLAYER][0].box, MP_RETIRE_RELEASE));
    MultiplayerOwnership_Acknowledge(TRUE);
    MultiplayerOwnership_Loaded();
    EXPECT(!MultiplayerOwnership_Retire(&gParties[B_TRAINER_PLAYER][0].box, MP_RETIRE_RELEASE));
}
