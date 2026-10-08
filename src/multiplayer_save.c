#include "global.h"
#include "multiplayer_save.h"
#include "multiplayer.h"
#include "multiplayer_ownership.h"
#include "event_data.h"
#include "main.h"
#include "overworld.h"
#include "load_save.h"
#include "save.h"
#include "fieldmap.h"
#include "field_player_avatar.h"
#include "script.h"
#include "sprite.h"
#include "palette.h"
#include "random.h"
#include "agb_flash.h"
#include "gba/flash_internal.h"
#include "pokemon_storage_system.h"
#include "link.h"
#include "trainer_hill.h"
#include "battle_pyramid.h"
#include "safari_zone.h"

_Static_assert(sizeof(struct MultiplayerSaveControl) == 240, "Save control ABI");
_Static_assert(sizeof(struct MultiplayerSaveMailbox) == 512, "Save mailbox ABI");
_Static_assert(offsetof(struct MultiplayerSaveMailbox, commandGeneration) == 24, "Save command generation");
_Static_assert(offsetof(struct MultiplayerSaveMailbox, command) == 28, "Save command");
_Static_assert(offsetof(struct MultiplayerSaveMailbox, responseGeneration) == 268, "Save response generation");
_Static_assert(offsetof(struct MultiplayerSaveMailbox, response) == 272, "Save response");
_Static_assert(offsetof(struct MultiplayerSaveControl, chunkCrc32) == 44, "Save CRC offset");
_Static_assert(offsetof(struct MultiplayerSaveControl, data) == 48, "Save payload offset");
_Static_assert(sizeof(struct SaveBlock1) == 15568, "Pinned SaveBlock1");
_Static_assert(sizeof(struct SaveBlock2) == 3884, "Pinned SaveBlock2");
_Static_assert(sizeof(struct SaveBlock3) == 600, "Ownership SaveBlock3");
_Static_assert(offsetof(struct SaveBlock1, daycare) == 0x3030, "Pinned daycare offset");
_Static_assert(sizeof(struct DaycareMon) == 140, "Pinned daycare size");
_Static_assert(sizeof(struct PokemonStorage) == 34144, "Pinned storage");
_Static_assert(sizeof(rng_value_t) == 16, "Pinned SFC32");

EWRAM_DATA volatile struct MultiplayerSaveMailbox gMultiplayerSaveMailbox = {0};
static EWRAM_DATA struct MultiplayerSaveControl sLastCommand = {0};
static EWRAM_DATA struct MultiplayerSaveControl sLastResponse = {0};
static EWRAM_DATA struct MultiplayerSaveControl sCachedResponse = {0};
static EWRAM_DATA u8 sRequestId[16] = {0};
static EWRAM_DATA u32 sState = 0;
static EWRAM_DATA u32 sFrame = 0;
static EWRAM_DATA u32 sSeenGeneration = 0;
static EWRAM_DATA u32 sIncarnation = 0;
static EWRAM_DATA u32 sSession = 0;
static EWRAM_DATA u32 sLastSeq = 0;
static EWRAM_DATA u32 sRevision = 0;
static EWRAM_DATA u32 sOffset = 0;
static EWRAM_DATA u32 sCrc = 0;
static EWRAM_DATA u32 sExpectedCrc = 0;
static EWRAM_DATA u32 sSourceCounter = 0;
static EWRAM_DATA u32 sSourceRotation = 0;
static EWRAM_DATA u32 sTrainerId = 0;
static EWRAM_DATA u32 sTargetCounter = 0;
static EWRAM_DATA u32 sTargetRotation = 0;
static EWRAM_DATA u32 sDeadline = 0;
static EWRAM_DATA u32 sBankCounters[2] = {0};
static EWRAM_DATA u32 sBankRotations[2] = {0};
static EWRAM_DATA u16 sBankIds[2] = {0};
static EWRAM_DATA u8 sBankErased[2] = {0};
static EWRAM_DATA u8 sPublishId = 0;
static EWRAM_DATA u32 sImportOwnership[2][150] = {0};
static EWRAM_DATA bool8 sPending = FALSE;
static EWRAM_DATA bool8 sSavedResetDisabled = FALSE;
static EWRAM_DATA bool8 sCommitted = FALSE;
static EWRAM_DATA u8 sReloadStatus = MP_SAVE_OK;
static const u16 sSectionSizes[14] = {3884, 3968, 3968, 3968, 3664, 3968, 3968, 3968, 3968, 3968, 3968, 3968, 3968, 2400};
// Bounds from this pinned build's map_groups.json and layouts.json.
static const u8 sMapCounts[] = {57,5,5,6,7,8,9,7,7,14,8,17,10,23,13,15,15,2,2,2,3,1,1,1,108,61,89,2,1,13,1,1,3,1,5,123,60,62,4,6,8,10,6,8,20,10,8,2,10,4,2,2,1,1,1,2,2,3,2,3,2,1,1,6,5,5,8,8,5,5,1,1,1,2,1};
extern const struct MapLayout *const gMapLayouts[];

