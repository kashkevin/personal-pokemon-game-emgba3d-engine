#ifndef GUARD_PLATFORM_MP_MINIGAMES_H
#define GUARD_PLATFORM_MP_MINIGAMES_H

// KNOWN_ISSUES MS-141c (PC game only): the wireless minigames with the world's players. The lobby is a page of the
// Players menu the companion draws (pages MP_MENU_PAGE_MINIGAME_*); a group's start and its link run here and in
// src/link_rfu_2.c (NetLink), their frames carried by the bridge (src/platform/mp_bridge.c, frames 'u' and 'm').
#include <stddef.h>
#include <stdint.h>

// The Players menu's lobby pages, one per game (an OPEN names its page: 1-7).
#define MP_MENU_PAGE_MINIGAME_BERRY_CRUSH 5
#define MP_MENU_PAGE_MINIGAME_POKEMON_JUMP 6
#define MP_MENU_PAGE_MINIGAME_DODRIO 7

// An 'm' frame's first byte; the lobby's operations.
#define MP_MINIGAME_KIND_LINK 1  // [kind, (inbound: source,) the link's frame]
#define MP_MINIGAME_KIND_LOBBY 2 // [kind, op, ...]
#define MP_MINIGAME_OP_HELLO 1   // out: [activity, link player (MP_MINIGAME_PLAYER_BYTES)]: this game opens a lobby
#define MP_MINIGAME_OP_LEAVE 2   // out: [reason]: this game leaves its lobby or group (MP_MINIGAME_LEAVE_*)
#define MP_MINIGAME_OP_STARTED 1 // in: [activity, players, own id, players x link player]: the group starts
#define MP_MINIGAME_OP_CLOSED 2  // in: [reason]: the group closed (during its game: the link is lost)
#define MP_MINIGAME_LEAVE_ENDED 0 // its game closed the group's link at its end (every player's game does)
#define MP_MINIGAME_LEAVE_LOST 1  // its group's link was lost (the communication error)
#define MP_MINIGAME_LEAVE_LOBBY 2 // it left the lobby (its group, if starting, has to end for the others)
#define MP_MINIGAME_PLAYER_BYTES 16 // name 8 (the game's characters), gender, language, version (u16), trainer id (u32)

bool8 MpMinigames_Take(uint8_t *data, size_t *length);
void MpMinigames_Deliver(const uint8_t *data, size_t length);
bool8 MpMinigames_InGroup(void);

#endif // GUARD_PLATFORM_MP_MINIGAMES_H
