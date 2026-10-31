// Native PC build: the TM catalogue (the owner's TM request of 2026-10-31, docs/owner-decisions.md 26-29), run before the
// game starts; then it exits. Project-owned. Every answer comes from the game's own tables and code: the 168 TMs and 8 HMs
// (include/constants/tms_hms.h, tms_hms_pc.h, the items 874-941), the Game Corner's TM CATALOGUE (its four lists and the
// coin prices, src/platform/coin_shop.c), who each TM can teach (pokemon.c CanLearnTmMove, data/pokemon/
// tm_learnsets_pc.h: the owner's Gen 1-6 rule), the move-switching list of TMs (move_relearner.c), and the TM case and its
// save record (item.c: TM01-TM50 and the HMs in the save's 64 TM pocket slots, TM51 and up in SaveBlock3's record). The
// shop's own screens and teaching a TM from the Bag are played in a real game by tools/battle_env/tm_catalogue_check.py.
//   --selftest-tm-catalogue
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "coins.h"
#include "item.h"
#include "load_save.h"
#include "malloc.h"
#include "move.h"
#include "move_relearner.h"
#include "party_menu.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "constants/characters.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "constants/species.h"
#include "platform/coin_shop.h"

extern const u16 MauvilleCity_GameCorner_Pokemart_TmPhysical[];
extern const u16 MauvilleCity_GameCorner_Pokemart_TmSpecial[];
extern const u16 MauvilleCity_GameCorner_Pokemart_TmStatus[];
extern const u16 MauvilleCity_GameCorner_Pokemart_TmUtility[];

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
    static char out[64];
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
    gSaveBlock2Ptr->encryptionKey = 0x5A17C3E9;   // quantities are stored encrypted, as in a real save
    SetBagItemsPointers();
    ClearBag();
    ResetPokemonStorageSystem();
    ZeroPlayerPartyMons();
    gSaveBlock2Ptr->playerTrainerId[0] = 0x39;
    gSaveBlock2Ptr->playerTrainerId[1] = 0x30;
    gRelearnMode = RELEARN_MODE_NONE;
}

// TM n's item: TM01-TM100 the expansion's (582-681), TM101-TM168 the PC game's (874-941)
static u16 TmItem(u32 n)
{
    return n <= 100 ? ITEM_TM01 + (n - 1) : 773 + n;
}

// ---- The items ---------------------------------------------------------------------------------------------------------
static void Items(void)
{
    bool32 numbers = TRUE, moves = TRUE, pocket = TRUE, reusable = TRUE, names = TRUE, distinct = TRUE, described = TRUE;
    char name[8];

    Expect("168 TMs and 8 HMs (176 machines)", NUM_TECHNICAL_MACHINES == 168 && NUM_HIDDEN_MACHINES == 8
                                                && NUM_ALL_MACHINES == 176);
    Expect("TM101 is item 874 and TM168 item 941, inside the item table", ITEM_TM101 == 874 && ITEM_TM168 == 941
                                                                           && ITEM_TM168 < ITEMS_COUNT);
    for (u32 n = 1; n <= NUM_TECHNICAL_MACHINES; n++)
    {
        u16 item = TmItem(n);
        enum Move move = GetTMHMMoveId(n);

        numbers &= GetTMHMItemId(n) == item && GetItemTMHMIndex(item) == n;
        moves &= move != MOVE_NONE && GetItemTMHMMoveId(item) == move;
        pocket &= GetItemPocket(item) == POCKET_TM_HM;
        reusable &= GetItemImportance(item) != 0;
        snprintf(name, sizeof(name), "TM%02u", (unsigned)n);
        names &= strcmp(Ascii(GetItemName(item)), name) == 0;
        for (u32 m = 1; m < n; m++)
            distinct &= GetTMHMMoveId(m) != move;
        if (n > 50)
            described &= GetItemDescription(item) == GetMoveDescription(move);
    }
    Expect("each TM's item and number agree (GetTMHMItemId, GetItemTMHMIndex)", numbers);
    Expect("each TM teaches its move", moves);
    Expect("each TM goes in the TM pocket", pocket);
    Expect("each TM is never used up (important: a shop shows one the player has as SOLD OUT)", reusable);
    Expect("each TM is named by its number (TM01 ... TM168)", names);
    Expect("no move has two TMs", distinct);
    Expect("TM51-TM168 are described by their moves", described);
    Expect("TM01-TM50 keep their own words", GetItemDescription(ITEM_TM01) != GetMoveDescription(MOVE_FOCUS_PUNCH));
    Expect("the catalogue's numbering: TM51 Roost, TM76 Stealth Rock, TM168 Nightmare",
           GetTMHMMoveId(51) == MOVE_ROOST && GetTMHMMoveId(76) == MOVE_STEALTH_ROCK && GetTMHMMoveId(168) == MOVE_NIGHTMARE);
    Expect("the HMs come after the TMs: HM01 Cut (machine 169) ... HM08 Dive (176)",
           GetTMHMItemId(169) == ITEM_HM01 && GetTMHMMoveId(169) == MOVE_CUT && GetTMHMItemId(176) == ITEM_HM08
           && GetTMHMMoveId(176) == MOVE_DIVE && GetItemTMHMIndex(ITEM_HM08) == 176);
}

