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
#define MP_STATIC_RESET_LAST MP_STATIC_RESET_RAYQUAZA
#define MP_STATIC_RESET_VERSION_1 0x5601

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
bool32 MultiplayerStaticReset_SetAdmission(u32 encounterId, u32 count);

#endif
