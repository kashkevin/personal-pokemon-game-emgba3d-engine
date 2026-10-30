// Native PC build: the Rotom Catalog (the owner's request of 2026-10-30), run before the game starts; then it exits.
// Project-owned. Every answer comes from the game's own tables and code: the Key Item's data, the Game Corner's RARE
// GOODS (the list, the coin price, a Key Item SOLD OUT once owned: src/shop.c's rule), the form table through
// TryFormChange with the catalog's choice (every form to every form, and a long chain on one Pokemon), each form's
// species data (types, Ability, base stats, pictures), what the change leaves alone, the PC's storage (a Pokemon put in
// a box and taken out) and a byte copy of the Pokemon as a save or a trade carries it, and the words the catalog's
// question uses (RotomCatalog_FormName, RotomCatalog_TypeText). The party menu's own steps (the list, the question,
// YES / NO, the signature moves) are played in a real game by tools/battle_env/rotom_check.py.
//   --selftest-rotom-catalog
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "battle.h"
#include "coins.h"
#include "event_data.h"
#include "item.h"
#include "item_menu.h"
#include "item_use.h"
#include "load_save.h"
#include "malloc.h"
#include "party_menu.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "string_util.h"
#include "constants/abilities.h"
#include "constants/characters.h"
#include "constants/items.h"
#include "constants/species.h"
#include "platform/coin_shop.h"

extern const u16 MauvilleCity_GameCorner_Pokemart_RareGoodsEvolution[];
enum Species GetFormChangeTargetSpecies(struct Pokemon *mon, enum FormChanges method);   // src/pokemon.c

static int sChecks, sFailures;

