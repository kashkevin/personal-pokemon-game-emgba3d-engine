#ifndef GUARD_CONSTANTS_MULTIPLAYER_BALANCE_H
#define GUARD_CONSTANTS_MULTIPLAYER_BALANCE_H

// Multiplayer balance defaults approved by the owner on Oct 9, 2026 ("use your
// recommended values"). See docs/balance-defaults.md in the app repository.

// Shiny Charm: a guaranteed Game Corner prize (never a random roll). The
// Gen 6 Charm effect itself is the engine's: 1 + I_SHINY_CHARM_ADDITIONAL_ROLLS
// (2) rolls of 1/4096, i.e. 3/4096 per personal shiny roll. Coins cap at 9,999.
#define MP_SHINY_CHARM_COINS 9000

// Mega Stones: sold by the Mauville City Poke Mart dealer once the player has
// the Mega Ring (given by Flannery with the 4th badge).
#define MP_MEGA_STONE_PRICE 60000

#endif // GUARD_CONSTANTS_MULTIPLAYER_BALANCE_H