static u32 Crc32(u32 crc, const u8 *data, u32 size)
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

static u32 ReadU32(const u8 *data)
{
    return data[0] | data[1] << 8 | data[2] << 16 | (u32)data[3] << 24;
}

static void PutU32(u8 *data, u32 value)
{
    data[0] = value; data[1] = value >> 8; data[2] = value >> 16; data[3] = value >> 24;
}

static bool32 AllBytes(const u8 *data, u32 size, u8 value)
{
    while (size--)
        if (*data++ != value)
            return FALSE;
    return TRUE;
}

static bool32 NameValid(const u8 *data)
{
    u32 i;
    if (data[0] == 0xFF)
        return FALSE;
    for (i = 1; i < 8; i++)
        if (data[i] == 0xFF)
            return TRUE;
    return FALSE;
}

static u16 Checksum(const u8 *data, u32 size)
{
    u32 checksum = 0;
    while (size >= 4)
    {
        checksum += ReadU32(data);
        data += 4; size -= 4;
    }
    return checksum + (checksum >> 16);
}

bool32 MultiplayerSave_IsHeld(void)
{
    return sState >= MP_SAVE_EXPORTING;
}

bool32 MultiplayerSave_SkipCallbacks(void)
{
    return MultiplayerSave_IsHeld() && sState != MP_SAVE_RELOADING;
}

bool32 MultiplayerSave_PauseClocks(void)
{
    return MultiplayerSave_SkipCallbacks();
}

static bool32 FieldReady(void)
{
    u32 id = gPlayerAvatar.objectEventId;
    return gMain.callback1 == CB1_Overworld && gMain.callback2 == CB2_Overworld
        && gSaveBlock1Ptr != NULL && gSaveBlock2Ptr != NULL && gMapHeader.mapLayout != NULL
        && id < OBJECT_EVENTS_COUNT && gObjectEvents[id].active && gObjectEvents[id].isPlayer
        && gObjectEvents[id].spriteId < MAX_SPRITES && gSprites[gObjectEvents[id].spriteId].inUse
        && !gPaletteFade.active && !ArePlayerFieldControlsLocked() && !ScriptContext_IsEnabled()
        && gPlayerAvatar.tileTransitionState == 0 && gPlayerAvatar.runningState == 0
        && !gMain.inBattle && !gReceivedRemoteLinkPlayers && !gWirelessCommType
        && gTrainerHillVBlankCounter == NULL && !GetCurrentTrainerHillMapId()
        && !InBattlePyramid() && !GetSafariZoneFlag()
        && !gSaveBlock2Ptr->frontier.challengeStatus && !gSaveBlock2Ptr->frontier.challengePaused
        && (!B_FLAG_SKY_BATTLE || !FlagGet(B_FLAG_SKY_BATTLE));
}

static void PublishResponse(const struct MultiplayerSaveControl *command, u8 status, const u8 *data, u16 length, u32 offset)
{
    u32 generation = (gMultiplayerSaveMailbox.responseGeneration + 1) | 1;
    sLastResponse = *command;
    sLastResponse.status = status;
    sLastResponse.offset = offset;
    sLastResponse.length = length;
    memset(sLastResponse.data, 0, sizeof(sLastResponse.data));
    if (length)
        memcpy(sLastResponse.data, data, length);
    sLastResponse.chunkCrc32 = Crc32(0, sLastResponse.data, length);
    if (!memcmp(command, &sLastCommand, sizeof(*command)))
        sCachedResponse = sLastResponse;
    gMultiplayerSaveMailbox.responseGeneration = generation;
    gMultiplayerSaveMailbox.response = sLastResponse;
    gMultiplayerSaveMailbox.state = sState;
    gMultiplayerSaveMailbox.responseGeneration = generation + 1;
}