static void Expect(const char *what, bool32 ok)
{
    sChecks++;
    if (!ok)
        sFailures++;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static const char *Ascii(const u8 *text)
{
    static char out[256];
    u32 n = 0;

    for (; text != NULL && *text != EOS && n < sizeof(out) - 1; text++)
    {
        u8 c = *text;

        if (c >= CHAR_A && c <= CHAR_Z)
            out[n++] = 'A' + (c - CHAR_A);
        else if (c >= CHAR_a && c <= CHAR_z)
            out[n++] = 'a' + (c - CHAR_a);
        else if (c >= CHAR_0 && c <= CHAR_9)
            out[n++] = '0' + (c - CHAR_0);
        else if (c == CHAR_SLASH)
            out[n++] = '/';
        else
            out[n++] = ' ';
    }
    out[n] = 0;
    return out;
}

static void Setup(void)
{
    InitHeap(gHeap, HEAP_SIZE);
    SetSaveBlocksPointers(0);
    memset(gSaveBlock1Ptr, 0, sizeof(*gSaveBlock1Ptr));
    memset(gSaveBlock2Ptr, 0, sizeof(*gSaveBlock2Ptr));
    memset(gSaveBlock3Ptr, 0, sizeof(*gSaveBlock3Ptr));
    SetBagItemsPointers();
    ClearBag();
    ResetPokemonStorageSystem();
    ZeroPlayerPartyMons();
    ZeroEnemyPartyMons();
    gSaveBlock2Ptr->playerTrainerId[0] = 0x39;
    gSaveBlock2Ptr->playerTrainerId[1] = 0x30;
    gBattleTypeFlags = 0;
}

// The catalog's six choices, in the list's order (the form table's param2: 0 Rotom ... 5 Mow Rotom)
static const struct
{
    u16 species;
    const char *name;
    u8 type2;
    u8 hp, atk, def, spAtk, spDef, speed;
    const char *types;
} sForms[] = {
    {SPECIES_ROTOM,       "Normal Rotom", TYPE_GHOST,  50, 50,  77, 95,  77,  91, "Electric/Ghost"},
    {SPECIES_ROTOM_HEAT,  "Heat Rotom",   TYPE_FIRE,   50, 65, 107, 105, 107, 86, "Electric/Fire"},
    {SPECIES_ROTOM_WASH,  "Wash Rotom",   TYPE_WATER,  50, 65, 107, 105, 107, 86, "Electric/Water"},
    {SPECIES_ROTOM_FROST, "Frost Rotom",  TYPE_ICE,    50, 65, 107, 105, 107, 86, "Electric/Ice"},
    {SPECIES_ROTOM_FAN,   "Fan Rotom",    TYPE_FLYING, 50, 65, 107, 105, 107, 86, "Electric/Flying"},
    {SPECIES_ROTOM_MOW,   "Mow Rotom",    TYPE_GRASS,  50, 65, 107, 105, 107, 86, "Electric/Grass"},
};

// A party Pokemon's type as its species gives it (no battle has changed it)
static enum Type MonType(struct Pokemon *mon, u8 slot)
{
    return GetSpeciesType(GetMonData(mon, MON_DATA_SPECIES), slot);
}

static const u32 sStatFields[NUM_STATS] = {MON_DATA_MAX_HP, MON_DATA_ATK, MON_DATA_DEF, MON_DATA_SPEED, MON_DATA_SPATK,
                                           MON_DATA_SPDEF};

static void PartyRotom(struct Pokemon *mon, u16 species)
{
    static const u16 sMoves[MAX_MON_MOVES] = {MOVE_THUNDER_SHOCK, MOVE_THUNDER_WAVE, MOVE_NONE, MOVE_NONE};
    u8 item[2] = {ITEM_SITRUS_BERRY & 0xFF, ITEM_SITRUS_BERRY >> 8};
    u8 friendship = 123;
    u32 i;

    CreateMon(mon, species, 37, 0x2468ACE1, OTID_STRUCT_PLAYER_ID);
    for (i = 0; i < NUM_STATS; i++)
    {
        u8 iv = 5 + 4 * i, ev = 40 * i;

        SetMonData(mon, MON_DATA_HP_IV + i, &iv);
        SetMonData(mon, MON_DATA_HP_EV + i, &ev);
    }
    for (i = 0; i < MAX_MON_MOVES; i++)
        SetMonMoveSlot(mon, sMoves[i], i);
    SetMonData(mon, MON_DATA_HELD_ITEM, item);
    SetMonData(mon, MON_DATA_FRIENDSHIP, &friendship);
    CalculateMonStats(mon);
}

// The catalog's form change as the party menu makes it (TryMultichoiceFormChange's TryFormChange), the item and the
// choice set as the bag and the list set them
static bool32 Catalog(struct Pokemon *mon, u32 choice)
{
    bool32 changed;

    gSpecialVar_ItemId = ITEM_ROTOM_CATALOG;
    gSpecialVar_Result = choice;
    changed = TryFormChange(mon, FORM_CHANGE_ITEM_USE_MULTICHOICE, B_TRAINER_PLAYER);
    gSpecialVar_ItemId = ITEM_NONE;
    return changed;
}

// Everything but the species and the stats: what a form change must leave as it was
struct Kept
{
    u32 personality, otId, exp, level, friendship, item, ability, nature;
    u32 moves[MAX_MON_MOVES], ivs[NUM_STATS], evs[NUM_STATS];
};

static void Keep(struct Pokemon *mon, struct Kept *k)
{
    u32 i;

    memset(k, 0, sizeof(*k));
    k->personality = GetMonData(mon, MON_DATA_PERSONALITY);
    k->otId = GetMonData(mon, MON_DATA_OT_ID);
    k->exp = GetMonData(mon, MON_DATA_EXP);
    k->level = GetMonData(mon, MON_DATA_LEVEL);
    k->friendship = GetMonData(mon, MON_DATA_FRIENDSHIP);
    k->item = GetMonData(mon, MON_DATA_HELD_ITEM);
    k->ability = GetMonData(mon, MON_DATA_ABILITY_NUM);
    k->nature = GetNature(mon);
    for (i = 0; i < MAX_MON_MOVES; i++)
        k->moves[i] = GetMonData(mon, MON_DATA_MOVE1 + i);
    for (i = 0; i < NUM_STATS; i++)
    {
        k->ivs[i] = GetMonData(mon, MON_DATA_HP_IV + i);
        k->evs[i] = GetMonData(mon, MON_DATA_HP_EV + i);
    }
}

// The stats a Pokemon of `species` with this one's level, nature, IVs and effort has (a fresh one, calculated)
static bool32 StatsAreTheFormsOwn(struct Pokemon *mon, u16 species)
{
    struct Pokemon *fresh = AllocZeroed(sizeof(*fresh));
    bool32 same = TRUE;
    u32 i;

    *fresh = *mon;
    SetMonData(fresh, MON_DATA_SPECIES, &species);
    CalculateMonStats(fresh);
    for (i = 0; i < NUM_STATS; i++)
        same &= GetMonData(fresh, sStatFields[i]) == GetMonData(mon, sStatFields[i]);
    Free(fresh);
    return same;
}

static void FormData(void)
{
    u32 i;
    char line[240];

    for (i = 0; i < ARRAY_COUNT(sForms); i++)
    {
        u16 s = sForms[i].species;
        const struct SpeciesInfo *info = &gSpeciesInfo[s];
        u8 typeText[64];

        snprintf(line, sizeof(line), "%s (species %u): Electric/%u, Levitate, base %u/%u/%u/%u/%u/%u, enabled", sForms[i].name,
                 s, sForms[i].type2, sForms[i].hp, sForms[i].atk, sForms[i].def, sForms[i].spAtk, sForms[i].spDef,
                 sForms[i].speed);
        Expect(line, IsSpeciesEnabled(s) && GetSpeciesType(s, 0) == TYPE_ELECTRIC && GetSpeciesType(s, 1) == sForms[i].type2
                     && GetSpeciesAbility(s, 0) == ABILITY_LEVITATE
                     && GetSpeciesBaseHP(s) == sForms[i].hp && GetSpeciesBaseAttack(s) == sForms[i].atk
                     && GetSpeciesBaseDefense(s) == sForms[i].def && GetSpeciesBaseSpAttack(s) == sForms[i].spAtk
                     && GetSpeciesBaseSpDefense(s) == sForms[i].spDef && GetSpeciesBaseSpeed(s) == sForms[i].speed
                     && GET_BASE_SPECIES_ID(s) == SPECIES_ROTOM);
        // the pictures the field, the party, the summary and the 3D battle draw (platform/battle_sprites.h reads the
        // front and back pictures the battle loads); an appliance form's are its own, not Rotom's
        snprintf(line, sizeof(line), "%s: front, back and icon pictures and palettes, its own", sForms[i].name);
        Expect(line, info->frontPic != NULL && info->backPic != NULL && info->iconSprite != NULL && info->palette != NULL
                     && info->shinyPalette != NULL
                     && (i == 0 || (info->frontPic != gSpeciesInfo[SPECIES_ROTOM].frontPic
                                    && info->backPic != gSpeciesInfo[SPECIES_ROTOM].backPic
                                    && info->iconSprite != gSpeciesInfo[SPECIES_ROTOM].iconSprite)));
        snprintf(line, sizeof(line), "the list's row %u and the question name \"%s\"", (unsigned)i, sForms[i].name);
        Expect(line, strcmp(Ascii(RotomCatalog_FormName(i)), sForms[i].name) == 0);
        RotomCatalog_TypeText(s, typeText);
        snprintf(line, sizeof(line), "the question's type for %s: \"%s\" (rule %s)", sForms[i].name, Ascii(typeText),
                 sForms[i].types);
        Expect(line, strcmp(Ascii(typeText), sForms[i].types) == 0);
    }
}

static void EveryChange(void)
{
    struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][0];
    u32 from, to;
    char line[240];

    for (from = 0; from < ARRAY_COUNT(sForms); from++)
    {
        for (to = 0; to < ARRAY_COUNT(sForms); to++)
        {
            struct Kept before, after;
            u16 asked;
            bool32 changed;

            ZeroPlayerPartyMons();
            PartyRotom(mon, sForms[from].species);
            gPartiesCount[B_TRAINER_PLAYER] = 1;
            Keep(mon, &before);
            gSpecialVar_ItemId = ITEM_ROTOM_CATALOG;
            gSpecialVar_Result = to;
            asked = GetFormChangeTargetSpecies(mon, FORM_CHANGE_ITEM_USE_MULTICHOICE);   // the question's form
            gSpecialVar_ItemId = ITEM_NONE;
            changed = Catalog(mon, to);
            Keep(mon, &after);
            snprintf(line, sizeof(line), "%s -> %s: %s, species %u (the question named %u), %s, the rest kept",
                     sForms[from].name, sForms[to].name, changed ? "changed" : "no change",
                     (unsigned)GetMonData(mon, MON_DATA_SPECIES), (unsigned)asked,
                     from == to ? "already that form" : "its types, Ability and stats");
            Expect(line, changed == (from != to) && GetMonData(mon, MON_DATA_SPECIES) == sForms[to].species
                         && asked == sForms[to].species && RotomCatalog_IsRotom(mon)
                         && MonType(mon, 0) == TYPE_ELECTRIC && MonType(mon, 1) == sForms[to].type2
                         && GetMonAbility(mon) == ABILITY_LEVITATE
                         && StatsAreTheFormsOwn(mon, sForms[to].species)
                         && memcmp(&before, &after, sizeof(before)) == 0);
        }
    }
}

