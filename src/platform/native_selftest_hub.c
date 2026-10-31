// Native PC build: the Game Corner's item hub (the owner's request and approved prices of 2026-10-31, docs/owner-decisions.md
// 30-31), run before the game starts; then it exits. Project-owned. Every answer comes from the game's own tables and code:
// the hub's eight lists (data/scripts/mp_item_hub_lists.inc, tools/mp/gen_item_hub.py), the coin shop's prices with each
// list open (src/platform/coin_shop.c: a RARE GOODS item at its RARE GOODS price, unchanged; an item the hub adds at the
// owner's approved price), the Mega Stone dealer's list, buying (coins taken, the item in the Bag, a key item SOLD OUT
// once owned, a Master Ball as often as the coins allow). The clerk's menu is played in a real game by
// tools/battle_env/item_hub_check.py; what each item does is checked by the item self-tests (balls, healing, held items,
// bag items, berries, evolution items: docs/item-audit-20261027.md).
//   --selftest-item-hub
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "coins.h"
#include "item.h"
#include "load_save.h"
#include "malloc.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "constants/characters.h"
#include "constants/items.h"
#include "native_ui/ui_engine.h"
#include "platform/coin_shop.h"

extern const u16 MauvilleCity_GameCorner_Pokemart_HubHeld[];
extern const u16 MauvilleCity_GameCorner_Pokemart_HubBattle[];
extern const u16 MauvilleCity_GameCorner_Pokemart_HubEvolution[];
extern const u16 MauvilleCity_GameCorner_Pokemart_HubBalls[];
extern const u16 MauvilleCity_GameCorner_Pokemart_HubHealing[];
extern const u16 MauvilleCity_GameCorner_Pokemart_HubBerries[];
extern const u16 MauvilleCity_GameCorner_Pokemart_HubTraining[];
extern const u16 MauvilleCity_GameCorner_Pokemart_HubOther[];
extern const u16 MauvilleCity_Mart_Pokemart_MegaStones[];

static int sChecks, sFailures;

