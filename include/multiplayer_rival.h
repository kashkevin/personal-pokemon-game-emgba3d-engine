#ifndef GUARD_MULTIPLAYER_RIVAL_H
#define GUARD_MULTIPLAYER_RIVAL_H

// Update 31 (native PC game only): the five May/Brendan rival battle scenes
// become shared multiplayer checkpoints, and a new character starts with the
// streamlined introduction. The GBA ROM build compiles none of this.
//
// The host coordinates every checkpoint (app: rival_checkpoints.py). The
// companion writes the host's summary into the world mailbox as an 8-byte
// RivalRecord at inbound offset 200 (gMultiplayerWorldMailbox.reserved[8..15])
// when the manifest advertises rival_abi_version 1, which this build does with
// gMultiplayerRivalAbiVersion. With no record (offline, an older host) every
// original scene plays exactly as before.
//
// The battle itself is the existing native PvP battle. A rival battle's
// preparation carries the checkpoint and its canonical battle environment
// (MP_RIVAL_PREP_WORD), so every core uses the same background and the same
// environment-dependent mechanics (Camouflage, Nature Power, Secret Power).

#include "constants/multiplayer_rival.h"

#ifdef PORTABLE
struct ObjectEvent;
struct MapPosition;

extern const u32 gMultiplayerRivalAbiVersion;

void MultiplayerRival_Init(void);
// From MultiplayerWorld_Tick: the record of the last complete inbound snapshot.
void MultiplayerRival_Tick(bool32 fieldReady, bool32 connected, const u8 *record);
// From ProcessPlayerFieldInput, where map ON_FRAME scripts start.
bool8 MultiplayerRival_TryStartFieldScript(void);
// From TrySpawnObjectEvents: the original rival object of a multiplayer checkpoint stays away.
bool32 MultiplayerRival_HidesObject(u8 localId, u8 mapNum, u8 mapGroup);

// Script specials (data/scripts/mp_rival.inc).
void MultiplayerRival_ScriptMode(void);      // VAR_0x8004 = checkpoint; VAR_RESULT = MP_RIVAL_MODE_*
void MultiplayerRival_RequestMenu(void);     // open the Players menu (its Rival checkpoint row) when the field is free
void MultiplayerRival_CatchUpNotice(void);   // the overworld banner of MP_RIVAL_MODE_CATCH_UP

// Battle preparation (multiplayer_battle.c) and the battle's environment (battle_main.c, battle_bg.c).
bool32 MultiplayerRival_IsPreparationWord(u32 flags);
bool32 MultiplayerRival_BattleEnvironment(u8 *environment);

// The streamlined new-character introduction (overworld.c CB2_NewGame); Update 32 the short introduction
// (main_menu.c) with no starter until one is claimed from the Poke Balls in Birch's lab.
bool32 MultiplayerIntro_IsEnabled(void);
void MultiplayerIntro_Begin(void);
void MultiplayerIntro_Save(void);            // special: the introduction's one save
bool32 MultiplayerIntro_StarterPending(void);
bool8 MultiplayerIntro_TryStartGuardScript(const struct MapPosition *position); // TryStartStepBasedScript
void MultiplayerIntro_OnTransition(void);                                        // RunOnTransitionMapScript
bool32 MultiplayerIntro_ShowsObject(u8 localId, u8 mapNum, u8 mapGroup);         // TrySpawnObjectEvents
const u8 *MultiplayerIntro_ObjectScript(const struct ObjectEvent *objectEvent);  // GetInteractedObjectEventScript
void MultiplayerIntro_ChooseLabStarter(void); // special (waits): the 18-starter screen from a lab ball
void MultiplayerIntro_GiveLabStarter(void);   // special: the chosen starter joins the party, once

// --selftest-rival and the field checks (tools/pc_port/rival_check.py).
void MultiplayerRival_TestSetRecord(const u8 *record);
u8 MultiplayerRival_TestMode(u32 checkpoint, u8 facing);
void MultiplayerRival_TestSetPreparation(u32 flags);
void MultiplayerIntro_TestApplyPostState(void);
void MultiplayerIntro_TestApplyShortIntroState(void);
void MultiplayerIntro_TestQuietClaimLog(bool8 quiet);
#endif

#endif // GUARD_MULTIPLAYER_RIVAL_H
