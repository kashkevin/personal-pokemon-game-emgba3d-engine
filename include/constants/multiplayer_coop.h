#ifndef GUARD_CONSTANTS_MULTIPLAYER_COOP_H
#define GUARD_CONSTANTS_MULTIPLAYER_COOP_H

// Co-op Gym battles (owner brief "GYMS AND TWIN GYM LEADERS"). A solo
// challenger faces the ordinary Gym Leader. Two teammates who challenge
// together face the Leader and a partner in a native double battle.
#define MP_COOP_GYM_NONE        0
#define MP_COOP_GYM_RUSTBORO    1
#define MP_COOP_GYM_DEWFORD     2
#define MP_COOP_GYM_MAUVILLE    3
#define MP_COOP_GYM_LAVARIDGE   4
#define MP_COOP_GYM_PETALBURG   5
#define MP_COOP_GYM_FORTREE     6
#define MP_COOP_GYM_MOSSDEEP    7
#define MP_COOP_GYM_SOOTOPOLIS  8
#define MP_COOP_GYM_COUNT       8

// VAR_RESULT values of MultiplayerCoop_CanOffer.
#define MP_COOP_OFFER_NONE      0 // no teammate here: the ordinary solo battle
#define MP_COOP_OFFER_READY     1

// VAR_RESULT values of MultiplayerCoop_ExpStep (PC game only): the co-op
// experience award after a co-op battle (multiplayer_coop.c).
#define MP_COOP_EXP_DONE        0
#define MP_COOP_EXP_MESSAGE     1 // gStringVar4
#define MP_COOP_EXP_MOVE_FULL   2 // STR_VAR_1 wants to learn STR_VAR_2 and knows four moves
#define MP_COOP_EXP_EVOLVE      3 // VAR_0x8005 = party slot

#endif // GUARD_CONSTANTS_MULTIPLAYER_COOP_H