static void Release(void)
{
    gSoftResetDisabled = sSavedResetDisabled;
    sState = MP_SAVE_BOUND;
    sPending = FALSE;
}

static void Hold(u32 state, const struct MultiplayerSaveControl *command)
{
    sSavedResetDisabled = gSoftResetDisabled;
    gSoftResetDisabled = TRUE;
    sState = state;
    memcpy(sRequestId, command->requestId, 16);
    sRevision = command->revision;
    sOffset = 0; sCrc = 0;
    sCommitted = FALSE;
    sDeadline = sFrame + 600;
    Multiplayer_LeaveField();
}

static bool32 EraseStaging(void)
{
    u32 i;
    for (i = 0; i < 14; i++)
    {
        u32 sector = i + 14 * (sTargetCounter & 1);
        if (EraseFlashSector(sector))
            return FALSE;
        ReadFlash(sector, 0, (u8 *)&gSaveDataBuffer, 4096);
        if (!AllBytes((u8 *)&gSaveDataBuffer, 4096, 0xFF))
            return FALSE;
    }
    return TRUE;
}

static bool32 CancelImport(void)
{
    if (!EraseStaging())
    {
        sState = MP_SAVE_ERROR;
        sPending = FALSE;
        return FALSE;
    }
    Release();
    return TRUE;
}

static void StartReload(u8 status)
{
    Multiplayer_BeginMapTransition();
    sReloadStatus = status;
    sState = MP_SAVE_RELOADING;
    SetMainCallback1(NULL);
    SetMainCallback2(CB2_ContinueSavedGame);
}

static void ReadFlashBytes(u32 offset, u8 *data, u32 size)
{
    while (size)
    {
        u32 take = min(size, 4096 - (offset & 4095));
        ReadFlash(offset / 4096, offset & 4095, data, take);
        data += take; offset += take; size -= take;
    }
}

static bool32 LocalBankValid(void)
{
    u32 i;
    u16 seen = 0;
    for (i = 0; i < 14; i++)
    {
        struct SaveSector *sector = &gSaveDataBuffer;
        ReadFlash(i + 14 * (gSaveCounter & 1), 0, (u8 *)sector, 4096);
        if (sector->id >= 14 || sector->signature != SECTOR_SIGNATURE || sector->counter != gSaveCounter
         || (seen & (1 << sector->id)) || sector->checksum != Checksum(sector->data, sSectionSizes[sector->id])
         || (sector->id + gLastWrittenSector) % 14 != i)
            return FALSE;
        seen |= 1 << sector->id;
    }
    return seen == 0x3FFF;
}

static bool32 ValidPlayerSector(const u8 *raw)
{
    u32 i;
    u32 group = raw[offsetof(struct SaveBlock1, location)];
    u32 number = raw[offsetof(struct SaveBlock1, location) + 1];
    u32 layout = raw[offsetof(struct SaveBlock1, mapLayoutId)] | raw[offsetof(struct SaveBlock1, mapLayoutId) + 1] << 8;
    s16 x = raw[0] | raw[1] << 8;
    s16 y = raw[2] | raw[3] << 8;
    u32 count = raw[offsetof(struct SaveBlock1, playerPartyCount)];
    if (group >= ARRAY_COUNT(sMapCounts) || number >= sMapCounts[group] || !layout || layout > 785
     || x < 0 || y < 0 || x >= gMapLayouts[layout - 1]->width || y >= gMapLayouts[layout - 1]->height || count > PARTY_SIZE)
        return FALSE;
    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        u8 id = raw[offsetof(struct SaveBlock1, objectEvents) + i * sizeof(struct ObjectEvent) + offsetof(struct ObjectEvent, localId)];
        if (id >= 241 && id <= 243)
            return FALSE;
    }
    for (i = 0; i < PARTY_SIZE; i++)
    {
        const u8 *mon = raw + offsetof(struct SaveBlock1, playerParty) + i * sizeof(struct Pokemon);
        u32 key = ReadU32(mon) ^ ReadU32(mon + 4);
        u32 checksum = 0;
        u32 j;
        u32 hp = mon[86] | mon[87] << 8;
        u32 maxHp = mon[88] | mon[89] << 8;
        for (j = 32; j < 80; j += 4)
        {
            u32 word = ReadU32(mon + j) ^ key;
            checksum += (word & 0xFFFF) + (word >> 16);
        }
        if ((checksum & 0xFFFF) != (u32)(mon[28] | mon[29] << 8) || (mon[19] & 1)
         || !!(mon[19] & 2) != (i < count)
         || (i < count && (!mon[84] || mon[84] > 100 || !maxHp || hp > maxHp)))
            return FALSE;
    }
    return TRUE;
}

