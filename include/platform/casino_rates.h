#ifndef GUARD_PLATFORM_CASINO_RATES_H
#define GUARD_PLATFORM_CASINO_RATES_H

// The PC game's Game Corner casino rates (the owner's request of 2026-10-25): what a bet costs and what a win pays at the
// Slot Machines (src/slot_machine.c) and the Roulette tables (src/roulette.c). Only coins: no new bet, bonus or prize.
// The GBA build keeps its own numbers (these are used under PORTABLE only). tools/pc_port/casino_econ.py reads this
// file, simulates both games and checks the earnings these numbers give (docs: CASINO_ECONOMY.md in the app repository).

// Slot Machines: every bet tile (1 to 3 of them: the middle row; and the top and bottom rows; and the diagonals) costs this
// many coins. Each winning line pays its table value below, whatever the bet.
#define CASINO_SLOT_COINS_PER_BET   5
#define CASINO_SLOT_PAY_CHERRY      25      // a cherry on the first reel's middle row
#define CASINO_SLOT_PAY_CHERRY_EDGE 50      // a cherry on the first reel's top or bottom row
#define CASINO_SLOT_PAY_LOTAD       75
#define CASINO_SLOT_PAY_AZURILL     150
#define CASINO_SLOT_PAY_POWER       40
#define CASINO_SLOT_PAY_MIXED_7     1800
#define CASINO_SLOT_PAY_RED_7       6000
#define CASINO_SLOT_PAY_BLUE_7      6000

// Roulette: the minimum bet of each table, in the order of the table list (left, right, left at the special rate, right at
// the special rate). A win pays the table's minimum bet times the multiplier of what the player bet on: the four
// multipliers are those the wheel's picture shows (graphics/roulette/multiplier.png): a colour row pays 1 to 4 as it
// is hit, a Pokemon column 2 to 4, a single square 4.
#define CASINO_ROULETTE_BET_LEFT          25
#define CASINO_ROULETTE_BET_RIGHT         50
#define CASINO_ROULETTE_BET_LEFT_SPECIAL  25
#define CASINO_ROULETTE_BET_RIGHT_SPECIAL 100
#define CASINO_ROULETTE_MULT_1 36
#define CASINO_ROULETTE_MULT_2 48
#define CASINO_ROULETTE_MULT_3 72
#define CASINO_ROULETTE_MULT_4 96

// What the tables can hold: a Slot Machine's credit and a Roulette table's are 4 digits (9,999) and a spin's payout is shown in 4;
// the multiplier picture has room for two digits and its four are in rising order.
#if CASINO_SLOT_PAY_RED_7 > 9000 || CASINO_SLOT_PAY_BLUE_7 > 9000 || CASINO_SLOT_PAY_MIXED_7 > CASINO_SLOT_PAY_RED_7
#error "a slot jackpot must leave room for the lines paid with it in 9,999 coins"
#endif
#if CASINO_ROULETTE_BET_RIGHT_SPECIAL * CASINO_ROULETTE_MULT_4 > 9999 || CASINO_ROULETTE_BET_RIGHT * CASINO_ROULETTE_MULT_4 > 9999
#error "a roulette win must fit the table's 9,999 coins"
#endif
#if !(CASINO_ROULETTE_MULT_1 < CASINO_ROULETTE_MULT_2 && CASINO_ROULETTE_MULT_2 < CASINO_ROULETTE_MULT_3 \
      && CASINO_ROULETTE_MULT_3 < CASINO_ROULETTE_MULT_4 && CASINO_ROULETTE_MULT_4 < 100)
#error "the roulette multipliers rise and have two digits at most (graphics/roulette/multiplier_pc.png)"
#endif

#endif // GUARD_PLATFORM_CASINO_RATES_H
