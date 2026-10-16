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
#endif
