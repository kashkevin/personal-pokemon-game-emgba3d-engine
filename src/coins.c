#include "global.h"
#include "coins.h"
#include "text.h"
#include "window.h"
#include "strings.h"
#include "string_util.h"
#include "menu.h"
#include "international_string_util.h"
#include "constants/coins.h"
#ifdef PORTABLE
#include <string.h>
#endif
#line 10 // gba-line

static EWRAM_DATA u8 sCoinsWindowId = 0;

void PrintCoinsString(u32 coinAmount)
{
    u32 xAlign;

#ifdef PORTABLE
    // Update 29: the wallet holds up to 999,999 coins: six digits in a box two tiles wider.
    ConvertIntToDecimalStringN(gStringVar1, coinAmount, STR_CONV_MODE_RIGHT_ALIGN, WALLET_COIN_DIGITS);
    StringExpandPlaceholders(gStringVar4, gText_Coins);

    xAlign = GetStringRightAlignXOffset(FONT_NORMAL, gStringVar4, COINS_BOX_WIDTH_TILES * 8);
#else
#line 17 // gba-line
    ConvertIntToDecimalStringN(gStringVar1, coinAmount, STR_CONV_MODE_RIGHT_ALIGN, MAX_COIN_DIGITS);
    StringExpandPlaceholders(gStringVar4, gText_Coins);

    xAlign = GetStringRightAlignXOffset(FONT_NORMAL, gStringVar4, 0x40);
#endif
#line 21 // gba-line
    AddTextPrinterParameterized(sCoinsWindowId, FONT_NORMAL, gStringVar4, xAlign, 1, 0, NULL);
}

void ShowCoinsWindow(u32 coinAmount, u8 x, u8 y)
{
    struct WindowTemplate template;
#ifdef PORTABLE
    SetWindowTemplateFields(&template, 0, x + 1, y + 1, COINS_BOX_WIDTH_TILES, 2, 0xF, 0x141);
#else
#line 27 // gba-line
    SetWindowTemplateFields(&template, 0, x + 1, y + 1, 8, 2, 0xF, 0x141);
#endif
#line 28 // gba-line
    sCoinsWindowId = AddWindow(&template);
    FillWindowPixelBuffer(sCoinsWindowId, PIXEL_FILL(0));
    PutWindowTilemap(sCoinsWindowId);
    DrawStdFrameWithCustomTileAndPalette(sCoinsWindowId, FALSE, 0x214, 0xE);
    PrintCoinsString(coinAmount);
}

void HideCoinsWindow(void)
{
    ClearStdWindowAndFrame(sCoinsWindowId, TRUE);
    RemoveWindow(sCoinsWindowId);
}

#ifdef PORTABLE
// Update 29: the wallet. The save block keeps the vanilla encrypted u16 (the low half); the high half lives in the two unused
// bytes at 0x9C2 (plain: a save from before this update reads 0 there, so every old balance is unchanged). Up to 999,999 coins
// (the Mega Ring costs 25,000 and the Shiny Charm 100,000; the Slot Machine and the Roulette still take at most MAX_COINS
// onto the table, see coin_wallet.h).
static u32 WalletHigh(void)
{
    u16 high;

    memcpy(&high, &gSaveBlock1Ptr->unused_9C2[0], sizeof(high));
    return high;
}

u32 GetCoins(void)
{
    u32 low = (u16)(gSaveBlock1Ptr->coins ^ gSaveBlock2Ptr->encryptionKey);
    u32 total = (WalletHigh() << 16) | low;

    return total > MAX_COINS_WALLET ? MAX_COINS_WALLET : total;
}

void SetCoins(u32 coinAmount)
{
    u16 high, low;

    if (coinAmount > MAX_COINS_WALLET)
        coinAmount = MAX_COINS_WALLET;
    high = coinAmount >> 16;
    low = coinAmount & 0xFFFF;
    memcpy(&gSaveBlock1Ptr->unused_9C2[0], &high, sizeof(high));
    gSaveBlock1Ptr->coins = low ^ gSaveBlock2Ptr->encryptionKey;
}

bool8 AddCoins(u32 toAdd)
{
    u32 ownedCoins = GetCoins();

    if (ownedCoins >= MAX_COINS_WALLET)
        return FALSE;
    if (toAdd > MAX_COINS_WALLET - ownedCoins)
        toAdd = MAX_COINS_WALLET - ownedCoins;     // the wallet is full at the top: the rest is lost, as in the vanilla game
    SetCoins(ownedCoins + toAdd);
    return TRUE;
}

bool8 RemoveCoins(u32 toSub)
{
    u32 ownedCoins = GetCoins();

    if (ownedCoins >= toSub)
    {
        SetCoins(ownedCoins - toSub);
        return TRUE;
    }
    return FALSE;
}
#else
#line 41 // gba-line
u16 GetCoins(void)
{
    return gSaveBlock1Ptr->coins ^ gSaveBlock2Ptr->encryptionKey;
}

void SetCoins(u16 coinAmount)
{
    gSaveBlock1Ptr->coins = coinAmount ^ gSaveBlock2Ptr->encryptionKey;
}

bool8 AddCoins(u16 toAdd)
{
    u16 newAmount;
    u16 ownedCoins = GetCoins();
    if (ownedCoins >= MAX_COINS)
        return FALSE;
    // check overflow, can't have less coins than previously
    if (ownedCoins > ownedCoins + toAdd)
    {
        newAmount = MAX_COINS;
    }
    else
    {
        ownedCoins += toAdd;
        if (ownedCoins > MAX_COINS)
            ownedCoins = MAX_COINS;
        newAmount = ownedCoins;
    }
    SetCoins(newAmount);
    return TRUE;
}

bool8 RemoveCoins(u16 toSub)
{
    u16 ownedCoins = GetCoins();
    if (ownedCoins >= toSub)
    {
        SetCoins(ownedCoins - toSub);
        return TRUE;
    }
    return FALSE;
}

#endif
#line 84 // gba-line
#ifdef ENABLE_NATIVE_UI
#include "native_ui/ui_engine.h"

// The native PC UI (src/native_ui/screens/ui_game_corner.c) asks whether the
// coin box (ShowCoinsWindow) is up: its window still has the box's template
// and is on screen.
bool32 Coins_NativeUiBoxShown(void)
{
    const struct WindowTemplate *w;

    if (sCoinsWindowId >= WINDOWS_MAX)
        return FALSE;
    w = &gWindows[sCoinsWindowId].window;
    // COINS_BOX_WIDTH_TILES: the box is two tiles wider since Update 29 (six digits); with the old 8 the board never showed.
    return w->bg != 0xFF && w->width == COINS_BOX_WIDTH_TILES && w->height == 2 && w->baseBlock == 0x141
        && Window_NativeUiShown(sCoinsWindowId);
}

// The coin box's window while it is up (WINDOW_NONE otherwise): the field's messages (ui_field_text.c) draw the box
// themselves and do not count its window as one they leave to the game's picture.
u8 Coins_NativeUiBoxWindow(void)
{
    return Coins_NativeUiBoxShown() ? sCoinsWindowId : WINDOW_NONE;
}
#endif // ENABLE_NATIVE_UI
