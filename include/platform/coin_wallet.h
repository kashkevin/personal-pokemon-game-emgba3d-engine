#ifndef GUARD_PLATFORM_COIN_WALLET_H
#define GUARD_PLATFORM_COIN_WALLET_H
// Update 29 (PC game): the Coin Case holds up to MAX_COINS_WALLET (999,999) coins, but the Slot Machine and the Roulette
// are the GBA's: they show four digits and stop at MAX_COINS (9,999). A table therefore takes a *stake* from the wallet and
// puts the stake (plus winnings) back when you leave; coins above the stake wait in reserve. The save block is not touched
// until you leave the table (as in the vanilla game), so closing the game at the table loses nothing but the winnings.
#ifdef PORTABLE
#include "global.h"

// A wallet below this many coins is staked whole (as in the vanilla game); at or above it the table is dealt TABLE_STAKE.
#define COIN_TABLE_FULL_WALLET MAX_COINS
#define COIN_TABLE_STAKE 5000

u32 CoinWallet_StakeFor(u32 wallet);          // the stake a table takes from this wallet (pure)
u32 CoinWallet_BeginTable(void);              // call when a table opens: remembers the reserve, returns the stake
u32 CoinWallet_TableGet(void);                // the table's coins (what GetCoins is to the GBA code)
void CoinWallet_TableSet(u32 tableCoins);     // leaving: the wallet becomes the reserve plus these coins (capped)
u32 CoinWallet_Reserve(void);
#endif
#endif // GUARD_PLATFORM_COIN_WALLET_H
