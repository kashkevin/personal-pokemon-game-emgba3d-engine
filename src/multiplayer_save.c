#include "global.h"
#include "multiplayer_save.h"
#include "multiplayer_trade_effects.h"
#include "multiplayer.h"
#include "multiplayer_battle.h"
#include "multiplayer_menu.h"
#include "multiplayer_ownership.h"
#include "multiplayer_local_reset.h"
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
#ifdef PORTABLE
#include "platform.h"
#endif
#line 27 // gba-line

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
#ifdef PORTABLE
_Static_assert(sizeof(struct SaveBlock3) == 600 + sizeof(struct MPDaycareEggQueue) + sizeof(struct MPTowerRecord),
               "Ownership SaveBlock3, the Day Care egg queue and the Tower record");
#else
#line 38 // gba-line
_Static_assert(sizeof(struct SaveBlock3) == 600, "Ownership SaveBlock3");
#endif
#line 39 // gba-line
_Static_assert(offsetof(struct SaveBlock1, daycare) == 0x3030, "Pinned daycare offset");
_Static_assert(sizeof(struct DaycareMon) == 140, "Pinned daycare size");
_Static_assert(sizeof(struct PokemonStorage) == 34144, "Pinned storage");
_Static_assert(sizeof(rng_value_t) == 16, "Pinned SFC32");

const u32 gMultiplayerTradeAbiVersion = 1;
// Private record preparation only: never grants effect import/publication.
const u32 gMultiplayerTradeEffectPrepareAbiVersion = 1;
const u32 gMultiplayerTradeEffectSaveAbiVersion = 1;
const u32 gMultiplayerTradeEffectChoiceAbiVersion = 1;
_Static_assert(sizeof(struct MultiplayerTradeRecordPreparation) == 192, "Trade record response ABI");
// Optional read-only 4096-byte export window in the existing sector buffer.
const u32 gMultiplayerSaveWindowAbiVersion = 1;
#ifdef PORTABLE
// A trade's received save, a 4 KiB sector at a time (WRITE_WINDOW): the bridge writes this window, then the command
// naming its CRC. 1024 128-byte WRITEs, one or two frames each, kept the field frozen about 35 s after both players
// confirmed a trade (2026-10-20); 32 sectors take about a second. Only the bridge writes it; the ROM reads it only
// for that command while importing, so gSaveDataBuffer (the game's own save buffer) is never written from outside.
EWRAM_DATA u8 gMultiplayerSaveImportWindow[MULTIPLAYER_SAVE_IMPORT_WINDOW] = {0};
const u32 gMultiplayerSaveImportWindowAbiVersion = 1;
// KNOWN_ISSUES MS-60: QUERY_PARTY answers what a battle preparation will check, so the host can turn a game away at
// Enter, before anything is held, when its party or save counter is not the canonical save's the battle names.
const u32 gMultiplayerSavePartyQueryAbiVersion = 1;
#endif
#line 52 // gba-line
// Opt-in, immutable local preservation. This does not authorize a host commit.
const u32 gMultiplayerEmergencySaveAbiVersion = 2;
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
static EWRAM_DATA bool8 sTradeActive = FALSE;
static EWRAM_DATA bool8 sTradeExpired = FALSE;
static EWRAM_DATA bool8 sTradeImportStarted = FALSE;
static EWRAM_DATA u32 sTradePreparedRevision = 0;
static EWRAM_DATA struct MultiplayerTradeRecordPreparation sEffectRecord = {0};
static EWRAM_DATA u8 sEffectInput[192] = {0};
static EWRAM_DATA bool8 sEffectActive = FALSE;
static EWRAM_DATA u8 sEffectSaveInput[88] = {0};
static EWRAM_DATA u32 sEffectSaveCounter = 0;
static EWRAM_DATA u32 sEffectSaveRotation = 0;
static EWRAM_DATA bool8 sEffectSaveActive = FALSE;
static EWRAM_DATA bool8 sEmergencyActive = FALSE;
static EWRAM_DATA bool8 sEmergencyValid = FALSE;
static EWRAM_DATA u8 sEmergencyStatus = MP_SAVE_OK;
#ifdef PORTABLE
// Update 32: the counter the last imported canonical image names (a trade's post-image keeps its frozen export's) and
// the counter this game wrote it under (one higher). A battle preparation built from that canonical image names the
// first; the game is at the second until it saves again (multiplayer_battle.c PrepareSingles).
static EWRAM_DATA u32 sImportedSourceCounter = 0;
static EWRAM_DATA u32 sImportedTargetCounter = 0;
static EWRAM_DATA bool8 sImportedCounterValid = FALSE;
#endif
#line 99 // gba-line
static const u16 sSectionSizes[14] = {3884, 3968, 3968, 3968, 3664, 3968, 3968, 3968, 3968, 3968, 3968, 3968, 3968, 2400};
// Bounds from this pinned build's map_groups.json and layouts.json.
static const u8 sMapCounts[] = {57,5,5,6,7,8,9,7,7,14,8,17,10,23,13,15,15,2,2,2,3,1,1,1,108,61,89,2,1,13,1,1,3,1,5,123,60,62,4,6,8,10,6,8,20,10,8,2,10,4,2,2,1,1,1,2,2,3,2,3,2,1,1,6,5,5,8,8,5,5,1,1,1,2,1};
extern const struct MapLayout *const gMapLayouts[];

// Generated from reflected IEEE polynomial 0xEDB88320, eight steps per index.
// const places the 256 words in cartridge ROM; no dynamic or EWRAM allocation.
static const u32 sCrc32Table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
    0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
    0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
    0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
    0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
    0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
    0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
    0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
    0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
    0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
    0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
    0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
    0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
    0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
    0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
    0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
    0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
    0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D,
};

static u32 Crc32(u32 crc, const u8 *data, u32 size)
{
    crc ^= 0xFFFFFFFF;
    while (size--)
        crc = (crc >> 8) ^ sCrc32Table[(crc ^ *data++) & 0xFF];
    return crc ^ 0xFFFFFFFF;
}

#if TESTING
u32 MultiplayerSave_TestCrc32(u32 crc, const u8 *data, u32 size)
{
    return Crc32(crc, data, size);
}
#endif

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