static u8 ValidateSector(u32 physical)
{
    struct SaveSector *sector = &gSaveDataBuffer;
    u8 *raw = (u8 *)sector;
    u32 bank = physical / 14;
    u32 id, rotation, size;
    u8 compare[128];
    u32 i;
    if (physical >= 28)
    {
        for (i = 0; i < 4096; i += 128)
        {
            ReadFlash(physical, i, compare, 128);
            if (memcmp(compare, raw + i, 128))
                return MP_SAVE_SPECIAL;
        }
        return MP_SAVE_OK;
    }
    if (AllBytes(raw, 4096, 0xFF))
    {
        sBankErased[bank]++;
        return bank == (sSourceCounter & 1) ? MP_SAVE_VALIDATION : MP_SAVE_OK;
    }
    id = sector->id;
    if (id >= 14 || sector->signature != SECTOR_SIGNATURE || (sBankIds[bank] & (1 << id)))
        return MP_SAVE_VALIDATION;
    size = sSectionSizes[id];
    if (sector->checksum != Checksum(raw, size) || !AllBytes(raw + size, 3968 - size, 0)
     || !AllBytes(raw + 3968 + (id < 5 ? 116 : id == 5 ? 20 : 0), id < 5 ? 0 : id == 5 ? 96 : 116, 0))
        return MP_SAVE_VALIDATION;
    if (id < 6)
    {
        u32 chunkSize = id < 5 ? 116 : 20;
        memcpy((u8 *)sImportOwnership[bank] + id * 116, raw + 3968, chunkSize);
    }
    rotation = ((physical % 14) + 14 - id) % 14;
    if (sector->counter % 2 != bank)
        return MP_SAVE_VALIDATION;
    if (sBankIds[bank] && (sBankCounters[bank] != sector->counter || sBankRotations[bank] != rotation))
        return MP_SAVE_VALIDATION;
    sBankCounters[bank] = sector->counter;
    sBankRotations[bank] = rotation;
    sBankIds[bank] |= 1 << id;
    if (id == 0 && ReadU32(raw + offsetof(struct SaveBlock2, playerTrainerId)) != sTrainerId)
        return MP_SAVE_OWNERSHIP;
    if ((id == 0 && (raw[offsetof(struct SaveBlock2, playerGender)] > 1 || !NameValid(raw)))
     || (id == 1 && !ValidPlayerSector(raw)) || (id == 5 && raw[0] >= 14))
        return MP_SAVE_VALIDATION;
    if (bank == (sSourceCounter & 1))
    {
        u32 target;
        if (sector->counter != sSourceCounter || rotation != sSourceRotation)
            return MP_SAVE_VALIDATION;
        target = (id + sTargetRotation) % 14 + 14 * (sTargetCounter & 1);
        sector->counter = sTargetCounter;
        sector->signature = (SECTOR_SIGNATURE & ~0xFF) | 0xFF;
        if (ProgramFlashSectorAndVerify(target, raw))
            return MP_SAVE_FLASH;
    }
    return MP_SAVE_OK;
}

