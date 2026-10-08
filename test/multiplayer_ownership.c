#include "global.h"
#include "multiplayer_ownership.h"
#include "multiplayer_save.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "random.h"
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
    MultiplayerOwnership_Acknowledge();
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
