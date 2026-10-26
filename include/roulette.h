#ifndef GUARD_ROULETTE_H
#define GUARD_ROULETTE_H

void PlayRoulette(void);

#ifdef PORTABLE
// The native PC UI's roulette screen (src/native_ui/screens/ui_roulette.c): what the table shows now, read at the frame
// boundary (Roulette_NativeUiState; false when no table is up).
enum
{
    ROULETTE_NATIVE_OTHER,  // fading in or out, between steps
    ROULETTE_NATIVE_BET,    // choosing a bet on the board: the arrows move the cursor, A places it, B leaves
    ROULETTE_NATIVE_SPIN,   // the ball rolls
    ROULETTE_NATIVE_RESULT, // the ball has landed: a hit counts its payout into the credit
    ROULETTE_NATIVE_WAIT,   // a pause or one of the table's messages waiting for A or B
    ROULETTE_NATIVE_ASK,    // "Keep playing?" YES / NO
};

#define ROULETTE_NATIVE_BALLS   6
#define ROULETTE_NATIVE_SQUARES 12 // the board's squares: index row * 4 + column (rows orange, green, purple;
                                   // columns Wynaut, Azurill, Skitty, Makuhita)

// A selection (cursor, bet, winningSquare) is row * 5 + column: a column (1-4) with row 0 is that Pokemon's column, a
// row (1-3) with column 0 that colour's row, both a single square; 0 is none. hitFlags has bit (1 << selection) set
// for each square hit this round.
struct RouletteNativeState
{
    u8 phase;
    u8 tableId;                                // 0 the left table, 1 the right
    bool8 specialRate;
    bool8 waitingForKey;                       // a message waits for A or B
    u8 minBet;
    s16 coins;                                 // the table's credit
    u32 wallet;                                // the Coin Case as it stands: the credit and the coins kept in reserve
    s16 payout;                                // still to count (RESULT)
    u8 ballsLeft;                              // of the round's six
    u8 cursor;                                 // the selection the cursor is on (BET)
    u8 bet;                                    // the placed bet (SPIN, RESULT)
    u8 multiplier;                             // of the cursor's selection (BET) or the bet's
    u8 winningSquare;                          // RESULT: the square the ball landed on
    bool8 won;
    u32 hitFlags;
    s16 wheelAngle;                            // degrees the wheel has turned
    s16 squareAngle[ROULETTE_NATIVE_SQUARES];  // each square's slot: degrees from the wheel's top, turning with it
    u8 ballShown;                              // bit n: ball n is on the wheel
    s16 ballX[ROULETTE_NATIVE_BALLS];          // its centre from the wheel's, GBA pixels
    s16 ballY[ROULETTE_NATIVE_BALLS];
    const u8 *message;                         // the text window's (NULL: none)
    u8 yesNoCursor;                            // ASK: 0 YES, 1 NO
};

bool32 Roulette_NativeUiState(struct RouletteNativeState *out);
// the table's question in the field waits for A or B on its own message (too few coins, the special rate)
bool32 Roulette_NativeUiEntryWaits(void);
#endif

#endif // GUARD_ROULETTE_H