// ---- The Game Corner's TM CATALOGUE ------------------------------------------------------------------------------------
static const u16 *const sLists[] = {MauvilleCity_GameCorner_Pokemart_TmPhysical, MauvilleCity_GameCorner_Pokemart_TmSpecial,
                                    MauvilleCity_GameCorner_Pokemart_TmStatus, MauvilleCity_GameCorner_Pokemart_TmUtility};
static const char *const sListNames[] = {"PHYSICAL ATTACKS", "SPECIAL ATTACKS", "STATUS & SETUP", "DEFENSE & UTILITY"};
static const u32 sListSizes[] = {65, 52, 28, 23};

static bool32 IsTier(u32 coins)
{
    return coins == 1000 || coins == 2500 || coins == 5000 || coins == 7500 || coins == 10000;
}

static void Catalogue(void)
{
    u8 seen[NUM_TECHNICAL_MACHINES + 1] = {0};
    u32 tiers[5] = {0};
    char what[160];
    bool32 once = TRUE;

    for (u32 l = 0; l < ARRAY_COUNT(sLists); l++)
    {
        u32 count = 0, previous = 0;
        bool32 kind = TRUE, ordered = TRUE, priced = TRUE;

        CoinShop_OnOpen(sLists[l]);
        for (const u16 *item = sLists[l]; *item != ITEM_NONE; item++, count++)
        {
            u32 n = GetItemTMHMIndex(*item);
            enum DamageCategory category = GetMoveCategory(GetItemTMHMMoveId(*item));

            if (n == 0 || n > NUM_TECHNICAL_MACHINES || seen[n]++)
                once = FALSE;
            ordered &= n > previous;
            previous = n;
            kind &= l == 0 ? category == DAMAGE_CATEGORY_PHYSICAL
                  : l == 1 ? category == DAMAGE_CATEGORY_SPECIAL : category == DAMAGE_CATEGORY_STATUS;
            // in the open list the coin shop asks the catalogue's price (no other price list names a TM)
            priced &= CoinShop_ItemPrice(*item) == CoinShop_TmPrice(*item) && CoinShop_TmPrice(*item) != 0;
        }
        snprintf(what, sizeof(what), "%s: %u TMs (%u expected), by number, each of its kind, at the catalogue's price",
                 sListNames[l], (unsigned)count, (unsigned)sListSizes[l]);
        Expect(what, count == sListSizes[l] && ordered && kind && priced);
    }
    for (u32 n = 1; n <= NUM_TECHNICAL_MACHINES; n++)
        once &= seen[n] == 1;
    Expect("every TM is in exactly one list (168)", once);

    // the owner's prices: 1,000 / 2,500 / 5,000 / 7,500 / 10,000 by usefulness; the five the prize counter sold keep theirs
    {
        static const struct { u16 item; u32 coins; } sPrizes[] = {
            {ITEM_TM_DOUBLE_TEAM, 225}, {ITEM_TM_PSYCHIC, 525}, {ITEM_TM_FLAMETHROWER, 600}, {ITEM_TM_THUNDERBOLT, 600},
            {ITEM_TM_ICE_BEAM, 600},
        };
        bool32 tiered = TRUE, prizes = TRUE;

        for (u32 n = 1; n <= NUM_TECHNICAL_MACHINES; n++)
        {
            u16 item = TmItem(n);
            u32 coins = CoinShop_TmPrice(item);
            bool32 prize = FALSE;

            for (u32 i = 0; i < ARRAY_COUNT(sPrizes); i++)
            {
                if (sPrizes[i].item == item)
                {
                    prize = TRUE;
                    prizes &= coins == sPrizes[i].coins;
                }
            }
            if (!prize)
            {
                tiered &= IsTier(coins);
                tiers[coins == 1000 ? 0 : coins == 2500 ? 1 : coins == 5000 ? 2 : coins == 7500 ? 3 : 4]++;
            }
        }
        Expect("the five TM PRIZES cost the same in the catalogue (TM32 225, TM29 525, TM35/TM24/TM13 600 coins)", prizes);
        snprintf(what, sizeof(what), "the other 163 at the owner's tiers: 1,000 x%u, 2,500 x%u, 5,000 x%u, 7,500 x%u, "
                 "10,000 x%u", (unsigned)tiers[0], (unsigned)tiers[1], (unsigned)tiers[2], (unsigned)tiers[3],
                 (unsigned)tiers[4]);
        Expect(what, tiered && tiers[0] + tiers[1] + tiers[2] + tiers[3] + tiers[4] == 163
                     && tiers[0] == 40 && tiers[1] == 47 && tiers[2] == 36 && tiers[3] == 27 && tiers[4] == 13);
        Expect("examples: TM26 Earthquake 10,000, TM76 Stealth Rock 10,000, TM51 Roost 10,000, TM143 Metronome 1,000",
               CoinShop_TmPrice(ITEM_TM_EARTHQUAKE) == 10000 && CoinShop_TmPrice(ITEM_TM_STEALTH_ROCK) == 10000
               && CoinShop_TmPrice(ITEM_TM_ROOST) == 10000 && CoinShop_TmPrice(ITEM_TM_METRONOME) == 1000);
        Expect("an HM or another item has no catalogue price", CoinShop_TmPrice(ITEM_HM_SURF) == 0
                                                               && CoinShop_TmPrice(ITEM_POKE_BALL) == 0);
    }

    // buying: coins taken, the TM in the Bag, then SOLD OUT (src/shop.c's rule for an important item the player has)
    ClearBag();
    CoinShop_OnOpen(MauvilleCity_GameCorner_Pokemart_TmUtility);
    SetCoins(9999);
    Expect("9,999 coins: TM76 Stealth Rock (10,000) is out of reach",
           !CoinShop_CanPay(NULL, CoinShop_ItemPrice(ITEM_TM_STEALTH_ROCK)));
    SetCoins(12000);
    Expect("12,000 coins: enough, and not SOLD OUT before", CoinShop_CanPay(NULL, CoinShop_ItemPrice(ITEM_TM_STEALTH_ROCK))
           && !(GetItemImportance(ITEM_TM_STEALTH_ROCK) && CheckBagHasItem(ITEM_TM_STEALTH_ROCK, 1)));
    CoinShop_Pay(NULL, CoinShop_ItemPrice(ITEM_TM_STEALTH_ROCK));
    AddBagItem(ITEM_TM_STEALTH_ROCK, 1);
    Expect("bought: 2,000 coins left, one TM76 in the Bag, and now SOLD OUT",
           GetCoins() == 2000 && CountTotalItemQuantityInBag(ITEM_TM_STEALTH_ROCK) == 1
           && GetItemImportance(ITEM_TM_STEALTH_ROCK) && CheckBagHasItem(ITEM_TM_STEALTH_ROCK, 1));
    ClearBag();
    AddPCItem(ITEM_TM_ROOST, 1);
    Expect("a TM in the PC's item storage is SOLD OUT too", GetItemImportance(ITEM_TM_ROOST) && CheckPCHasItem(ITEM_TM_ROOST, 1));
    CoinShop_OnOpen(NULL);
    Expect("out of the coin shop: a TM's price is money again", CoinShop_ItemPrice(ITEM_TM_ROOST) == GetItemPrice(ITEM_TM_ROOST));
    memset(gSaveBlock1Ptr->pcItems, 0, sizeof(gSaveBlock1Ptr->pcItems));
}

