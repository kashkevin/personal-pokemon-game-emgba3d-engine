#ifndef GUARD_CONSTANTS_COINS_H
#define GUARD_CONSTANTS_COINS_H

#define MAX_COINS 9999
#define MAX_COIN_DIGITS 4

#ifdef PORTABLE
// Update 29 (PC game): the wallet holds more than the Slot Machine and the Roulette can show. MAX_COINS stays the most they
// take onto a table; the rest of the wallet waits in reserve (src/platform/coin_wallet.c).
#define MAX_COINS_WALLET 999999
#define WALLET_COIN_DIGITS 6
#define COINS_BOX_WIDTH_TILES 10
#endif

#endif // GUARD_CONSTANTS_COINS_H
