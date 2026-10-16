/* The Mega Stone dealer's coin shop and the coin specials: see include/platform/coin_shop.h. Project-owned. */
#include "global.h"
#ifdef PORTABLE
#include "coins.h"
#include "event_data.h"
#include "item.h"
#include "money.h"
#include "string_util.h"
#include "strings.h"
#include "text.h"
#include "tv.h"
#include "window.h"
#include "menu.h"
#include "international_string_util.h"
#include "constants/coins.h"
#include "platform/coin_shop.h"

#include "mp_mega_prices.h"

bool8 gCoinShopActive;

void CoinShop_OnOpen(const u16 *itemsForSale)
{
    gCoinShopActive = itemsForSale == MauvilleCity_Mart_Pokemart_MegaStones;
}

u32 CoinShop_StonePrice(u16 item)
{
    for (u32 i = 0; i < MP_MEGA_STONE_COUNT; i++)
        if (sMPMegaStonePrices[i].item == item)
            return sMPMegaStonePrices[i].coins;
    return 0;
}

u32 CoinShop_ItemPrice(u16 item)
{
    if (gCoinShopActive)
    {
        u32 coins = CoinShop_StonePrice(item);

        return coins != 0 ? coins : 0xFFFFFF;   // not a priced stone: out of reach rather than free
    }
    return GetItemPrice(item);
}

u32 CoinShop_Balance(u32 *moneyPtr)
{
    return gCoinShopActive ? GetCoins() : GetMoney(moneyPtr);
}

bool8 CoinShop_CanPay(u32 *moneyPtr, u32 cost)
{
    return gCoinShopActive ? GetCoins() >= cost : IsEnoughMoney(moneyPtr, cost);
}

void CoinShop_Pay(u32 *moneyPtr, u32 cost)
{
    if (gCoinShopActive)
        RemoveCoins(cost);
    else
        RemoveMoney(moneyPtr, cost);
}

u32 CoinShop_SaleShift(u8 newsKind)
{
    return gCoinShopActive ? 0 : IsPokeNewsActive(newsKind);
}

// "123456 COINS" right aligned in the window (the money box and the quantity box are ten tiles wide)
void CoinShop_PrintAmount(u8 windowId, u8 x, u8 y, int amount, u8 speed)
{
    u32 xAlign;

    if (!gCoinShopActive)
    {
        PrintMoneyAmount(windowId, x, y, amount, speed);
        return;
    }
    ConvertIntToDecimalStringN(gStringVar1, amount, STR_CONV_MODE_LEFT_ALIGN, WALLET_COIN_DIGITS);
    StringExpandPlaceholders(gStringVar4, gText_Coins);
    xAlign = GetStringRightAlignXOffset(FONT_NORMAL, gStringVar4, 10 * 8 - 4);
    AddTextPrinterParameterized(windowId, FONT_NORMAL, gStringVar4, xAlign, y, speed, NULL);
}

void CoinShop_PrintBox(u8 windowId, int amount, u8 speed)
{
    if (gCoinShopActive)
        CoinShop_PrintAmount(windowId, 0, 1, amount, speed);
    else
        PrintMoneyAmountInMoneyBox(windowId, amount, speed);
}

void CoinShop_PrintBoxWithBorder(u8 windowId, u16 tileStart, u8 palette, int amount)
{
    if (gCoinShopActive)
    {
        DrawStdFrameWithCustomTileAndPalette(windowId, FALSE, tileStart, palette);
        CoinShop_PrintBox(windowId, amount, 0);
    }
    else
    {
        PrintMoneyAmountInMoneyBoxWithBorder(windowId, tileStart, palette, amount);
    }
}

const u8 *CoinShop_PriceText(void)
{
    return gCoinShopActive ? gText_Coins : gText_PokedollarVar1;
}

static bool8 sLabelAdded;

void CoinShop_AddLabel(u16 x, u16 y)
{
    sLabelAdded = !gCoinShopActive;
    if (sLabelAdded)
        AddMoneyLabelObject(x, y);          // the "MONEY" label has no place over a coin amount
}

void CoinShop_RemoveLabel(void)
{
    if (sLabelAdded)
        RemoveMoneyLabelObject();
    sLabelAdded = FALSE;
}

// ---- script specials: the amount is VAR_0x8004 (high 16 bits) and VAR_0x8005 (low 16 bits)
static u32 ScriptAmount(void)
{
    return ((u32)gSpecialVar_0x8004 << 16) | gSpecialVar_0x8005;
}

u16 CoinWallet_Has(void)
{
    return GetCoins() >= ScriptAmount();
}

u16 CoinWallet_Spend(void)
{
    return RemoveCoins(ScriptAmount()) ? 1 : 0;
}

u16 CoinWallet_HasRoom(void)
{
    return GetCoins() + ScriptAmount() <= MAX_COINS_WALLET;
}

u16 CoinWallet_Give(void)
{
    if (!CoinWallet_HasRoom())
        return 0;
    return AddCoins(ScriptAmount()) ? 1 : 0;
}
#endif