// One Rotom changed over and over, back and forth, and to the form it has: its species each time, the rest never
static void LongChain(void)
{
    static const u8 sChain[] = {1, 2, 3, 4, 5, 0, 5, 4, 3, 2, 1, 0, 0, 3, 3, 1, 4, 2, 5, 1, 0, 2, 0, 4, 1, 5, 3, 0};
    struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][0];
    struct Kept first, now;
    u32 i, current = 0, wrong = 0;
    char line[200];

    ZeroPlayerPartyMons();
    PartyRotom(mon, SPECIES_ROTOM);
    gPartiesCount[B_TRAINER_PLAYER] = 1;
    Keep(mon, &first);
    for (i = 0; i < ARRAY_COUNT(sChain); i++)
    {
        bool32 changed = Catalog(mon, sChain[i]);

        Keep(mon, &now);
        if (changed != (sChain[i] != current) || GetMonData(mon, MON_DATA_SPECIES) != sForms[sChain[i]].species
         || !StatsAreTheFormsOwn(mon, sForms[sChain[i]].species) || memcmp(&first, &now, sizeof(first)) != 0)
            wrong++;
        current = sChain[i];
    }
    snprintf(line, sizeof(line), "one Rotom through %u changes in a row (each form from each, twice its own): %u wrong",
             (unsigned)ARRAY_COUNT(sChain), (unsigned)wrong);
    Expect(line, wrong == 0);
}