// ---- Who each TM can teach (the owner's rule) --------------------------------------------------------------------------
static u32 TmCount(enum Species species)
{
    u32 count = 0;

    for (u32 n = 1; n <= NUM_TECHNICAL_MACHINES; n++)
        count += CanLearnTmMove(species, GetTMHMMoveId(n)) != FALSE;
    return count;
}

static bool32 SameTms(enum Species a, enum Species b)
{
    for (u32 n = 1; n <= NUM_TECHNICAL_MACHINES; n++)
    {
        if ((CanLearnTmMove(a, GetTMHMMoveId(n)) != FALSE) != (CanLearnTmMove(b, GetTMHMMoveId(n)) != FALSE))
            return FALSE;
    }
    return TRUE;
}

static void Compatibility(void)
{
    u32 species = 0, pairs = 0;
    char what[160];
    bool32 hms = TRUE;

    for (u32 s = 1; s < NUM_SPECIES; s++)
    {
        u32 count = TmCount(s);

        species += count != 0;
        pairs += count;
    }
    snprintf(what, sizeof(what), "the table: %u species (forms counted) learn a TM, %u species-TM pairs (the generator: "
             "1536, 52684)", (unsigned)species, (unsigned)pairs);
    Expect(what, species == 1536 && pairs == 52684);

    Expect("Pikachu: TM24 Thunderbolt (a TM in every generation)", CanLearnTmMove(SPECIES_PIKACHU, MOVE_THUNDERBOLT));
    Expect("Magikarp and Smeargle: no TM", TmCount(SPECIES_MAGIKARP) == 0 && TmCount(SPECIES_SMEARGLE) == 0);
    Expect("Mew: all 168", TmCount(SPECIES_MEW) == 168);
    Expect("Chansey: TM148 Soft-Boiled (Gen 1's TM41); Blissey never had it by TM",
           CanLearnTmMove(SPECIES_CHANSEY, MOVE_SOFT_BOILED) && !CanLearnTmMove(SPECIES_BLISSEY, MOVE_SOFT_BOILED));
    Expect("a move that left the TMs later still counts: Gyarados TM Dragon Rage (Gen 1's TM23)",
           CanLearnTmMove(SPECIES_GYARADOS, MOVE_DRAGON_RAGE));
    Expect("the owner's extension: Ferrothorn and Ferroseed learn TM76 Stealth Rock",
           CanLearnTmMove(SPECIES_FERROTHORN, MOVE_STEALTH_ROCK) && CanLearnTmMove(SPECIES_FERROSEED, MOVE_STEALTH_ROCK));
    Expect("... and no other extension: Treecko gets no TM76", !CanLearnTmMove(SPECIES_TREECKO, MOVE_STEALTH_ROCK));
    Expect("a tutor still teaches what the TM can't: Body Slam to Treecko (Emerald's tutor), not by TM",
           !CanLearnTmMove(SPECIES_TREECKO, MOVE_BODY_SLAM) && CanLearnTeachableMove(SPECIES_TREECKO, MOVE_BODY_SLAM));
    Expect("forms: Wash Rotom as Rotom, Mega Charizard X as Charizard, Origin Dialga as Dialga",
           SameTms(SPECIES_ROTOM_WASH, SPECIES_ROTOM) && SameTms(SPECIES_CHARIZARD_MEGA_X, SPECIES_CHARIZARD)
           && SameTms(SPECIES_DIALGA_ORIGIN, SPECIES_DIALGA) && TmCount(SPECIES_DIALGA) > 0);
    Expect("a Pokemon first seen after Gen 6 keeps its own games' list: Kleavor (Scyther + Black Augurite) TM80 Rock Slide,"
           " TM75 Swords Dance, no Thunderbolt",
           CanLearnTmMove(SPECIES_KLEAVOR, MOVE_ROCK_SLIDE) && CanLearnTmMove(SPECIES_KLEAVOR, MOVE_SWORDS_DANCE)
           && !CanLearnTmMove(SPECIES_KLEAVOR, MOVE_THUNDERBOLT)
           && CanLearnTmMove(SPECIES_KLEAVOR, MOVE_ROCK_SLIDE) == CanLearnTeachableMove(SPECIES_KLEAVOR, MOVE_ROCK_SLIDE));
    for (u32 s = 1; s < NUM_SPECIES; s++)
    {
        if (!IsSpeciesEnabled(s) || s == SPECIES_EGG)   // no teachable list to read (a TM's bit needs none: above)
            continue;
        for (u32 h = NUM_TECHNICAL_MACHINES + 1; h <= NUM_ALL_MACHINES; h++)
            hms &= (CanLearnTmMove(s, GetTMHMMoveId(h)) != FALSE) == (CanLearnTeachableMove(s, GetTMHMMoveId(h)) != FALSE);
    }
    Expect("the HMs keep their own lists (every enabled species, HM01-HM08)", hms);
    Expect("no species: no TM", TmCount(SPECIES_NONE) == 0);
}

