#include "global.h"
#include "multiplayer_static_reset.h"
#include "multiplayer_local_reset.h"
#include "multiplayer_reset_admission.h"
#include "multiplayer.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "fieldmap.h"
#include "pokemon.h"
#include "constants/flags.h"
#include "constants/maps.h"
#include "constants/map_groups.h"
#include "constants/vars.h"

static EWRAM_DATA u32 sAttempt;
static EWRAM_DATA u32 sAdmittedCount;
static EWRAM_DATA u16 sGeneratorState, sBlueState, sGreenState;

u32 MultiplayerStaticReset_Count(void)
{
    return VarGet(VAR_MP_VOLTORB_1_RESET_COUNT_LO)
        | (u32)VarGet(VAR_MP_VOLTORB_1_RESET_COUNT_HI) << 16;
}

bool32 MultiplayerStaticReset_EntryReady(u32 encounterId)
{
#if MP_LOCAL_RESET_NEW_MAUVILLE_PILOT || TESTING
    u8 objectId;
    u16 state;
    if (encounterId != MP_STATIC_RESET_VOLTORB_1 || !gSaveBlock1Ptr
     || gSaveBlock1Ptr->location.mapGroup != MAP_GROUP(MAP_NEW_MAUVILLE_INSIDE)
     || gSaveBlock1Ptr->location.mapNum != MAP_NUM(MAP_NEW_MAUVILLE_INSIDE)
     || gSpecialVar_LastTalked != 6
     || FlagGet(FLAG_HIDE_NEW_MAUVILLE_VOLTORB_1)
     || FlagGet(FLAG_DEFEATED_VOLTORB_1_NEW_MAUVILLE))
        return FALSE;
    state = VarGet(VAR_NEW_MAUVILLE_STATE);
    // Door was opened normally; both running/stopped generator remain valid.
    if ((state != 1 && state != 2)
     || TryGetObjectEventIdByLocalIdAndMap(6, MAP_NUM(MAP_NEW_MAUVILLE_INSIDE), MAP_GROUP(MAP_NEW_MAUVILLE_INSIDE), &objectId))
        return FALSE;
    return !gObjectEvents[objectId].isPlayer
        && !Multiplayer_IsRemoteObject(&gObjectEvents[objectId])
        && gObjectEvents[objectId].currentCoords.x == 25 + MAP_OFFSET
        && gObjectEvents[objectId].currentCoords.y == 18 + MAP_OFFSET;
#else
    return FALSE;
#endif
}

bool32 MultiplayerStaticReset_AdvanceAttempt(u32 encounterId)
{
    u32 count;
    u16 version;
    if (!MultiplayerStaticReset_EntryReady(encounterId)) return FALSE;
    version = VarGet(VAR_MP_VOLTORB_1_RESET_VERSION);
    count = MultiplayerStaticReset_Count();
    // Old zero-filled saves initialize lazily. Unknown/nonzero older data is
    // preserved and refuses the pilot; never silently clear or reinterpret it.
    if ((version != 0 && version != MP_STATIC_RESET_VERSION_1)
     || (!version && count) || count == 0xFFFFFFFFu)
        return FALSE;
    sAttempt = sAdmittedCount ? sAdmittedCount : count + 1;
    VarSet(VAR_MP_VOLTORB_1_RESET_VERSION, MP_STATIC_RESET_VERSION_1);
    VarSet(VAR_MP_VOLTORB_1_RESET_COUNT_LO, sAttempt);
    VarSet(VAR_MP_VOLTORB_1_RESET_COUNT_HI, sAttempt >> 16);
    sGeneratorState = VarGet(VAR_NEW_MAUVILLE_STATE);
    sBlueState = VarGet(VAR_TEMP_1);
    sGreenState = VarGet(VAR_TEMP_2);
    return TRUE;
}

bool32 MultiplayerStaticReset_GuardMatches(u32 encounterId)
{
    struct Pokemon enemy;
    u32 slot;
    if (!MultiplayerStaticReset_EntryReady(encounterId)
     || VarGet(VAR_MP_VOLTORB_1_RESET_VERSION) != MP_STATIC_RESET_VERSION_1
     || MultiplayerStaticReset_Count() != sAttempt
     || VarGet(VAR_NEW_MAUVILLE_STATE) != sGeneratorState
     || VarGet(VAR_TEMP_1) != sBlueState || VarGet(VAR_TEMP_2) != sGreenState)
        return FALSE;
    // The authored script still uses its original setwildbattle generator.
    // Check its actual output before offering any reset; clone reads because
    // native checksum failure handling is allowed to mark a record as bad egg.
    enemy = gParties[B_TRAINER_OPPONENT_A][0];
    if (GetMonData(&enemy, MON_DATA_SPECIES) != SPECIES_VOLTORB
     || !GetMonData(&enemy, MON_DATA_SANITY_HAS_SPECIES)
     || GetMonData(&enemy, MON_DATA_SANITY_IS_BAD_EGG)
     || GetMonData(&enemy, MON_DATA_IS_EGG) || enemy.box.isEgg
     || GetMonData(&enemy, MON_DATA_LEVEL) != 25)
        return FALSE;
    for (slot = 1; slot < PARTY_SIZE; slot++)
    {
        enemy = gParties[B_TRAINER_OPPONENT_A][slot];
        if (GetMonData(&enemy, MON_DATA_SPECIES)) return FALSE;
    }
    return TRUE;
}

void MultiplayerStaticReset_BeginVoltorb1(void)
{
#if TESTING
    gSpecialVar_Result = MultiplayerLocalReset_TryBegin(MP_STATIC_RESET_VOLTORB_1);
#else
    MultiplayerResetAdmission_Begin(MP_STATIC_RESET_VOLTORB_1);
#endif
}

bool32 MultiplayerStaticReset_SetAdmission(u32 encounterId, u32 count)
{
    if (!encounterId && !count) { sAdmittedCount = 0; return TRUE; }
    if (encounterId != MP_STATIC_RESET_VOLTORB_1 || count <= MultiplayerStaticReset_Count()
     || !MultiplayerStaticReset_EntryReady(encounterId)) return FALSE;
    sAdmittedCount = count;
    return TRUE;
}