static bool32 ValidBanks(void)
{
    u32 selected = sSourceCounter & 1;
    u32 other = selected ^ 1;
    if (sBankIds[selected] != 0x3FFF || sBankErased[selected]
     || !MultiplayerOwnership_ImportMatches((const u8 *)sImportOwnership[selected]))
        return FALSE;
    if (!sBankErased[other])
    {
        const struct MultiplayerOwnershipRecord *previous = (const void *)((const u8 *)sImportOwnership[other] + 4);
        const struct MultiplayerOwnershipRecord *current = (const void *)((const u8 *)sImportOwnership[selected] + 4);
        if (!MultiplayerOwnership_ValidateSave((const u8 *)sImportOwnership[other], TRUE))
            return FALSE;
        if (!MultiplayerOwnership_IsLegacy((const u8 *)sImportOwnership[other])
         && (memcmp(previous->worldId, current->worldId, 16) || memcmp(previous->playerId, current->playerId, 16)))
            return FALSE;
    }
    if (sBankErased[other] == 14 && !sBankIds[other])
        return TRUE;
    return sBankIds[other] == 0x3FFF && !sBankErased[other]
        && sBankCounters[other] == sSourceCounter - 1
        && sSourceRotation == (sBankRotations[other] + 1) % 14;
}

static void Error(const struct MultiplayerSaveControl *command, u8 status)
{
    if (sState == MP_SAVE_IMPORTING || sState == MP_SAVE_FINALIZING)
        if (!CancelImport())
            status = MP_SAVE_FLASH;
    PublishResponse(command, status, NULL, 0, sOffset);
}

void MultiplayerSave_Init(void)
{
    memset((void *)&gMultiplayerSaveMailbox, 0, sizeof(gMultiplayerSaveMailbox));
    sState = MP_SAVE_UNBOUND; sFrame = 0; sSeenGeneration = 0;
    sIncarnation = 0; sSession = 0; sLastSeq = 0; sPending = FALSE;
    MultiplayerOwnership_InvalidateGrant();
    // Retain the linker-visible ownership marker despite --gc-sections.
    gMultiplayerSaveMailbox.abiVersion = *(const volatile u32 *)&gMultiplayerOwnershipAbi;
    gMultiplayerSaveMailbox.byteSize = 512;
    gMultiplayerSaveMailbox.magic = MULTIPLAYER_SAVE_MAGIC;
}

