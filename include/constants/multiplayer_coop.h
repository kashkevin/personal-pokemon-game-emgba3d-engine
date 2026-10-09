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

#endif // GUARD_CONSTANTS_MULTIPLAYER_COOP_H
