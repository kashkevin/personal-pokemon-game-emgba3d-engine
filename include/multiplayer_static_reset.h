#ifndef GUARD_MULTIPLAYER_STATIC_RESET_H
#define GUARD_MULTIPLAYER_STATIC_RESET_H

#define MP_STATIC_RESET_VOLTORB_1 0x1001
#define MP_STATIC_RESET_VOLTORB_2 0x1002
#define MP_STATIC_RESET_VOLTORB_3 0x1003
#define MP_STATIC_RESET_ELECTRODE_1 0x1004
#define MP_STATIC_RESET_ELECTRODE_2 0x1005
#define MP_STATIC_RESET_REGISTEEL 0x1006
#define MP_STATIC_RESET_REGIROCK 0x1007
#define MP_STATIC_RESET_REGICE 0x1008
#define MP_STATIC_RESET_RAYQUAZA 0x1009
#define MP_STATIC_RESET_SUDOWOODO 0x100A
#define MP_STATIC_RESET_KYOGRE 0x100B
#define MP_STATIC_RESET_GROUDON 0x100C
#define MP_STATIC_RESET_ROUTE120_KECLEON_1 0x100D
#define MP_STATIC_RESET_ROUTE120_KECLEON_2 0x100E
#define MP_STATIC_RESET_ROUTE120_KECLEON_3 0x100F
#define MP_STATIC_RESET_ROUTE120_KECLEON_4 0x1010
#define MP_STATIC_RESET_ROUTE120_KECLEON_5 0x1011
#define MP_STATIC_RESET_ROUTE119_KECLEON_1 0x1012
#define MP_STATIC_RESET_ROUTE119_KECLEON_2 0x1013
#define MP_STATIC_RESET_LUGIA 0x1014
#define MP_STATIC_RESET_HO_OH 0x1015
#define MP_STATIC_RESET_MEW 0x1016
#define MP_STATIC_RESET_DEOXYS 0x1017
#define MP_STATIC_RESET_LATIOS 0x1018
#define MP_STATIC_RESET_LATIAS 0x1019
#define MP_STATIC_RESET_LAST MP_STATIC_RESET_LATIAS
#define MP_STATIC_RESET_VERSION_1 0x5601

// Immutable ROM catalogue. Flags describe compiled adapters and controls only;
// exact script, ticket, progression and first-action gates still own eligibility.
struct MultiplayerResetCatalogue {
    u32 version, firstId, sourceCount, enabledMask, controlFlags, buildFlags;
};
extern const struct MultiplayerResetCatalogue gMultiplayerResetCatalogue;

// Internal adapters for exact authored personal encounters. No generic
// wild/event enrollment; the common reset core owns the connection/action gates.
bool32 MultiplayerStaticReset_EntryReady(u32 encounterId);
bool32 MultiplayerStaticReset_AdvanceAttempt(u32 encounterId);
bool32 MultiplayerStaticReset_GuardMatches(u32 encounterId);
u32 MultiplayerStaticReset_Count(void);
u32 MultiplayerStaticReset_GetCount(u32 encounterId);
bool32 MultiplayerStaticReset_IsLegendary(u32 encounterId);
void MultiplayerStaticReset_InitCache(void);
void MultiplayerStaticReset_BeginVoltorb1(void);
void MultiplayerStaticReset_BeginCurrent(void);
void MultiplayerStaticReset_ReturnWeatherSource(void);
void MultiplayerStaticReset_CaptureMew(void);
void MultiplayerStaticReset_ReturnTicketSource(void);
void MultiplayerStaticReset_ForgetMew(void);
bool32 MultiplayerStaticReset_SetAdmission(u32 encounterId, u32 count);

#endif