static void Expect(const char *what, bool32 ok)
{
    sChecks++;
    if (!ok)
        sFailures++;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static void Setup(void)
{
    InitHeap(gHeap, HEAP_SIZE);
    SetSaveBlocksPointers(0);
    memset(gSaveBlock1Ptr, 0, sizeof(*gSaveBlock1Ptr));
    memset(gSaveBlock2Ptr, 0, sizeof(*gSaveBlock2Ptr));
    memset(gSaveBlock3Ptr, 0, sizeof(*gSaveBlock3Ptr));
    gSaveBlock2Ptr->encryptionKey = 0x13572468;
    SetBagItemsPointers();
    ClearBag();
    ResetPokemonStorageSystem();
}

static const struct
{
    const u16 *list;
    const char *name;
    u32 size;
} sLists[] = {
    {MauvilleCity_GameCorner_Pokemart_HubHeld, "HELD ITEMS", 62},
    {MauvilleCity_GameCorner_Pokemart_HubBattle, "BATTLE ITEMS", 96},
    {MauvilleCity_GameCorner_Pokemart_HubEvolution, "EVOLUTION & FORMS", 44},
    {MauvilleCity_GameCorner_Pokemart_HubBalls, "POKE BALLS", 27},
    {MauvilleCity_GameCorner_Pokemart_HubHealing, "HEALING", 44},
    {MauvilleCity_GameCorner_Pokemart_HubBerries, "BERRIES", 67},
    {MauvilleCity_GameCorner_Pokemart_HubTraining, "TRAINING", 63},
    {MauvilleCity_GameCorner_Pokemart_HubOther, "OTHER", 45},
};

static u32 ListSize(const u16 *list)
{
    u32 n = 0;

    while (list[n] != ITEM_NONE)
        n++;
    return n;
}

static void Lists(void)
{
    static u8 sSeen[ITEMS_COUNT];
    u32 total = 0, rare = 0, added = 0;
    bool32 once = TRUE;
    char what[200];

    memset(sSeen, 0, sizeof(sSeen));
    for (u32 l = 0; l < ARRAY_COUNT(sLists); l++)
    {
        u32 size = ListSize(sLists[l].list), unpriced = 0, wrong = 0, ordered = TRUE;

        CoinShop_OnOpen(sLists[l].list);
        for (u32 i = 0; i < size; i++)
        {
            u16 item = sLists[l].list[i];
            u32 coins = CoinShop_ItemPrice(item), goods = CoinShop_RareGoodsPrice(item), hub = CoinShop_HubPrice(item);

            if (item >= ITEMS_COUNT || sSeen[item]++)
                once = FALSE;
            if (i > 0 && item <= sLists[l].list[i - 1])
                ordered = FALSE;
            if (coins == 0 || coins == 0xFFFFFF)
                unpriced++;
            // a RARE GOODS item at its own price, an added item at the owner's; never both
            if (goods != 0 ? (coins != goods || hub != 0) : (hub == 0 || coins != hub))
                wrong++;
            rare += goods != 0;
            added += goods == 0 && hub != 0;
        }
        total += size;
        snprintf(what, sizeof(what), "%s: %u items (%u expected, the shop screen holds %u), by item number, each priced "
                 "(%u not), at its own price (%u wrong)", sLists[l].name, (unsigned)size, (unsigned)sLists[l].size,
                 (unsigned)NATIVE_SHOP_MAX_ITEMS, (unsigned)unpriced, (unsigned)wrong);
        Expect(what, size == sLists[l].size && size <= NATIVE_SHOP_MAX_ITEMS && ordered && unpriced == 0 && wrong == 0);
    }
    snprintf(what, sizeof(what), "448 items in the eight lists, each in one: the 249 RARE GOODS (%u) and 199 added (%u)",
             (unsigned)rare, (unsigned)added);
    Expect(what, total == 448 && once && rare == 249 && added == 199);
    CoinShop_OnOpen(NULL);
}

static u32 PriceIn(const u16 *list, u16 item)
{
    u32 coins;

    CoinShop_OnOpen(list);
    coins = CoinShop_ItemPrice(item);
    CoinShop_OnOpen(NULL);
    return coins;
}

static void Prices(void)
{
    static const struct { u16 item; const u16 *list; u32 coins; const char *name; } sApproved[] = {
        {ITEM_MASTER_BALL, MauvilleCity_GameCorner_Pokemart_HubBalls, 200000, "Master Ball"},
        {ITEM_RARE_CANDY, MauvilleCity_GameCorner_Pokemart_HubTraining, 5000, "Rare Candy"},
        {ITEM_ABILITY_CAPSULE, MauvilleCity_GameCorner_Pokemart_HubTraining, 5000, "Ability Capsule"},
        {ITEM_BOTTLE_CAP, MauvilleCity_GameCorner_Pokemart_HubTraining, 2500, "Bottle Cap"},
        {ITEM_HP_UP, MauvilleCity_GameCorner_Pokemart_HubTraining, 300, "HP Up (the vitamins, as the Mochi)"},
        {ITEM_CHOICE_BAND, MauvilleCity_GameCorner_Pokemart_HubBattle, 10000, "Choice Band"},
        {ITEM_LIFE_ORB, MauvilleCity_GameCorner_Pokemart_HubBattle, 10000, "Life Orb"},
        {ITEM_LEFTOVERS, MauvilleCity_GameCorner_Pokemart_HubBattle, 10000, "Leftovers"},
        {ITEM_FOCUS_SASH, MauvilleCity_GameCorner_Pokemart_HubBattle, 7500, "Focus Sash"},
        {ITEM_PSYCHIC_SEED, MauvilleCity_GameCorner_Pokemart_HubBattle, 750, "Psychic Seed (on sale, as approved)"},
        {ITEM_LIGHT_BALL, MauvilleCity_GameCorner_Pokemart_HubHeld, 5000, "Light Ball"},
        {ITEM_LUM_BERRY, MauvilleCity_GameCorner_Pokemart_HubBerries, 5000, "Lum Berry"},
        {ITEM_SITRUS_BERRY, MauvilleCity_GameCorner_Pokemart_HubBerries, 2500, "Sitrus Berry"},
        {ITEM_ULTRA_BALL, MauvilleCity_GameCorner_Pokemart_HubBalls, 150, "Ultra Ball"},
        {ITEM_POKE_BALL, MauvilleCity_GameCorner_Pokemart_HubBalls, 10, "Poke Ball"},
        {ITEM_POTION, MauvilleCity_GameCorner_Pokemart_HubHealing, 15, "Potion"},
        {ITEM_SACRED_ASH, MauvilleCity_GameCorner_Pokemart_HubHealing, 1200, "Sacred Ash"},
        {ITEM_FIRE_STONE, MauvilleCity_GameCorner_Pokemart_HubEvolution, 1500, "Fire Stone"},
        {ITEM_TOWN_MAP, MauvilleCity_GameCorner_Pokemart_HubOther, 1000, "Town Map"},
        {ITEM_HEART_SCALE, MauvilleCity_GameCorner_Pokemart_HubOther, 1000, "Heart Scale"},
    };
    static const struct { u16 item; const u16 *list; u32 coins; const char *name; } sKept[] = {
        {ITEM_ROTOM_CATALOG, MauvilleCity_GameCorner_Pokemart_HubEvolution, 15000, "Rotom Catalog"},
        {ITEM_ABILITY_PATCH, MauvilleCity_GameCorner_Pokemart_HubTraining, 15000, "Ability Patch"},
        {ITEM_GOLD_BOTTLE_CAP, MauvilleCity_GameCorner_Pokemart_HubTraining, 15000, "Gold Bottle Cap"},
        {ITEM_EXP_CANDY_XL, MauvilleCity_GameCorner_Pokemart_HubTraining, 1500, "Exp. Candy XL"},
    };
    char what[160];
    bool32 approved = TRUE, kept = TRUE;

    for (u32 i = 0; i < ARRAY_COUNT(sApproved); i++)
    {
        u32 coins = PriceIn(sApproved[i].list, sApproved[i].item);

        if (coins != sApproved[i].coins)
        {
            approved = FALSE;
            printf("     %s: %u coins, approved %u\n", sApproved[i].name, (unsigned)coins, (unsigned)sApproved[i].coins);
        }
    }
    Expect("the owner's approved prices: Master Ball 200,000, Rare Candy and Ability Capsule 5,000, Bottle Cap 2,500, the "
           "vitamins 300, Choice Band, Life Orb, Leftovers 10,000, Focus Sash 7,500, ... Potion 15, Town Map 1,000", approved);
    for (u32 i = 0; i < ARRAY_COUNT(sKept); i++)
    {
        u32 coins = PriceIn(sKept[i].list, sKept[i].item);

        if (coins != sKept[i].coins || coins != CoinShop_RareGoodsPrice(sKept[i].item))
        {
            kept = FALSE;
            printf("     %s: %u coins, before %u\n", sKept[i].name, (unsigned)coins, (unsigned)sKept[i].coins);
        }
    }
    Expect("the RARE GOODS keep their prices in the hub: Rotom Catalog, Ability Patch, Gold Bottle Cap 15,000, Exp. Candy XL "
           "1,500", kept);
    {
        u32 n = ListSize(MauvilleCity_Mart_Pokemart_MegaStones), wrong = 0;

        CoinShop_OnOpen(MauvilleCity_Mart_Pokemart_MegaStones);
        for (u32 i = 0; i < n; i++)
            wrong += CoinShop_ItemPrice(MauvilleCity_Mart_Pokemart_MegaStones[i]) != CoinShop_StonePrice(MauvilleCity_Mart_Pokemart_MegaStones[i])
                     || CoinShop_StonePrice(MauvilleCity_Mart_Pokemart_MegaStones[i]) == 0;
        CoinShop_OnOpen(NULL);
        snprintf(what, sizeof(what), "MEGA STONES: the dealer's %u stones at the dealer's prices (%u wrong)", (unsigned)n,
                 (unsigned)wrong);
        Expect(what, n == 82 && wrong == 0);
    }
    Expect("out of the coin shop, a hub item's price is money again",
           CoinShop_ItemPrice(ITEM_MASTER_BALL) == GetItemPrice(ITEM_MASTER_BALL));
}

static void Buying(void)
{
    u32 price;

    ClearBag();
    CoinShop_OnOpen(MauvilleCity_GameCorner_Pokemart_HubBalls);
    price = CoinShop_ItemPrice(ITEM_MASTER_BALL);
    SetCoins(450000);
    Expect("Master Ball: not a key item, so as many as the coins allow (the owner: buy as many as you like)",
           GetItemImportance(ITEM_MASTER_BALL) == 0);
    CoinShop_Pay(NULL, price);
    AddBagItem(ITEM_MASTER_BALL, 1);
    Expect("... one bought for 200,000: 250,000 coins left, one in the Bag", GetCoins() == 250000
           && CountTotalItemQuantityInBag(ITEM_MASTER_BALL) == 1);
    Expect("... a second can be bought (not SOLD OUT)", CoinShop_CanPay(NULL, price)
           && !(GetItemImportance(ITEM_MASTER_BALL) && CheckBagHasItem(ITEM_MASTER_BALL, 1)));
    CoinShop_Pay(NULL, price);
    AddBagItem(ITEM_MASTER_BALL, 1);
    Expect("... two in the Bag, 50,000 coins left, a third out of reach", CountTotalItemQuantityInBag(ITEM_MASTER_BALL) == 2
           && GetCoins() == 50000 && !CoinShop_CanPay(NULL, price));
    CoinShop_OnOpen(MauvilleCity_GameCorner_Pokemart_HubOther);
    Expect("Town Map: one per save - a key item, SOLD OUT once owned",
           GetItemImportance(ITEM_TOWN_MAP) != 0 && !CheckBagHasItem(ITEM_TOWN_MAP, 1));
    CoinShop_Pay(NULL, CoinShop_ItemPrice(ITEM_TOWN_MAP));
    AddBagItem(ITEM_TOWN_MAP, 1);
    Expect("... bought for 1,000 (49,000 coins left), then SOLD OUT", GetCoins() == 49000
           && GetItemImportance(ITEM_TOWN_MAP) && CheckBagHasItem(ITEM_TOWN_MAP, 1));
    CoinShop_OnOpen(NULL);
    ClearBag();
}

static void Items(void)
{
    u32 nameless = 0, keyItems = 0;

    for (u32 l = 0; l < ARRAY_COUNT(sLists); l++)
    {
        const u16 *list = sLists[l].list;

        for (u32 i = 0; list[i] != ITEM_NONE; i++)
        {
            const u8 *name = GetItemName(list[i]);

            nameless += name == NULL || name[0] == EOS || name[0] == CHAR_QUESTION_MARK;
            keyItems += CoinShop_HubPrice(list[i]) != 0 && GetItemImportance(list[i]) != 0;
        }
    }
    Expect("every hub item is a real item of the game (a name, not a placeholder)", nameless == 0);
    Expect("of the added items only the Town Map is one per save", keyItems == 1);
}

int NativeSelfTest_ItemHub(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    Setup();
    printf("selftest-item-hub: the Game Corner's item hub - its lists, the prices kept and approved, the Mega Stones, buying\n");
    Lists();
    Prices();
    Buying();
    Items();
    printf("selftest-item-hub: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}
