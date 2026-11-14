/* Slot Machine / Roulette stake for a Coin Case above 9,999 coins: see include/platform/coin_wallet.h. Project-owned. */
#include "global.h"
#ifdef PORTABLE
#include "coins.h"
#include "constants/coins.h"
#include "platform/coin_wallet.h"

static u32 sReserve;

u32 CoinWallet_StakeFor(u32 wallet)
{
    return wallet < COIN_TABLE_FULL_WALLET ? wallet : COIN_TABLE_STAKE;
}

u32 CoinWallet_BeginTable(void)
{
    u32 wallet = GetCoins(), stake = CoinWallet_StakeFor(wallet);

    sReserve = wallet - stake;
    return stake;
}

u32 CoinWallet_TableGet(void)
{
    u32 wallet = GetCoins();

    return wallet > sReserve ? wallet - sReserve : 0;
}

void CoinWallet_TableSet(u32 tableCoins)
{
    SetCoins(sReserve + tableCoins);        // SetCoins caps at MAX_COINS_WALLET
}

u32 CoinWallet_Reserve(void)
{
    return sReserve;
}

u32 CoinWallet_PayTable(u32 credit, u32 coins)
{
    u32 total = sReserve + credit;
    u32 room = total < MAX_COINS_WALLET ? MAX_COINS_WALLET - total : 0;
    u32 kept = coins < room ? coins : room;
    u32 creditRoom = credit < MAX_COINS ? MAX_COINS - credit : 0;
    u32 toCredit = kept < creditRoom ? kept : creditRoom;

    sReserve += kept - toCredit;
    return credit + toCredit;
}

bool32 CoinWallet_TableFull(u32 credit)
{
    return sReserve + credit >= MAX_COINS_WALLET;
}

u32 CoinWallet_Total(u32 credit)
{
    u32 total = sReserve + credit;

    return total < MAX_COINS_WALLET ? total : MAX_COINS_WALLET;
}

u32 CoinWallet_TopUp(u32 credit, u32 need)
{
    u32 want, moved;

    if (credit >= need || sReserve == 0)
        return credit;
    want = COIN_TABLE_STAKE > credit ? COIN_TABLE_STAKE - credit : 0;
    moved = want < sReserve ? want : sReserve;
    sReserve -= moved;
    return credit + moved;
}
#endif
