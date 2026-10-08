#ifndef GUARD_MULTIPLAYER_LOCAL_RESET_H
#define GUARD_MULTIPLAYER_LOCAL_RESET_H
#include "config/multiplayer_local_reset.h"

#define MP_LOCAL_RESET_DEVELOPMENT_ID 0xD001
#define MP_LOCAL_RESET_COUNTER_LO VAR_UNUSED_0x40F7
#define MP_LOCAL_RESET_COUNTER_HI VAR_UNUSED_0x40F8
#define MP_LOCAL_RESET_COUNTER_MAGIC VAR_UNUSED_0x40F9
#define MP_LOCAL_RESET_CAPTURED VAR_UNUSED_0x40FA
#define MP_LOCAL_RESET_MAGIC 0x4C52

enum MultiplayerLocalResetInput {
    MP_LOCAL_RESET_INPUT_PASS, MP_LOCAL_RESET_INPUT_HANDLED,
    MP_LOCAL_RESET_INPUT_CANCELED,
};
void MultiplayerLocalReset_Init(void);
bool32 MultiplayerLocalReset_HandleChord(void);
const u8 *MultiplayerLocalReset_GetActionPrompt(enum BattlerId battler);
u32 MultiplayerLocalReset_HandlePlayerInput(enum BattlerId battler);
bool32 MultiplayerLocalReset_TryBegin(u32 encounterId);
u32 MultiplayerLocalReset_Count(void);
void MultiplayerLocalReset_BeginDevelopmentHunt(void);
void MultiplayerLocalReset_Finish(void);
void MultiplayerLocalReset_ConsumeResult(void);
void MultiplayerLocalReset_CloseAction(void);
void MultiplayerLocalReset_CloseCapture(void);
bool32 MultiplayerLocalReset_WasAccepted(void);
void BattleMain_QuitPersonalEncounter(void);
#if TESTING
bool32 MultiplayerLocalReset_TestOpenWindow(void);
bool32 MultiplayerLocalReset_TestRequest(void);
bool32 MultiplayerLocalReset_TestConfirm(bool32 yes);
bool32 MultiplayerLocalReset_TestSelect(enum BattlerId battler);
#endif
#endif