static void NotRotom(void)
{
    struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][0];
    bool8 isEgg = TRUE;

    ZeroPlayerPartyMons();
    PartyRotom(mon, SPECIES_PIKACHU);
    Expect("the catalog on a Pikachu: not a Rotom, no change",
           !RotomCatalog_IsRotom(mon) && !Catalog(mon, 1) && GetMonData(mon, MON_DATA_SPECIES) == SPECIES_PIKACHU);
    PartyRotom(mon, SPECIES_ROTOM);
    SetMonData(mon, MON_DATA_IS_EGG, &isEgg);
    Expect("the catalog on a Rotom Egg: not opened, no change",
           !RotomCatalog_IsRotom(mon) && !Catalog(mon, 2) && GetMonData(mon, MON_DATA_SPECIES) == SPECIES_ROTOM);
}

// The PC: a Wash Rotom put in a box and taken out (the storage's own conversions), and the Pokemon's bytes copied as a
// save and a world trade carry them
static void Storage(void)
{
    struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][0];
    struct Pokemon *back = AllocZeroed(sizeof(*back));
    struct Kept before, after;
    u32 stats[NUM_STATS], i;
    bool32 statsSame = TRUE;
    u8 result;

    ResetPokemonStorageSystem();
    ZeroPlayerPartyMons();
    PartyRotom(mon, SPECIES_ROTOM);
    Catalog(mon, 2);
    Keep(mon, &before);
    for (i = 0; i < NUM_STATS; i++)
        stats[i] = GetMonData(mon, sStatFields[i]);
    result = CopyMonToPC(mon);
    Expect("Wash Rotom put in the PC: box 1, first slot, a Wash Rotom",
           result != MON_CANT_GIVE && GetBoxMonDataAt(0, 0, MON_DATA_SPECIES) == SPECIES_ROTOM_WASH);
    BoxMonToMon(GetBoxedMonPtr(0, 0), back);
    Keep(back, &after);
    for (i = 0; i < NUM_STATS; i++)
        statsSame &= GetMonData(back, sStatFields[i]) == stats[i];
    Expect("taken out of the PC: still a Wash Rotom, its stats, moves, IVs and the rest as they were",
           GetMonData(back, MON_DATA_SPECIES) == SPECIES_ROTOM_WASH && statsSame && memcmp(&before, &after, sizeof(before)) == 0);
    memcpy(back, mon, sizeof(*back));
    Expect("its bytes copied (a save's, a trade's): a Wash Rotom, not a bad egg",
           GetMonData(back, MON_DATA_SPECIES) == SPECIES_ROTOM_WASH && !GetMonData(back, MON_DATA_SANITY_IS_BAD_EGG)
           && MonType(back, 1) == TYPE_WATER);
    Free(back);
}

