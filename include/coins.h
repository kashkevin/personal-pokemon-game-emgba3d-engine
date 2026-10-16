#ifndef GUARD_COINS_H
#define GUARD_COINS_H

void PrintCoinsString(u32 coinAmount);
void ShowCoinsWindow(u32 coinAmount, u8 x, u8 y);
void HideCoinsWindow(void);
#ifdef PORTABLE
#include "constants/coins.h"
// Update 29 (PC game): a 32-bit wallet of up to MAX_COINS_WALLET coins.
u32 GetCoins(void);
void SetCoins(u32 coinAmount);
bool8 AddCoins(u32 toAdd);
bool8 RemoveCoins(u32 toSub);
#else
u16 GetCoins(void);
void SetCoins(u16 coinAmount);
bool8 AddCoins(u16 toAdd);
bool8 RemoveCoins(u16 toSub);
#endif

#endif // GUARD_COINS_H