static bool32 FieldReadyWithMenu(bool32 admittedMenu)
{
    u32 id = gPlayerAvatar.objectEventId;
    return (admittedMenu || !MultiplayerMenu_IsActive()) && !MultiplayerBattle_IsActive() && gMain.callback1 == CB1_Overworld && gMain.callback2 == CB2_Overworld
        && gSaveBlock1Ptr != NULL && gSaveBlock2Ptr != NULL && gMapHeader.mapLayout != NULL
        && id < OBJECT_EVENTS_COUNT && gObjectEvents[id].active && gObjectEvents[id].isPlayer
        && gObjectEvents[id].spriteId < MAX_SPRITES && gSprites[gObjectEvents[id].spriteId].inUse
        && !gPaletteFade.active && (admittedMenu || !ArePlayerFieldControlsLocked()) && !ScriptContext_IsEnabled()
        && gPlayerAvatar.tileTransitionState == 0 && gPlayerAvatar.runningState == 0
        && !gMain.inBattle && !IsNativeLinkSessionActive()
        && gTrainerHillVBlankCounter == NULL && !GetCurrentTrainerHillMapId()
        && !InBattlePyramid() && !GetSafariZoneFlag()
        && !gSaveBlock2Ptr->frontier.challengeStatus && !gSaveBlock2Ptr->frontier.challengePaused
        && (!B_FLAG_SKY_BATTLE || !FlagGet(B_FLAG_SKY_BATTLE));
}

static bool32 FieldReady(void) { return FieldReadyWithMenu(FALSE); }

bool32 MultiplayerSave_FieldReadyForBattleMenu(u32 session, u32 incarnation)
{
    return !MultiplayerSave_IsHeld() && MultiplayerMenu_CanAdmitBattle(session, incarnation)
        && FieldReadyWithMenu(TRUE);
}

bool32 MultiplayerSave_FieldReady(void)
{
    return !MultiplayerSave_IsHeld() && FieldReady();
}

bool32 MultiplayerSave_HasBinding(void)
{
    return sState != MP_SAVE_UNBOUND && sSession && sIncarnation;
}

bool32 MultiplayerSave_GetTradeMenuBinding(u32 *session, u32 *incarnation)
{
    // Only an exported, unchanged party can be presented for consent. The
    // menu never reads a half-imported result or relaxes the save hold.
    if (sState != MP_SAVE_TRADE_HELD || !sTradeActive || sTradeExpired
     || sTradeImportStarted || !sSession || !sIncarnation)
        return FALSE;
    *session = sSession;
    *incarnation = sIncarnation;
    return TRUE;
}

bool32 MultiplayerSave_GetIdleBinding(u32 *session, u32 *incarnation)
{
    if (sState != MP_SAVE_BOUND || sTradeActive || !sSession || !sIncarnation)
        return FALSE;
    *session = sSession; *incarnation = sIncarnation;
    return TRUE;
}

bool32 MultiplayerSave_CheckBinding(u32 session, u32 incarnation)
{
    return session > sSession || (session == sSession && incarnation >= sIncarnation);
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
    // No ordinary completion, cancellation or watchdog may resume this core.
    // It must close after durable local preservation, then use closed recovery.
    if (sEmergencyActive)
        return;
    gSoftResetDisabled = sSavedResetDisabled;
    sState = MP_SAVE_BOUND;
    sPending = FALSE;
    sTradeActive = FALSE; sTradeExpired = FALSE; sTradeImportStarted = FALSE;
    sEffectActive = FALSE;
    sEffectSaveActive = FALSE;
    memset(sEffectSaveInput, 0, sizeof(sEffectSaveInput));
    memset(&sEffectRecord, 0, sizeof(sEffectRecord));
    memset(sEffectInput, 0, sizeof(sEffectInput));
}

static void Hold(u32 state, const struct MultiplayerSaveControl *command)
{
    if (!MultiplayerSave_IsHeld())
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
    if (sTradeActive)
    {
        sState = MP_SAVE_TRADE_HELD;
        sPending = FALSE;
        sOffset = 0; sCrc = 0;
    }
    else
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

#ifdef PORTABLE
// SaveBlock3 bytes a sector's 116-byte chunk may carry: the 600-byte ownership profile plus the Day Care egg queue (Update 30)
static u32 Block3Used(u32 id)
{
    s32 used = (s32)MP_SAVE_BLOCK_3_BYTES - (s32)(id * 116);

    return used < 0 ? 0 : used > 116 ? 116 : (u32)used;
}
#endif
#line 429 // gba-line
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
#ifdef PORTABLE
     || !AllBytes(raw + 3968 + Block3Used(id), 116 - Block3Used(id), 0))
#else
#line 457 // gba-line
     || !AllBytes(raw + 3968 + (id < 5 ? 116 : id == 5 ? 20 : 0), id < 5 ? 0 : id == 5 ? 96 : 116, 0))
#endif
#line 458 // gba-line
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
    MultiplayerLocalReset_Init();
    memset((void *)&gMultiplayerSaveMailbox, 0, sizeof(gMultiplayerSaveMailbox));
    sState = MP_SAVE_UNBOUND; sFrame = 0; sSeenGeneration = 0;
    sIncarnation = 0; sSession = 0; sLastSeq = 0; sPending = FALSE;
    sTradeActive = FALSE; sTradeExpired = FALSE; sTradeImportStarted = FALSE;
    sTradePreparedRevision = 0;
    sEffectActive = FALSE;
    sEffectSaveActive = FALSE;
    memset(sEffectSaveInput, 0, sizeof(sEffectSaveInput));
    memset(&sEffectRecord, 0, sizeof(sEffectRecord));
    memset(sEffectInput, 0, sizeof(sEffectInput));
    sEmergencyActive = FALSE; sEmergencyValid = FALSE; sEmergencyStatus = MP_SAVE_OK;
    sRevision = 0; sOffset = 0; sCrc = 0; sDeadline = 0; sCommitted = FALSE;
    memset(sRequestId, 0, sizeof(sRequestId));
    // Read the capability through volatile storage so section GC retains it.
    (void)*(const volatile u32 *)&gMultiplayerTradeAbiVersion;
    (void)*(const volatile u32 *)&gMultiplayerTradeEffectPrepareAbiVersion;
    (void)*(const volatile u32 *)&gMultiplayerTradeEffectSaveAbiVersion;
    (void)*(const volatile u32 *)&gMultiplayerTradeEffectChoiceAbiVersion;
    (void)*(const volatile u32 *)&gMultiplayerSaveWindowAbiVersion;
