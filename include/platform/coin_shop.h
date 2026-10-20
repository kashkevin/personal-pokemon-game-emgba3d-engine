#ifndef GUARD_PLATFORM_COIN_SHOP_H
#define GUARD_PLATFORM_COIN_SHOP_H
// Update 29 (PC game): the Mega Stone dealer in the Mauville Poke Mart sells for Game Corner coins. The shop menu is the game's
// own (src/shop.c); while the dealer's list is open it asks these wrappers for prices, balance and payment instead of the
// money functions, and shows coins where it showed yen. Every other shop is unchanged.
#ifdef PORTABLE
#include "global.h"

extern bool8 gCoinShopActive;                 // the open shop is the Mega Stone dealer's (set by CreatePokemartMenu)
extern const u16 MauvilleCity_Mart_Pokemart_MegaStones[];

void CoinShop_OnOpen(const u16 *itemsForSale);
u32 CoinShop_StonePrice(u16 item);            // coins for a Mega Stone the dealer sells, else 0
u32 CoinShop_ItemPrice(u16 item);             // the coin price in the dealer's shop, else the item's yen price
u32 CoinShop_Balance(u32 *moneyPtr);          // the wallet in the dealer's shop, else the money
bool8 CoinShop_CanPay(u32 *moneyPtr, u32 cost);
void CoinShop_Pay(u32 *moneyPtr, u32 cost);
u32 CoinShop_SaleShift(u8 newsKind);          // the POKeNEWS sale never applies to coins
void CoinShop_PrintBoxWithBorder(u8 windowId, u16 tileStart, u8 palette, int amount);
void CoinShop_PrintBox(u8 windowId, int amount, u8 speed);
void CoinShop_PrintAmount(u8 windowId, u8 x, u8 y, int amount, u8 speed);
const u8 *CoinShop_PriceText(void);           // the list's price: the bare number in the dealer's shop, else the yen text
const u8 *CoinShop_WantedText(void);          // "... That will be {STR_VAR_3} COINS." in the dealer's shop, else the yen line
const u8 *CoinShop_WantedOneText(void);       // the same for an item bought one at a time
const u8 *CoinShop_NotEnoughText(void);       // "You don't have enough COINS." in the dealer's shop
void CoinShop_AddLabel(u16 x, u16 y);
void CoinShop_RemoveLabel(void);

// Specials for the scripts (data/scripts/mp_progression.inc): the price and the amounts are 32-bit, so they travel in two
// script variables, high half in VAR_0x8004 and low half in VAR_0x8005.
u16 CoinWallet_Has(void);                     // VAR_RESULT: the wallet holds at least that many coins
u16 CoinWallet_Spend(void);                   // takes them (VAR_RESULT: 1 done, 0 not enough)
u16 CoinWallet_HasRoom(void);                 // the wallet has room for that many more
u16 CoinWallet_Give(void);                    // adds them (VAR_RESULT: 1 done)
#endif
#endif // GUARD_PLATFORM_COIN_SHOP_H
