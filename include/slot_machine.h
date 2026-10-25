#ifndef GUARD_SLOT_MACHINE_H
#define GUARD_SLOT_MACHINE_H

void PlaySlotMachine(u8 machineId, MainCallback exitCallback);

#ifdef PORTABLE
// The native PC UI's slot machine screen (src/native_ui/screens/ui_slot_machine.c): what the machine shows now, read
// at the frame boundary (SlotMachine_NativeUiState; false when no machine is up).
enum
{
    SLOT_NATIVE_OTHER,     // fading in or out, between steps
    SLOT_NATIVE_BET,       // waiting for the bet: DOWN puts a coin in, A spins with what is in (3 at once with none)
    SLOT_NATIVE_SPIN,      // the reels turn: A stops the next one
    SLOT_NATIVE_WIN,       // a match: the payout counts into the credit
    SLOT_NATIVE_LOSE,      // no match
    SLOT_NATIVE_REEL_TIME, // the Pika Power / Reel Time show
    SLOT_NATIVE_ASK_QUIT,  // "Quit the game?" YES / NO
    SLOT_NATIVE_MESSAGE,   // one of the machine's own messages (text)
    SLOT_NATIVE_INFO,      // the payout table (SELECT)
};

#define SLOT_NATIVE_REELS      3
#define SLOT_NATIVE_ROWS       5   // the symbol sprites of a reel (one 120-pixel band)
#define SLOT_NATIVE_SYMBOLS    7   // 7 red, 7 blue, Azurill, Lotad, Cherry, Power, Replay
#define SLOT_NATIVE_LINES      5   // middle, top, bottom, NW-SE, NE-SW
#define SLOT_NATIVE_PAYOUTS    9   // cherry, two cherries (top/bottom), replay, Lotad, Azurill, power, mixed 7, red 7, blue 7

struct SlotMachineNativeState
{
    u8 phase;
    u8 machineId;
    s16 coins;
    s16 payout;
    s16 bet;
    u8 pikaPowerBolts;
    u8 reelTimeSpinsLeft;
    u8 currentReel;                                       // the reel A stops next (SLOT_NATIVE_SPIN)
    u8 reelsTurning;                                      // bit n: reel n turns
    u8 linesLit;                                          // bit n: line n matches now (WIN)
    u8 symbol[SLOT_NATIVE_REELS][SLOT_NATIVE_ROWS];       // each sprite's symbol (0..6)
    s16 symbolY[SLOT_NATIVE_REELS][SLOT_NATIVE_ROWS];     // its centre in GBA pixels (the rows rest at 52, 76, 100)
    u8 quitCursor;                                        // SLOT_NATIVE_ASK_QUIT: 0 YES, 1 NO
    const u8 *message;                                    // SLOT_NATIVE_MESSAGE
    u16 payouts[SLOT_NATIVE_PAYOUTS];
};

bool32 SlotMachine_NativeUiState(struct SlotMachineNativeState *out);
// a symbol's 32x32 picture: 4bpp tiles in the sprite layout, and the reels' palette
const u8 *SlotMachine_NativeUiSymbolTiles(u8 symbol);
const u16 *SlotMachine_NativeUiSymbolPalette(void);
#endif

#endif // GUARD_SLOT_MACHINE_H
