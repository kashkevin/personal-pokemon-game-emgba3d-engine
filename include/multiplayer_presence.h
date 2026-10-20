#ifndef GUARD_MULTIPLAYER_PRESENCE_H
#define GUARD_MULTIPLAYER_PRESENCE_H

// Native PC only (src/multiplayer_presence.c): why the local player is off the field. The pose the game publishes
// carries it in its last byte (struct MultiplayerPose.reserved) while its activity is UNAVAILABLE; the companion
// turns it into the session's presence activity ("battle", "menu", "transition"), so the other players' Players lists
// say "In battle" instead of losing the player. Other players' poses keep the byte zero. The GBA build publishes zero.
#define MULTIPLAYER_DETAIL_NONE 0        // (not reported since Update 32: a dialogue on the field is MENU)
#define MULTIPLAYER_DETAIL_BATTLE 1
#define MULTIPLAYER_DETAIL_MENU 2        // the START menu, PLAYERS, the bag, the party, the PC, a dialogue, a save...
#define MULTIPLAYER_DETAIL_TRANSITION 3  // a warp, door, stairs, Fly, a whiteout: the map is changing
#define MULTIPLAYER_DETAIL_MAX MULTIPLAYER_DETAIL_TRANSITION
#define MULTIPLAYER_PRESENCE_DETAIL_ABI 1

// generate_manifest.py reads this marker (presence_detail_abi_version); the bridge accepts the detail with it.
extern const u32 gMultiplayerPresenceDetailAbiVersion;

u8 MultiplayerPresence_Detail(void);
// START > PLAYER > PLAYERS was refused: a field banner says why (not connected, link lost, not ready yet).
void MultiplayerPresence_PlayersRefused(void);
void MultiplayerPresence_Refuse(const char *text); // the same banner, another reason ('|' starts its second line)
void MultiplayerPresence_Tick(void); // once a frame (Multiplayer_Tick)

#endif // GUARD_MULTIPLAYER_PRESENCE_H
