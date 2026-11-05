// Legendary Tickets: the world host's ticket reward (reward word kind 8), PC build only. See include/platform/mp_tickets.h
// and the app's docs/legendary-tickets.md.
#include "global.h"
#include "event_data.h"
#include "item.h"
#include "constants/flags.h"
#include "constants/items.h"
#include "platform/mp_tickets.h"

const u32 gMultiplayerTicketAbiVersion = 1;

struct TicketInfo
{
    u16 item;
    u16 shipFlag;
    u16 receivedFlag;   // 0: none (the Eon Ticket: record mixing set none)
};

static const struct TicketInfo sTickets[MP_TICKET_COUNT + 1] =
{
    [MP_TICKET_EON]         = {ITEM_EON_TICKET,    FLAG_ENABLE_SHIP_SOUTHERN_ISLAND, 0},
    [MP_TICKET_MYSTIC]      = {ITEM_MYSTIC_TICKET, FLAG_ENABLE_SHIP_NAVEL_ROCK,      FLAG_RECEIVED_MYSTIC_TICKET},
    [MP_TICKET_AURORA]      = {ITEM_AURORA_TICKET, FLAG_ENABLE_SHIP_BIRTH_ISLAND,    FLAG_RECEIVED_AURORA_TICKET},
    [MP_TICKET_OLD_SEA_MAP] = {ITEM_OLD_SEA_MAP,   FLAG_ENABLE_SHIP_FARAWAY_ISLAND,  FLAG_RECEIVED_OLD_SEA_MAP},
};

u16 MultiplayerTickets_Item(u32 index)
{
    return index >= 1 && index <= MP_TICKET_COUNT ? sTickets[index].item : ITEM_NONE;
}

static void SetIslandFlags(const struct TicketInfo *ticket)
{
    FlagSet(ticket->shipFlag);
    if (ticket->receivedFlag != 0)
        FlagSet(ticket->receivedFlag);
}

u32 MultiplayerTickets_Grant(u32 payload)
{
    const struct TicketInfo *ticket;

    // The marker read here so it stays in every build (generate_manifest.py looks for it).
    if (*(const volatile u32 *)&gMultiplayerTicketAbiVersion != 1)
        return MP_TICKET_RECEIPT_NOTHING;
    if ((payload & 0xFFFF00) != 0 || payload < 1 || payload > MP_TICKET_COUNT)
        return MP_TICKET_RECEIPT_NOTHING;
    ticket = &sTickets[payload];
    if (CheckBagHasItem(ticket->item, 1) || CheckPCHasItem(ticket->item, 1))
    {
        SetIslandFlags(ticket);
        return MP_TICKET_RECEIPT_OWNED;
    }
    if (!AddBagItem(ticket->item, 1) && !AddPCItem(ticket->item, 1))
        return MP_TICKET_RECEIPT_NOTHING;
    SetIslandFlags(ticket);
    return MP_TICKET_RECEIPT_GIVEN;
}
