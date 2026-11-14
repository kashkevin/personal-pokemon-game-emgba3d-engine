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
// A win of `coins` at a table whose credit is `credit`: the credit takes what it has room for (it stops at MAX_COINS), the
// rest goes to the reserve, so it reaches the Coin Case when the table is left instead of being cut - the owner's "no
// cap" (2026-10-26). What would take the Coin Case (reserve and credit together) past MAX_COINS_WALLET is not kept: the
// tables say so (CoinWallet_TableFull; the owner's decision #65, 2026-11-14). Returns the new credit.
u32 CoinWallet_PayTable(u32 credit, u32 coins);
// The Coin Case holds MAX_COINS_WALLET with this credit: the tables' "Your COIN CASE is full" (#65).
bool32 CoinWallet_TableFull(u32 credit);
// The whole Coin Case at the table: the reserve and the credit (what the 3D tables and the Classic 2D tables' COIN CASE
// line show, #65).
u32 CoinWallet_Total(u32 credit);
// A credit below `need` (a bet the table asks for) topped up from the reserve to the stake: a big Coin Case's table never
// says it ran out of coins while the Coin Case holds some (#65: the Classic 2D tables show the whole Coin Case).
u32 CoinWallet_TopUp(u32 credit, u32 need);
#endif
#endif // GUARD_PLATFORM_COIN_WALLET_H
