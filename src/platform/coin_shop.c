/* The coin shops - the Mega Stone dealer's and the Game Corner's RARE GOODS - and the coin specials: see
 * include/platform/coin_shop.h. Project-owned. */
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
#include "mp_rare_goods_prices.h"

bool8 gCoinShopActive;

void CoinShop_OnOpen(const u16 *itemsForSale)
{
    gCoinShopActive = itemsForSale == MauvilleCity_Mart_Pokemart_MegaStones;
    for (u32 i = 0; i < MP_RARE_GOODS_LIST_COUNT; i++)
        if (itemsForSale == sMPRareGoodsLists[i])
            gCoinShopActive = TRUE;
}

// The Game Corner's RARE GOODS (the owner's request of 2026-10-24): every item no player can get anywhere else, priced by
// what it does (tools/mp/gen_rare_goods.py); 0 for any other item.
u32 CoinShop_RareGoodsPrice(u16 item)
{
    for (u32 i = 0; i < MP_RARE_GOODS_COUNT; i++)
        if (sMPRareGoodsPrices[i].item == item)
            return sMPRareGoodsPrices[i].coins;
    return 0;
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

        if (coins == 0)
            coins = CoinShop_RareGoodsPrice(item);
        return coins != 0 ? coins : 0xFFFFFF;   // not a priced item: out of reach rather than free
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

// The dealer's lines and prices name coins. "COINS" fits in the balance box only: the list's prices stand beside the
// longest stone names ("Charizardite X") and the quantity box's beside "x01", so there they are the bare number.
static const u8 sText_CoinNumber[] = _("{STR_VAR_1}");
static const u8 sText_CoinsWanted[] = _("{STR_VAR_1}? And you wanted {STR_VAR_2}?\nThat will be {STR_VAR_3} COINS.");
static const u8 sText_CoinsWantedOne[] = _("You wanted {STR_VAR_1}?\nThat'll be {STR_VAR_2} COINS. Will that be okay?");
static const u8 sText_NotEnoughCoins[] = _("You don't have enough COINS.{PAUSE_UNTIL_PRESS}");

// The amount right aligned in a ten-tile window (the balance box and the quantity box)
static void PrintCoinAmount(u8 windowId, u8 y, int amount, u8 speed, const u8 *text)
{
    ConvertIntToDecimalStringN(gStringVar1, amount, STR_CONV_MODE_LEFT_ALIGN, WALLET_COIN_DIGITS);
    StringExpandPlaceholders(gStringVar4, text);
    AddTextPrinterParameterized(windowId, FONT_NORMAL, gStringVar4,
                                GetStringRightAlignXOffset(FONT_NORMAL, gStringVar4, 10 * 8 - 4), y, speed, NULL);
}

// The quantity box's total (the box is cleared before; "x01" is printed after it, on its left)
void CoinShop_PrintAmount(u8 windowId, u8 x, u8 y, int amount, u8 speed)
{
    if (!gCoinShopActive)
        PrintMoneyAmount(windowId, x, y, amount, speed);
    else
        PrintCoinAmount(windowId, y, amount, speed, sText_CoinNumber);
}

// "123456 COINS". The box is cleared first: the money box pads its amount to a fixed width, a coin amount is right
// aligned, so after a purchase the shorter balance would leave the longer one's first digits showing.
void CoinShop_PrintBox(u8 windowId, int amount, u8 speed)
{
    if (gCoinShopActive)
    {
        FillWindowPixelBuffer(windowId, PIXEL_FILL(1));
        PrintCoinAmount(windowId, 1, amount, speed, gText_Coins);
    }
    else
    {
        PrintMoneyAmountInMoneyBox(windowId, amount, speed);
    }
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
    return gCoinShopActive ? sText_CoinNumber : gText_PokedollarVar1;
}

const u8 *CoinShop_WantedText(void)
{
    return gCoinShopActive ? sText_CoinsWanted : gText_Var1AndYouWantedVar2;
}

const u8 *CoinShop_WantedOneText(void)
{
    return gCoinShopActive ? sText_CoinsWantedOne : gText_YouWantedVar1ThatllBeVar2;
}

const u8 *CoinShop_NotEnoughText(void)
{
    return gCoinShopActive ? sText_NotEnoughCoins : gText_YouDontHaveMoney;
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