// ---- Move switching: the list of TMs -----------------------------------------------------------------------------------
static bool32 ListHas(struct BoxPokemon *mon, enum Move move, u32 *countOut)
{
    u16 moves[MAX_RELEARNER_MOVES];
    u32 count = MoveRelearner_GetListMoves(mon, MOVE_RELEARNER_TM_MOVES, moves);
    bool32 has = FALSE;

    for (u32 i = 0; i < count; i++)
        has |= moves[i] == move;
    if (countOut != NULL)
        *countOut = count;
    return has;
}

static void SwitchList(void)
{
    struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][0];
    u32 count = 0, known = 0, high = 0, low = 0;
    char what[160];

    ClearBag();
    CreateMon(mon, SPECIES_PIKACHU, 30, 0x1234, OTID_STRUCT_PLAYER_ID);
    GiveMonInitialMoveset(mon);
    CalculatePlayerPartyCount();
    for (u32 n = 101; n <= NUM_TECHNICAL_MACHINES && (high == 0 || low == 0); n++)
    {
        if (CanLearnTmMove(SPECIES_PIKACHU, GetTMHMMoveId(n)) && !MonKnowsMove(mon, GetTMHMMoveId(n)))
            high = high ? high : n;
        else if (!CanLearnTmMove(SPECIES_PIKACHU, GetTMHMMoveId(n)))
            low = low ? low : n;
    }
    Expect("Pikachu: a TM past TM100 it can learn and one it can't", high != 0 && low != 0);
    Expect("... neither on its TM list while not in the Bag",
           !ListHas(&mon->box, GetTMHMMoveId(high), NULL) && !ListHas(&mon->box, GetTMHMMoveId(low), NULL));
    AddBagItem(TmItem(high), 1);
    AddBagItem(TmItem(low), 1);
    snprintf(what, sizeof(what), "... in the Bag: TM%u on the list, TM%u not", (unsigned)high, (unsigned)low);
    Expect(what, ListHas(&mon->box, GetTMHMMoveId(high), NULL) && !ListHas(&mon->box, GetTMHMMoveId(low), NULL));

    // every TM and HM in the Bag: Mew's list has each it does not know (the list holds 200)
    ClearBag();
    for (u32 n = 1; n <= NUM_ALL_MACHINES; n++)
        AddBagItem(GetTMHMItemId(n), 1);
    CreateMon(mon, SPECIES_MEW, 50, 0x1234, OTID_STRUCT_PLAYER_ID);
    GiveMonInitialMoveset(mon);
    for (u32 n = 1; n <= NUM_ALL_MACHINES; n++)
        known += MonKnowsMove(mon, GetTMHMMoveId(n)) != FALSE;
    ListHas(&mon->box, MOVE_NONE, &count);
    snprintf(what, sizeof(what), "every TM and HM in the Bag: Mew's TM list has %u moves (176 less the %u it knows; room "
             "for %u)", (unsigned)count, (unsigned)known, (unsigned)MAX_RELEARNER_MOVES);
    Expect(what, count == NUM_ALL_MACHINES - known && MAX_RELEARNER_MOVES >= NUM_ALL_MACHINES);
    Expect("... TM168 Nightmare on it", ListHas(&mon->box, MOVE_NIGHTMARE, NULL) || MonKnowsMove(mon, MOVE_NIGHTMARE));
    ZeroPlayerPartyMons();
}