static void Handle(const struct MultiplayerSaveControl *command)
{
    u8 data[80];
    u8 status = MP_SAVE_OK;
    if (command->status || command->length > 192 || !AllBytes(command->data + command->length, 192 - command->length, 0)
     || command->chunkCrc32 != Crc32(0, command->data, command->length)
     || command->totalSize != MULTIPLAYER_SAVE_FLASH_SIZE || !command->incarnation || !command->sessionEpoch
     || !command->commandSeq || AllBytes(command->requestId, 16, 0))
    {
        PublishResponse(command, MP_SAVE_MALFORMED, NULL, 0, sOffset); return;
    }
    if (command->opcode == MP_SAVE_BIND)
    {
        // A game process retains its binding fence across bridge reconnects.
        // Only the original, still-unprogressed BIND can be replayed verbatim.
        if (command->sessionEpoch < sSession
         || (command->sessionEpoch == sSession && command->incarnation < sIncarnation))
            status = MP_SAVE_SESSION;
        else if (command->sessionEpoch == sSession && command->incarnation == sIncarnation)
        {
            if (sLastSeq == 1 && sLastCommand.opcode == MP_SAVE_BIND
             && !memcmp(command, &sLastCommand, sizeof(*command)))
            {
                struct MultiplayerSaveControl cached = sCachedResponse;
                PublishResponse(&cached, cached.status, cached.data, cached.length, cached.offset);
                return;
            }
            status = MP_SAVE_ORDER;
        }
        else if (MultiplayerSave_IsHeld())
            status = MP_SAVE_BUSY;
        else if (command->length || command->commandSeq != 1 || command->revision || command->offset)
            status = MP_SAVE_MALFORMED;
        else
        {
            sIncarnation = command->incarnation; sSession = command->sessionEpoch;
            sLastSeq = 1; sState = MP_SAVE_BOUND;
            MultiplayerOwnership_InvalidateGrant();
            sLastCommand = *command;
        }
        PublishResponse(command, status, NULL, 0, 0); return;
    }
    if (command->incarnation != sIncarnation || command->sessionEpoch != sSession || sState == MP_SAVE_UNBOUND)
    {
        PublishResponse(command, MP_SAVE_SESSION, NULL, 0, sOffset); return;
    }
    if (command->commandSeq == sLastSeq && !memcmp(command, &sLastCommand, sizeof(*command)))
    {
        if (!sPending)
        {
            struct MultiplayerSaveControl cached = sCachedResponse;
            PublishResponse(&cached, cached.status, cached.data, cached.length, cached.offset);
        }
        return;
    }
    if (command->commandSeq <= sLastSeq || sPending)
    {
        PublishResponse(command, sPending ? MP_SAVE_BUSY : MP_SAVE_ORDER, NULL, 0, sOffset); return;
    }
    if (MultiplayerSave_IsHeld() && (memcmp(command->requestId, sRequestId, 16) || command->revision != sRevision))
    {
        PublishResponse(command, MP_SAVE_SESSION, NULL, 0, sOffset); return;
    }
    sLastSeq = command->commandSeq; sLastCommand = *command;
    sDeadline = sFrame + 600;
    switch (command->opcode)
    {
    case MP_SAVE_BEGIN_EXPORT:
        if (sState != MP_SAVE_BOUND) { status = MP_SAVE_BUSY; break; }
        if (command->length || command->offset) { status = MP_SAVE_MALFORMED; break; }
        if (!FieldReady() || !gFlashMemoryPresent || !MultiplayerOwnership_CanSaveParty()) { status = MP_SAVE_UNSAFE; break; }
        Hold(MP_SAVE_EXPORTING, command);
        status = MultiplayerOwnership_PrepareExport();
        if (status != MP_SAVE_OK) { Release(); break; }
        SaveMapView();
        if (HandleSavingData(SAVE_NORMAL) == SAVE_STATUS_ERROR)
        {
            Release(); status = MP_SAVE_UNSAFE; break;
        }
        if (gDamagedSaveSectors) { Release(); status = MP_SAVE_FLASH; break; }
        PutU32(data, gSaveCounter); PutU32(data + 4, gLastWrittenSector);
        memcpy(data + 8, &gRngValue, 16); memcpy(data + 24, &gRng2Value, 16);
        PublishResponse(command, MP_SAVE_OK, data, 40, 0); return;
    case MP_SAVE_READ:
        if (sState != MP_SAVE_EXPORTING && sState != MP_SAVE_COMMITTED) { status = MP_SAVE_ORDER; break; }
        if (command->length || command->offset != sOffset || sOffset >= 131072 || (sOffset & 127)) { status = MP_SAVE_ORDER; break; }
        ReadFlashBytes(sOffset, sLastResponse.data, 128);
        sCrc = Crc32(sCrc, sLastResponse.data, 128); sOffset += 128;
        // PublishResponse clears its destination; retain read bytes on the stack.
        {
            u8 chunk[128];
            memcpy(chunk, sLastResponse.data, 128);
            PublishResponse(command, MP_SAVE_OK, chunk, 128, command->offset);
        }
        return;
    case MP_SAVE_BEGIN_IMPORT:
        if (sState != MP_SAVE_BOUND) { status = MP_SAVE_BUSY; break; }
        if (command->length != 16 || command->offset) { status = MP_SAVE_MALFORMED; break; }
        if (!FieldReady() || !gFlashMemoryPresent) { status = MP_SAVE_UNSAFE; break; }
        if (!LocalBankValid()) { status = MP_SAVE_VALIDATION; break; }
        sTrainerId = ReadU32(command->data + 12);
        if (sTrainerId != ReadU32(gSaveBlock2Ptr->playerTrainerId)) { status = MP_SAVE_OWNERSHIP; break; }
        sSourceRotation = ReadU32(command->data + 8);
        if (sSourceRotation >= 14) { status = MP_SAVE_MALFORMED; break; }
        Hold(MP_SAVE_IMPORTING, command);
        sExpectedCrc = ReadU32(command->data); sSourceCounter = ReadU32(command->data + 4);
        sTargetCounter = gSaveCounter + 1; sTargetRotation = (gLastWrittenSector + 1) % 14;
        memset(sBankIds, 0, sizeof(sBankIds)); memset(sBankErased, 0, sizeof(sBankErased));
        memset(sImportOwnership, 0, sizeof(sImportOwnership));
        MultiplayerOwnership_InvalidateGrant();
        if (!EraseStaging()) { sState = MP_SAVE_ERROR; status = MP_SAVE_FLASH; break; }
        PutU32(data, sTargetCounter); PutU32(data + 4, sTargetRotation);
        PublishResponse(command, MP_SAVE_OK, data, 8, 0); return;
    case MP_SAVE_WRITE:
        if (sState != MP_SAVE_IMPORTING) { status = MP_SAVE_ORDER; break; }
        if (command->length != 128 || command->offset != sOffset || sOffset >= 131072 || (sOffset & 127)) { status = MP_SAVE_ORDER; break; }
        memcpy((u8 *)&gSaveDataBuffer + (sOffset & 4095), command->data, 128);
        sCrc = Crc32(sCrc, command->data, 128); sOffset += 128;
        if (!(sOffset & 4095))
        {
            status = ValidateSector(sOffset / 4096 - 1);
            if (status != MP_SAVE_OK) { Error(command, status); return; }
        }
        PublishResponse(command, MP_SAVE_OK, NULL, 0, sOffset); return;
    case MP_SAVE_COMMIT:
        if (sState != MP_SAVE_IMPORTING || command->length || sOffset != 131072) { status = MP_SAVE_ORDER; break; }
        if (sCrc != sExpectedCrc || !ValidBanks()) { Error(command, sCrc != sExpectedCrc ? MP_SAVE_CRC : MP_SAVE_VALIDATION); return; }
        sState = MP_SAVE_FINALIZING; sPublishId = 0; sPending = TRUE; return;
    case MP_SAVE_END:
        if ((sState != MP_SAVE_EXPORTING && sState != MP_SAVE_COMMITTED) || command->length || sOffset != 131072) { status = MP_SAVE_ORDER; break; }
        if (sState == MP_SAVE_COMMITTED)
        {
            sPending = TRUE; StartReload(MP_SAVE_OK); return;
        }
        PutU32(data, sCrc); MultiplayerOwnership_Acknowledge(TRUE); Release();
        PublishResponse(command, MP_SAVE_OK, data, 4, sOffset); return;
    case MP_SAVE_CANCEL:
        if (command->length) { status = MP_SAVE_MALFORMED; break; }
        if (sState == MP_SAVE_IMPORTING)
        {
            if (!CancelImport()) { status = MP_SAVE_FLASH; break; }
        }
        else if (sState == MP_SAVE_ERROR)
        {
            if (sCommitted)
            {
                if (LoadGameSave(SAVE_NORMAL) != SAVE_STATUS_OK) { status = MP_SAVE_FLASH; break; }
                sPending = TRUE; StartReload(MP_SAVE_CANCELED); return;
            }
            if (!CancelImport()) { status = MP_SAVE_FLASH; break; }
        }
        else if (sState == MP_SAVE_COMMITTED)
        {
            sPending = TRUE; StartReload(MP_SAVE_CANCELED); return;
        }
        else if (sState == MP_SAVE_EXPORTING)
            Release();
        PublishResponse(command, MP_SAVE_CANCELED, NULL, 0, sOffset); return;
    case MP_SAVE_QUERY_OWNERSHIP:
        if (sState != MP_SAVE_BOUND) { status = MP_SAVE_BUSY; break; }
        if (command->length || command->offset) { status = MP_SAVE_MALFORMED; break; }
        if (!FieldReady()) { status = MP_SAVE_UNSAFE; break; }
        status = MultiplayerOwnership_Query(data);
        if (status == MP_SAVE_OK) { PublishResponse(command, status, data, 80, 0); return; }
        break;
    case MP_SAVE_GRANT_OWNERSHIP:
        if (sState != MP_SAVE_BOUND) { status = MP_SAVE_BUSY; break; }
        if (command->length != 60 || command->offset) { status = MP_SAVE_MALFORMED; break; }
        if (!FieldReady()) { status = MP_SAVE_UNSAFE; break; }
        status = MultiplayerOwnership_Grant(command->data, command->sessionEpoch, command->incarnation);
        if (status == MP_SAVE_OK)
            status = MultiplayerOwnership_Query(data);
        if (status == MP_SAVE_OK) { PublishResponse(command, status, data, 80, 0); return; }
        break;
    case MP_SAVE_QUERY:
        if (command->length) { status = MP_SAVE_MALFORMED; break; }
        PutU32(data, sState); PutU32(data + 4, sOffset); PutU32(data + 8, sCrc); PutU32(data + 12, gSaveCounter);
        memcpy(data + 16, &gRngValue, 16); memcpy(data + 32, &gRng2Value, 16);
        PublishResponse(command, MP_SAVE_OK, data, 48, sOffset); return;
    default:
        status = MP_SAVE_MALFORMED;
    }
    PublishResponse(command, status, NULL, 0, sOffset);
}

