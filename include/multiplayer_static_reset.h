#ifndef GUARD_MULTIPLAYER_STATIC_RESET_H
#define GUARD_MULTIPLAYER_STATIC_RESET_H

#define MP_STATIC_RESET_VOLTORB_1 0x1001
#define MP_STATIC_RESET_VERSION_1 0x5601

// Internal adapter for exactly one authored personal encounter. No generic
// wild/event enrollment; the common reset core owns the connection/action gates.
bool32 MultiplayerStaticReset_EntryReady(u32 encounterId);
bool32 MultiplayerStaticReset_AdvanceAttempt(u32 encounterId);
bool32 MultiplayerStaticReset_GuardMatches(u32 encounterId);
u32 MultiplayerStaticReset_Count(void);
void MultiplayerStaticReset_BeginVoltorb1(void);
bool32 MultiplayerStaticReset_SetAdmission(u32 encounterId, u32 count);

#endif