#ifdef PORTABLE
    (void)*(const volatile u32 *)&gMultiplayerSaveImportWindowAbiVersion;
    (void)*(const volatile u32 *)&gMultiplayerSavePartyQueryAbiVersion;
#endif
#line 545 // gba-line
    (void)*(const volatile u32 *)&gMultiplayerEmergencySaveAbiVersion;
    MultiplayerOwnership_InvalidateGrant();
    // Retain the linker-visible ownership marker despite --gc-sections.
    gMultiplayerSaveMailbox.abiVersion = *(const volatile u32 *)&gMultiplayerOwnershipAbi;
    gMultiplayerSaveMailbox.byteSize = 512;
    gMultiplayerSaveMailbox.magic = MULTIPLAYER_SAVE_MAGIC;
}

static void QueryData(u8 *data)
{
    PutU32(data, sState); PutU32(data + 4, sOffset); PutU32(data + 8, sCrc); PutU32(data + 12, gSaveCounter);
    memcpy(data + 16, &gRngValue, 16); memcpy(data + 32, &gRng2Value, 16);
}

static void QueryTradeData(u8 *data)
{
    u32 flags = sTradeActive | sTradeExpired << 1 | sTradeImportStarted << 2;
    PutU32(data, flags); PutU32(data + 4, sState);
    if (sTradeActive)
        memcpy(data + 8, sRequestId, 16);
    else
        memset(data + 8, 0, 16);
    PutU32(data + 24, sSession); PutU32(data + 28, sIncarnation); PutU32(data + 32, sRevision);
    PutU32(data + 36, sOffset); PutU32(data + 40, sCrc);
    PutU32(data + 44, sTradeActive && !sTradeExpired ? max(0, (s32)(sDeadline - sFrame)) : 0);
}

static void QueryEmergencyData(u8 *data)
{
    memset(data, 0, 48);
    PutU32(data + 4, sState);
    if (!sEmergencyActive)
        return;
    PutU32(data, 1 | sEmergencyValid << 1 | (sState == MP_SAVE_EMERGENCY_PRESERVED) << 2);
    memcpy(data + 8, sRequestId, 16);
    PutU32(data + 24, sSession); PutU32(data + 28, sIncarnation); PutU32(data + 32, sRevision);
    PutU32(data + 36, sOffset); PutU32(data + 40, sCrc); PutU32(data + 44, sEmergencyStatus);
}