void MultiplayerSave_Tick(void)
{
    u32 generation;
    struct MultiplayerSaveControl command;
    u8 data[4];
    sFrame++; gMultiplayerSaveMailbox.frame = sFrame;
    if (sState == MP_SAVE_FINALIZING)
    {
        u32 target = (sPublishId + sTargetRotation) % 14 + 14 * (sTargetCounter & 1);
        if (ProgramFlashByte(target, SECTOR_SIGNATURE_OFFSET, SECTOR_SIGNATURE & 0xFF))
        {
            Error(&sLastCommand, MP_SAVE_FLASH); sPending = FALSE;
        }
        else
        {
            ReadFlash(target, SECTOR_SIGNATURE_OFFSET, data, 4);
            if (ReadU32(data) != SECTOR_SIGNATURE)
            {
                Error(&sLastCommand, MP_SAVE_FLASH); sPending = FALSE;
            }
            else if (++sPublishId == 14)
            {
                sCommitted = TRUE;
                if (LoadGameSave(SAVE_NORMAL) != SAVE_STATUS_OK)
                {
                    sState = MP_SAVE_ERROR; sPending = FALSE;
                    PublishResponse(&sLastCommand, MP_SAVE_FLASH, NULL, 0, sOffset);
                }
                else
                {
                    sState = MP_SAVE_COMMITTED; sPending = FALSE; sOffset = 0; sCrc = 0;
                    sDeadline = sFrame + 600;
                    PublishResponse(&sLastCommand, MP_SAVE_OK, NULL, 0, 0);
                }
            }
        }
    }
    else if (sState == MP_SAVE_RELOADING)
    {
        gMain.newKeys = 0; gMain.heldKeys = 0;
        if (FieldReady())
        {
            PutU32(data, sCrc);
            if (sReloadStatus == MP_SAVE_OK && sLastCommand.opcode == MP_SAVE_END)
                MultiplayerOwnership_Acknowledge(FALSE);
            Release();
            PublishResponse(&sLastCommand, sReloadStatus, sReloadStatus == MP_SAVE_OK ? data : NULL,
                            sReloadStatus == MP_SAVE_OK ? 4 : 0, sOffset);
        }
    }
    else if (MultiplayerSave_IsHeld() && sState != MP_SAVE_ERROR && (s32)(sFrame - sDeadline) > 0)
    {
        if (sState == MP_SAVE_COMMITTED)
        {
            sPending = TRUE; StartReload(MP_SAVE_TIMEOUT);
        }
        else if (sState == MP_SAVE_IMPORTING)
        {
            bool32 cleaned = CancelImport();
            PublishResponse(&sLastCommand, cleaned ? MP_SAVE_TIMEOUT : MP_SAVE_FLASH, NULL, 0, sOffset);
        }
        else
        {
            Release();
            PublishResponse(&sLastCommand, MP_SAVE_TIMEOUT, NULL, 0, sOffset);
        }
    }
    generation = gMultiplayerSaveMailbox.commandGeneration;
    if (!(generation & 1) && generation != sSeenGeneration)
    {
        command = gMultiplayerSaveMailbox.command;
        if (generation == gMultiplayerSaveMailbox.commandGeneration)
        {
            sSeenGeneration = generation;
            Handle(&command);
        }
    }
    gMultiplayerSaveMailbox.state = sState;
}