static void ShopAndItem(void)
{
    const u16 *item;
    bool32 listed = FALSE;

    for (item = MauvilleCity_GameCorner_Pokemart_RareGoodsEvolution; *item != ITEM_NONE; item++)
        listed |= *item == ITEM_ROTOM_CATALOG;
    Expect("RARE GOODS > EVOLUTION lists the Rotom Catalog", listed);
    Expect("the Rotom Catalog: a Key Item (KEY ITEMS pocket), one each, used on a party Pokemon from the Bag, "
           "no use in battle",
           GetItemPocket(ITEM_ROTOM_CATALOG) == POCKET_KEY_ITEMS && GetItemImportance(ITEM_ROTOM_CATALOG)
           && GetItemType(ITEM_ROTOM_CATALOG) == ITEM_USE_PARTY_MENU
           && GetItemFieldFunc(ITEM_ROTOM_CATALOG) == ItemUseOutOfBattle_RotomCatalog
           && GetItemBattleUsage(ITEM_ROTOM_CATALOG) == 0);

    CoinShop_OnOpen(MauvilleCity_GameCorner_Pokemart_RareGoodsEvolution);
    Expect("its price at the prize counter: 15,000 coins", CoinShop_ItemPrice(ITEM_ROTOM_CATALOG) == 15000);
    ClearBag();
    SetCoins(14999);
    Expect("14,999 coins: not enough", !CoinShop_CanPay(NULL, CoinShop_ItemPrice(ITEM_ROTOM_CATALOG)));
    SetCoins(20000);
    Expect("20,000 coins: enough; not yet owned, so not SOLD OUT",
           CoinShop_CanPay(NULL, CoinShop_ItemPrice(ITEM_ROTOM_CATALOG))
           && !(GetItemImportance(ITEM_ROTOM_CATALOG) && (CheckBagHasItem(ITEM_ROTOM_CATALOG, 1) || CheckPCHasItem(ITEM_ROTOM_CATALOG, 1))));
    CoinShop_Pay(NULL, CoinShop_ItemPrice(ITEM_ROTOM_CATALOG));
    AddBagItem(ITEM_ROTOM_CATALOG, 1);
    Expect("bought: 5,000 coins left, one in the Bag, and now SOLD OUT (src/shop.c's rule for a Key Item owned)",
           GetCoins() == 5000 && CountTotalItemQuantityInBag(ITEM_ROTOM_CATALOG) == 1
           && GetItemImportance(ITEM_ROTOM_CATALOG) && CheckBagHasItem(ITEM_ROTOM_CATALOG, 1));
    CoinShop_OnOpen(NULL);
    Expect("out of the coin shop: prices are money again", CoinShop_ItemPrice(ITEM_ROTOM_CATALOG) == GetItemPrice(ITEM_ROTOM_CATALOG));
}

int NativeSelfTest_RotomCatalog(void)
{
    Setup();
    printf("selftest-rotom-catalog: the Rotom Catalog - RARE GOODS, the six forms, what a change keeps, the PC\n");
    ShopAndItem();
    FormData();
    EveryChange();
    LongChain();
    NotRotom();
    Storage();
    printf("selftest-rotom-catalog: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}