#ifdef PORTABLE
// KNOWN_ISSUES MS-60: what PREPARE (multiplayer_battle.c) compares with a preparation the host builds from the
// canonical save: the save counter, the counter PREPARE also takes right after a trade's import (else the same), the
// trainer id, the party's count and CRC. An observation like QUERY_EMERGENCY: it never rebinds, consumes a sequence or
// displaces a lost ACK, and it is answered while the Players menu holds a battle admission (the moment it is asked).
static void QueryParty(const struct MultiplayerSaveControl *command)
{
    struct MultiplayerSaveControl cached = sCachedResponse;
    u8 data[20];
    u8 status = MP_SAVE_OK;

    if (command->status || command->length || command->offset || !AllBytes(command->data, 192, 0)
     || command->chunkCrc32 != Crc32(0, command->data, 0) || command->totalSize != MULTIPLAYER_SAVE_FLASH_SIZE
     || !command->incarnation || !command->sessionEpoch || !command->commandSeq || AllBytes(command->requestId, 16, 0))
        status = MP_SAVE_MALFORMED;
    else if (sState == MP_SAVE_UNBOUND || command->incarnation != sIncarnation || command->sessionEpoch != sSession)
        status = MP_SAVE_SESSION;
    else if (sState != MP_SAVE_BOUND || sEmergencyActive || MultiplayerBattle_IsActive())
        status = MP_SAVE_BUSY;
    if (status == MP_SAVE_OK)
    {
        PutU32(data, gSaveCounter);
        PutU32(data + 4, MultiplayerSave_IsImportedCounter(sImportedSourceCounter) ? sImportedSourceCounter : gSaveCounter);
        PutU32(data + 8, READ_OTID_FROM_SAVE);
        PutU32(data + 12, gPartiesCount[B_TRAINER_PLAYER]);
        PutU32(data + 16, MultiplayerBattle_PartyCrc32());
        PublishResponse(command, MP_SAVE_OK, data, 20, sOffset);
    }
    else
    {
        PublishResponse(command, status, NULL, 0, sOffset);
    }
    sCachedResponse = cached;
}
#endif
#line 584 // gba-line
static void Handle(const struct MultiplayerSaveControl *command)
{
    u8 data[80];
    u8 status = MP_SAVE_OK;
#ifdef PORTABLE
    if (command->opcode == MP_SAVE_QUERY_PARTY)
    {
        QueryParty(command);
        return;
    }
#endif
#line 588 // gba-line
    // The diagnostic channel owns the field hold. Neither rebinding nor a
    // save cancellation may invalidate its ownership or release that hold.
    // Neutral cancellation keeps the field frozen through fresh-saver arm.
    // Permit only ownership arm/observation and emergency preservation here;
    // ordinary export/import/trade cannot race the final authenticated HANDOFF.
    if (MultiplayerBattle_IsAdmissionCleanupHeld()
     && command->opcode != MP_SAVE_BIND && command->opcode != MP_SAVE_QUERY
     && command->opcode != MP_SAVE_QUERY_OWNERSHIP && command->opcode != MP_SAVE_GRANT_OWNERSHIP
     && command->opcode != MP_SAVE_QUERY_TRADE && command->opcode != MP_SAVE_QUERY_EMERGENCY
     && command->opcode != MP_SAVE_BEGIN_EMERGENCY_EXPORT && command->opcode != MP_SAVE_READ_WINDOW
     && !(command->opcode == MP_SAVE_READ && sEmergencyActive)
     && command->opcode != MP_SAVE_PRESERVE_EMERGENCY)
    {
        struct MultiplayerSaveControl cached = sCachedResponse;
        PublishResponse(command, MP_SAVE_BUSY, NULL, 0, sOffset);
        sCachedResponse = cached;
        return;
    }
    if ((MultiplayerBattle_IsActive() || (MultiplayerMenu_IsBattleAdmissionHeld()
         && command->opcode != MP_SAVE_BEGIN_EMERGENCY_EXPORT))
     && command->opcode != MP_SAVE_QUERY && command->opcode != MP_SAVE_QUERY_TRADE
     && command->opcode != MP_SAVE_QUERY_EMERGENCY)
    {
        struct MultiplayerSaveControl cached = sCachedResponse;
        PublishResponse(command, MP_SAVE_BUSY, NULL, 0, sOffset);
        sCachedResponse = cached;
        return;
    }
    if (command->status || command->length > 192 || !AllBytes(command->data + command->length, 192 - command->length, 0)
     || command->chunkCrc32 != Crc32(0, command->data, command->length)
     || command->totalSize != MULTIPLAYER_SAVE_FLASH_SIZE || !command->incarnation || !command->sessionEpoch
     || !command->commandSeq || AllBytes(command->requestId, 16, 0))
    {
        PublishResponse(command, MP_SAVE_MALFORMED, NULL, 0, sOffset); return;
    }
    if (command->opcode == MP_SAVE_QUERY_EMERGENCY)
    {
        struct MultiplayerSaveControl cached = sCachedResponse;
        // Observation cannot rebind, consume sequence or displace a lost ACK.
        if (sState == MP_SAVE_UNBOUND || command->incarnation != sIncarnation || command->sessionEpoch != sSession
         || (sEmergencyActive && (memcmp(command->requestId, sRequestId, 16) || command->revision != sRevision)))
            status = MP_SAVE_SESSION;
        else if (command->length || command->offset)
            status = MP_SAVE_MALFORMED;
        else
        {
            QueryEmergencyData(data);
            PublishResponse(command, MP_SAVE_OK, data, 48, sOffset);
            sCachedResponse = cached;
            return;
        }
        PublishResponse(command, status, NULL, 0, sOffset);
        sCachedResponse = cached;
        return;
    }
    if (sEmergencyActive && command->opcode != MP_SAVE_BEGIN_EMERGENCY_EXPORT
     && command->opcode != MP_SAVE_PROMOTE_EMERGENCY_EXPORT
     && command->opcode != MP_SAVE_READ && command->opcode != MP_SAVE_READ_WINDOW
     && command->opcode != MP_SAVE_PRESERVE_EMERGENCY)
    {
        // Preserve the original binding, retirement entries, Flash and replay.
        struct MultiplayerSaveControl cached = sCachedResponse;
        PublishResponse(command, MP_SAVE_BUSY, NULL, 0, sOffset);
        sCachedResponse = cached;
        return;
    }
    if (command->opcode == MP_SAVE_QUERY_TRADE)
    {
        // Recovery can observe a lost revision-transition ACK using its newer
        // authenticated lease. Observation never changes the binding or cache.
        if (sState == MP_SAVE_UNBOUND || command->incarnation != sIncarnation || command->sessionEpoch < sSession
         || (sTradeActive && memcmp(command->requestId, sRequestId, 16)))
            status = MP_SAVE_SESSION;
        else if (command->length || command->offset)
            status = MP_SAVE_MALFORMED;
        else if (sPending)
            status = MP_SAVE_BUSY;
        else
        {
            QueryTradeData(data);
            PublishResponse(command, MP_SAVE_OK, data, 48, sOffset); return;
        }
        PublishResponse(command, status, NULL, 0, sOffset); return;
    }
    if (command->opcode == MP_SAVE_BIND)
    {
        // A game process retains its binding fence across bridge reconnects.
        // Only the original, still-unprogressed BIND can be replayed verbatim.
        if (!MultiplayerBattle_CheckBinding(command->sessionEpoch, command->incarnation) || command->sessionEpoch < sSession
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
    if (command->opcode == MP_SAVE_REBIND_TRADE)
    {
        if (!sTradeActive || command->incarnation != sIncarnation || command->sessionEpoch < sSession
         || memcmp(command->requestId, sRequestId, 16) || command->revision != sRevision)
            status = MP_SAVE_SESSION;
        else if (command->sessionEpoch == sSession)
        {
            if (sLastSeq == 1 && sLastCommand.opcode == MP_SAVE_REBIND_TRADE
             && !memcmp(command, &sLastCommand, sizeof(*command)))
            {
                struct MultiplayerSaveControl cached = sCachedResponse;
                PublishResponse(&cached, cached.status, cached.data, cached.length, cached.offset);
                return;
            }
            status = MP_SAVE_ORDER;
        }
        else if (sPending)
            status = MP_SAVE_BUSY;
        else if (command->length || command->offset || command->commandSeq != 1)
            status = MP_SAVE_MALFORMED;
        else
        {
            sSession = command->sessionEpoch; sLastSeq = 1; sLastCommand = *command;
            sTradeExpired = FALSE; sDeadline = sFrame + 600;
            MultiplayerOwnership_InvalidateGrant();
            QueryData(data);
            PublishResponse(command, MP_SAVE_OK, data, 48, sOffset); return;
        }
        PublishResponse(command, status, NULL, 0, sOffset); return;
    }
    if (command->incarnation != sIncarnation || command->sessionEpoch != sSession || sState == MP_SAVE_UNBOUND)
    {
        PublishResponse(command, MP_SAVE_SESSION, NULL, 0, sOffset); return;
    }
    if (command->commandSeq == sLastSeq && !memcmp(command, &sLastCommand, sizeof(*command)))
    {
        // The sector buffer is only immutable while this export is held.
        // A watchdog/cancel may have released it since a lost reply.
        if (command->opcode == MP_SAVE_READ_WINDOW
         && sState != MP_SAVE_EXPORTING && sState != MP_SAVE_COMMITTED)
        {
            PublishResponse(command, MP_SAVE_ORDER, NULL, 0, sOffset); return;
        }
        if (command->opcode == MP_SAVE_READ_EFFECT_WINDOW
         && (!sEffectActive || !sEffectSaveActive || sTradeExpired || sTradeImportStarted
          || sState != MP_SAVE_TRADE_HELD || gSaveCounter != sEffectSaveCounter
          || gLastWrittenSector != sEffectSaveRotation
          || (sCachedResponse.status == MP_SAVE_OK
           && ReadU32(sCachedResponse.data) != Crc32(0, (const u8 *)&gSaveDataBuffer, 4096))))
        {
            PublishResponse(command, MP_SAVE_ORDER, NULL, 0, sOffset); return;
        }
        if (command->opcode == MP_SAVE_QUERY_EFFECT_CHOICE
         && (!sTradeActive || !sEffectActive || sTradeExpired || sTradeImportStarted
          || sState != MP_SAVE_TRADE_HELD
          || (sCachedResponse.status == MP_SAVE_OK
           && ReadU32(sCachedResponse.data + 12) != Crc32(0, (const u8 *)&sEffectRecord, sizeof(sEffectRecord)))))
        {
            PublishResponse(command, MP_SAVE_ORDER, NULL, 0, sOffset); return;
        }
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
    if (MultiplayerSave_IsHeld() && (memcmp(command->requestId, sRequestId, 16)
     || (command->revision != sRevision && !(sTradeActive && !sTradeImportStarted
       && command->opcode == MP_SAVE_BEGIN_IMPORT && sRevision != 0xFFFFFFFF && command->revision == sRevision + 1))))
    {
        PublishResponse(command, MP_SAVE_SESSION, NULL, 0, sOffset); return;
    }
    if (sTradeActive && sTradeExpired && command->opcode != MP_SAVE_KEEP_TRADE
     && command->opcode != MP_SAVE_QUERY_TRADE && command->opcode != MP_SAVE_QUERY)
    {
        PublishResponse(command, MP_SAVE_TIMEOUT, NULL, 0, sOffset); return;
    }
    if (command->opcode == MP_SAVE_BEGIN_EMERGENCY_EXPORT && sState == MP_SAVE_BOUND
     && !sTradeActive && !sEmergencyActive && MultiplayerMenu_IsBattleAdmissionHeld()
     && !command->length && !command->offset && command->revision && command->revision <= 0x7FFFFFFF)
    {
        // Enter already owns an immutable field hold before IPC. Preserve all
        // save sequence/cache/grant/export/RNG state so exact ADMIT can consume
        // that hold after its durable provisional identity has been published.
        struct MultiplayerSaveControl cached = sCachedResponse;
        PublishResponse(command, MP_SAVE_BATTLE_ADMISSION, NULL, 0, sOffset);
        sCachedResponse = cached;
        return;
    }
    sLastSeq = command->commandSeq; sLastCommand = *command;
    if (!sTradeActive || (command->opcode != MP_SAVE_QUERY && command->opcode != MP_SAVE_QUERY_TRADE))
        sDeadline = sFrame + 600;
    switch (command->opcode)
    {
    case MP_SAVE_PROMOTE_EMERGENCY_EXPORT:
        if (command->length || command->offset || !command->revision || command->revision > 0x7FFFFFFF)
            { status = MP_SAVE_MALFORMED; break; }
        if (sTradeActive || (sState != MP_SAVE_EXPORTING && sState != MP_SAVE_BOUND))
            { status = MP_SAVE_BUSY; break; }
        if (sState == MP_SAVE_BOUND)
        {
            // A lost END or an expired ordinary watchdog cannot authorize a
            // second snapshot under the old immutable transaction identity.
            Hold(MP_SAVE_EMERGENCY_UNSAVED, command);
            sEmergencyActive = TRUE; sEmergencyValid = FALSE;
            sEmergencyStatus = MP_SAVE_ORDER; status = MP_SAVE_ORDER;
            break;
        }
        // UUID/revision/binding were checked above. Rewind only the reader;
        // retain this exact frozen Flash, ownership retirements and RNG.
        sEmergencyActive = TRUE; sEmergencyValid = TRUE;
        sEmergencyStatus = MP_SAVE_OK; sOffset = 0; sCrc = 0;
        PutU32(data, gSaveCounter); PutU32(data + 4, gLastWrittenSector);
        memcpy(data + 8, &gRngValue, 16); memcpy(data + 24, &gRng2Value, 16);
        PublishResponse(command, MP_SAVE_OK, data, 40, 0); return;
    case MP_SAVE_BEGIN_EMERGENCY_EXPORT:
        if (sState != MP_SAVE_BOUND || sTradeActive) { status = MP_SAVE_BUSY; break; }
        if (command->length || command->offset || !command->revision || command->revision > 0x7FFFFFFF)
            { status = MP_SAVE_MALFORMED; break; }
        // An authenticated same-core request freezes even an unsafe field.
        // Never serialize battle/menu/transition state or pretend it was saved.
        status = FieldReady() && gFlashMemoryPresent && MultiplayerOwnership_CanSaveParty() ? MP_SAVE_OK : MP_SAVE_UNSAFE;
        Hold(MP_SAVE_EMERGENCY_UNSAVED, command);
        sEmergencyActive = TRUE;
        sEmergencyValid = FALSE;
        if (status == MP_SAVE_OK)
            status = MultiplayerOwnership_PrepareExport();
        if (status == MP_SAVE_OK)
        {
            SaveMapView();
            if (HandleSavingData(SAVE_NORMAL) == SAVE_STATUS_ERROR || gDamagedSaveSectors)
                status = MP_SAVE_FLASH;
        }
        sEmergencyStatus = status;
        if (status != MP_SAVE_OK)
            break;
        sEmergencyValid = TRUE;
        sState = MP_SAVE_EXPORTING;
        PutU32(data, gSaveCounter); PutU32(data + 4, gLastWrittenSector);
        memcpy(data + 8, &gRngValue, 16); memcpy(data + 24, &gRng2Value, 16);
        PublishResponse(command, MP_SAVE_OK, data, 40, 0); return;
    case MP_SAVE_PRESERVE_EMERGENCY:
        if (!sEmergencyActive || !sEmergencyValid
         || (sState != MP_SAVE_EXPORTING && sState != MP_SAVE_EMERGENCY_PRESERVED)
         || command->length || command->offset != 131072 || sOffset != 131072)
            { status = MP_SAVE_ORDER; break; }
        // Caller must durably record bytes and intent before this command.
        // This CRC receipt proves the stream only, never host acceptance.
        sState = MP_SAVE_EMERGENCY_PRESERVED;
        PutU32(data, sCrc);
        PublishResponse(command, MP_SAVE_OK, data, 4, sOffset); return;
    case MP_SAVE_BEGIN_TRADE_EXPORT:
        if (!command->revision || command->revision >= 0x7FFFFFFF) { status = MP_SAVE_ORDER; break; }
        if (sTradeActive && !sTradeImportStarted && (sState == MP_SAVE_EXPORTING || sState == MP_SAVE_TRADE_HELD))
        {
            if (command->length || command->offset) { status = MP_SAVE_MALFORMED; break; }
            // Restart a lost read buffer from the same frozen Flash snapshot.
            // Never save again, mint again, or change the prepared revision.
            sState = MP_SAVE_EXPORTING; sOffset = 0; sCrc = 0;
            PutU32(data, gSaveCounter); PutU32(data + 4, gLastWrittenSector);
            memcpy(data + 8, &gRngValue, 16); memcpy(data + 24, &gRng2Value, 16);
            PublishResponse(command, MP_SAVE_OK, data, 40, 0); return;
        }
        // A fresh trade uses the same native save admission as ordinary export.
        __attribute__((fallthrough));
    case MP_SAVE_BEGIN_EXPORT:
        if (sState != MP_SAVE_BOUND) { status = MP_SAVE_BUSY; break; }
        if (command->length || command->offset) { status = MP_SAVE_MALFORMED; break; }
        if (!FieldReady() || !gFlashMemoryPresent || !MultiplayerOwnership_CanSaveParty()) { status = MP_SAVE_UNSAFE; break; }
        Hold(MP_SAVE_EXPORTING, command);
        sTradeActive = command->opcode == MP_SAVE_BEGIN_TRADE_EXPORT;
        sTradePreparedRevision = command->revision;
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
    case MP_SAVE_READ_WINDOW:
        if (sState != MP_SAVE_EXPORTING && sState != MP_SAVE_COMMITTED) { status = MP_SAVE_ORDER; break; }
        if (command->length || command->offset != sOffset || sOffset >= 131072 || (sOffset & 4095))
            { status = MP_SAVE_ORDER; break; }
        ReadFlashBytes(sOffset, (u8 *)&gSaveDataBuffer, 4096);
        PutU32(data, Crc32(0, (u8 *)&gSaveDataBuffer, 4096));
        sCrc = Crc32(sCrc, (u8 *)&gSaveDataBuffer, 4096); sOffset += 4096;
        PublishResponse(command, MP_SAVE_OK, data, 4, command->offset);
        return;
    case MP_SAVE_BEGIN_IMPORT:
        if (sState != MP_SAVE_BOUND && !(sTradeActive && (sState == MP_SAVE_TRADE_HELD
         || (sTradeImportStarted && (sState == MP_SAVE_IMPORTING || sState == MP_SAVE_COMMITTED))))) { status = MP_SAVE_BUSY; break; }
        if (command->length != 16 || command->offset) { status = MP_SAVE_MALFORMED; break; }
        if ((!sTradeActive && !FieldReady()) || !gFlashMemoryPresent) { status = MP_SAVE_UNSAFE; break; }
        if (sTradeActive && (sTradePreparedRevision == 0xFFFFFFFF || command->revision != sTradePreparedRevision + 1))
            { status = MP_SAVE_ORDER; break; }
        if (sTradeActive && sTradeImportStarted && (ReadU32(command->data) != sExpectedCrc
         || ReadU32(command->data + 4) != sSourceCounter || ReadU32(command->data + 8) != sSourceRotation
         || ReadU32(command->data + 12) != sTrainerId)) { status = MP_SAVE_ORDER; break; }
        if (!LocalBankValid()) { status = MP_SAVE_VALIDATION; break; }
        sTrainerId = ReadU32(command->data + 12);
        if (sTrainerId != ReadU32(gSaveBlock2Ptr->playerTrainerId)) { status = MP_SAVE_OWNERSHIP; break; }
        sSourceRotation = ReadU32(command->data + 8);
        if (sSourceRotation >= 14) { status = MP_SAVE_MALFORMED; break; }
        Hold(MP_SAVE_IMPORTING, command);
        if (sTradeActive)
            sTradeImportStarted = TRUE;
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
#ifdef PORTABLE
    case MP_SAVE_WRITE_WINDOW:
        // The same staging as 32 WRITEs of this sector: the same order, buffer, running CRC and sector validation.
        if (sState != MP_SAVE_IMPORTING) { status = MP_SAVE_ORDER; break; }
        if (command->length != 4 || command->offset != sOffset || sOffset >= 131072 || (sOffset & 4095)) { status = MP_SAVE_ORDER; break; }
        if (Crc32(0, gMultiplayerSaveImportWindow, MULTIPLAYER_SAVE_IMPORT_WINDOW) != ReadU32(command->data)) { status = MP_SAVE_CRC; break; }
        memcpy((u8 *)&gSaveDataBuffer, gMultiplayerSaveImportWindow, MULTIPLAYER_SAVE_IMPORT_WINDOW);
        sCrc = Crc32(sCrc, gMultiplayerSaveImportWindow, MULTIPLAYER_SAVE_IMPORT_WINDOW); sOffset += 4096;
        status = ValidateSector(sOffset / 4096 - 1);
        if (status != MP_SAVE_OK) { Error(command, status); return; }
        PublishResponse(command, MP_SAVE_OK, NULL, 0, sOffset); return;
#endif
#line 949 // gba-line
    case MP_SAVE_COMMIT:
        if (sState != MP_SAVE_IMPORTING || command->length || sOffset != 131072) { status = MP_SAVE_ORDER; break; }
        if (sCrc != sExpectedCrc || !ValidBanks()) { Error(command, sCrc != sExpectedCrc ? MP_SAVE_CRC : MP_SAVE_VALIDATION); return; }
        sState = MP_SAVE_FINALIZING; sPublishId = 0; sPending = TRUE; return;
    case MP_SAVE_END:
        if ((sState != MP_SAVE_EXPORTING && sState != MP_SAVE_COMMITTED) || command->length || sOffset != 131072) { status = MP_SAVE_ORDER; break; }
        if (sState == MP_SAVE_COMMITTED)
        {
            if (sTradeActive) { status = MP_SAVE_ORDER; break; }
            sPending = TRUE; StartReload(MP_SAVE_OK); return;
        }
        PutU32(data, sCrc); MultiplayerOwnership_Acknowledge(TRUE);
        if (sTradeActive)
            sState = MP_SAVE_TRADE_HELD;
        else
            Release();
        PublishResponse(command, MP_SAVE_OK, data, 4, sOffset); return;
    case MP_SAVE_CANCEL:
        if (sTradeActive) { status = MP_SAVE_ORDER; break; }
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
        QueryData(data);
        PublishResponse(command, MP_SAVE_OK, data, 48, sOffset); return;
    case MP_SAVE_KEEP_TRADE:
        if (!sTradeActive) { status = MP_SAVE_ORDER; break; }
        if (command->length || command->offset) { status = MP_SAVE_MALFORMED; break; }
        sTradeExpired = FALSE;
        QueryData(data);
        PublishResponse(command, MP_SAVE_OK, data, 48, sOffset); return;
    case MP_SAVE_ABORT_TRADE:
        if (!sTradeActive || sTradeImportStarted || (sState != MP_SAVE_EXPORTING && sState != MP_SAVE_TRADE_HELD))
            { status = MP_SAVE_ORDER; break; }
        if (command->length || command->offset) { status = MP_SAVE_MALFORMED; break; }
        Release();
        PublishResponse(command, MP_SAVE_OK, NULL, 0, sOffset); return;
    case MP_SAVE_BEGIN_EFFECT_RECORD:
    case MP_SAVE_CHOOSE_EFFECT_MOVE:
    case MP_SAVE_QUERY_EFFECT_RECORD:
    case MP_SAVE_DISCARD_EFFECT_RECORD:
    case MP_SAVE_QUERY_EFFECT_CHOICE:
        // The original frozen export and live party still belong to this hold.
        // No path here changes a party, Flash, save counter or hold ownership.
        if (!sTradeActive || sTradeImportStarted || sState != MP_SAVE_TRADE_HELD
         || sOffset != MULTIPLAYER_SAVE_FLASH_SIZE || command->revision != sTradePreparedRevision)
            { status = MP_SAVE_ORDER; break; }
        if (command->offset || command->totalSize != MULTIPLAYER_SAVE_FLASH_SIZE
         || command->length != (command->opcode == MP_SAVE_BEGIN_EFFECT_RECORD ? 192
                              : command->opcode == MP_SAVE_CHOOSE_EFFECT_MOVE ? 56 : 48))
            { status = MP_SAVE_MALFORMED; break; }
        if (AllBytes(command->data, 16, 0) || AllBytes(command->data + 16, 32, 0))
            { status = MP_SAVE_MALFORMED; break; }
        if (command->opcode == MP_SAVE_BEGIN_EFFECT_RECORD)
        {
            u32 i, slot = ReadU32(command->data + 80);
            u8 generation = 0, pair = 0, source = 0;
            for (i = 0; i < 16; i++) generation |= command->data[i];
            for (i = 16; i < 48; i++) pair |= command->data[i];
            for (i = 48; i < 80; i++) source |= command->data[i];
            if (!generation || !pair || !source || slot >= gPlayerPartyCount || slot >= PARTY_SIZE
             || (ReadU32(command->data + 84) & ~(MP_TRADE_RESOLVED_FRIENDSHIP_MASK | MP_TRADE_RESOLVED_LUCKY))
             || ReadU32(command->data + 188) != sCrc)
                { status = MP_SAVE_VALIDATION; break; }
            if (sEffectActive)
            {
                if (memcmp(sEffectInput, command->data, sizeof(sEffectInput)))
                    { status = MP_SAVE_ORDER; break; }
            }
            else
            {
                if (!MultiplayerTradeEffects_BeginRecord((const struct Pokemon *)(command->data + 88),
                    &gPlayerParty[slot], ReadU32(command->data + 84), &sEffectRecord))
                    { status = MP_SAVE_VALIDATION; break; }
                memcpy(sEffectInput, command->data, sizeof(sEffectInput));
                sEffectActive = TRUE;
            }
        }
        else
        {
            if (memcmp(sEffectInput, command->data, 48)
             || (!sEffectActive && command->opcode != MP_SAVE_DISCARD_EFFECT_RECORD))
                { status = MP_SAVE_ORDER; break; }
            if (command->opcode == MP_SAVE_DISCARD_EFFECT_RECORD)
            {
                sEffectActive = FALSE;
                sEffectSaveActive = FALSE;
                memset(sEffectSaveInput, 0, sizeof(sEffectSaveInput));
                memset(&sEffectRecord, 0, sizeof(sEffectRecord));
                // Retain only the exact input/context until Release so a lost
                // discard receipt can be retried, including after REBIND.
                PublishResponse(command, MP_SAVE_OK, NULL, 0, 0); return;
            }
            if (command->opcode == MP_SAVE_CHOOSE_EFFECT_MOVE
             && (command->data[55] || !MultiplayerTradeEffects_ChooseMove(&sEffectRecord,
                 ReadU32(command->data + 48), command->data[52] | command->data[53] << 8,
                 command->data[54])))
                { status = MP_SAVE_VALIDATION; break; }
        }
        if (command->opcode == MP_SAVE_QUERY_EFFECT_CHOICE)
        {
            struct MultiplayerTradeChoiceView view;
            if (!MultiplayerTradeEffects_DescribeRecord(&sEffectRecord, &view))
                { status = MP_SAVE_VALIDATION; break; }
            view.recordCrc = Crc32(0, (const u8 *)&sEffectRecord, sizeof(sEffectRecord));
            PublishResponse(command, MP_SAVE_OK, (const u8 *)&view, sizeof(view), 0); return;
        }
        PublishResponse(command, MP_SAVE_OK, (const u8 *)&sEffectRecord, sizeof(sEffectRecord), 0); return;
    case MP_SAVE_BEGIN_EFFECT_SAVE:
    case MP_SAVE_READ_EFFECT_WINDOW:
        if (!sTradeActive || sTradeImportStarted || sState != MP_SAVE_TRADE_HELD
         || sOffset != MULTIPLAYER_SAVE_FLASH_SIZE || command->revision != sTradePreparedRevision
         || !sEffectActive || sEffectRecord.state != MP_TRADE_RECORD_READY)
            { status = MP_SAVE_ORDER; break; }
        if (command->totalSize != MULTIPLAYER_SAVE_FLASH_SIZE
         || command->length != (command->opcode == MP_SAVE_BEGIN_EFFECT_SAVE ? 88 : 48)
         || memcmp(sEffectInput, command->data, 48))
            { status = MP_SAVE_MALFORMED; break; }
        if (command->opcode == MP_SAVE_BEGIN_EFFECT_SAVE)
        {
            if (command->offset || command->data[87] || AllBytes(command->data + 48, 32, 0))
                { status = MP_SAVE_MALFORMED; break; }
            if (sEffectSaveActive)
            {
                if (memcmp(sEffectSaveInput, command->data, sizeof(sEffectSaveInput))
                 || gSaveCounter != sEffectSaveCounter || gLastWrittenSector != sEffectSaveRotation)
                    { status = MP_SAVE_ORDER; break; }
            }
            else
            {
                if (!LocalBankValid()) { status = MP_SAVE_VALIDATION; break; }
                memcpy(sEffectSaveInput, command->data, sizeof(sEffectSaveInput));
                sEffectSaveCounter = gSaveCounter;
                sEffectSaveRotation = gLastWrittenSector;
                sEffectSaveActive = TRUE;
            }
            PutU32(data, sEffectSaveCounter); PutU32(data + 4, sEffectSaveRotation);
            PublishResponse(command, MP_SAVE_OK, data, 8, 0); return;
        }
        if (!sEffectSaveActive || gSaveCounter != sEffectSaveCounter || gLastWrittenSector != sEffectSaveRotation
         || command->offset >= MULTIPLAYER_SAVE_FLASH_SIZE || (command->offset & 4095))
            { status = MP_SAVE_ORDER; break; }
        {
            u32 sector = command->offset / 4096;
            struct Pokemon incoming;
            memcpy(&incoming, sEffectInput + 88, sizeof(incoming));
            ReadFlashBytes(command->offset, (u8 *)&gSaveDataBuffer, 4096);
            if (sector / 14 == (sEffectSaveCounter & 1) && sector < 28)
            {
                u32 id = gSaveDataBuffer.id;
                if (id >= 14 || gSaveDataBuffer.signature != SECTOR_SIGNATURE
                 || gSaveDataBuffer.counter != sEffectSaveCounter
                 || (id + sEffectSaveRotation) % 14 != sector % 14
                 || gSaveDataBuffer.checksum != Checksum(gSaveDataBuffer.data, sSectionSizes[id]))
                    { status = MP_SAVE_VALIDATION; break; }
                if (id <= 4)
                {
                    if (!MultiplayerTradeEffects_OverlaySaveSlice(gSaveBlock1Ptr, gSaveBlock2Ptr,
                        &incoming, &sEffectRecord, sEffectSaveInput + 80,
                        ReadU32(sEffectInput + 80), id == 0 ? 2 : 1,
                        id == 0 ? 0 : (id - 1) * 3968, sSectionSizes[id], gSaveDataBuffer.data))
                        { status = MP_SAVE_VALIDATION; break; }
                    gSaveDataBuffer.checksum = Checksum(gSaveDataBuffer.data, sSectionSizes[id]);
                }
            }
        }
        // Read-only candidate window. Original export offset/CRC and Flash
        // remain unchanged; repeated windows never increment a counter twice.
        PutU32(data, Crc32(0, (const u8 *)&gSaveDataBuffer, 4096));
        PublishResponse(command, MP_SAVE_OK, data, 4, command->offset); return;
    case MP_SAVE_RELEASE_TRADE:
        if (!sTradeActive || !sTradeImportStarted || sState != MP_SAVE_COMMITTED || sOffset != 131072)
            { status = MP_SAVE_ORDER; break; }
        if (command->length || command->offset) { status = MP_SAVE_MALFORMED; break; }
        sPending = TRUE; StartReload(MP_SAVE_OK); return;
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
#ifdef PORTABLE
    // Battle speed (include/platform/game_speed.h): a held save's deadline counts real frames at any speed.
    if (Platform_RealFrame())
        sFrame++;
    gMultiplayerSaveMailbox.frame = sFrame;
#else
#line 1170 // gba-line
    sFrame++; gMultiplayerSaveMailbox.frame = sFrame;
#endif
#line 1171 // gba-line
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
#ifdef PORTABLE
                    sImportedSourceCounter = sSourceCounter; sImportedTargetCounter = gSaveCounter;
                    sImportedCounterValid = TRUE;
#endif
#line 1195 // gba-line
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
            if (sReloadStatus == MP_SAVE_OK && (sLastCommand.opcode == MP_SAVE_END || sLastCommand.opcode == MP_SAVE_RELEASE_TRADE))
                MultiplayerOwnership_Acknowledge(sLastCommand.opcode == MP_SAVE_RELEASE_TRADE);
            Release();
            PublishResponse(&sLastCommand, sReloadStatus, sReloadStatus == MP_SAVE_OK ? data : NULL,
                            sReloadStatus == MP_SAVE_OK ? 4 : 0, sOffset);
        }
    }
    else if (MultiplayerSave_IsHeld() && !sEmergencyActive && sState != MP_SAVE_ERROR && (s32)(sFrame - sDeadline) > 0)
    {
        if (sTradeActive)
        {
            // A lost coordinator cannot decide whether the paired store committed.
            // Preserve the stream and cached ACK; only KEEP/REBIND restores commands.
            sTradeExpired = TRUE;
        }
        else if (sState == MP_SAVE_COMMITTED)
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

#ifdef PORTABLE
bool32 MultiplayerSave_IsImportedCounter(u32 counter)
{
    return sImportedCounterValid && counter == sImportedSourceCounter && gSaveCounter == sImportedTargetCounter
        && sImportedTargetCounter != sImportedSourceCounter;
}
#endif
