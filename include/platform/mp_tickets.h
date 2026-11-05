#ifndef GUARD_PLATFORM_MP_TICKETS_H
#define GUARD_PLATFORM_MP_TICKETS_H
// Legendary Tickets (the owner's decision of 2026-11-05; the app's docs/legendary-tickets.md): the world's host gives a
// player one of the island legends' tickets for a postgame achievement, a quest or a Battle Frontier milestone, as reward
// word kind 8 on the wallet stream (MP_REWARD_TICKET, multiplayer_world.h). PC build only.
#ifdef PORTABLE
#include "global.h"

// Payload indexes (bits 0-7 of the word; bits 8-23 zero): the app's legendary_tickets.py TICKETS.
#define MP_TICKET_EON          1   // ITEM_EON_TICKET: Southern Island (Latios or Latias)
#define MP_TICKET_MYSTIC       2   // ITEM_MYSTIC_TICKET: Navel Rock (Lugia, Ho-Oh)
#define MP_TICKET_AURORA       3   // ITEM_AURORA_TICKET: Birth Island (Deoxys)
#define MP_TICKET_OLD_SEA_MAP  4   // ITEM_OLD_SEA_MAP: Faraway Island (Mew)
#define MP_TICKET_COUNT        4

#define MP_TICKET_RECEIPT_NOTHING 0   // no room in the Bag nor the PC, or a payload this game does not know: nothing changed
#define MP_TICKET_RECEIPT_GIVEN   1   // the ticket added (Bag, else PC) and its island's flags set
#define MP_TICKET_RECEIPT_OWNED   2   // already in the Bag or the PC: its island's flags made sure, nothing added

// Gives a ticket as the Mystery Gift scripts did (data/scripts/gift_*.inc; the Eon Ticket as record mixing did): the item
// (in the Bag, else the PC, never a second copy) and, in the same step, the island's ship flag and the received flag (none
// for the Eon Ticket). All or nothing: without room, no flag is set. Returns an MP_TICKET_RECEIPT_* value.
u32 MultiplayerTickets_Grant(u32 payload);
// The ticket's item for a payload index (ITEM_NONE for one this game does not know).
u16 MultiplayerTickets_Item(u32 index);
// 1: this game applies reward kind 8 (generate_manifest.py: ticket_abi_version; the host sends kind 8 only then).
extern const u32 gMultiplayerTicketAbiVersion;
#endif
#endif // GUARD_PLATFORM_MP_TICKETS_H
