#include "global.h"
#include "multiplayer_ownership.h"
#include "multiplayer_save.h"
#include "multiplayer_battle.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "malloc.h"
#include "main.h"
#include "event_data.h"

_Static_assert(sizeof(struct BoxPokemon) == 80, "Identity preserves boxed layout");
_Static_assert(sizeof(struct Pokemon) == 100, "Identity preserves party layout");
_Static_assert(sizeof(struct SaveBlock3) == 600, "Ownership save profile");
_Static_assert(offsetof(struct SaveBlock3, ownership) == 4, "Legacy prefix");
_Static_assert(sizeof(struct MultiplayerOwnershipRecord) == 596, "Ownership record");
_Static_assert(offsetof(struct SaveBlock3, ownership.retirements) == 84, "Retirement journal");
_Static_assert(offsetof(struct SaveBlock3, ownership.checksum) == 596, "Ownership checksum");

const u32 gMultiplayerOwnershipAbi = 1;
static EWRAM_DATA bool8 sGrantActive = FALSE;
static EWRAM_DATA bool8 sMetadataTrusted = FALSE;
static EWRAM_DATA bool8 sRetirementAcknowledged = FALSE;
static EWRAM_DATA u32 sGrantFlags = 0;

static bool32 Zero(const u8 *data, u32 size)
{
    while (size--)
        if (*data++)
            return FALSE;
    return TRUE;
}

