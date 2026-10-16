#ifndef GUARD_CONSTANTS_MULTIPLAYER_BALANCE_H
#define GUARD_CONSTANTS_MULTIPLAYER_BALANCE_H

// Multiplayer balance defaults approved by the owner on Oct 9, 2026 ("use your
// recommended values"). See docs/balance-defaults.md in the app repository.

// Shiny Charm: a guaranteed Game Corner prize (never a random roll). The
// Gen 6 Charm effect itself is the engine's: 1 + I_SHINY_CHARM_ADDITIONAL_ROLLS
// (2) rolls of 1/4096, i.e. 3/4096 per personal shiny roll. Coins cap at 9,999.
// Update 29 (PC game): Game Corner coin prices, the Mega Evolution unlock and the Shiny Charm. The Coin Case holds up to
// 999,999 coins. Each Mega Stone has its own price (src/platform/mp_mega_prices.h, tools/mp/gen_mega_prices.py). The scripts
// are assembled without PORTABLE (the PC game's branches are chosen by the assembler), so these are plain constants; the GBA
// build keeps its 9,000-coin Charm (MP_SHINY_CHARM_COINS in its scripts) and has no Mega Ring or coin pack sale.
#define MP_SHINY_CHARM_COINS_PC 100000
#define MP_MEGA_RING_COINS 25000
#define MP_COINS_PACK_LARGE 5000          // the coin counter's large pack ...
#define MP_COINS_PACK_LARGE_YEN 100000    // ... at the same 20 yen a coin as the other two
#define MP_SHINY_CHARM_COINS_GBA 9000
#ifdef PORTABLE
#define MP_SHINY_CHARM_COINS MP_SHINY_CHARM_COINS_PC
#else
#define MP_SHINY_CHARM_COINS MP_SHINY_CHARM_COINS_GBA
#endif

// Mega Stones: sold by the Mauville City Poke Mart dealer once the player has
// the Mega Ring (given by Flannery with the 4th badge).
#define MP_MEGA_STONE_PRICE 60000

// Lucky Pokemon: every IV is raised to at least this floor (0-31 scale); an IV
// is never lowered and never exceeds 31. The trade chance per friendship level
// lives on the host (balance.py).
#define MP_LUCKY_IV_FLOOR 12

#endif // GUARD_CONSTANTS_MULTIPLAYER_BALANCE_H
