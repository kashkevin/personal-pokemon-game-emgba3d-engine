#ifndef GUARD_CONSTANTS_MULTIPLAYER_RIVAL_H
#define GUARD_CONSTANTS_MULTIPLAYER_RIVAL_H

// Update 31 multiplayer rival checkpoints (include/multiplayer_rival.h). Defines only:
// the event scripts use them too (data/scripts/mp_rival.inc, PC game only).

#define MP_RIVAL_ABI_VERSION        1
#define MP_RIVAL_CHECKPOINT_COUNT   5
#define MP_RIVAL_RECORD_OFFSET      8 // in gMultiplayerWorldMailbox.reserved
#define MP_RIVAL_RECORD_BYTES       8

// Story order; bit i of the record's masks is checkpoint i (rival_catalogue.IDS).
#define MP_RIVAL_ROUTE103           0
#define MP_RIVAL_RUSTBORO           1
#define MP_RIVAL_ROUTE110           2
#define MP_RIVAL_ROUTE119           3
#define MP_RIVAL_LILYCOVE           4

// RivalRecord: flags, completed mask, cleared mask, here (1..5, 0 none), state, 3 zero bytes.
#define MP_RIVAL_FLAG_VALID         (1 << 0)
#define MP_RIVAL_FLAG_PRESENT       (1 << 1)
#define MP_RIVAL_FLAG_MY_MATCH      (1 << 2)
#define MP_RIVAL_STATE_OPEN         1
#define MP_RIVAL_STATE_COUNTDOWN    2
#define MP_RIVAL_STATE_BATTLING     3
#define MP_RIVAL_STATE_COMPLETED    4

// What a checkpoint's scene script does now (VAR_RESULT of MultiplayerRival_ScriptMode).
#define MP_RIVAL_MODE_ORIGINAL      0 // not in multiplayer: the original scene
#define MP_RIVAL_MODE_BLOCK         1 // an armed trigger line: stop the player
#define MP_RIVAL_MODE_WAIT          2 // connected, the host's rival status not known yet: stop for now
#define MP_RIVAL_MODE_REWARD        3 // passed personally: the original scene's personal effects, once
#define MP_RIVAL_MODE_CATCH_UP      4 // the world completed it, this player has not: say how, let them pass
#define MP_RIVAL_MODE_PASS          5 // nothing to do (going back, optional checkpoint, already applied)

// A rival battle's preparation flags word: 'R' 'V', the checkpoint (1..5), the battle environment.
#define MP_RIVAL_PREP_TAG           0x52560000u
#define MP_RIVAL_PREP_WORD(checkpoint, environment) (MP_RIVAL_PREP_TAG | ((u32)(checkpoint) << 8) | (u32)(environment))

// Update 32, the short introduction: VAR_MULTIPLAYER_STARTER_CHOICE of a new character until it claims its
// starter in Birch's lab (then 0x6100 | the starter, starter_choose.c SavePlayerStarterChoice).
#define MP_INTRO_STARTER_PENDING    0x62FF
// gSpecialVar_Result of the 18-starter screen opened from the lab when it is left with B.
#define MP_INTRO_NO_STARTER         0xFFFF

#endif // GUARD_CONSTANTS_MULTIPLAYER_RIVAL_H