static u32 Crc32(const u8 *data, u32 size)
{
    u32 crc = 0xFFFFFFFF;
    while (size--)
    {
        u32 i;
        crc ^= *data++;
        for (i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return crc ^ 0xFFFFFFFF;
}

static void PutU32(u8 *data, u32 value)
{
    data[0] = value; data[1] = value >> 8; data[2] = value >> 16; data[3] = value >> 24;
}

static u32 ReadU32(const u8 *data)
{
    return data[0] | data[1] << 8 | data[2] << 16 | (u32)data[3] << 24;
}

bool32 MultiplayerOwnership_IsLegacy(const u8 *data)
{
    return Zero(data + 4, 596);
}

bool32 MultiplayerOwnership_ValidateSave(const u8 *data, bool32 allowLegacy)
{
    const struct MultiplayerOwnershipRecord *record = (const void *)(data + 4);
    u32 i, j;
    if (MultiplayerOwnership_IsLegacy(data))
        return allowLegacy;
    if (record->magic != MULTIPLAYER_OWNERSHIP_MAGIC || record->version != 1 || record->byteSize != 596
     || Zero(record->worldId, 16) || Zero(record->playerId, 16) || Zero(record->grantId, 16)
     || !record->start || record->start >= record->end || record->end > MULTIPLAYER_OWNERSHIP_SERIAL_LIMIT
     || record->next < record->start || record->next > record->end
     || !record->lease || !record->incarnation || record->reserved || record->retirementCount > 128
     || record->checksum != Crc32(data, 596))
        return FALSE;
    for (i = 0; i < 128; i++)
    {
        u32 value = record->retirements[i], identity = value & 0xFFFFF, reason = value >> 20;
        if (i >= record->retirementCount)
        {
            if (value)
                return FALSE;
        }
        else
        {
            if (!identity || (reason != MP_RETIRE_RELEASE && reason != MP_RETIRE_NPC_EXCHANGE))
                return FALSE;
            for (j = 0; j < i; j++)
                if ((record->retirements[j] & 0xFFFFF) == identity)
                    return FALSE;
        }
    }
    return TRUE;
}

void MultiplayerOwnership_InvalidateGrant(void)
{
    sGrantActive = FALSE;
    sRetirementAcknowledged = FALSE;
}

void MultiplayerOwnership_Loaded(void)
{
    MultiplayerOwnership_InvalidateGrant();
    sMetadataTrusted = MultiplayerOwnership_ValidateSave((const void *)gSaveBlock3Ptr, TRUE);
}

void MultiplayerOwnership_RefreshChecksum(void)
{
    // Only engine-validated records may be refreshed. Do not repair corrupt loaded Flash.
    if (sMetadataTrusted && gSaveBlock3Ptr->ownership.magic == MULTIPLAYER_OWNERSHIP_MAGIC)
        gSaveBlock3Ptr->ownership.checksum = Crc32((const void *)gSaveBlock3Ptr, 596);
}

static bool32 Ready(void)
{
    if (MultiplayerOwnership_IsLegacy((const void *)gSaveBlock3Ptr))
        return TRUE;
    if (!sMetadataTrusted)
        return FALSE;
    // The retained legacy DexNav prefix can legitimately change during play.
    MultiplayerOwnership_RefreshChecksum();
    return MultiplayerOwnership_ValidateSave((const void *)gSaveBlock3Ptr, FALSE);
}

bool32 MultiplayerOwnership_IsEnrolled(void)
{
    return !MultiplayerOwnership_IsLegacy((const void *)gSaveBlock3Ptr);
}

bool32 MultiplayerOwnership_CanSaveParty(void)
{
    if (MultiplayerBattle_IsActive())
        return FALSE;
    if (!Ready())
        return FALSE;
    if (!MultiplayerOwnership_IsEnrolled())
        return TRUE;
    return !gMain.inBattle && !gSaveBlock2Ptr->frontier.challengeStatus
        && !gSaveBlock2Ptr->frontier.challengePaused
        && (!B_FLAG_SKY_BATTLE || !FlagGet(B_FLAG_SKY_BATTLE));
}

static struct BoxPokemon *OwnedMon(u32 slot)
{
    if (slot < 6)
        return &gParties[B_TRAINER_PLAYER][slot].box;
    slot -= 6;
    if (slot < 420)
        return &gPokemonStoragePtr->boxes[slot / 30][slot % 30];
    slot -= 420;
    if (slot < 2)
        return &gSaveBlock1Ptr->daycare.mons[slot].mon;
    return &gPokemonStoragePtr->fusions[slot - 2].box;
}

static u8 InspectRoster(u32 *missing, u32 *identities)
{
    u32 i, j;
    bool32 legacy = MultiplayerOwnership_IsLegacy((const void *)gSaveBlock3Ptr);
    *missing = 0;
    for (i = 0; i < MULTIPLAYER_OWNERSHIP_SLOTS; i++)
    {
        u32 identity;
        bool32 occupied;
        if (!InspectBoxMonIdentity(OwnedMon(i), &identity, &occupied))
            return MP_SAVE_VALIDATION;
        if (legacy && identity)
            return MP_SAVE_OWNERSHIP;
        identities[i] = identity;
        if (!occupied)
            continue;
        if (!identity)
            (*missing)++;
        else
        {
            for (j = 0; j < i; j++)
                if (identities[j] == identity)
                    return MP_SAVE_OWNERSHIP;
            for (j = 0; j < gSaveBlock3Ptr->ownership.retirementCount; j++)
                if ((gSaveBlock3Ptr->ownership.retirements[j] & 0xFFFFF) == identity)
                    return MP_SAVE_OWNERSHIP;
        }
    }
    return MP_SAVE_OK;
}

u8 MultiplayerOwnership_Grant(const u8 *data, u32 lease, u32 incarnation)
{
    struct MultiplayerOwnershipRecord *record = &gSaveBlock3Ptr->ownership;
    u32 start = ReadU32(data + 48), end = ReadU32(data + 52), flags = ReadU32(data + 56);
    u32 missing, *identities;
    u8 status;
    bool32 legacy = MultiplayerOwnership_IsLegacy((const void *)gSaveBlock3Ptr);
    if (!Ready())
        return MP_SAVE_VALIDATION;
    if (Zero(data, 16) || Zero(data + 16, 16) || Zero(data + 32, 16)
     || !start || start >= end || end > MULTIPLAYER_OWNERSHIP_SERIAL_LIMIT || flags > 1)
        return MP_SAVE_MALFORMED;
    if (legacy)
    {
        if (flags != 1)
            return MP_SAVE_OWNERSHIP;
        // Enrollment must not reinterpret nonzero old padding or malformed creatures.
        identities = Alloc(MULTIPLAYER_OWNERSHIP_SLOTS * sizeof(u32));
        if (!identities)
            return MP_SAVE_BUSY;
        status = InspectRoster(&missing, identities);
        Free(identities);
        if (status != MP_SAVE_OK)
            return status;
        memset(record, 0, sizeof(*record));
        record->magic = MULTIPLAYER_OWNERSHIP_MAGIC; record->version = 1; record->byteSize = 596;
        memcpy(record->worldId, data, 16); memcpy(record->playerId, data + 16, 16);
    }
    else
    {
        if (memcmp(record->worldId, data, 16) || memcmp(record->playerId, data + 16, 16))
            return MP_SAVE_OWNERSHIP;
        if (!memcmp(record->grantId, data + 32, 16))
            return sGrantActive && record->start == start && record->end == end
                && record->lease == lease && record->incarnation == incarnation
                && flags == sGrantFlags ? MP_SAVE_OK : MP_SAVE_OWNERSHIP;
        if (flags || start < record->end)
            return MP_SAVE_OWNERSHIP;
    }
    memcpy(record->grantId, data + 32, 16);
    record->start = start; record->end = end; record->next = start;
    record->lease = lease; record->incarnation = incarnation;
    sGrantActive = TRUE; sMetadataTrusted = TRUE; sGrantFlags = flags;
    MultiplayerOwnership_RefreshChecksum();
    return MP_SAVE_OK;
}

u8 MultiplayerOwnership_Query(u8 *data)
{
    struct MultiplayerOwnershipRecord *record = &gSaveBlock3Ptr->ownership;
    u32 missing, *identities;
    u8 status;
    bool32 legacy;
    if (!Ready())
        return MP_SAVE_VALIDATION;
    identities = Alloc(MULTIPLAYER_OWNERSHIP_SLOTS * sizeof(u32));
    if (!identities)
        return MP_SAVE_BUSY;
    status = InspectRoster(&missing, identities);
    Free(identities);
    if (status != MP_SAVE_OK)
        return status;
    legacy = MultiplayerOwnership_IsLegacy((const void *)gSaveBlock3Ptr);
    memset(data, 0, 80);
    PutU32(data, legacy ? 0 : 1 | (sGrantActive ? 2 : 0));
    PutU32(data + 4, missing);
    PutU32(data + 8, !legacy && sGrantActive ? record->end - record->next : 0);
    PutU32(data + 12, record->retirementCount);
    memcpy(data + 16, record->worldId, 16); memcpy(data + 32, record->playerId, 16); memcpy(data + 48, record->grantId, 16);
    PutU32(data + 64, record->next); PutU32(data + 68, record->end);
    PutU32(data + 72, record->lease); PutU32(data + 76, record->incarnation);
    return MP_SAVE_OK;
}

u8 MultiplayerOwnership_PrepareExport(void)
{
    struct MultiplayerOwnershipRecord *record = &gSaveBlock3Ptr->ownership;
    u32 missing, i, *identities;
    u8 status;
    if (!Ready() || MultiplayerOwnership_IsLegacy((const void *)gSaveBlock3Ptr) || !sGrantActive)
        return MP_SAVE_OWNERSHIP;
    identities = Alloc(MULTIPLAYER_OWNERSHIP_SLOTS * sizeof(u32));
    if (!identities)
        return MP_SAVE_BUSY;
    status = InspectRoster(&missing, identities);
    if (status == MP_SAVE_OK && missing > record->end - record->next)
        status = MP_SAVE_OWNERSHIP;
    // Newly minted IDs cannot be retired until the host durably accepts this export.
    if (status == MP_SAVE_OK && missing)
        sRetirementAcknowledged = FALSE;
    // Validate the entire roster and range before the first creature mutation.
    if (status == MP_SAVE_OK)
        for (i = 0; i < MULTIPLAYER_OWNERSHIP_SLOTS; i++)
        {
            u32 identity;
            bool32 occupied;
            InspectBoxMonIdentity(OwnedMon(i), &identity, &occupied);
            if (occupied && !identity)
            {
                identity = record->next++;
                SetBoxMonData(OwnedMon(i), MON_DATA_MULTIPLAYER_ID, &identity);
            }
        }
    Free(identities);
    if (status == MP_SAVE_OK)
        MultiplayerOwnership_RefreshChecksum();
    return status;
}

bool32 MultiplayerOwnership_Retire(const struct BoxPokemon *mon, u32 reason)
{
    struct MultiplayerOwnershipRecord *record = &gSaveBlock3Ptr->ownership;
    u32 identity, i;
    bool32 occupied;
    if (!Ready())
        return FALSE;
    if (MultiplayerOwnership_IsLegacy((const void *)gSaveBlock3Ptr))
        return TRUE;
    if (!InspectBoxMonIdentity(mon, &identity, &occupied) || !occupied)
        return FALSE;
    if (!identity)
        return TRUE;
    if (!sRetirementAcknowledged)
        return FALSE;
    if ((reason != MP_RETIRE_RELEASE && reason != MP_RETIRE_NPC_EXCHANGE) || record->retirementCount == 128)
        return FALSE;
    for (i = 0; i < record->retirementCount; i++)
        if ((record->retirements[i] & 0xFFFFF) == identity)
            return FALSE;
    record->retirements[record->retirementCount++] = identity | reason << 20;
    MultiplayerOwnership_RefreshChecksum();
    return TRUE;
}

void MultiplayerOwnership_Acknowledge(bool32 exported)
{
    if (Ready() && !MultiplayerOwnership_IsLegacy((const void *)gSaveBlock3Ptr))
    {
        sRetirementAcknowledged = exported;
        gSaveBlock3Ptr->ownership.retirementCount = 0;
        memset(gSaveBlock3Ptr->ownership.retirements, 0, sizeof(gSaveBlock3Ptr->ownership.retirements));
        MultiplayerOwnership_RefreshChecksum();
    }
}

bool32 MultiplayerOwnership_ImportMatches(const u8 *data)
{
    const struct MultiplayerOwnershipRecord *source = (const void *)(data + 4);
    const struct MultiplayerOwnershipRecord *local = &gSaveBlock3Ptr->ownership;
    if (!Ready() || !MultiplayerOwnership_ValidateSave(data, FALSE)
     || MultiplayerOwnership_IsLegacy((const void *)gSaveBlock3Ptr))
        return FALSE;
    return !memcmp(source->worldId, local->worldId, 16) && !memcmp(source->playerId, local->playerId, 16);
}