// ---- The TM case and its save record -----------------------------------------------------------------------------------
static u32 Crc32(const u8 *data, u32 size)   // table-driven, independent of item.c's
{
    static u32 table[256];
    u32 crc = 0xFFFFFFFF;

    if (table[1] == 0)
    {
        for (u32 i = 0; i < 256; i++)
        {
            u32 c = i;

            for (u32 k = 0; k < 8; k++)
                c = (c & 1) ? 0xEDB88320 ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    }
    while (size--)
        crc = table[(crc ^ *data++) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}

static bool32 RecordIsZero(void)
{
    const u8 *bytes = (const u8 *)&gSaveBlock3Ptr->tmRecord;

    for (u32 i = 0; i < sizeof(struct MPTmRecord); i++)
    {
        if (bytes[i] != 0)
            return FALSE;
    }
    return TRUE;
}

static bool32 RecordHas(u32 tm)
{
    u32 bit = tm - MP_TM_RECORD_FIRST;

    return (gSaveBlock3Ptr->tmRecord.owned[bit / 8] >> (bit % 8)) & 1;
}

static u32 PocketCount(void)
{
    u32 count = 0;

    for (u32 i = 0; i < gBagPockets[POCKET_TM_HM].capacity; i++)
        count += gBagPockets[POCKET_TM_HM].itemSlots[i].itemId != ITEM_NONE;
    return count;
}

static void SaveRecord(void)
{
    static const u16 sOld[] = {ITEM_TM01, ITEM_TM_ROCK_TOMB, ITEM_HM01, ITEM_HM03};
    static const u16 sNew[] = {ITEM_TM_ROOST, ITEM_TM100, ITEM_TM101, ITEM_TM168};
    struct ItemSlot before[BAG_TMHM_COUNT];
    const struct MPTmRecord *record = &gSaveBlock3Ptr->tmRecord;
    bool32 all = TRUE, ones = TRUE;

    Expect("the record: 36 bytes after the Tower record (SaveBlock3 1076-1111), CRC-32 '123456789' = CBF43926",
           sizeof(struct MPTmRecord) == 36 && offsetof(struct SaveBlock3, tmRecord) == 1076
           && Crc32((const u8 *)"123456789", 9) == 0xCBF43926);
    Expect("the case holds every machine; the save's pocket keeps its 64 slots",
           gBagPockets[POCKET_TM_HM].capacity == NUM_ALL_MACHINES && ARRAY_COUNT(gSaveBlock1Ptr->bag.TMsHMs) == 64);

    // a save from before the catalogue: TMs and HMs in the save's slots, the record all zeros
    ClearBag();
    Expect("a new game: the case empty, the record all zeros", PocketCount() == 0 && RecordIsZero());
    for (u32 i = 0; i < ARRAY_COUNT(sOld); i++)
    {
        gSaveBlock1Ptr->bag.TMsHMs[i].itemId = sOld[i];
        gSaveBlock1Ptr->bag.TMsHMs[i].quantity = 1 ^ gSaveBlock2Ptr->encryptionKey;
    }
    memcpy(before, gSaveBlock1Ptr->bag.TMsHMs, sizeof(before));
    TmCase_LoadFromSave();
    Expect("an older save loaded: its four TMs and HMs in the Bag, one each",
           PocketCount() == 4 && CountTotalItemQuantityInBag(ITEM_TM01) == 1 && CountTotalItemQuantityInBag(ITEM_HM03) == 1
           && CheckBagHasItem(ITEM_TM_ROCK_TOMB, 1) && CheckBagHasItem(ITEM_HM01, 1));
    TmCase_WriteToSave();
    Expect("... saved again with no TM past TM50: byte for byte as it was, the record still zeros",
           memcmp(before, gSaveBlock1Ptr->bag.TMsHMs, sizeof(before)) == 0 && RecordIsZero());

    // TMs past TM50 go in the record
    for (u32 i = 0; i < ARRAY_COUNT(sNew); i++)
        AddBagItem(sNew[i], 1);
    TmCase_WriteToSave();
    Expect("TM51, TM100, TM101 and TM168 bought and saved: the save's slots hold the first four, the record the rest",
           memcmp(before, gSaveBlock1Ptr->bag.TMsHMs, sizeof(before)) == 0
           && gSaveBlock1Ptr->bag.TMsHMs[4].itemId == ITEM_NONE);
    Expect("... the record: magic PTM1, version 1, 4 TMs (bits of TM51, TM100, TM101, TM168), its CRC-32",
           record->magic == MP_TM_RECORD_MAGIC && record->version == 1 && record->count == 4 && record->reserved == 0
           && RecordHas(51) && RecordHas(100) && RecordHas(101) && RecordHas(168) && !RecordHas(52)
           && record->crc32 == Crc32((const u8 *)record, offsetof(struct MPTmRecord, crc32)));
    {
        struct MPTmRecord saved = *record;

        ClearBag();   // nothing in memory (a new start): what comes back is the save's alone
        memcpy(gSaveBlock1Ptr->bag.TMsHMs, before, sizeof(before));
        gSaveBlock3Ptr->tmRecord = saved;
    }
    TmCase_LoadFromSave();
    for (u32 i = 0; i < ARRAY_COUNT(sNew); i++)
        ones &= CountTotalItemQuantityInBag(sNew[i]) == 1;
    Expect("... loaded: all eight back, one each", PocketCount() == 8 && ones && CheckBagHasItem(ITEM_TM01, 1));

    // the key that encrypts quantities changes (the game does this): still one each, saved and loaded
    ApplyNewEncryptionKeyToBagItems(0x0BADF00D);
    gSaveBlock2Ptr->encryptionKey = 0x0BADF00D;
    TmCase_WriteToSave();
    TmCase_LoadFromSave();
    Expect("a new encryption key: still one each, saved and loaded",
           CountTotalItemQuantityInBag(ITEM_TM168) == 1 && CountTotalItemQuantityInBag(ITEM_TM01) == 1 && PocketCount() == 8);

    // a damaged record is not read (the launcher refuses such a save: flash.py parse_tm_record)
    gSaveBlock3Ptr->tmRecord.owned[0] ^= 2;
    TmCase_LoadFromSave();
    Expect("a record whose CRC does not match: not read (the four in the slots only)",
           PocketCount() == 4 && !CheckBagHasItem(ITEM_TM_ROOST, 1));

    // every machine: 176 in the case, 58 in the slots and 118 in the record, and back
    ClearBag();
    for (u32 n = 1; n <= NUM_ALL_MACHINES; n++)
        all &= AddBagItem(GetTMHMItemId(n), 1);
    Expect("every TM and HM fits in the Bag (176)", all && PocketCount() == NUM_ALL_MACHINES);
    TmCase_WriteToSave();
    {
        u32 slots = 0;

        for (u32 i = 0; i < BAG_TMHM_COUNT; i++)
            slots += gSaveBlock1Ptr->bag.TMsHMs[i].itemId != ITEM_NONE;
        Expect("... saved: 58 in the save's slots (TM01-TM50, the HMs), 118 in the record", slots == 58
               && record->count == 118);
    }
    TmCase_LoadFromSave();
    all = TRUE;
    for (u32 n = 1; n <= NUM_ALL_MACHINES; n++)
        all &= CountTotalItemQuantityInBag(GetTMHMItemId(n)) == 1;
    Expect("... loaded: all 176 back, one each", all && PocketCount() == NUM_ALL_MACHINES);
    ClearBag();
}

int NativeSelfTest_TmCatalogue(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    Setup();
    printf("selftest-tm-catalogue: the TM catalogue - the TMs, the Game Corner's lists and prices, who each TM teaches, "
           "the move-switching list, the TM case and its save record\n");
    Items();
    Catalogue();
    Compatibility();
    SwitchList();
    SaveRecord();
    printf("selftest-tm-catalogue: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}
