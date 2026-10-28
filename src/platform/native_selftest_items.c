// Native PC build: the 2026-10-27 wild item audit (docs/gameplay-visual-updates-20261027.md section 5 in the app repo).
// Project-owned. Every number here comes from the game's own code (ComputePlayerShinyOdds, TryGenerateWildMon,
// CreateWildMon, the visible spawner's wait, UpdateRepelCounter, IsWildLevelAllowedByRepel, IsAbilityAllowingEncounter,
// ChooseWildMonLevel, SetWildMonHeldItem, the critical capture roll, the Day Care's Egg check), run before the game
// starts; then it exits. Rates are compared with the expected value at 4.5 sigma.
//   --selftest-wild-items
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "battle.h"
#include "berry.h"
#include "daycare.h"
#include "event_data.h"
#include "item.h"
#include "item_menu.h"
#include "load_save.h"
#include "mail.h"
#include "malloc.h"
#include "mp_wild_random.h"
#include "overworld.h"
#include "pokedex.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "random.h"
#include "wild_encounter.h"
#include "constants/abilities.h"
#include "constants/berry.h"
#include "constants/flags.h"
#include "constants/items.h"
#include "constants/layouts.h"
#include "constants/maps.h"
#include "constants/moves.h"
#include "constants/species.h"
#include "constants/weather.h"
#include "platform/mp_breeding.h"

extern bool8 gMPTestLocalResetGeneration;
extern bool32 MPTest_CriticalCapture(u32 odds);
extern u32 MPOWE_TestWaitFrames(u32 frames);
extern u32 MPWild_TestFamilyCount(void);
extern const struct MPWildFamily *MPWild_TestFamily(u32 index);
extern const struct MPWildMember *MPWild_TestMembers(void);

static int sChecks, sFailures;

static void Expect(const char *what, bool32 ok)
{
    sChecks++;
    if (!ok)
        sFailures++;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

// A measured count against its expectation: printed, and a check at 4.5 sigma.
static void ExpectRate(const char *what, u32 hits, u32 n, double p)
{
    double expected = n * p, sigma = __builtin_sqrt(n * p * (1.0 - p)), diff = (double)hits - expected;
    char line[200];

    if (sigma < 1.0)
        sigma = 1.0;
    printf("     %s: %u of %u = %.4f%% (expected %.4f%%, %.0f +- %.0f)\n", what, hits, n, 100.0 * hits / n, 100.0 * p, expected, sigma);
    snprintf(line, sizeof(line), "%s: %.4f%% (within 4.5 sigma)", what, 100.0 * p);
    Expect(line, diff > -4.5 * sigma && diff < 4.5 * sigma);
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

static void UseMap(u16 map)
{
    gMapHeader = *Overworld_GetMapHeaderByGroupAndId(MAP_GROUP(map), MAP_NUM(map));
    gSaveBlock1Ptr->location.mapGroup = MAP_GROUP(map);
    gSaveBlock1Ptr->location.mapNum = MAP_NUM(map);
}

static const struct WildPokemonInfo *LandInfo(u16 map)
{
    for (u32 i = 0; gWildMonHeaders[i].mapGroup != MAP_GROUP(MAP_UNDEFINED); i++)
        if (gWildMonHeaders[i].mapGroup == MAP_GROUP(map) && gWildMonHeaders[i].mapNum == MAP_NUM(map))
            return gWildMonHeaders[i].encounterTypes[TIME_MORNING].landMonsInfo;
    return NULL;
}

// The party's lead: `species` at `level`, with `ability` (searched among its three slots; FALSE if it has none of it),
// a fixed nature, and `item` held. Party size 1.
static bool32 SetLead(enum Species species, u32 level, enum Ability ability, u32 nature, u32 item)
{
    struct Pokemon *lead = &gParties[B_TRAINER_PLAYER][0];
    u32 personality = GetMonPersonality(species, MON_FEMALE, nature, RANDOM_UNOWN_LETTER);
    bool32 found = (ability == ABILITY_NONE);

    ZeroPlayerPartyMons();
    CreateMon(lead, species, level, personality, OTID_STRUCT_PLAYER_ID);
    for (u32 slot = 0; slot < NUM_ABILITY_SLOTS && !found; slot++)
    {
        SetMonData(lead, MON_DATA_ABILITY_NUM, &slot);
        found = (GetMonAbility(lead) == ability);
    }
    SetMonData(lead, MON_DATA_HELD_ITEM, &item);
    gPartiesCount[B_TRAINER_PLAYER] = 1;
    return found;
}

static u32 FamilyIndexOf(u16 species)
{
    const struct MPWildMember *members = MPWild_TestMembers();

    for (u32 f = 0; f < MPWild_TestFamilyCount(); f++)
    {
        const struct MPWildFamily *family = MPWild_TestFamily(f);
        for (u32 j = 0; j < family->memberCount; j++)
            if (members[family->firstMember + j].species == species)
                return f;
    }
    return 0xFFFF;
}

static bool32 FamilyOfType(u16 species, u32 type)
{
    u32 f = FamilyIndexOf(species);
    const struct MPWildFamily *family;

    if (f == 0xFFFF)
        return FALSE;
    family = MPWild_TestFamily(f);
    return family->type1 == type || family->type2 == type;
}

// ---------------------------------------------------------------------------------------------------------------------

static void TestShinyCharm(void)
{
    const u32 n = 20000000;
    const double p = 16.0 / 65536.0, f = 41848.0 / 65536.0;
    u32 hits;

    printf("1. Shiny Charm and lures in the shiny roll (ComputePlayerShinyOdds)\n");
    // An authored Local Soft Reset hunt (MultiplayerLocalReset_PersonalGeneration): 1 in 4096, with the Charm 1 in 2500
    gMPTestLocalResetGeneration = TRUE;
    for (u32 charm = 0; charm < 2; charm++)
    {
        ClearBag();
        if (charm)
            AddBagItem(ITEM_SHINY_CHARM, 1);
        VarSet(VAR_REPEL_STEP_COUNT, 0);
        SeedRng(0x15A0 + charm);
        hits = 0;
        for (u32 i = 0; i < n; i++)
            if (ComputePlayerShinyOdds(Random32(), 0x00123456))
                hits++;
        ExpectRate(charm ? "Local Soft Reset hunt with the Shiny Charm: 1 in 2500" : "Local Soft Reset hunt: 1 in 4096",
                   hits, n, charm ? p + f * p * (1.0 - p) : p);
    }
    // a lure's extra roll never reaches an authored hunt
    ClearBag();
    VarSet(VAR_REPEL_STEP_COUNT, 100 | REPEL_LURE_MASK);
    SeedRng(0x15A2);
    hits = 0;
    for (u32 i = 0; i < n; i++)
        if (ComputePlayerShinyOdds(Random32(), 0x00123456))
            hits++;
    ExpectRate("Local Soft Reset hunt while a lure is active: still 1 in 4096", hits, n, p);
    gMPTestLocalResetGeneration = FALSE;

    // An ordinary wild Pokemon while a lure is active: one extra roll (inherited from pokeemerald-expansion)
    for (u32 charm = 0; charm < 2; charm++)
    {
        ClearBag();
        if (charm)
            AddBagItem(ITEM_SHINY_CHARM, 1);
        VarSet(VAR_REPEL_STEP_COUNT, 100 | REPEL_LURE_MASK);
        FlagClear(FLAG_SYS_SAFARI_MODE);
        SeedRng(0x15B0 + charm);
        hits = 0;
        for (u32 i = 0; i < n; i++)
            if (ComputePlayerShinyOdds(Random32(), 0x00123456))
                hits++;
        // rolls: the base roll, the lure's roll, and with the Charm one more made f of the time
        ExpectRate(charm ? "wild Pokemon, lure and Shiny Charm: 1 in 1552.7" : "wild Pokemon while a lure is active: 1 in 2048.3",
                   hits, n, charm ? 1.0 - (1.0 - p) * (1.0 - p) * (1.0 - f * p) : 1.0 - (1.0 - p) * (1.0 - p));
    }
    // a Repel (the same variable without the lure bit) adds nothing
    ClearBag();
    VarSet(VAR_REPEL_STEP_COUNT, 100);
    SeedRng(0x15B2);
    hits = 0;
    for (u32 i = 0; i < n; i++)
        if (ComputePlayerShinyOdds(Random32(), 0x00123456))
            hits++;
    ExpectRate("wild Pokemon while a Repel is active: 1 in 4096 (a Repel adds no roll)", hits, n, p);
    VarSet(VAR_REPEL_STEP_COUNT, 0);

    // a stored shiny keeps its status: through the box copy and the PC
    {
        struct Pokemon mon, back;
        u32 shiny = TRUE, ok;

        CreateMon(&mon, SPECIES_ZIGZAGOON, 5, 0x1234, OTID_STRUCT_PLAYER_ID);
        SetMonData(&mon, MON_DATA_IS_SHINY, &shiny);
        ResetPokemonStorageSystem();
        ok = CopyMonToPC(&mon) != MON_CANT_GIVE;
        BoxMonToMon(GetBoxedMonPtr(0, 0), &back);
        Expect("a shiny Pokemon deposited in the PC and read back is still shiny (stored, not rolled again)",
               ok && GetMonData(&back, MON_DATA_IS_SHINY) && GetMonData(&back, MON_DATA_PERSONALITY) == 0x1234);
    }
}

static void TestLuresAndRepels(void)
{
    const struct WildPokemonInfo *info = LandInfo(MAP_ROUTE101);

    printf("2. Lures and Repels: steps, levels, the repel's level rule\n");
    // UpdateRepelCounter: one step each call; the 100th step of a Lure ends it (the "wore off" script), a Repel likewise
    for (u32 lure = 0; lure < 2; lure++)
    {
        u32 steps, ended = 0, endedAt = 0;

        VarSet(VAR_REPEL_STEP_COUNT, lure ? (100 | REPEL_LURE_MASK) : 100);
        for (steps = 1; steps <= 120; steps++)
        {
            if (UpdateRepelCounter())
            {
                ended++;
                endedAt = steps;
            }
        }
        Expect(lure ? "a 100-step Lure ends on its 100th step, once, and then counts nothing (LURE_STEP_COUNT 0)"
                    : "a 100-step Repel ends on its 100th step, once, and then counts nothing (REPEL_STEP_COUNT 0)",
               ended == 1 && endedAt == 100 && LURE_STEP_COUNT == 0 && REPEL_STEP_COUNT == 0);
    }
    VarSet(VAR_REPEL_STEP_COUNT, 50 | REPEL_LURE_MASK);
    Expect("a Lure and a Repel share one counter: with a Lure active there is no Repel (they never stack)",
           LURE_STEP_COUNT == 50 && REPEL_STEP_COUNT == 0);
    VarSet(VAR_REPEL_STEP_COUNT, 50);
    Expect("... and with a Repel active there is no Lure", LURE_STEP_COUNT == 0 && REPEL_STEP_COUNT == 50);

    // A lure's level rule: the highest level the area's table gives that slot's species, plus one
    {
        u32 i, bad = 0, n = 200000;

        UseMap(MAP_ROUTE101);
        SetLead(SPECIES_TREECKO, 5, ABILITY_NONE, 0, ITEM_NONE);
        VarSet(VAR_REPEL_STEP_COUNT, 100 | REPEL_LURE_MASK);
        SeedRng(0x2A10);
        for (i = 0; i < n; i++)
        {
            u32 slot = i % NUM_LAND_MONS_ENCOUNTER_SLOTS, max = 0, level = ChooseWildMonLevel(info->wildPokemon, slot, WILD_AREA_LAND);
            for (u32 j = 0; j < NUM_LAND_MONS_ENCOUNTER_SLOTS; j++)
                if (info->wildPokemon[j].species == info->wildPokemon[slot].species && info->wildPokemon[j].maxLevel > max)
                    max = info->wildPokemon[j].maxLevel;
            if (level != max + 1)
                bad++;
        }
        Expect("while a lure is active every Route 101 level is the species' highest there plus one (200,000 rolls)", bad == 0);
        VarSet(VAR_REPEL_STEP_COUNT, 0);
    }

    // The Repel's rule (Gen 6): the first party member that is not an Egg, fainted or not; lower levels are kept away
    {
        u32 hp = 0, isEgg = TRUE;

        SetLead(SPECIES_ZIGZAGOON, 20, ABILITY_NONE, 0, ITEM_NONE);
        Expect("no Repel: every level is met", IsWildLevelAllowedByRepel(2) && IsWildLevelAllowedByRepel(19));
        VarSet(VAR_REPEL_STEP_COUNT, 100);
        Expect("a Repel and a level-20 lead: level 19 is kept away, 20 and 21 are met",
               !IsWildLevelAllowedByRepel(19) && IsWildLevelAllowedByRepel(20) && IsWildLevelAllowedByRepel(21));
        SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_HP, &hp);
        Expect("... a fainted lead still counts (Gen 6)", !IsWildLevelAllowedByRepel(19) && IsWildLevelAllowedByRepel(20));
        // an Egg first, then the level-20 Pokemon: the Egg is passed over
        gParties[B_TRAINER_PLAYER][1] = gParties[B_TRAINER_PLAYER][0];
        CreateMon(&gParties[B_TRAINER_PLAYER][0], SPECIES_TOGEPI, 1, 0x55, OTID_STRUCT_PLAYER_ID);
        SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_IS_EGG, &isEgg);
        gPartiesCount[B_TRAINER_PLAYER] = 2;
        Expect("... an Egg at the head is passed over: the level-20 Pokemon behind it decides",
               !IsWildLevelAllowedByRepel(19) && IsWildLevelAllowedByRepel(20));
        VarSet(VAR_REPEL_STEP_COUNT, 100 | REPEL_LURE_MASK);
        Expect("a Lure keeps nothing away (it is no Repel)", IsWildLevelAllowedByRepel(2));
        VarSet(VAR_REPEL_STEP_COUNT, 0);
    }
}

static void TestEncounterRate(void)
{
    static const struct { const char *name; enum Species species; enum Ability ability; u32 item; u32 flag; u8 weather; u32 num, den; } sCases[] =
    {
        {"no item, no ability",              SPECIES_ZIGZAGOON, ABILITY_NONE,        ITEM_NONE,         0,                     WEATHER_NONE,      1, 1},
        {"White Flute",                      SPECIES_ZIGZAGOON, ABILITY_NONE,        ITEM_NONE,         FLAG_SYS_ENC_UP_ITEM,  WEATHER_NONE,      3, 2},
        {"Black Flute",                      SPECIES_ZIGZAGOON, ABILITY_NONE,        ITEM_NONE,         FLAG_SYS_ENC_DOWN_ITEM, WEATHER_NONE,     1, 2},
        {"Cleanse Tag held by the lead",     SPECIES_ZIGZAGOON, ABILITY_NONE,        ITEM_CLEANSE_TAG,  0,                     WEATHER_NONE,      2, 3},
        {"Pure Incense held by the lead",    SPECIES_ZIGZAGOON, ABILITY_NONE,        ITEM_PURE_INCENSE, 0,                     WEATHER_NONE,      2, 3},
        {"Stench",                           SPECIES_GRIMER,    ABILITY_STENCH,      ITEM_NONE,         0,                     WEATHER_NONE,      1, 2},
        {"White Smoke",                      SPECIES_TORKOAL,   ABILITY_WHITE_SMOKE, ITEM_NONE,         0,                     WEATHER_NONE,      1, 2},
        {"Quick Feet",                       SPECIES_TEDDIURSA, ABILITY_QUICK_FEET,  ITEM_NONE,         0,                     WEATHER_NONE,      1, 2},
        {"Sand Veil in a sandstorm",         SPECIES_DIGLETT,   ABILITY_SAND_VEIL,   ITEM_NONE,         0,                     WEATHER_SANDSTORM, 1, 2},
        {"Sand Veil in clear weather",       SPECIES_DIGLETT,   ABILITY_SAND_VEIL,   ITEM_NONE,         0,                     WEATHER_NONE,      1, 1},
        {"Snow Cloak in snow",               SPECIES_SWINUB,    ABILITY_SNOW_CLOAK,  ITEM_NONE,         0,                     WEATHER_SNOW,      1, 2},
        {"Illuminate",                       SPECIES_STARYU,    ABILITY_ILLUMINATE,  ITEM_NONE,         0,                     WEATHER_NONE,      2, 1},
        {"Arena Trap",                       SPECIES_DIGLETT,   ABILITY_ARENA_TRAP,  ITEM_NONE,         0,                     WEATHER_NONE,      2, 1},
        {"No Guard",                         SPECIES_MACHOP,    ABILITY_NO_GUARD,    ITEM_NONE,         0,                     WEATHER_NONE,      2, 1},
        {"White Flute and a Cleanse Tag",    SPECIES_ZIGZAGOON, ABILITY_NONE,        ITEM_CLEANSE_TAG,  FLAG_SYS_ENC_UP_ITEM,  WEATHER_NONE,      1, 1},
        {"Black Flute, Cleanse Tag, Stench", SPECIES_GRIMER,    ABILITY_STENCH,      ITEM_CLEANSE_TAG,  FLAG_SYS_ENC_DOWN_ITEM, WEATHER_NONE,     1, 6},
    };

    printf("3. Encounter-rate items and abilities on visible Pokemon (MPWild_EncounterRateFactor, the spawner's wait)\n");
    UseMap(MAP_ROUTE101);
    for (u32 c = 0; c < ARRAY_COUNT(sCases); c++)
    {
        u32 num, den, wait, expectedWait = (120 * sCases[c].den + sCases[c].num - 1) / sCases[c].num;
        bool32 ok = SetLead(sCases[c].species, 20, sCases[c].ability, 0, sCases[c].item);
        char line[200];

        FlagClear(FLAG_SYS_ENC_UP_ITEM);
        FlagClear(FLAG_SYS_ENC_DOWN_ITEM);
        if (sCases[c].flag)
            FlagSet(sCases[c].flag);
        gSaveBlock1Ptr->weather = sCases[c].weather;
        MPWild_EncounterRateFactor(&num, &den);
        wait = MPOWE_TestWaitFrames(120);
        snprintf(line, sizeof(line), "%s: encounter rate x%u/%u; a 120-frame spawn wait lasts %u frames (expected %u)",
                 sCases[c].name, sCases[c].num, sCases[c].den, wait, expectedWait);
        Expect(line, ok && (u64)num * sCases[c].den == (u64)den * sCases[c].num && (wait == expectedWait || wait + 1 == expectedWait));
    }
    FlagClear(FLAG_SYS_ENC_UP_ITEM);
    FlagClear(FLAG_SYS_ENC_DOWN_ITEM);
    gSaveBlock1Ptr->weather = WEATHER_NONE;
    // an Egg at the head has no ability
    {
        u32 num, den, isEgg = TRUE;

        SetLead(SPECIES_STARYU, 20, ABILITY_ILLUMINATE, 0, ITEM_NONE);
        SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_IS_EGG, &isEgg);
        MPWild_EncounterRateFactor(&num, &den);
        Expect("an Egg at the head of the party changes nothing", num == den);
    }
}

// The nature of a visible spawn's battle Pokemon, made as wild_encounter_ow.c StartWildBattleWithOWE makes it (PC build)
static u32 BattleNature(void)
{
    return GetNatureFromPersonality(GetMonPersonality(SPECIES_ZIGZAGOON, MON_MALE, MPWild_PickNature(SPECIES_ZIGZAGOON), RANDOM_UNOWN_LETTER));
}

static void TestNatureAndGender(void)
{
    const u32 n = 1000000;
    u32 hits;

    printf("4. Synchronize and Cute Charm (CreateWildMon, and MPWild_PickNature for the visible spawns' battles)\n");
    UseMap(MAP_ROUTE101);
    for (u32 sync = 0; sync < 2; sync++)
    {
        bool32 ok = SetLead(SPECIES_RALTS, 20, sync ? ABILITY_SYNCHRONIZE : ABILITY_TRACE, NATURE_TIMID, ITEM_NONE);

        Expect(sync ? "the test's lead (Ralts) has Synchronize" : "the test's other lead (Ralts) has Trace", ok);
        SeedRng(0x4A00 + sync);
        hits = 0;
        for (u32 i = 0; i < n; i++)
            if (BattleNature() == NATURE_TIMID)
                hits++;
        ExpectRate(sync ? "a visible spawn's battle with a Synchronize lead: the lead's nature (50% + 1/25 of the rest)"
                        : "... without Synchronize: 1 in 25", hits, n, sync ? 0.5 + 0.5 / 25.0 : 1.0 / 25.0);
        SeedRng(0x4A10 + sync);
        hits = 0;
        for (u32 i = 0; i < n / 4; i++)
        {
            CreateWildMon(SPECIES_ZIGZAGOON, 5);
            if (GetNatureFromPersonality(GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_PERSONALITY)) == NATURE_TIMID)
                hits++;
        }
        ExpectRate(sync ? "CreateWildMon (Rock Smash, fishing, a spawn) with a Synchronize lead: the same"
                        : "CreateWildMon without Synchronize: 1 in 25", hits, n / 4, sync ? 0.5 + 0.5 / 25.0 : 1.0 / 25.0);
    }
    // an Egg with Synchronize at the head does nothing
    {
        u32 isEgg = TRUE;

        SetLead(SPECIES_RALTS, 20, ABILITY_SYNCHRONIZE, NATURE_TIMID, ITEM_NONE);
        SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_IS_EGG, &isEgg);
        SeedRng(0x4A20);
        hits = 0;
        for (u32 i = 0; i < n; i++)
            if (BattleNature() == NATURE_TIMID)
                hits++;
        ExpectRate("an Egg with Synchronize at the head: 1 in 25", hits, n, 1.0 / 25.0);
    }
    // Cute Charm (a female lead): two thirds of the time the opposite gender, else the species' own ratio (Zigzagoon 50:50)
    for (u32 charm = 0; charm < 2; charm++)
    {
        bool32 ok = SetLead(SPECIES_CLEFAIRY, 20, charm ? ABILITY_CUTE_CHARM : ABILITY_MAGIC_GUARD, 0, ITEM_NONE);

        Expect(charm ? "the test's lead (a female Clefairy) has Cute Charm" : "the test's other lead (Clefairy) has Magic Guard",
               ok && GetMonGender(&gParties[B_TRAINER_PLAYER][0]) == MON_FEMALE);
        SeedRng(0x4B00 + charm);
        hits = 0;
        for (u32 i = 0; i < n / 4; i++)
        {
            CreateWildMon(SPECIES_ZIGZAGOON, 5);
            if (GetMonGender(&gParties[B_TRAINER_OPPONENT_A][0]) == MON_MALE)
                hits++;
        }
        ExpectRate(charm ? "a spawn with a female Cute Charm lead is male: 2/3 + 1/3 of one half" : "... without Cute Charm: one half",
                   hits, n / 4, charm ? 2.0 / 3.0 + 1.0 / 6.0 : 0.5);
    }
}

static void TestTypeAbilities(void)
{
    const u32 n = 400000;
    const struct WildPokemonInfo *info = LandInfo(MAP_ROUTE101);
    static const struct { const char *name; enum Species species; enum Ability ability; enum Type type; } sCases[] =
    {
        {"Magnet Pull (Steel)", SPECIES_MAGNEMITE, ABILITY_MAGNET_PULL, TYPE_STEEL},
        {"Static (Electric)",   SPECIES_PIKACHU,   ABILITY_STATIC,      TYPE_ELECTRIC},
    };

    printf("5. Magnet Pull and Static among the randomized species (TryGenerateWildMon, Route 101 grass)\n");
    UseMap(MAP_ROUTE101);
    for (u32 c = 0; c < ARRAY_COUNT(sCases); c++)
    {
        u32 base = 0, boosted = 0;
        double share;

        // without the ability: the share of that type's families
        SetLead(SPECIES_ZIGZAGOON, 5, ABILITY_NONE, 0, ITEM_NONE);
        SeedRng(0x5C00 + c);
        for (u32 i = 0; i < n; i++)
        {
            TryGenerateWildMon(info, WILD_AREA_LAND, 0);
            if (FamilyOfType(GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_SPECIES), sCases[c].type))
                base++;
        }
        share = (double)base / n;
        Expect(sCases[c].name, SetLead(sCases[c].species, 5, sCases[c].ability, 0, ITEM_NONE));
        SeedRng(0x5C10 + c);
        for (u32 i = 0; i < n; i++)
        {
            TryGenerateWildMon(info, WILD_AREA_LAND, 0);
            if (FamilyOfType(GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_SPECIES), sCases[c].type))
                boosted++;
        }
        printf("     %s: without it %.2f%% of the spawns are of its type\n", sCases[c].name, 100.0 * share);
        ExpectRate(sCases[c].name, boosted, n, 0.5 + 0.5 * share);
    }
}

static void TestLevelAbilities(void)
{
    const u32 n = 1000000;
    u32 hits;

    printf("6. Keen Eye / Intimidate, Hustle / Pressure / Vital Spirit\n");
    for (u32 keen = 0; keen < 3; keen++)
    {
        static const enum Ability sAbilities[] = {ABILITY_NONE, ABILITY_KEEN_EYE, ABILITY_INTIMIDATE};
        static const enum Species sSpecies[] = {SPECIES_ZIGZAGOON, SPECIES_PIDGEY, SPECIES_EKANS};
        u32 kept = 0, keptClose = 0;

        Expect("the test's lead has the ability", SetLead(sSpecies[keen], 30, sAbilities[keen], 0, ITEM_NONE));
        SeedRng(0x6A00 + keen);
        for (u32 i = 0; i < n; i++)
        {
            if (!IsAbilityAllowingEncounter(25))
                kept++;
            if (!IsAbilityAllowingEncounter(26))
                keptClose++;
        }
        ExpectRate(keen ? "a level-30 Keen Eye / Intimidate lead keeps a level-25 Pokemon away half the time" : "no such lead: none kept away",
                   kept, n, keen ? 0.5 : 0.0);
        Expect("... a level-26 Pokemon (fewer than 5 levels lower) is never kept away", keptClose == 0);
    }
    {
        static const struct WildPokemon sSlot[] = {{10, 14, SPECIES_ZIGZAGOON}};

        for (u32 hustle = 0; hustle < 2; hustle++)
        {
            Expect("the test's lead (Togepi) has Hustle / none", SetLead(SPECIES_TOGEPI, 20, hustle ? ABILITY_HUSTLE : ABILITY_SERENE_GRACE, 0, ITEM_NONE));
            SeedRng(0x6B00 + hustle);
            hits = 0;
            for (u32 i = 0; i < n; i++)
                if (ChooseWildMonLevel(sSlot, 0, WILD_AREA_LAND) == 14)
                    hits++;
            ExpectRate(hustle ? "a Hustle lead: the slot's highest level (10-14) half the time" : "no Hustle: the highest level 1 in 5",
                       hits, n, hustle ? 0.5 : 0.2);
        }
    }
}

static void TestHeldItemsAndCapture(void)
{
    const u32 n = 1000000;

    enum Species holder = SPECIES_NONE;

    printf("7. Compound Eyes (wild held items) and the Catching Charm (critical captures)\n");
    UseMap(MAP_ROUTE101);
    // the first species with a common and a different rare held item
    for (enum Species s = SPECIES_BULBASAUR; s < NUM_SPECIES && holder == SPECIES_NONE; s++)
        if (gSpeciesInfo[s].itemCommon != ITEM_NONE && gSpeciesInfo[s].itemRare != ITEM_NONE
         && gSpeciesInfo[s].itemCommon != gSpeciesInfo[s].itemRare)
            holder = s;
    printf("     the wild Pokemon: species %u (common item %u, rare item %u)\n", holder, gSpeciesInfo[holder].itemCommon, gSpeciesInfo[holder].itemRare);
    for (u32 eyes = 0; eyes < 2; eyes++)
    {
        u32 common = 0, rare = 0;

        Expect("the test's lead (Butterfree) has Compound Eyes / Tinted Lens",
               SetLead(SPECIES_BUTTERFREE, 20, eyes ? ABILITY_COMPOUND_EYES : ABILITY_TINTED_LENS, 0, ITEM_NONE));
        SeedRng(0x7A00 + eyes);
        for (u32 i = 0; i < n; i++)
        {
            u32 none = ITEM_NONE;

            CreateMon(&gParties[B_TRAINER_OPPONENT_A][0], holder, 5, 0, OTID_STRUCT_PLAYER_ID);
            SetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_HELD_ITEM, &none);
            gBattleTypeFlags = 0;
            SetWildMonHeldItem();
            if (GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_HELD_ITEM) == gSpeciesInfo[holder].itemCommon)
                common++;
            else if (GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_HELD_ITEM) == gSpeciesInfo[holder].itemRare)
                rare++;
        }
        ExpectRate(eyes ? "a wild Pokemon holds its common item (Compound Eyes lead)" : "a wild Pokemon holds its common item",
                   common, n, eyes ? 0.60 : 0.50);
        ExpectRate(eyes ? "... its rare item (Compound Eyes lead)" : "... its rare item",
                   rare, n, eyes ? 0.20 : 0.05);
    }
    // critical captures: with 31 to 70 Hoenn Pokemon caught the odds are halved, then /6, out of 256; the Charm doubles them
    {
        u32 caught = 0;

        for (enum Species s = SPECIES_BULBASAUR; s < NUM_SPECIES && caught < 40; s++)
        {
            if (SpeciesToHoennPokedexNum(s) != 0 && SpeciesToHoennPokedexNum(s) <= HOENN_DEX_COUNT)
            {
                GetSetPokedexFlag(SpeciesToNationalPokedexNum(s), FLAG_SET_CAUGHT);
                caught++;
            }
        }
        for (u32 charm = 0; charm < 2; charm++)
        {
            u32 hits = 0, odds = 120, expect;

            ClearBag();
            if (charm)
                AddBagItem(ITEM_CATCHING_CHARM, 1);
            SeedRng(0x7B00 + charm);
            for (u32 i = 0; i < n; i++)
                if (MPTest_CriticalCapture(odds))
                    hits++;
            expect = (odds * 50 * (charm ? 2 : 1) / 100) / 6;   // CriticalCapture's own arithmetic for this dex count
            printf("     %u Pokemon caught (Pokedex count %u)\n", caught, GetNationalPokedexCount(FLAG_GET_CAUGHT));
            ExpectRate(charm ? "a critical capture, ball odds 120, 40 caught, with the Catching Charm (twice the chance)"
                             : "a critical capture, ball odds 120, 40 caught", hits, n, expect / 256.0);
        }
        ClearBag();
    }
}

// The Day Care's Egg check every 64 steps (Update 30), with and without the Oval Charm (Gen 6: 20/50/70% become 40/80/88%).
static u32 EggChecks(enum Species a, u32 ga, u32 ota, enum Species b, u32 gb, u32 otb, u32 checks)
{
    u32 eggs = 0;

    for (u32 slot = 0; slot < 2; slot++)
    {
        struct Pokemon mon;
        u32 personality = GetMonPersonality(slot ? b : a, slot ? gb : ga, NATURE_RANDOM, RANDOM_UNOWN_LETTER), shiny = FALSE;

        CreateMonWithIVs(&mon, slot ? b : a, 30, personality, OTID_STRUCT_PRESET(slot ? otb : ota), 31);
        SetMonData(&mon, MON_DATA_IS_SHINY, &shiny);
        gSaveBlock1Ptr->daycare.mons[slot].mon = mon.box;
    }
    for (u32 i = 0; i < checks; i++)
    {
        struct Pokemon egg;

        gSaveBlock1Ptr->daycare.mons[0].steps = 63;
        gSaveBlock1Ptr->daycare.mons[1].steps = 63;
        IncrementDaycareSteps();
        if (MPDaycare_QueuePop(&egg))
            eggs++;
        FlagClear(FLAG_PENDING_DAYCARE_EGG);
    }
    return eggs;
}

static void TestOvalCharm(void)
{
    const u32 n = 100000;

    printf("8. The Oval Charm (the Day Care's Egg chance per 64-step check)\n");
    for (u32 charm = 0; charm < 2; charm++)
    {
        ClearBag();
        if (charm)
            AddBagItem(ITEM_OVAL_CHARM, 1);
        memset(&gSaveBlock1Ptr->daycare, 0, sizeof(gSaveBlock1Ptr->daycare));
        MPDaycare_QueueClear();
        SeedRng(0x8A00 + charm);
        // same species, different trainers: 70 %; same species, same trainer: 50 %; different species, same trainer: 20 %
        ExpectRate(charm ? "70% pair with the Oval Charm: 88%" : "70% pair: 70%",
                   EggChecks(SPECIES_PIKACHU, MON_FEMALE, 1, SPECIES_PIKACHU, MON_MALE, 2, n), n, charm ? 0.88 : 0.70);
        ExpectRate(charm ? "50% pair with the Oval Charm: 80%" : "50% pair: 50%",
                   EggChecks(SPECIES_PIKACHU, MON_FEMALE, 1, SPECIES_PIKACHU, MON_MALE, 1, n), n, charm ? 0.80 : 0.50);
        ExpectRate(charm ? "20% pair with the Oval Charm: 40%" : "20% pair: 20%",
                   EggChecks(SPECIES_PIKACHU, MON_FEMALE, 1, SPECIES_RAICHU, MON_MALE, 1, n), n, charm ? 0.40 : 0.20);
    }
    ClearBag();
}

// ---------------------------------------------------------------------------------------------------------------------
// --selftest-balls (the owner's request of 2026-10-27, the full item audit: every Poke Ball's capture math). Each ball's
// capture odds through the game's own ComputeCaptureOdds (battle_script_commands.c MPTest_CaptureOdds), in the
// situations its rule names, against the Gen 6 rule this build is set to (include/config/battle.h, GEN_LATEST = GEN_6),
// worked out here with the game's own integer steps: odds = (3 maxHP - 2 HP) x catch rate / (3 maxHP) x multiplier /
// divider, x 2.5 asleep or frozen, x 1.5 paralysed, burned or poisoned. 255 or more catches at once; below it each of
// three shakes holds with MPTest_BallShakeOdds(odds) / 65536. Critical captures are left out (none with an empty
// Pokedex). A Safari Zone battle uses its own catch factor; the Safari Ball's multiplier is checked as a ball's.
#include "battle_main.h"
#include "constants/battle.h"
#include "constants/pokeball.h"

extern u32 MPTest_CaptureOdds(u32 ballItem, u32 wildBattler, u32 playerBattler);
extern u32 MPTest_BallShakeOdds(u32 odds);
extern bool8 gIsFishingEncounter, gIsSurfingEncounter;

#define WILD   1
#define PLAYER 0
#define SURE_CATCH ((u32)-1)    // battle_script_commands.c CAPTURE_GUARANTEED

// The battle: the player's Pokemon in slot 0 (battler 0), the wild one in the opponent's slot 0 (battler 1), each at
// `level` with full HP of `maxHP`, a gender, no status; turn 0, no fishing or surfing.
static void SetBattle(enum Species wild, u32 wildLevel, u32 wildGender, enum Species mine, u32 myLevel, u32 myGender)
{
    struct BattlePokemon *mon;
    u32 i;

    ZeroPlayerPartyMons();
    ZeroEnemyPartyMons();
    CreateMon(&gParties[B_TRAINER_PLAYER][0], mine, myLevel, GetMonPersonality(mine, myGender, NATURE_HARDY, 0), OTID_STRUCT_PLAYER_ID);
    CreateMon(&gParties[B_TRAINER_OPPONENT_A][0], wild, wildLevel, GetMonPersonality(wild, wildGender, NATURE_HARDY, 0), OTID_STRUCT_PLAYER_ID);
    gPartiesCount[B_TRAINER_PLAYER] = gPartiesCount[B_TRAINER_OPPONENT_A] = 1;
    memset(gBattleMons, 0, sizeof(gBattleMons));
    gBattlersCount = 2;
    gBattlerPositions[PLAYER] = B_POSITION_PLAYER_LEFT;
    gBattlerPositions[WILD] = B_POSITION_OPPONENT_LEFT;
    gBattlerPartyIndexes[PLAYER] = gBattlerPartyIndexes[WILD] = 0;
    for (i = 0; i < 2; i++)
    {
        enum Species species = i == WILD ? wild : mine;

        mon = &gBattleMons[i];
        mon->species = species;
        mon->level = i == WILD ? wildLevel : myLevel;
        mon->maxHP = mon->hp = 120;
        mon->types[0] = gSpeciesInfo[species].types[0];
        mon->types[1] = gSpeciesInfo[species].types[1];
        mon->types[2] = TYPE_MYSTERY;
    }
    gBattleResults.battleTurnCounter = 0;
    gIsFishingEncounter = gIsSurfingEncounter = FALSE;
    gBattleTypeFlags = 0;     // a wild battle
}

// The odds the rule gives: the game's integer steps with this multiplier / divider and flat bonus (catch rate at least 1;
// the odds themselves can be 0)
static u32 RuleOdds(u32 multiplier, u32 divider, s32 flatBonus)
{
    struct BattlePokemon *mon = &gBattleMons[WILD];
    s32 catchRate = gSpeciesInfo[mon->species].catchRate + flatBonus;
    u32 odds;

    if (catchRate < 1)
        catchRate = 1;
    odds = (mon->maxHP * 3 - mon->hp * 2) * (u32)catchRate / (mon->maxHP * 3);
    odds = odds * multiplier / divider;
    if (mon->status1 & STATUS1_INCAPACITATED)
        odds = odds * 25 / 10;
    if (mon->status1 & STATUS1_CAN_MOVE)
        odds = odds * 15 / 10;
    return odds;
}

static void ExpectBall(const char *what, u32 ball, u32 multiplier, u32 divider, s32 flatBonus)
{
    u32 got = MPTest_CaptureOdds(ball, WILD, PLAYER), want = RuleOdds(multiplier, divider, flatBonus);
    char line[200];

    snprintf(line, sizeof(line), "%s: odds %u (rule %u: x%u/%u%s)", what, got, want, multiplier, divider,
             flatBonus ? ", catch rate changed" : "");
    Expect(line, got == want);
}

// The chance a throw catches (no critical capture): 1 at odds 255 or more, else four checks (Cmd_handleballthrow's loop
// to BALL_3_SHAKES_SUCCESS, 4: the three shakes and the click) each holding odds'/65536
static double CatchChance(u32 odds)
{
    double shake;

    if (odds == SURE_CATCH || odds > 254)
        return 1.0;
    shake = MPTest_BallShakeOdds(odds) / 65536.0;
    return shake * shake * shake * shake;
}

int NativeSelfTest_Balls(void)
{
    static const struct { u16 ball; const char *name; } sPlainBalls[] = {
        {ITEM_POKE_BALL, "Poke Ball"}, {ITEM_PREMIER_BALL, "Premier Ball"}, {ITEM_LUXURY_BALL, "Luxury Ball"},
        {ITEM_HEAL_BALL, "Heal Ball"}, {ITEM_FRIEND_BALL, "Friend Ball"}, {ITEM_CHERISH_BALL, "Cherish Ball (no effect in this game)"},
        {ITEM_PARK_BALL, "Park Ball (no effect in this game)"}, {ITEM_DREAM_BALL, "Dream Ball (awake)"},
        {ITEM_STRANGE_BALL, "Strange Ball (a later game's ball, x1; sold in the coin shop)"},
    };
    u32 i;

    Setup();
    printf("selftest-balls: every Poke Ball's capture odds, Gen 6 rules\n");
    UseMap(MAP_ROUTE101);
    // the battle's state the Dream Ball's Comatose check reads through GetBattlerAbilityIgnoreMoldBreaker (a battle has it)
    gBattleStruct = calloc(1, sizeof(*gBattleStruct));

    // A catch rate 45 Pokemon at full HP (Bulbasaur), the player's level 50 Treecko
    SetBattle(SPECIES_BULBASAUR, 20, MON_MALE, SPECIES_TREECKO, 50, MON_MALE);
    for (i = 0; i < ARRAY_COUNT(sPlainBalls); i++)
        ExpectBall(sPlainBalls[i].name, sPlainBalls[i].ball, 100, 100, 0);
    ExpectBall("Great Ball x1.5", ITEM_GREAT_BALL, 150, 100, 0);
    ExpectBall("Ultra Ball x2", ITEM_ULTRA_BALL, 200, 100, 0);
    ExpectBall("Safari Ball x1.5 (Gen 2-7)", ITEM_SAFARI_BALL, 150, 100, 0);
    ExpectBall("Sport Ball x1 (no effect in this game: the owner's decision, 2026-10-27)", ITEM_SPORT_BALL, 100, 100, 0);
    Expect("Master Ball: a sure catch", MPTest_CaptureOdds(ITEM_MASTER_BALL, WILD, PLAYER) == SURE_CATCH);
    gBattleMons[WILD].status1 = STATUS1_SLEEP_TURN(3);
    ExpectBall("Dream Ball on a sleeping Pokemon: x4 (the Gen 8-9 rule: the owner's decision, 2026-10-27)", ITEM_DREAM_BALL, 400, 100, 0);
    ExpectBall("asleep: x2.5 for any ball (Poke Ball)", ITEM_POKE_BALL, 100, 100, 0);
    gBattleMons[WILD].status1 = STATUS1_PARALYSIS;
    ExpectBall("paralysed: x1.5 for any ball (Great Ball)", ITEM_GREAT_BALL, 150, 100, 0);
    gBattleMons[WILD].status1 = 0;
    gBattleMons[WILD].hp = 1;
    ExpectBall("1 HP left (Ultra Ball)", ITEM_ULTRA_BALL, 200, 100, 0);
    gBattleMons[WILD].hp = gBattleMons[WILD].maxHP;

    // Net Ball: Water or Bug x3 (x3.5 is Gen 7)
    SetBattle(SPECIES_MAGIKARP, 20, MON_MALE, SPECIES_TREECKO, 50, MON_MALE);
    ExpectBall("Net Ball on a Water type x3", ITEM_NET_BALL, 300, 100, 0);
    SetBattle(SPECIES_CATERPIE, 20, MON_MALE, SPECIES_TREECKO, 50, MON_MALE);
    ExpectBall("Net Ball on a Bug type x3", ITEM_NET_BALL, 300, 100, 0);
    SetBattle(SPECIES_BULBASAUR, 20, MON_MALE, SPECIES_TREECKO, 50, MON_MALE);
    ExpectBall("Net Ball on another type x1", ITEM_NET_BALL, 100, 100, 0);

    // Nest Ball: (41 - level) / 10 below level 30 (Gen 6), else x1
    SetBattle(SPECIES_BULBASAUR, 5, MON_MALE, SPECIES_TREECKO, 50, MON_MALE);
    ExpectBall("Nest Ball, level 5: x3.6", ITEM_NEST_BALL, (41 - 5) * 4096 / 10, 4096, 0);
    SetBattle(SPECIES_BULBASAUR, 29, MON_MALE, SPECIES_TREECKO, 50, MON_MALE);
    ExpectBall("Nest Ball, level 29: x1.2", ITEM_NEST_BALL, (41 - 29) * 4096 / 10, 4096, 0);
    SetBattle(SPECIES_BULBASAUR, 30, MON_MALE, SPECIES_TREECKO, 50, MON_MALE);
    ExpectBall("Nest Ball, level 30: x1", ITEM_NEST_BALL, 100, 100, 0);

    // Dive Ball: x3.5 under the water, surfing or fishing (Gen 4+)
    SetBattle(SPECIES_BULBASAUR, 20, MON_MALE, SPECIES_TREECKO, 50, MON_MALE);
    ExpectBall("Dive Ball on land x1", ITEM_DIVE_BALL, 100, 100, 0);
    gIsSurfingEncounter = TRUE;
    ExpectBall("Dive Ball, surfing x3.5", ITEM_DIVE_BALL, 350, 100, 0);
    gIsSurfingEncounter = FALSE;
    gIsFishingEncounter = TRUE;
    ExpectBall("Dive Ball, fishing x3.5", ITEM_DIVE_BALL, 350, 100, 0);
    ExpectBall("Lure Ball, fishing x3 (Gen 6)", ITEM_LURE_BALL, 300, 100, 0);
    gIsFishingEncounter = FALSE;
    ExpectBall("Lure Ball, not fishing x1", ITEM_LURE_BALL, 100, 100, 0);
    UseMap(MAP_UNDERWATER_ROUTE124);
    ExpectBall("Dive Ball, under the water x3.5", ITEM_DIVE_BALL, 350, 100, 0);

    // Dusk Ball: x3.5 in a cave (and at night: the clock's, not checked here), x1 by day outside
    UseMap(MAP_GRANITE_CAVE_1F);
    ExpectBall("Dusk Ball in a cave x3.5 (x3 is Gen 7)", ITEM_DUSK_BALL, 350, 100, 0);
    UseMap(MAP_ROUTE101);

    // Timer Ball: 1 + 1229/4096 a turn, at most x4 (Gen 5+); Quick Ball x5 on the first turn (Gen 5+)
    ExpectBall("Timer Ball, turn 0: x1", ITEM_TIMER_BALL, 4096, 4096, 0);
    gBattleResults.battleTurnCounter = 1;
    ExpectBall("Timer Ball, turn 1: x1.3", ITEM_TIMER_BALL, 4096 + 1229, 4096, 0);
    ExpectBall("Quick Ball after the first turn x1", ITEM_QUICK_BALL, 100, 100, 0);
    gBattleResults.battleTurnCounter = 5;
    ExpectBall("Timer Ball, turn 5: x2.5", ITEM_TIMER_BALL, 4096 + 5 * 1229, 4096, 0);
    gBattleResults.battleTurnCounter = 10;
    ExpectBall("Timer Ball, turn 10: x4 (its cap)", ITEM_TIMER_BALL, 4 * 4096, 4096, 0);
    gBattleResults.battleTurnCounter = 0;
    ExpectBall("Quick Ball on the first turn x5", ITEM_QUICK_BALL, 500, 100, 0);

    // Repeat Ball: x3 for a species caught before (x3.5 is Gen 7)
    ExpectBall("Repeat Ball, never caught x1", ITEM_REPEAT_BALL, 100, 100, 0);
    GetSetPokedexFlag(SpeciesToNationalPokedexNum(SPECIES_BULBASAUR), FLAG_SET_CAUGHT);
    ExpectBall("Repeat Ball, caught before x3", ITEM_REPEAT_BALL, 300, 100, 0);

    // Level Ball: x8 at four times the level or more, x4 over twice, x2 over it, else x1
    SetBattle(SPECIES_BULBASAUR, 10, MON_MALE, SPECIES_TREECKO, 40, MON_MALE);
    ExpectBall("Level Ball, 40 vs 10: x8", ITEM_LEVEL_BALL, 800, 100, 0);
    SetBattle(SPECIES_BULBASAUR, 10, MON_MALE, SPECIES_TREECKO, 25, MON_MALE);
    ExpectBall("Level Ball, 25 vs 10: x4", ITEM_LEVEL_BALL, 400, 100, 0);
    SetBattle(SPECIES_BULBASAUR, 10, MON_MALE, SPECIES_TREECKO, 15, MON_MALE);
    ExpectBall("Level Ball, 15 vs 10: x2", ITEM_LEVEL_BALL, 200, 100, 0);
    SetBattle(SPECIES_BULBASAUR, 10, MON_MALE, SPECIES_TREECKO, 10, MON_MALE);
    ExpectBall("Level Ball, 10 vs 10: x1", ITEM_LEVEL_BALL, 100, 100, 0);

    // Moon Ball: x4 for a Pokemon that evolves with a Moon Stone; Fast Ball: x4 at base Speed 100 or more
    SetBattle(SPECIES_NIDORINO, 20, MON_MALE, SPECIES_TREECKO, 50, MON_MALE);
    ExpectBall("Moon Ball on Nidorino (Moon Stone) x4", ITEM_MOON_BALL, 400, 100, 0);
    SetBattle(SPECIES_BULBASAUR, 20, MON_MALE, SPECIES_TREECKO, 50, MON_MALE);
    ExpectBall("Moon Ball on Bulbasaur x1", ITEM_MOON_BALL, 100, 100, 0);
    ExpectBall("Fast Ball on Bulbasaur (Speed 45) x1", ITEM_FAST_BALL, 100, 100, 0);
    SetBattle(SPECIES_ELECTRODE, 20, MON_GENDERLESS, SPECIES_TREECKO, 50, MON_MALE);
    ExpectBall("Fast Ball on Electrode (Speed 150) x4", ITEM_FAST_BALL, 400, 100, 0);

    // Love Ball: x8 for the player's species of the other gender
    SetBattle(SPECIES_NIDORINO, 20, MON_MALE, SPECIES_NIDORINO, 50, MON_MALE);
    ExpectBall("Love Ball, the same species and gender x1", ITEM_LOVE_BALL, 100, 100, 0);
    SetBattle(SPECIES_PIKACHU, 20, MON_FEMALE, SPECIES_PIKACHU, 50, MON_MALE);
    ExpectBall("Love Ball, the same species, the other gender x8", ITEM_LOVE_BALL, 800, 100, 0);
    SetBattle(SPECIES_PIKACHU, 20, MON_FEMALE, SPECIES_TREECKO, 50, MON_MALE);
    ExpectBall("Love Ball, another species x1", ITEM_LOVE_BALL, 100, 100, 0);

    // Heavy Ball: the catch rate -20 under 204.8 kg, +20 to 307.1, +30 to 409.5, +40 from 409.6 (Gen 4-6), at least 1
    SetBattle(SPECIES_BULBASAUR, 20, MON_MALE, SPECIES_TREECKO, 50, MON_MALE);
    ExpectBall("Heavy Ball, Bulbasaur 6.9 kg: catch rate -20", ITEM_HEAVY_BALL, 100, 100, -20);
    SetBattle(SPECIES_GOLEM, 20, MON_MALE, SPECIES_TREECKO, 50, MON_MALE);
    ExpectBall("Heavy Ball, Golem 300 kg: +20", ITEM_HEAVY_BALL, 100, 100, 20);
    SetBattle(SPECIES_STEELIX, 20, MON_MALE, SPECIES_TREECKO, 50, MON_MALE);
    ExpectBall("Heavy Ball, Steelix 400 kg: +30", ITEM_HEAVY_BALL, 100, 100, 30);
    SetBattle(SPECIES_SNORLAX, 20, MON_MALE, SPECIES_TREECKO, 50, MON_MALE);
    ExpectBall("Heavy Ball, Snorlax 460 kg: +40", ITEM_HEAVY_BALL, 100, 100, 40);
    // a light Pokemon whose catch rate is under 21: the rule's minimum of 1 (odds 0 at any HP), never a sure catch
    SetBattle(SPECIES_LATIAS, 40, MON_FEMALE, SPECIES_TREECKO, 50, MON_MALE);
    ExpectBall("Heavy Ball, Latias 40 kg (catch rate 3): catch rate 1, not a sure catch", ITEM_HEAVY_BALL, 100, 100, -20);
    SetBattle(SPECIES_JIRACHI, 40, MON_GENDERLESS, SPECIES_TREECKO, 50, MON_MALE);
    ExpectBall("Heavy Ball, Jirachi 1.1 kg (catch rate 3): catch rate 1", ITEM_HEAVY_BALL, 100, 100, -20);
    SetBattle(SPECIES_UXIE, 40, MON_GENDERLESS, SPECIES_TREECKO, 50, MON_MALE);
    ExpectBall("Heavy Ball, Uxie 0.3 kg (catch rate 3): catch rate 1", ITEM_HEAVY_BALL, 100, 100, -20);
    gBattleMons[WILD].hp = 1;
    ExpectBall("Heavy Ball, Uxie at 1 HP: catch rate 1, odds still small", ITEM_HEAVY_BALL, 100, 100, -20);
    gBattleMons[WILD].hp = gBattleMons[WILD].maxHP;
    // the Beast Ball (Gen 7) on a Pokemon that is not an Ultra Beast: x410/4096 (a catch rate 3 Pokemon: odds 0)
    SetBattle(SPECIES_LATIAS, 40, MON_FEMALE, SPECIES_TREECKO, 50, MON_MALE);
    ExpectBall("Beast Ball on Latias (not an Ultra Beast): x0.1", ITEM_BEAST_BALL, 410, 4096, 0);
    // odds of 0 divide by zero in the shake check: the GBA's division gives 0xFFFFFFFF, so one shake holds with
    // 1048560 / Sqrt(Sqrt(0xFFFFFFFF)) = 1048560 / 255 = 4112 out of 65536, the same as before the division was written out
    Expect("a throw at odds 0: the shake chance the GBA's division gives (4112 / 65536)", MPTest_BallShakeOdds(0) == 4112);
    Expect("a throw at odds 1: 1048560 / Sqrt(Sqrt(16711680)) = 16643 / 65536 a shake",
           MPTest_BallShakeOdds(1) == 16643 && CatchChance(1) > CatchChance(0));
    printf("     a throw at odds 0 catches %.3f%%, at odds 1 %.2f%%\n", 100.0 * CatchChance(0), 100.0 * CatchChance(1));

    // What a throw's odds mean: the chance it catches, for a catch rate 45 Pokemon at full HP and at 1 HP
    {
        static const struct { u16 ball; const char *name; } sTable[] = {
            {ITEM_POKE_BALL, "Poke Ball"}, {ITEM_GREAT_BALL, "Great Ball"}, {ITEM_ULTRA_BALL, "Ultra Ball"},
            {ITEM_QUICK_BALL, "Quick Ball (turn 0)"}, {ITEM_NET_BALL, "Net Ball (not Water/Bug)"},
        };

        SetBattle(SPECIES_BULBASAUR, 20, MON_MALE, SPECIES_TREECKO, 50, MON_MALE);
        for (i = 0; i < ARRAY_COUNT(sTable); i++)
        {
            u32 full, low;

            gBattleMons[WILD].hp = gBattleMons[WILD].maxHP;
            full = MPTest_CaptureOdds(sTable[i].ball, WILD, PLAYER);
            gBattleMons[WILD].hp = 1;
            low = MPTest_CaptureOdds(sTable[i].ball, WILD, PLAYER);
            printf("     %-26s catch rate 45: full HP odds %3u, a throw catches %5.1f%%; 1 HP odds %3u, %5.1f%%\n",
                   sTable[i].name, full, 100.0 * CatchChance(full), low, 100.0 * CatchChance(low));
        }
        gBattleMons[WILD].hp = gBattleMons[WILD].maxHP;
        Expect("the shake chance rises with the odds and a throw at 255 catches at once",
               CatchChance(10) < CatchChance(100) && CatchChance(100) < CatchChance(200) && CatchChance(255) == 1.0);
    }
    gBattleTypeFlags = 0;
    free(gBattleStruct);
    gBattleStruct = NULL;
    printf("selftest-balls: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}

int NativeSelfTest_WildItems(void)
{
    Setup();
    printf("selftest-wild-items: the 2026-10-27 wild item audit\n");
    TestShinyCharm();
    TestLuresAndRepels();
    TestEncounterRate();
    TestNatureAndGender();
    TestTypeAbilities();
    TestLevelAbilities();
    TestHeldItemsAndCapture();
    TestOvalCharm();
    printf("selftest-wild-items: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// --selftest-healing (the full item audit, its healing items): each healing item used from the bag on a party Pokemon
// through the game's own PokemonUseItemEffects (what the bag's use calls out of battle), against the Gen 6 rule this
// build is set to (include/config/item.h I_HEALTH_RECOVERY = GEN_LATEST = GEN_6): the HP restored (never past the
// Pokemon's maximum), the statuses cured, a revival, the PP restored, the friendship the herbal medicines cost, and no
// effect (the item kept) where the item would do nothing. The bag's own consumption is not part of it.

#define HEAL_SPECIES SPECIES_SNORLAX    // a maximum HP over 200 at level 100, so a Hyper Potion's 200 shows whole

static const u16 sHealMoves[MAX_MON_MOVES] = {MOVE_TACKLE, MOVE_SURF, MOVE_THUNDER, MOVE_HYPER_BEAM}; // 35, 15, 10, 5 PP

static struct Pokemon *HealMon(u16 hp, u32 status, u8 friendship)
{
    struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][0];
    u32 i;

    ZeroPlayerPartyMons();
    CreateMon(mon, HEAL_SPECIES, 100, GetMonPersonality(HEAL_SPECIES, MON_MALE, NATURE_HARDY, 0), OTID_STRUCT_PLAYER_ID);
    CalculateMonStats(mon);     // CreateMon leaves the stats to its callers
    gPartiesCount[B_TRAINER_PLAYER] = 1;
    {
        // met somewhere else than here (Route 101): the friendship bonus for a Pokemon met on this map is not part
        // of the herbal medicines' rule (see the note below)
        u8 metLocation = MAPSEC_LITTLEROOT_TOWN;
        SetMonData(mon, MON_DATA_MET_LOCATION, &metLocation);
    }
    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        u16 move = sHealMoves[i];
        u8 pp = 0;

        SetMonData(mon, MON_DATA_MOVE1 + i, &move);
        SetMonData(mon, MON_DATA_PP1 + i, &pp);
    }
    {
        u8 bonuses = 0;
        SetMonData(mon, MON_DATA_PP_BONUSES, &bonuses);
    }
    SetMonData(mon, MON_DATA_HP, &hp);
    SetMonData(mon, MON_DATA_STATUS, &status);
    SetMonData(mon, MON_DATA_FRIENDSHIP, &friendship);
    return mon;
}

static u16 MaxHP(struct Pokemon *mon)
{
    return GetMonData(mon, MON_DATA_MAX_HP);
}

// An HP item from 1 HP: the HP after it (and whether it had an effect)
static void ExpectHeal(const char *what, u16 item, u16 startHp, u16 wantHp)
{
    struct Pokemon *mon = HealMon(startHp, 0, 100);
    bool8 noEffect = PokemonUseItemEffects(mon, item, 0, 0, FALSE);
    u16 hp = GetMonData(mon, MON_DATA_HP);
    char line[200];

    snprintf(line, sizeof(line), "%s: HP %u -> %u (rule %u of %u)", what, startHp, hp, wantHp, MaxHP(mon));
    Expect(line, !noEffect && hp == wantHp);
}

static void ExpectNoEffect(const char *what, u16 item, u16 startHp, u32 status, u8 moveIndex)
{
    struct Pokemon *mon = HealMon(startHp, status, 100);
    bool8 noEffect;
    u16 hp;
    u32 statusAfter;

    if (startHp == 0xFFFF)  // full HP
    {
        hp = MaxHP(mon);
        SetMonData(mon, MON_DATA_HP, &hp);
        startHp = hp;
    }
    noEffect = PokemonUseItemEffects(mon, item, 0, moveIndex, FALSE);
    hp = GetMonData(mon, MON_DATA_HP);
    statusAfter = GetMonData(mon, MON_DATA_STATUS);
    Expect(what, noEffect && hp == startHp && statusAfter == status);
}

static void ExpectCure(const char *what, u16 item, u32 status)
{
    struct Pokemon *mon = HealMon(100, status, 100);
    bool8 noEffect = PokemonUseItemEffects(mon, item, 0, 0, FALSE);
    char line[200];

    snprintf(line, sizeof(line), "%s: status %x -> %x", what, (unsigned)status, (unsigned)GetMonData(mon, MON_DATA_STATUS));
    Expect(line, !noEffect && GetMonData(mon, MON_DATA_STATUS) == 0 && GetMonData(mon, MON_DATA_HP) == 100);
}

// A herbal medicine's friendship cost at a starting friendship
static void ExpectFriendship(const char *what, u16 item, u16 startHp, u32 status, u8 friendship, u8 want)
{
    struct Pokemon *mon = HealMon(startHp, status, friendship);
    bool8 noEffect = PokemonUseItemEffects(mon, item, 0, 0, FALSE);
    u8 after = GetMonData(mon, MON_DATA_FRIENDSHIP);
    char line[200];

    snprintf(line, sizeof(line), "%s: friendship %u -> %u (rule %u)", what, friendship, after, want);
    Expect(line, !noEffect && after == want);
}

static u8 PPOf(struct Pokemon *mon, u32 i)
{
    return GetMonData(mon, MON_DATA_PP1 + i);
}

static u8 MaxPPOf(u32 i)
{
    return CalculatePPWithBonus(sHealMoves[i], 0, i);
}

int NativeSelfTest_Healing(void)
{
    static const struct { u16 item; const char *name; u16 amount; } sAmounts[] = {
        // Gen 6 (I_HEALTH_RECOVERY below Gen 7): Super Potion 50, Hyper Potion 200, Fresh Water 50, Soda Pop 60,
        // Lemonade 80, Energy Powder 50, Energy Root 200; Gen 7 changed those to 60, 120, 30, 50, 70, 60, 120
        {ITEM_POTION, "Potion", 20}, {ITEM_SUPER_POTION, "Super Potion", 50}, {ITEM_HYPER_POTION, "Hyper Potion", 200},
        {ITEM_FRESH_WATER, "Fresh Water", 50}, {ITEM_SODA_POP, "Soda Pop", 60}, {ITEM_LEMONADE, "Lemonade", 80},
        {ITEM_MOOMOO_MILK, "Moomoo Milk", 100}, {ITEM_ENERGY_POWDER, "Energy Powder", 50},
        {ITEM_ENERGY_ROOT, "Energy Root", 200}, {ITEM_BERRY_JUICE, "Berry Juice", 20}, {ITEM_SWEET_HEART, "Sweet Heart", 20},
    };
    static const struct { u16 item; const char *name; } sFullHeals[] = {
        {ITEM_FULL_HEAL, "Full Heal"}, {ITEM_HEAL_POWDER, "Heal Powder"}, {ITEM_LAVA_COOKIE, "Lava Cookie"},
        {ITEM_RAGE_CANDY_BAR, "Rage Candy Bar"}, {ITEM_OLD_GATEAU, "Old Gateau"}, {ITEM_CASTELIACONE, "Casteliacone"},
        {ITEM_LUMIOSE_GALETTE, "Lumiose Galette"}, {ITEM_SHALOUR_SABLE, "Shalour Sable"}, {ITEM_LUM_BERRY, "Lum Berry"},
    };
    static const u32 sStatuses[] = {STATUS1_SLEEP_TURN(3), STATUS1_POISON, STATUS1_BURN, STATUS1_FREEZE, STATUS1_PARALYSIS,
                                    STATUS1_TOXIC_POISON};
    struct Pokemon *mon;
    u16 max;
    u32 i;
    char line[200];

    Setup();
    UseMap(MAP_ROUTE101);
    printf("selftest-healing: every healing item through PokemonUseItemEffects, Gen 6 rules\n");
    max = MaxHP(HealMon(1, 0, 100));
    printf("     the test Pokemon: Snorlax level 100, %u HP\n", max);

    // HP: from 1 HP, the amount restored; never past the maximum
    for (i = 0; i < ARRAY_COUNT(sAmounts); i++)
    {
        snprintf(line, sizeof(line), "%s restores %u", sAmounts[i].name, sAmounts[i].amount);
        ExpectHeal(line, sAmounts[i].item, 1, 1 + sAmounts[i].amount);
    }
    ExpectHeal("Max Potion restores all", ITEM_MAX_POTION, 1, max);
    ExpectHeal("Hyper Potion 10 HP short of full: to the maximum, no more", ITEM_HYPER_POTION, max - 10, max);
    ExpectHeal("Potion 5 HP short of full: to the maximum", ITEM_POTION, max - 5, max);
    mon = HealMon(1, STATUS1_PARALYSIS, 100);
    Expect("Full Restore: all HP and the status cured",
           !PokemonUseItemEffects(mon, ITEM_FULL_RESTORE, 0, 0, FALSE) && GetMonData(mon, MON_DATA_HP) == max
           && GetMonData(mon, MON_DATA_STATUS) == 0);
    ExpectNoEffect("Potion at full HP: no effect (kept)", ITEM_POTION, 0xFFFF, 0, 0);
    ExpectNoEffect("Max Potion at full HP: no effect", ITEM_MAX_POTION, 0xFFFF, 0, 0);
    ExpectNoEffect("Potion on a fainted Pokemon: no effect", ITEM_POTION, 0, 0, 0);
    ExpectNoEffect("Full Restore on a fainted Pokemon: no effect", ITEM_FULL_RESTORE, 0, 0, 0);

    // Statuses
    ExpectCure("Antidote cures poison", ITEM_ANTIDOTE, STATUS1_POISON);
    ExpectCure("Antidote cures bad poison", ITEM_ANTIDOTE, STATUS1_TOXIC_POISON);
    ExpectCure("Burn Heal cures a burn", ITEM_BURN_HEAL, STATUS1_BURN);
    ExpectCure("Ice Heal thaws", ITEM_ICE_HEAL, STATUS1_FREEZE);
    ExpectCure("Awakening wakes", ITEM_AWAKENING, STATUS1_SLEEP_TURN(3));
    ExpectCure("Paralyze Heal cures paralysis", ITEM_PARALYZE_HEAL, STATUS1_PARALYSIS);
    ExpectCure("Blue Flute wakes", ITEM_BLUE_FLUTE, STATUS1_SLEEP_TURN(2));
    ExpectCure("Pecha Berry cures poison", ITEM_PECHA_BERRY, STATUS1_POISON);
    ExpectCure("Rawst Berry cures a burn", ITEM_RAWST_BERRY, STATUS1_BURN);
    ExpectCure("Aspear Berry thaws", ITEM_ASPEAR_BERRY, STATUS1_FREEZE);
    ExpectCure("Chesto Berry wakes", ITEM_CHESTO_BERRY, STATUS1_SLEEP_TURN(3));
    ExpectCure("Cheri Berry cures paralysis", ITEM_CHERI_BERRY, STATUS1_PARALYSIS);
    for (i = 0; i < ARRAY_COUNT(sFullHeals); i++)
    {
        u32 s;
        bool32 all = TRUE;

        for (s = 0; s < ARRAY_COUNT(sStatuses); s++)
        {
            struct Pokemon *healed = HealMon(100, sStatuses[s], 100);

            all &= !PokemonUseItemEffects(healed, sFullHeals[i].item, 0, 0, FALSE) && GetMonData(healed, MON_DATA_STATUS) == 0;
        }
        snprintf(line, sizeof(line), "%s cures sleep, poison, a burn, freezing, paralysis and bad poison", sFullHeals[i].name);
        Expect(line, all);
    }
    ExpectNoEffect("Antidote on a burned Pokemon: no effect", ITEM_ANTIDOTE, 100, STATUS1_BURN, 0);
    ExpectNoEffect("Burn Heal on a healthy Pokemon: no effect", ITEM_BURN_HEAL, 100, 0, 0);
    ExpectNoEffect("Full Heal on a healthy Pokemon: no effect", ITEM_FULL_HEAL, 100, 0, 0);

    // Revival
    ExpectHeal("Revive: a fainted Pokemon back with half its HP", ITEM_REVIVE, 0, max / 2);
    ExpectHeal("Max Revive: back with all its HP", ITEM_MAX_REVIVE, 0, max);
    ExpectHeal("Revival Herb: back with all its HP", ITEM_REVIVAL_HERB, 0, max);
    ExpectHeal("Sacred Ash (this Pokemon): back with all its HP", ITEM_SACRED_ASH, 0, max);
    ExpectNoEffect("Revive on a Pokemon that has not fainted: no effect", ITEM_REVIVE, 50, 0, 0);
    ExpectNoEffect("Max Revive at full HP: no effect", ITEM_MAX_REVIVE, 0xFFFF, 0, 0);

    // Herbal medicines cost friendship: under 100, 100-199, 200 and over (Gen 3-7)
    ExpectFriendship("Energy Powder", ITEM_ENERGY_POWDER, 1, 0, 50, 45);
    ExpectFriendship("Energy Powder", ITEM_ENERGY_POWDER, 1, 0, 150, 145);
    ExpectFriendship("Energy Powder", ITEM_ENERGY_POWDER, 1, 0, 250, 240);
    ExpectFriendship("Energy Root", ITEM_ENERGY_ROOT, 1, 0, 50, 40);
    ExpectFriendship("Energy Root", ITEM_ENERGY_ROOT, 1, 0, 150, 140);
    ExpectFriendship("Energy Root", ITEM_ENERGY_ROOT, 1, 0, 250, 235);
    ExpectFriendship("Heal Powder", ITEM_HEAL_POWDER, 100, STATUS1_POISON, 50, 45);
    ExpectFriendship("Heal Powder", ITEM_HEAL_POWDER, 100, STATUS1_POISON, 150, 145);
    ExpectFriendship("Heal Powder", ITEM_HEAL_POWDER, 100, STATUS1_POISON, 250, 240);
    ExpectFriendship("Revival Herb", ITEM_REVIVAL_HERB, 0, 0, 50, 35);
    ExpectFriendship("Revival Herb", ITEM_REVIVAL_HERB, 0, 0, 150, 135);
    ExpectFriendship("Revival Herb", ITEM_REVIVAL_HERB, 0, 0, 250, 230);
    ExpectFriendship("Energy Powder at friendship 3: not below 0", ITEM_ENERGY_POWDER, 1, 0, 3, 0);
    ExpectFriendship("Potion: no friendship change", ITEM_POTION, 1, 0, 150, 150);
    // MS-150 (the owner's decision, 2026-10-27): the met-here bonus and the Luxury Ball's add to a gain only, as in the
    // official games, so a herbal medicine costs its whole 5 or 10 for a Pokemon met on the current map or in a Luxury
    // Ball too (CalculateFriendshipBonuses; before the fix, 146 and 141)
    {
        u8 here = GetCurrentRegionMapSectionId(), friendship = 150, luxury = BALL_LUXURY;

        mon = HealMon(1, 0, friendship);
        SetMonData(mon, MON_DATA_MET_LOCATION, &here);
        PokemonUseItemEffects(mon, ITEM_ENERGY_POWDER, 0, 0, FALSE);
        Expect("Energy Powder on a Pokemon met on this map: friendship 150 -> 145 (no bonus on a loss)",
               GetMonData(mon, MON_DATA_FRIENDSHIP) == 145);
        mon = HealMon(1, 0, friendship);
        SetMonData(mon, MON_DATA_POKEBALL, &luxury);
        PokemonUseItemEffects(mon, ITEM_ENERGY_ROOT, 0, 0, FALSE);
        Expect("Energy Root on a Pokemon in a Luxury Ball: friendship 150 -> 140 (no bonus on a loss)",
               GetMonData(mon, MON_DATA_FRIENDSHIP) == 140);
    }

    // PP: Ether and Max Ether on one move, Elixir and Max Elixir on all four (from 0 PP; Tackle 35, Surf 15, Thunder 10,
    // Hyper Beam 5)
    mon = HealMon(100, 0, 100);
    Expect("Ether: 10 PP to the move chosen (Tackle 0 -> 10)", !PokemonUseItemEffects(mon, ITEM_ETHER, 0, 0, FALSE)
           && PPOf(mon, 0) == 10 && PPOf(mon, 1) == 0);
    Expect("Ether again on Hyper Beam: to its maximum of 5, no more", !PokemonUseItemEffects(mon, ITEM_ETHER, 0, 3, FALSE)
           && PPOf(mon, 3) == MaxPPOf(3));
    Expect("Max Ether: all of one move's PP (Surf 0 -> 15)", !PokemonUseItemEffects(mon, ITEM_MAX_ETHER, 0, 1, FALSE)
           && PPOf(mon, 1) == MaxPPOf(1) && PPOf(mon, 2) == 0);
    Expect("Ether on a move at full PP: no effect", PokemonUseItemEffects(mon, ITEM_ETHER, 0, 1, FALSE) && PPOf(mon, 1) == MaxPPOf(1));
    mon = HealMon(100, 0, 100);
    Expect("Elixir: 10 PP to every move, each to its maximum", !PokemonUseItemEffects(mon, ITEM_ELIXIR, 0, 0, FALSE)
           && PPOf(mon, 0) == 10 && PPOf(mon, 1) == 10 && PPOf(mon, 2) == 10 && PPOf(mon, 3) == MaxPPOf(3));
    mon = HealMon(100, 0, 100);
    Expect("Max Elixir: every move's PP to its maximum", !PokemonUseItemEffects(mon, ITEM_MAX_ELIXIR, 0, 0, FALSE)
           && PPOf(mon, 0) == MaxPPOf(0) && PPOf(mon, 1) == MaxPPOf(1) && PPOf(mon, 2) == MaxPPOf(2) && PPOf(mon, 3) == MaxPPOf(3));
    Expect("Max Elixir again at full PP: no effect", PokemonUseItemEffects(mon, ITEM_MAX_ELIXIR, 0, 0, FALSE));

    printf("selftest-healing: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// --selftest-bag-items (the full item audit, the other items given to a Pokemon from the bag): the berries, the
// vitamins, the wings, PP Up and PP Max, the Rare Candy, the evolution stones, TMs and HMs, through the game's own
// code: PokemonUseItemEffects, what the bag's USE calls (for a stone, GetEvolutionTargetSpecies: the evolution
// PokemonUseItemEffects asks for before it starts the evolution scene; for a TM, the item's move and the teachable
// learnsets), against the rules this build is set to (include/config, Gen 6): a vitamin raises a stat's effort values
// by 10 up to 100 (I_VITAMIN_EV_CAP below Gen 8), a wing by 1 up to 252, never past 510 in all; an EV-lowering berry
// lowers them by 10 (no Gen 4 drop to 100, I_BERRY_EV_JUMP) and raises friendship; the Sitrus Berry restores a quarter
// of the maximum HP. Each item's friendship change is its own data's (src/data/pokemon/item_effects.h). Not the bag's
// own consumption, the evolution scene or the moves learnt on a level up (party_menu.c).

#define BAG_SPECIES SPECIES_SNORLAX

static const u8 sEVData[NUM_STATS] = {
    MON_DATA_HP_EV, MON_DATA_ATK_EV, MON_DATA_DEF_EV, MON_DATA_SPEED_EV, MON_DATA_SPATK_EV, MON_DATA_SPDEF_EV,
};
static const char *const sStatNames[NUM_STATS] = {"HP", "Attack", "Defense", "Speed", "Sp. Atk", "Sp. Def"};
static const u16 sBagMoves[MAX_MON_MOVES] = {MOVE_TACKLE, MOVE_SURF, MOVE_HYPER_BEAM, MOVE_SKETCH}; // 35, 15, 5, 1 PP

// A party Pokemon for the bag's items: `species` at `level`, no effort values, met somewhere else than here (no
// friendship bonus, see the healing test's note), `friendship`
static struct Pokemon *BagMon(enum Species species, u32 level, u32 gender, u8 friendship)
{
    struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][0];
    u8 metLocation = MAPSEC_LITTLEROOT_TOWN;

    ZeroPlayerPartyMons();
    CreateMon(mon, species, level, GetMonPersonality(species, gender, NATURE_HARDY, 0), OTID_STRUCT_PLAYER_ID);
    CalculateMonStats(mon);
    gPartiesCount[B_TRAINER_PLAYER] = 1;
    SetMonData(mon, MON_DATA_MET_LOCATION, &metLocation);
    SetMonData(mon, MON_DATA_FRIENDSHIP, &friendship);
    return mon;
}

// `stat` at `start` effort values, `others` more spread over the next stats (252 at most each)
static void SetEVs(struct Pokemon *mon, u32 stat, u8 start, u16 others)
{
    u32 i;

    for (i = 0; i < NUM_STATS; i++)
    {
        u8 ev = 0;

        if (i == stat)
            ev = start;
        else if (others != 0)
        {
            ev = others > MAX_PER_STAT_EVS ? MAX_PER_STAT_EVS : others;
            others -= ev;
        }
        SetMonData(mon, sEVData[i], &ev);
    }
    CalculateMonStats(mon);
}

// An EV item on one stat at `start` (the other stats holding `others` in all): the stat's effort values after it, and
// whether it was used (an item with no effect is kept)
static void ExpectEVs(const char *what, u16 item, u32 stat, u8 start, u16 others, u8 want, bool32 used)
{
    struct Pokemon *mon = BagMon(BAG_SPECIES, 50, MON_MALE, 100);
    bool8 noEffect;
    u8 after;
    char line[200];

    SetEVs(mon, stat, start, others);
    noEffect = PokemonUseItemEffects(mon, item, 0, 0, FALSE);
    after = GetMonData(mon, sEVData[stat]);
    snprintf(line, sizeof(line), "%s: %s EVs %u -> %u (rule %u)%s", what, sStatNames[stat], start, after, want,
             used ? "" : ", no effect, kept");
    Expect(line, after == want && noEffect == !used);
}

// The test moves at their full PP, with no PP Up (CreateMon gives a Pokemon no moves)
static void GiveBagMoves(struct Pokemon *mon)
{
    u8 bonuses = 0;
    u32 i;

    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        u16 move = sBagMoves[i];
        u8 pp = CalculatePPWithBonus(move, 0, i);

        SetMonData(mon, MON_DATA_MOVE1 + i, &move);
        SetMonData(mon, MON_DATA_PP1 + i, &pp);
    }
    SetMonData(mon, MON_DATA_PP_BONUSES, &bonuses);
}

static struct Pokemon *PPMon(void)
{
    struct Pokemon *mon = BagMon(BAG_SPECIES, 50, MON_MALE, 100);

    GiveBagMoves(mon);
    return mon;
}

// An item's friendship change from `start`, on a Pokemon it has an effect on (50 effort values in every stat, level 50,
// the test moves)
static void ExpectItemFriendship(const char *what, u16 item, u8 start, u8 want)
{
    struct Pokemon *mon = BagMon(BAG_SPECIES, 50, MON_MALE, start);
    bool8 noEffect;
    u8 after;
    u32 i;
    char line[200];

    for (i = 0; i < NUM_STATS; i++)
    {
        u8 ev = 50;
        SetMonData(mon, sEVData[i], &ev);
    }
    CalculateMonStats(mon);
    GiveBagMoves(mon);
    noEffect = PokemonUseItemEffects(mon, item, 0, 0, FALSE);
    after = GetMonData(mon, MON_DATA_FRIENDSHIP);
    snprintf(line, sizeof(line), "%s: friendship %u -> %u (rule %u)", what, start, after, want);
    Expect(line, !noEffect && after == want);
}

static u8 MaxPPNow(struct Pokemon *mon, u32 i)
{
    return CalculatePPWithBonus(GetMonData(mon, MON_DATA_MOVE1 + i), GetMonData(mon, MON_DATA_PP_BONUSES), i);
}

// PP Up or PP Max on move `i`: its maximum PP and its PP after it
static void ExpectPP(const char *what, struct Pokemon *mon, u16 item, u32 i, u8 wantMax, u8 wantPP, bool32 used)
{
    u8 maxBefore = MaxPPNow(mon, i), ppBefore = GetMonData(mon, MON_DATA_PP1 + i);
    bool8 noEffect = PokemonUseItemEffects(mon, item, 0, i, FALSE);
    u8 maxAfter = MaxPPNow(mon, i), ppAfter = GetMonData(mon, MON_DATA_PP1 + i);
    char line[200];

    snprintf(line, sizeof(line), "%s: maximum PP %u -> %u, PP %u -> %u (rule %u, %u)%s", what, maxBefore, maxAfter,
             ppBefore, ppAfter, wantMax, wantPP, used ? "" : ", no effect, kept");
    Expect(line, maxAfter == wantMax && ppAfter == wantPP && noEffect == !used);
}

// The evolution a stone starts (CHECK_EVO: the Pokemon is left as it is); SPECIES_NONE: none, the stone kept
static void ExpectStone(const char *what, enum Species species, u32 gender, u16 item, u16 held, enum Species want)
{
    struct Pokemon *mon = BagMon(species, 30, gender, 100);
    bool32 canStopEvo = TRUE;
    enum Species target;
    char line[200];

    SetMonData(mon, MON_DATA_HELD_ITEM, &held);
    target = GetEvolutionTargetSpecies(mon, EVO_MODE_ITEM_USE, item, NULL, &canStopEvo, CHECK_EVO);
    snprintf(line, sizeof(line), "%s (species %u, rule %u)", what, (unsigned)target, (unsigned)want);
    Expect(line, target == want && GetMonData(mon, MON_DATA_SPECIES) == species);
}

int NativeSelfTest_BagItems(void)
{
    static const struct { u16 item; const char *name; u8 stat; } sVitamins[] = {
        {ITEM_HP_UP, "HP Up", STAT_HP}, {ITEM_PROTEIN, "Protein", STAT_ATK}, {ITEM_IRON, "Iron", STAT_DEF},
        {ITEM_CARBOS, "Carbos", STAT_SPEED}, {ITEM_CALCIUM, "Calcium", STAT_SPATK}, {ITEM_ZINC, "Zinc", STAT_SPDEF},
    };
    static const struct { u16 item; const char *name; u8 stat; } sWings[] = {
        {ITEM_HEALTH_WING, "Health Wing", STAT_HP}, {ITEM_MUSCLE_WING, "Muscle Wing", STAT_ATK},
        {ITEM_RESIST_WING, "Resist Wing", STAT_DEF}, {ITEM_SWIFT_WING, "Swift Wing", STAT_SPEED},
        {ITEM_GENIUS_WING, "Genius Wing", STAT_SPATK}, {ITEM_CLEVER_WING, "Clever Wing", STAT_SPDEF},
    };
    static const struct { u16 item; const char *name; u8 stat; } sEVBerries[] = {
        {ITEM_POMEG_BERRY, "Pomeg Berry", STAT_HP}, {ITEM_KELPSY_BERRY, "Kelpsy Berry", STAT_ATK},
        {ITEM_QUALOT_BERRY, "Qualot Berry", STAT_DEF}, {ITEM_TAMATO_BERRY, "Tamato Berry", STAT_SPEED},
        {ITEM_HONDEW_BERRY, "Hondew Berry", STAT_SPATK}, {ITEM_GREPA_BERRY, "Grepa Berry", STAT_SPDEF},
    };
    struct Pokemon *mon;
    u16 max;
    u32 i;
    char line[200];

    Setup();
    UseMap(MAP_ROUTE101);
    printf("selftest-bag-items: berries, vitamins, wings, PP Up, PP Max, Rare Candy, evolution stones, TMs and HMs "
           "through the game's own code, Gen 6 rules\n");
    printf("     the rules: a vitamin +10 effort values up to %d in a stat, a wing +1 up to %d, %d in all\n",
           EV_ITEM_RAISE_LIMIT, MAX_PER_STAT_EVS, MAX_TOTAL_EVS);

    // Berries a Pokemon eats from the bag (the status-curing ones and the Lum Berry: --selftest-healing)
    mon = BagMon(BAG_SPECIES, 50, MON_MALE, 100);
    max = GetMonData(mon, MON_DATA_MAX_HP);
    printf("     the test Pokemon: Snorlax level 50, %u HP\n", max);
    {
        u16 hp = 1;

        SetMonData(mon, MON_DATA_HP, &hp);
        snprintf(line, sizeof(line), "Oran Berry restores 10: HP 1 -> %u", 1 + 10);
        Expect(line, !PokemonUseItemEffects(mon, ITEM_ORAN_BERRY, 0, 0, FALSE) && GetMonData(mon, MON_DATA_HP) == 11);
        SetMonData(mon, MON_DATA_HP, &hp);
        {
            bool8 noEffect = PokemonUseItemEffects(mon, ITEM_SITRUS_BERRY, 0, 0, FALSE);
            u16 after = GetMonData(mon, MON_DATA_HP);

            snprintf(line, sizeof(line), "Sitrus Berry restores a quarter of the maximum: HP 1 -> %u (rule %u)", after,
                     1 + max / 4);
            Expect(line, !noEffect && after == 1 + max / 4);
        }
        SetMonData(mon, MON_DATA_HP, &max);
        Expect("Oran Berry at full HP: no effect (kept)", PokemonUseItemEffects(mon, ITEM_ORAN_BERRY, 0, 0, FALSE));
    }
    mon = PPMon();
    {
        u8 pp = 0;

        SetMonData(mon, MON_DATA_PP1, &pp);
        Expect("Leppa Berry restores 10 PP to the move chosen (Tackle 0 -> 10)",
               !PokemonUseItemEffects(mon, ITEM_LEPPA_BERRY, 0, 0, FALSE) && GetMonData(mon, MON_DATA_PP1) == 10
               && GetMonData(mon, MON_DATA_PP2) == CalculatePPWithBonus(MOVE_SURF, 0, 1));
        Expect("Leppa Berry on a move at full PP: no effect", PokemonUseItemEffects(mon, ITEM_LEPPA_BERRY, 0, 1, FALSE));
    }
    Expect("Persim Berry outside a battle (it cures confusion): no effect",
           PokemonUseItemEffects(BagMon(BAG_SPECIES, 50, MON_MALE, 100), ITEM_PERSIM_BERRY, 0, 0, FALSE));
    Expect("Figy Berry has no use from the bag (eaten only when held)", GetItemEffect(ITEM_FIGY_BERRY) == NULL
           && PokemonUseItemEffects(BagMon(BAG_SPECIES, 50, MON_MALE, 100), ITEM_FIGY_BERRY, 0, 0, FALSE));

    // Vitamins: +10, up to 100 in the stat, never past 510 in all
    for (i = 0; i < ARRAY_COUNT(sVitamins); i++)
        ExpectEVs(sVitamins[i].name, sVitamins[i].item, sVitamins[i].stat, 0, 0, 10, TRUE);
    ExpectEVs("HP Up 5 short of 100: to 100, no more", ITEM_HP_UP, STAT_HP, 95, 0, 100, TRUE);
    ExpectEVs("Protein at 100", ITEM_PROTEIN, STAT_ATK, 100, 0, 100, FALSE);
    ExpectEVs("Carbos with 505 in the other stats: to 510 in all", ITEM_CARBOS, STAT_SPEED, 0, 505, 5, TRUE);
    ExpectEVs("Zinc with 510 in the other stats", ITEM_ZINC, STAT_SPDEF, 0, 510, 0, FALSE);

    // Wings: +1, up to 252, never past 510 in all
    for (i = 0; i < ARRAY_COUNT(sWings); i++)
        ExpectEVs(sWings[i].name, sWings[i].item, sWings[i].stat, 0, 0, 1, TRUE);
    ExpectEVs("Health Wing at 100 (a vitamin's limit): on to 101", ITEM_HEALTH_WING, STAT_HP, 100, 0, 101, TRUE);
    ExpectEVs("Muscle Wing at 251: to 252", ITEM_MUSCLE_WING, STAT_ATK, 251, 0, 252, TRUE);
    ExpectEVs("Muscle Wing at 252", ITEM_MUSCLE_WING, STAT_ATK, 252, 0, 252, FALSE);
    ExpectEVs("Swift Wing with 510 in the other stats", ITEM_SWIFT_WING, STAT_SPEED, 0, 510, 0, FALSE);
    Expect("Pretty Wing: no effect", PokemonUseItemEffects(BagMon(BAG_SPECIES, 50, MON_MALE, 100), ITEM_PRETTY_WING, 0, 0, FALSE));

    // EV-lowering berries: -10 (Gen 6: no drop to 100 first), not below 0; at 0 the friendship is still raised
    for (i = 0; i < ARRAY_COUNT(sEVBerries); i++)
        ExpectEVs(sEVBerries[i].name, sEVBerries[i].item, sEVBerries[i].stat, 100, 0, 90, TRUE);
    ExpectEVs("Pomeg Berry at 252: to 242 (no Gen 4 drop to 100)", ITEM_POMEG_BERRY, STAT_HP, 252, 0, 242, TRUE);
    ExpectEVs("Kelpsy Berry at 5: to 0, not below", ITEM_KELPSY_BERRY, STAT_ATK, 5, 0, 0, TRUE);
    mon = BagMon(BAG_SPECIES, 50, MON_MALE, 150);
    Expect("Qualot Berry at 0 effort values: used, friendship 150 -> 155",
           !PokemonUseItemEffects(mon, ITEM_QUALOT_BERRY, 0, 0, FALSE) && GetMonData(mon, MON_DATA_DEF_EV) == 0
           && GetMonData(mon, MON_DATA_FRIENDSHIP) == 155);
    // At 0 effort values and the highest friendship nothing can change: PokemonUseItemEffects still reports the berry
    // used, and the bag's own ItemUseCB_ReduceEV (party_menu.c) keeps it ("It won't have any effect") because neither
    // value changed
    mon = BagMon(BAG_SPECIES, 50, MON_MALE, MAX_FRIENDSHIP);
    {
        bool8 noEffect = PokemonUseItemEffects(mon, ITEM_TAMATO_BERRY, 0, 0, FALSE);

        snprintf(line, sizeof(line), "Tamato Berry at 0 Speed effort values and friendship 255: neither changes (effort "
                 "values %u, friendship %u; the function says %s), so the bag keeps it",
                 (unsigned)GetMonData(mon, MON_DATA_SPEED_EV), (unsigned)GetMonData(mon, MON_DATA_FRIENDSHIP),
                 noEffect ? "no effect" : "used");
        Expect(line, GetMonData(mon, MON_DATA_SPEED_EV) == 0 && GetMonData(mon, MON_DATA_FRIENDSHIP) == MAX_FRIENDSHIP);
    }

    // Friendship: under 100, 100-199, 200 and over (vitamins, PP Up, PP Max, Rare Candy 5/3/2; wings 3/2/1; EV-lowering
    // berries 10/5/2)
    ExpectItemFriendship("HP Up", ITEM_HP_UP, 50, 55);
    ExpectItemFriendship("HP Up", ITEM_HP_UP, 150, 153);
    ExpectItemFriendship("HP Up", ITEM_HP_UP, 250, 252);
    ExpectItemFriendship("Health Wing", ITEM_HEALTH_WING, 50, 53);
    ExpectItemFriendship("Health Wing", ITEM_HEALTH_WING, 150, 152);
    ExpectItemFriendship("Health Wing", ITEM_HEALTH_WING, 250, 251);
    ExpectItemFriendship("Pomeg Berry", ITEM_POMEG_BERRY, 50, 60);
    ExpectItemFriendship("Pomeg Berry", ITEM_POMEG_BERRY, 150, 155);
    ExpectItemFriendship("Pomeg Berry", ITEM_POMEG_BERRY, 250, 252);
    ExpectItemFriendship("PP Up", ITEM_PP_UP, 50, 55);
    ExpectItemFriendship("PP Max", ITEM_PP_MAX, 150, 153);
    ExpectItemFriendship("Rare Candy", ITEM_RARE_CANDY, 250, 252);
    ExpectItemFriendship("HP Up at friendship 254: not past 255", ITEM_HP_UP, 254, 255);
    {
        u16 bell = ITEM_SOOTHE_BELL;

        mon = BagMon(BAG_SPECIES, 50, MON_MALE, 150);
        SetMonData(mon, MON_DATA_HELD_ITEM, &bell);
        PokemonUseItemEffects(mon, ITEM_HP_UP, 0, 0, FALSE);
        printf("     note: HP Up holding a Soothe Bell: friendship 150 -> %u (3 and half again, rounded down: 154)\n",
               (unsigned)GetMonData(mon, MON_DATA_FRIENDSHIP));
    }

    // PP Up: a fifth of the move's base PP more, three times at most; PP Max: all three at once (Tackle 35, Surf 15,
    // Hyper Beam 5, Sketch 1)
    mon = PPMon();
    ExpectPP("PP Up on Tackle", mon, ITEM_PP_UP, 0, 42, 42, TRUE);
    ExpectPP("PP Up on Tackle, the second", mon, ITEM_PP_UP, 0, 49, 49, TRUE);
    ExpectPP("PP Up on Tackle, the third", mon, ITEM_PP_UP, 0, 56, 56, TRUE);
    ExpectPP("PP Up on Tackle, a fourth", mon, ITEM_PP_UP, 0, 56, 56, FALSE);
    ExpectPP("PP Max on Tackle after three PP Ups", mon, ITEM_PP_MAX, 0, 56, 56, FALSE);
    ExpectPP("PP Max on Surf", mon, ITEM_PP_MAX, 1, 24, 24, TRUE);
    ExpectPP("PP Up on Surf after PP Max", mon, ITEM_PP_UP, 1, 24, 24, FALSE);
    ExpectPP("PP Up on Hyper Beam", mon, ITEM_PP_UP, 2, 6, 6, TRUE);
    ExpectPP("PP Max on Hyper Beam after one PP Up", mon, ITEM_PP_MAX, 2, 8, 8, TRUE);
    ExpectPP("PP Up on Sketch (1 PP)", mon, ITEM_PP_UP, 3, 1, 1, FALSE);
    ExpectPP("PP Max on Sketch", mon, ITEM_PP_MAX, 3, 1, 1, FALSE);
    mon = PPMon();
    {
        u8 pp = 10;

        SetMonData(mon, MON_DATA_PP1, &pp);
        ExpectPP("PP Up on Tackle with 10 PP left: its PP rise by the same 7", mon, ITEM_PP_UP, 0, 42, 17, TRUE);
    }

    // Rare Candy: the next level's experience, the HP the level adds
    {
        enum Species species = BAG_SPECIES;
        u32 growth = gSpeciesInfo[species].growthRate;
        u16 hpBefore, maxBefore, hp = 100;

        mon = BagMon(species, 50, MON_MALE, 100);
        SetMonData(mon, MON_DATA_HP, &hp);
        maxBefore = GetMonData(mon, MON_DATA_MAX_HP);
        hpBefore = GetMonData(mon, MON_DATA_HP);
        {
            bool8 noEffect = PokemonUseItemEffects(mon, ITEM_RARE_CANDY, 0, 0, FALSE);
            u32 level = GetMonData(mon, MON_DATA_LEVEL);
            u16 hpAfter = GetMonData(mon, MON_DATA_HP), rise = GetMonData(mon, MON_DATA_MAX_HP) - maxBefore;

            snprintf(line, sizeof(line), "Rare Candy: level 50 -> %u, experience at level 51's start, HP %u -> %u (+%u, "
                     "the maximum's rise)", level, hpBefore, hpAfter, rise);
            Expect(line, !noEffect && level == 51 && GetMonData(mon, MON_DATA_EXP) == gExperienceTables[growth][51]
                   && hpAfter == hpBefore + rise);
        }
        mon = BagMon(species, 99, MON_MALE, 100);
        Expect("Rare Candy at level 99: to 100", !PokemonUseItemEffects(mon, ITEM_RARE_CANDY, 0, 0, FALSE)
               && GetMonData(mon, MON_DATA_LEVEL) == MAX_LEVEL);
        mon = BagMon(species, MAX_LEVEL, MON_MALE, 100);
        Expect("Rare Candy at level 100: no effect (kept), no friendship", PokemonUseItemEffects(mon, ITEM_RARE_CANDY, 0, 0, FALSE)
               && GetMonData(mon, MON_DATA_LEVEL) == MAX_LEVEL && GetMonData(mon, MON_DATA_FRIENDSHIP) == 100);
        // Measured, printed: on a fainted Pokemon
        mon = BagMon(species, 50, MON_MALE, 100);
        hp = 0;
        SetMonData(mon, MON_DATA_HP, &hp);
        {
            bool8 noEffect = PokemonUseItemEffects(mon, ITEM_RARE_CANDY, 0, 0, FALSE);

            printf("     note: Rare Candy on a fainted Pokemon: %s, level %u, HP 0 -> %u\n", noEffect ? "no effect" : "used",
                   (unsigned)GetMonData(mon, MON_DATA_LEVEL), (unsigned)GetMonData(mon, MON_DATA_HP));
        }
    }

    // Evolution stones (a Pokemon at level 30): the evolution the stone starts, none where it has no use
    ExpectStone("Thunder Stone: Pikachu -> Raichu (Hoenn: not Alola's)", SPECIES_PIKACHU, MON_MALE, ITEM_THUNDER_STONE, ITEM_NONE, SPECIES_RAICHU);
    ExpectStone("Thunder Stone: Eevee -> Jolteon", SPECIES_EEVEE, MON_MALE, ITEM_THUNDER_STONE, ITEM_NONE, SPECIES_JOLTEON);
    ExpectStone("Water Stone: Eevee -> Vaporeon", SPECIES_EEVEE, MON_MALE, ITEM_WATER_STONE, ITEM_NONE, SPECIES_VAPOREON);
    ExpectStone("Fire Stone: Eevee -> Flareon", SPECIES_EEVEE, MON_MALE, ITEM_FIRE_STONE, ITEM_NONE, SPECIES_FLAREON);
    ExpectStone("Leaf Stone: Eevee -> Leafeon (this build's data, besides Petalburg Woods)", SPECIES_EEVEE, MON_MALE, ITEM_LEAF_STONE, ITEM_NONE, SPECIES_LEAFEON);
    ExpectStone("Ice Stone: Eevee -> Glaceon (this build's data, besides Shoal Cave)", SPECIES_EEVEE, MON_MALE, ITEM_ICE_STONE, ITEM_NONE, SPECIES_GLACEON);
    ExpectStone("Moon Stone: Eevee, no use", SPECIES_EEVEE, MON_MALE, ITEM_MOON_STONE, ITEM_NONE, SPECIES_NONE);
    ExpectStone("Fire Stone: Growlithe -> Arcanine", SPECIES_GROWLITHE, MON_MALE, ITEM_FIRE_STONE, ITEM_NONE, SPECIES_ARCANINE);
    ExpectStone("Fire Stone: Vulpix -> Ninetales", SPECIES_VULPIX, MON_FEMALE, ITEM_FIRE_STONE, ITEM_NONE, SPECIES_NINETALES);
    ExpectStone("Water Stone: Lombre -> Ludicolo", SPECIES_LOMBRE, MON_MALE, ITEM_WATER_STONE, ITEM_NONE, SPECIES_LUDICOLO);
    ExpectStone("Water Stone: Staryu -> Starmie", SPECIES_STARYU, MON_GENDERLESS, ITEM_WATER_STONE, ITEM_NONE, SPECIES_STARMIE);
    ExpectStone("Leaf Stone: Nuzleaf -> Shiftry", SPECIES_NUZLEAF, MON_MALE, ITEM_LEAF_STONE, ITEM_NONE, SPECIES_SHIFTRY);
    ExpectStone("Leaf Stone: Gloom -> Vileplume", SPECIES_GLOOM, MON_MALE, ITEM_LEAF_STONE, ITEM_NONE, SPECIES_VILEPLUME);
    ExpectStone("Sun Stone: Gloom -> Bellossom", SPECIES_GLOOM, MON_MALE, ITEM_SUN_STONE, ITEM_NONE, SPECIES_BELLOSSOM);
    ExpectStone("Sun Stone: Sunkern -> Sunflora", SPECIES_SUNKERN, MON_MALE, ITEM_SUN_STONE, ITEM_NONE, SPECIES_SUNFLORA);
    ExpectStone("Moon Stone: Skitty -> Delcatty", SPECIES_SKITTY, MON_FEMALE, ITEM_MOON_STONE, ITEM_NONE, SPECIES_DELCATTY);
    ExpectStone("Moon Stone: Clefairy -> Clefable", SPECIES_CLEFAIRY, MON_FEMALE, ITEM_MOON_STONE, ITEM_NONE, SPECIES_CLEFABLE);
    ExpectStone("Shiny Stone: Togetic -> Togekiss", SPECIES_TOGETIC, MON_MALE, ITEM_SHINY_STONE, ITEM_NONE, SPECIES_TOGEKISS);
    ExpectStone("Shiny Stone: Roselia -> Roserade", SPECIES_ROSELIA, MON_MALE, ITEM_SHINY_STONE, ITEM_NONE, SPECIES_ROSERADE);
    ExpectStone("Dusk Stone: Murkrow -> Honchkrow", SPECIES_MURKROW, MON_MALE, ITEM_DUSK_STONE, ITEM_NONE, SPECIES_HONCHKROW);
    ExpectStone("Dusk Stone: Misdreavus -> Mismagius", SPECIES_MISDREAVUS, MON_FEMALE, ITEM_DUSK_STONE, ITEM_NONE, SPECIES_MISMAGIUS);
    ExpectStone("Dawn Stone: a male Kirlia -> Gallade", SPECIES_KIRLIA, MON_MALE, ITEM_DAWN_STONE, ITEM_NONE, SPECIES_GALLADE);
    ExpectStone("Dawn Stone: a female Kirlia, no use", SPECIES_KIRLIA, MON_FEMALE, ITEM_DAWN_STONE, ITEM_NONE, SPECIES_NONE);
    ExpectStone("Dawn Stone: a female Snorunt -> Froslass", SPECIES_SNORUNT, MON_FEMALE, ITEM_DAWN_STONE, ITEM_NONE, SPECIES_FROSLASS);
    ExpectStone("Dawn Stone: a male Snorunt, no use", SPECIES_SNORUNT, MON_MALE, ITEM_DAWN_STONE, ITEM_NONE, SPECIES_NONE);
    ExpectStone("Fire Stone: Charmander, no use", SPECIES_CHARMANDER, MON_MALE, ITEM_FIRE_STONE, ITEM_NONE, SPECIES_NONE);
    ExpectStone("Water Stone: Pikachu, no use", SPECIES_PIKACHU, MON_MALE, ITEM_WATER_STONE, ITEM_NONE, SPECIES_NONE);
    ExpectStone("Thunder Stone: Raichu (fully evolved), no use", SPECIES_RAICHU, MON_MALE, ITEM_THUNDER_STONE, ITEM_NONE, SPECIES_NONE);
    ExpectStone("Thunder Stone: Pikachu holding an Everstone, none (Emerald's rule)", SPECIES_PIKACHU, MON_MALE, ITEM_THUNDER_STONE, ITEM_EVERSTONE, SPECIES_NONE);

    // TMs and HMs: Emerald's numbering, each in the TM pocket, the eight HM moves, who can learn them (the game's own
    // teachable learnsets; a TM's reuse, its sale and an HM move's forgetting: --selftest-move-switch)
    {
        static const u16 sEmeraldTMHMs[] = {
            MOVE_FOCUS_PUNCH, MOVE_DRAGON_CLAW, MOVE_WATER_PULSE, MOVE_CALM_MIND, MOVE_ROAR, MOVE_TOXIC, MOVE_HAIL,
            MOVE_BULK_UP, MOVE_BULLET_SEED, MOVE_HIDDEN_POWER, MOVE_SUNNY_DAY, MOVE_TAUNT, MOVE_ICE_BEAM, MOVE_BLIZZARD,
            MOVE_HYPER_BEAM, MOVE_LIGHT_SCREEN, MOVE_PROTECT, MOVE_RAIN_DANCE, MOVE_GIGA_DRAIN, MOVE_SAFEGUARD,
            MOVE_FRUSTRATION, MOVE_SOLAR_BEAM, MOVE_IRON_TAIL, MOVE_THUNDERBOLT, MOVE_THUNDER, MOVE_EARTHQUAKE,
            MOVE_RETURN, MOVE_DIG, MOVE_PSYCHIC, MOVE_SHADOW_BALL, MOVE_BRICK_BREAK, MOVE_DOUBLE_TEAM, MOVE_REFLECT,
            MOVE_SHOCK_WAVE, MOVE_FLAMETHROWER, MOVE_SLUDGE_BOMB, MOVE_SANDSTORM, MOVE_FIRE_BLAST, MOVE_ROCK_TOMB,
            MOVE_AERIAL_ACE, MOVE_TORMENT, MOVE_FACADE, MOVE_SECRET_POWER, MOVE_REST, MOVE_ATTRACT, MOVE_THIEF,
            MOVE_STEEL_WING, MOVE_SKILL_SWAP, MOVE_SNATCH, MOVE_OVERHEAT,
            MOVE_CUT, MOVE_FLY, MOVE_SURF, MOVE_STRENGTH, MOVE_FLASH, MOVE_ROCK_SMASH, MOVE_WATERFALL, MOVE_DIVE,
        };
        static const u16 sNoTMs[] = {
            SPECIES_MAGIKARP, SPECIES_DITTO, SPECIES_SMEARGLE, SPECIES_CATERPIE, SPECIES_METAPOD, SPECIES_KAKUNA,
            SPECIES_WURMPLE, SPECIES_SILCOON, SPECIES_CASCOON, SPECIES_BELDUM,
        };
        static const struct { u16 species; u16 move; bool8 learns; const char *what; } sLearns[] = {
            {SPECIES_SNORLAX, MOVE_SURF, TRUE, "Snorlax: HM03 Surf"}, {SPECIES_SNORLAX, MOVE_EARTHQUAKE, TRUE, "Snorlax: TM26 Earthquake"},
            {SPECIES_MACHAMP, MOVE_STRENGTH, TRUE, "Machamp: HM04 Strength"}, {SPECIES_PIDGEOT, MOVE_FLY, TRUE, "Pidgeot: HM02 Fly"},
            {SPECIES_CHARIZARD, MOVE_FLY, TRUE, "Charizard: HM02 Fly"}, {SPECIES_GYARADOS, MOVE_WATERFALL, TRUE, "Gyarados: HM07 Waterfall"},
            {SPECIES_WAILMER, MOVE_DIVE, TRUE, "Wailmer: HM08 Dive"}, {SPECIES_GEODUDE, MOVE_ROCK_SMASH, TRUE, "Geodude: HM06 Rock Smash"},
            {SPECIES_ZIGZAGOON, MOVE_CUT, TRUE, "Zigzagoon: HM01 Cut"}, {SPECIES_PIKACHU, MOVE_THUNDERBOLT, TRUE, "Pikachu: TM24 Thunderbolt"},
            {SPECIES_GYARADOS, MOVE_FLY, FALSE, "Gyarados: not HM02 Fly"}, {SPECIES_BULBASAUR, MOVE_FLY, FALSE, "Bulbasaur: not HM02 Fly"},
            {SPECIES_PIKACHU, MOVE_FLY, FALSE, "Pikachu: not HM02 Fly"},
        };
        u32 item, found = 0, wrong = 0, pocket = 0, mew = 0;

        for (item = ITEM_NONE + 1; item < ITEMS_COUNT; item++)
        {
            u32 index = GetItemTMHMIndex(item);

            if (index == 0)
                continue;
            found++;
            if (index > ARRAY_COUNT(sEmeraldTMHMs) || GetItemTMHMMoveId(item) != sEmeraldTMHMs[index - 1])
            {
                wrong++;
                printf("     TM/HM item %u (number %u) teaches move %u\n", item, index, (unsigned)GetItemTMHMMoveId(item));
            }
            if (GetItemPocket(item) != POCKET_TM_HM)
                pocket++;
        }
        snprintf(line, sizeof(line), "TM01-TM50 and HM01-HM08: %u items, each teaching Emerald's move for its number "
                 "(%u wrong), each in the TM pocket (%u not)", found, wrong, pocket);
        Expect(line, found == ARRAY_COUNT(sEmeraldTMHMs) && wrong == 0 && pocket == 0);
        for (i = 0; i < ARRAY_COUNT(sEmeraldTMHMs); i++)
            wrong += IsMoveHM(sEmeraldTMHMs[i]) != (i >= 50);
        Expect("HM moves: Cut, Fly, Surf, Strength, Flash, Rock Smash, Waterfall and Dive, and no TM's move", wrong == 0);
        for (i = 0; i < ARRAY_COUNT(sEmeraldTMHMs); i++)
            mew += CanLearnTeachableMove(SPECIES_MEW, sEmeraldTMHMs[i]);
        snprintf(line, sizeof(line), "Mew can learn every TM and HM (%u of %u)", mew, (unsigned)ARRAY_COUNT(sEmeraldTMHMs));
        Expect(line, mew == ARRAY_COUNT(sEmeraldTMHMs));
        for (i = 0; i < ARRAY_COUNT(sNoTMs); i++)
        {
            u32 learns = 0, m;

            for (m = 0; m < ARRAY_COUNT(sEmeraldTMHMs); m++)
                learns += CanLearnTeachableMove(sNoTMs[i], sEmeraldTMHMs[m]);
            snprintf(line, sizeof(line), "species %u (Magikarp, Ditto, Smeargle, the cocoons, Beldum...) can learn no TM "
                     "or HM (%u)", sNoTMs[i], learns);
            Expect(line, learns == 0);
        }
        for (i = 0; i < ARRAY_COUNT(sLearns); i++)
            Expect(sLearns[i].what, CanLearnTeachableMove(sLearns[i].species, sLearns[i].move) == sLearns[i].learns);
        // Measured, printed: the learnsets are the expansion's (the later games'), not Emerald's
        printf("     note: from the later games' learnsets, not Emerald's: Pikachu HM03 Surf %s, Wobbuffet TM20 Safeguard %s, "
               "Unown TM10 Hidden Power %s\n", CanLearnTeachableMove(SPECIES_PIKACHU, MOVE_SURF) ? "yes" : "no",
               CanLearnTeachableMove(SPECIES_WOBBUFFET, MOVE_SAFEGUARD) ? "yes" : "no",
               CanLearnTeachableMove(SPECIES_UNOWN, MOVE_HIDDEN_POWER) ? "yes" : "no");
    }

    printf("selftest-bag-items: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// --selftest-held-items (the full item audit, the held items whose effect is worked out outside a battle's own scripts):
// the effort values a Pokemon gains for a defeated Pokemon (MonGainEVs, what a battle calls for each Pokemon that gains
// experience) with a Macho Brace, each Power item and Pokerus, against this build's rules (Gen 6: a Power item adds 4
// to its stat, I_POWER_ITEM_BOOST below Gen 7; the Macho Brace and Pokerus double; 252 in a stat, 510 in all). The
// Pokemon defeated is a Zigzagoon, which yields 1 Speed effort value. And the Soothe Bell's friendship (AdjustFriendship).

static void ExpectGain(const char *what, u16 held, u8 pokerus, u32 stat, u8 start, u16 others, const u8 *want)
{
    struct Pokemon *mon = BagMon(BAG_SPECIES, 50, MON_MALE, 100);
    u8 got[NUM_STATS];
    bool32 same = TRUE;
    u32 i;
    char line[240];

    SetEVs(mon, stat, start, others);
    SetMonData(mon, MON_DATA_HELD_ITEM, &held);
    SetMonData(mon, MON_DATA_POKERUS, &pokerus);
    MonGainEVs(mon, SPECIES_ZIGZAGOON);
    for (i = 0; i < NUM_STATS; i++)
    {
        got[i] = GetMonData(mon, sEVData[i]);
        same &= got[i] == want[i];
    }
    snprintf(line, sizeof(line), "%s: HP %u, Attack %u, Defense %u, Speed %u, Sp. Atk %u, Sp. Def %u", what, got[0],
             got[1], got[2], got[3], got[4], got[5]);
    Expect(line, same);
}

int NativeSelfTest_HeldItems(void)
{
    static const struct { u16 item; const char *name; u8 stat; } sPowerItems[] = {
        {ITEM_POWER_WEIGHT, "Power Weight", STAT_HP}, {ITEM_POWER_BRACER, "Power Bracer", STAT_ATK},
        {ITEM_POWER_BELT, "Power Belt", STAT_DEF}, {ITEM_POWER_ANKLET, "Power Anklet", STAT_SPEED},
        {ITEM_POWER_LENS, "Power Lens", STAT_SPATK}, {ITEM_POWER_BAND, "Power Band", STAT_SPDEF},
    };
    const u8 pokerus = 0x11, cured = 0x10;    // strain 1 with a day left; strain 1, cured
    u32 i;

    Setup();
    UseMap(MAP_ROUTE101);
    printf("selftest-held-items: Macho Brace, the Power items and Pokerus through MonGainEVs, Gen 6 rules (a Power item "
           "+%u)\n", (unsigned)GetItemHoldEffectParam(ITEM_POWER_ANKLET));

    ExpectGain("A defeated Zigzagoon, nothing held: 1 Speed", ITEM_NONE, 0, STAT_SPEED, 0, 0, (const u8[]){0, 0, 0, 1, 0, 0});
    ExpectGain("Macho Brace: doubled", ITEM_MACHO_BRACE, 0, STAT_SPEED, 0, 0, (const u8[]){0, 0, 0, 2, 0, 0});
    ExpectGain("Pokerus: doubled", ITEM_NONE, pokerus, STAT_SPEED, 0, 0, (const u8[]){0, 0, 0, 2, 0, 0});
    ExpectGain("Pokerus, cured: still doubled", ITEM_NONE, cured, STAT_SPEED, 0, 0, (const u8[]){0, 0, 0, 2, 0, 0});
    ExpectGain("Macho Brace and Pokerus: four times", ITEM_MACHO_BRACE, pokerus, STAT_SPEED, 0, 0, (const u8[]){0, 0, 0, 4, 0, 0});
    for (i = 0; i < ARRAY_COUNT(sPowerItems); i++)
    {
        u8 want[NUM_STATS] = {0, 0, 0, 1, 0, 0};
        char what[80];

        want[sPowerItems[i].stat] += 4;
        snprintf(what, sizeof(what), "%s: 4 more in its stat", sPowerItems[i].name);
        ExpectGain(what, sPowerItems[i].item, 0, STAT_SPEED, 0, 0, want);
    }
    ExpectGain("Power Anklet and Pokerus: (1 + 4) x 2", ITEM_POWER_ANKLET, pokerus, STAT_SPEED, 0, 0, (const u8[]){0, 0, 0, 10, 0, 0});
    ExpectGain("Power Bracer and Pokerus: 4 x 2 Attack, 1 x 2 Speed", ITEM_POWER_BRACER, pokerus, STAT_SPEED, 0, 0, (const u8[]){0, 8, 0, 2, 0, 0});
    ExpectGain("Speed at 252: no more", ITEM_NONE, 0, STAT_SPEED, 252, 0, (const u8[]){0, 0, 0, 252, 0, 0});
    ExpectGain("Power Anklet at 250 Speed: to 252", ITEM_POWER_ANKLET, 0, STAT_SPEED, 250, 0, (const u8[]){0, 0, 0, 252, 0, 0});
    ExpectGain("Power Anklet with 509 in all: 1 more (510)", ITEM_POWER_ANKLET, 0, STAT_SPEED, 0, 509, (const u8[]){252, 252, 5, 1, 0, 0});
    ExpectGain("Macho Brace with 510 in all: none", ITEM_MACHO_BRACE, 0, STAT_SPEED, 0, 510, (const u8[]){252, 252, 6, 0, 0, 0});

    // Soothe Bell: a friendship gain half again, rounded down (Gen 4+), a loss as it is (AdjustFriendship, what a level
    // up and a fainting call); a Pokemon met elsewhere than here, so no met-here bonus (MS-150)
    {
        static const struct { u8 start; u8 event; u8 plain; u8 bell; const char *what; } sBell[] = {
            {50, FRIENDSHIP_EVENT_GROW_LEVEL, 55, 57, "a level up under 100: +5, with a Soothe Bell +7"},
            {150, FRIENDSHIP_EVENT_GROW_LEVEL, 153, 154, "a level up at 100-199: +3, with a Soothe Bell +4"},
            {250, FRIENDSHIP_EVENT_GROW_LEVEL, 252, 253, "a level up at 200 and over: +2, with a Soothe Bell +3"},
            {150, FRIENDSHIP_EVENT_FAINT_SMALL, 149, 149, "fainting: -1, with a Soothe Bell still -1"},
        };
        char line[200];

        for (i = 0; i < ARRAY_COUNT(sBell); i++)
        {
            u16 bell = ITEM_SOOTHE_BELL;
            struct Pokemon *mon = BagMon(BAG_SPECIES, 50, MON_MALE, sBell[i].start);
            u8 plain, withBell;

            AdjustFriendship(mon, sBell[i].event);
            plain = GetMonData(mon, MON_DATA_FRIENDSHIP);
            mon = BagMon(BAG_SPECIES, 50, MON_MALE, sBell[i].start);
            SetMonData(mon, MON_DATA_HELD_ITEM, &bell);
            AdjustFriendship(mon, sBell[i].event);
            withBell = GetMonData(mon, MON_DATA_FRIENDSHIP);
            snprintf(line, sizeof(line), "Soothe Bell, %s: friendship %u -> %u, with it %u", sBell[i].what, sBell[i].start,
                     plain, withBell);
            Expect(line, plain == sBell[i].plain && withBell == sBell[i].bell);
        }
    }

    // Luxury Ball: a friendship gain 1 more for a Pokemon caught in one (CalculateFriendshipBonuses); on a loss, MS-150
    {
        static const struct { u8 start; u8 plain; u8 luxury; const char *what; } sLuxury[] = {
            {50, 55, 56, "a level up under 100: +5, in a Luxury Ball +6"},
            {150, 153, 154, "a level up at 100-199: +3, in a Luxury Ball +4"},
        };
        char line[200];

        for (i = 0; i < ARRAY_COUNT(sLuxury); i++)
        {
            u8 ball = BALL_LUXURY;
            struct Pokemon *mon = BagMon(BAG_SPECIES, 50, MON_MALE, sLuxury[i].start);
            u8 plain, inLuxury;

            AdjustFriendship(mon, FRIENDSHIP_EVENT_GROW_LEVEL);
            plain = GetMonData(mon, MON_DATA_FRIENDSHIP);
            mon = BagMon(BAG_SPECIES, 50, MON_MALE, sLuxury[i].start);
            SetMonData(mon, MON_DATA_POKEBALL, &ball);
            AdjustFriendship(mon, FRIENDSHIP_EVENT_GROW_LEVEL);
            inLuxury = GetMonData(mon, MON_DATA_FRIENDSHIP);
            snprintf(line, sizeof(line), "Luxury Ball, %s: friendship %u -> %u, in it %u", sLuxury[i].what,
                     sLuxury[i].start, plain, inLuxury);
            Expect(line, plain == sLuxury[i].plain && inLuxury == sLuxury[i].luxury);
        }
    }

    printf("selftest-held-items: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// --selftest-mints (the full item audit, mints): each mint gives the nature it is named after (its data), and what the
// bag's mint does (party_menu.c Task_Mint: the Pokemon's stat nature set to the mint's, its stats worked out again)
// raises and lowers the stats that nature names by a tenth, leaves the Pokemon's own nature (its personality, which
// Synchronize and breeding read) as it was, and stays in the Pokemon's stored data.

static const u8 sNatureStats[] = {MON_DATA_MAX_HP, MON_DATA_ATK, MON_DATA_DEF, MON_DATA_SPEED, MON_DATA_SPATK, MON_DATA_SPDEF};

static void ReadStats(struct Pokemon *mon, u16 *stats)
{
    for (u32 i = 0; i < NUM_STATS; i++)
        stats[i] = GetMonData(mon, sNatureStats[i]);
}

// The bag's mint on `mon` (Task_Mint's last step), then each stat against the Hardy stats: x1.1 for `up`, x0.9 for
// `down` (rounded down), the others unchanged
static void ExpectMint(const char *what, struct Pokemon *mon, const u16 *hardy, u8 nature, u32 up, u32 down)
{
    u16 stats[NUM_STATS];
    bool32 ok = TRUE;
    char line[240];

    SetMonData(mon, MON_DATA_HIDDEN_NATURE, &nature);
    CalculateMonStats(mon);
    ReadStats(mon, stats);
    for (u32 i = 0; i < NUM_STATS; i++)
    {
        u16 want = i == up ? hardy[i] * 110 / 100 : i == down ? hardy[i] * 90 / 100 : hardy[i];

        ok &= stats[i] == want;
    }
    snprintf(line, sizeof(line), "%s: HP %u, Attack %u -> %u, Defense %u, Speed %u -> %u, Sp. Atk %u -> %u, Sp. Def %u; "
             "its own nature still %u", what, stats[0], hardy[1], stats[1], stats[2], hardy[3], stats[3], hardy[4], stats[4],
             stats[5], GetNature(mon));
    Expect(line, ok && GetNature(mon) == NATURE_HARDY && GetMonData(mon, MON_DATA_HIDDEN_NATURE) == nature);
}

int NativeSelfTest_Mints(void)
{
    static const struct { u16 item; u8 nature; } sMints[] = {
        {ITEM_LONELY_MINT, NATURE_LONELY}, {ITEM_ADAMANT_MINT, NATURE_ADAMANT}, {ITEM_NAUGHTY_MINT, NATURE_NAUGHTY},
        {ITEM_BRAVE_MINT, NATURE_BRAVE}, {ITEM_BOLD_MINT, NATURE_BOLD}, {ITEM_IMPISH_MINT, NATURE_IMPISH},
        {ITEM_LAX_MINT, NATURE_LAX}, {ITEM_RELAXED_MINT, NATURE_RELAXED}, {ITEM_MODEST_MINT, NATURE_MODEST},
        {ITEM_MILD_MINT, NATURE_MILD}, {ITEM_RASH_MINT, NATURE_RASH}, {ITEM_QUIET_MINT, NATURE_QUIET},
        {ITEM_CALM_MINT, NATURE_CALM}, {ITEM_GENTLE_MINT, NATURE_GENTLE}, {ITEM_CAREFUL_MINT, NATURE_CAREFUL},
        {ITEM_SASSY_MINT, NATURE_SASSY}, {ITEM_TIMID_MINT, NATURE_TIMID}, {ITEM_HASTY_MINT, NATURE_HASTY},
        {ITEM_JOLLY_MINT, NATURE_JOLLY}, {ITEM_NAIVE_MINT, NATURE_NAIVE}, {ITEM_SERIOUS_MINT, NATURE_SERIOUS},
    };
    struct Pokemon *mon;
    struct Pokemon stored;
    u16 hardy[NUM_STATS];
    u32 i, wrong = 0;
    char line[200];

    Setup();
    UseMap(MAP_ROUTE101);
    printf("selftest-mints: the 21 mints' natures and what a mint does to a Pokemon's stats\n");
    for (i = 0; i < ARRAY_COUNT(sMints); i++)
    {
        if (GetItemSecondaryId(sMints[i].item) != sMints[i].nature)
        {
            wrong++;
            printf("     item %u gives nature %u, not %u\n", sMints[i].item, (unsigned)GetItemSecondaryId(sMints[i].item),
                   sMints[i].nature);
        }
    }
    snprintf(line, sizeof(line), "21 mints, each giving the nature it is named after (%u wrong)", wrong);
    Expect(line, wrong == 0);

    // A Hardy Snorlax at level 100 (a neutral nature: its stats as they are)
    mon = BagMon(SPECIES_SNORLAX, MAX_LEVEL, MON_MALE, 100);
    ReadStats(mon, hardy);
    Expect("A new Pokemon's stat nature is its own (Hardy)", GetMonData(mon, MON_DATA_HIDDEN_NATURE) == NATURE_HARDY
           && GetNature(mon) == NATURE_HARDY);
    ExpectMint("Adamant Mint: Attack +10%, Sp. Atk -10%", mon, hardy, NATURE_ADAMANT, STAT_ATK, STAT_SPATK);
    ExpectMint("then a Jolly Mint: Speed +10%, Sp. Atk -10%, Attack back", mon, hardy, NATURE_JOLLY, STAT_SPEED, STAT_SPATK);
    ExpectMint("then a Modest Mint: Sp. Atk +10%, Attack -10%", mon, hardy, NATURE_MODEST, STAT_SPATK, STAT_ATK);
    ExpectMint("then a Serious Mint (neutral): every stat as Hardy's", mon, hardy, NATURE_SERIOUS, NUM_STATS, NUM_STATS);
    {
        u8 nature = NATURE_ADAMANT;

        SetMonData(mon, MON_DATA_HIDDEN_NATURE, &nature);
        CalculateMonStats(mon);
        BoxMonToMon(&mon->box, &stored);
        Expect("The stat nature is kept in the Pokemon's stored data (its box form, as saved): Adamant, its own nature "
               "Hardy", GetMonData(&stored, MON_DATA_HIDDEN_NATURE) == NATURE_ADAMANT && GetNature(&stored) == NATURE_HARDY
               && GetMonData(&stored, MON_DATA_ATK) == hardy[STAT_ATK] * 110 / 100);
    }

    printf("selftest-mints: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// --selftest-evo-items (the full item audit, the items a Pokemon evolves by holding when it is traded): the evolution a
// trade starts (GetEvolutionTargetSpecies in EVO_MODE_TRADE, what the trade's evolution asks; CHECK_EVO leaves the
// Pokemon as it was) for the Gen 1-6 trade evolutions, with their item and without it, with another's item, with an
// Everstone (it stops a trade evolution, but not Kadabra's from Gen 4: P_KADABRA_EVERSTONE), and the Karrablast and
// Shelmet pair, which evolve when traded for each other. The trade itself (a link or a world's trade) is not part of it.
// After the package: the Gen 4 evolutions on a level up while holding an item at a time of day (EVO_MODE_NORMAL, the
// level up's own check, the time set with the overworld's override): Happiny's Oval Stone by day, Sneasel's Razor
// Claw and Gligar's Razor Fang at night; none at the other time or without the item.

static enum Species TradeTarget(enum Species species, u16 held, struct Pokemon *partner)
{
    struct Pokemon *mon = BagMon(species, 40, MON_MALE, 100);
    bool32 canStopEvo = TRUE;

    SetMonData(mon, MON_DATA_HELD_ITEM, &held);
    return GetEvolutionTargetSpecies(mon, EVO_MODE_TRADE, ITEM_NONE, partner, &canStopEvo, CHECK_EVO);
}

// A level up's evolution check for `species` at level 40 holding `held` (EVO_MODE_NORMAL)
static enum Species LevelUpTarget(enum Species species, u16 held)
{
    struct Pokemon *mon = BagMon(species, 40, MON_MALE, 100);
    bool32 canStopEvo = TRUE;

    SetMonData(mon, MON_DATA_HELD_ITEM, &held);
    return GetEvolutionTargetSpecies(mon, EVO_MODE_NORMAL, ITEM_NONE, NULL, &canStopEvo, CHECK_EVO);
}

int NativeSelfTest_EvoItems(void)
{
    static const struct { u16 species; u16 held; u16 want; const char *what; } sTrades[] = {
        {SPECIES_KADABRA, ITEM_NONE, SPECIES_ALAKAZAM, "Kadabra -> Alakazam"},
        {SPECIES_MACHOKE, ITEM_NONE, SPECIES_MACHAMP, "Machoke -> Machamp"},
        {SPECIES_GRAVELER, ITEM_NONE, SPECIES_GOLEM, "Graveler -> Golem (not Alola's)"},
        {SPECIES_HAUNTER, ITEM_NONE, SPECIES_GENGAR, "Haunter -> Gengar"},
        {SPECIES_BOLDORE, ITEM_NONE, SPECIES_GIGALITH, "Boldore -> Gigalith"},
        {SPECIES_GURDURR, ITEM_NONE, SPECIES_CONKELDURR, "Gurdurr -> Conkeldurr"},
        {SPECIES_PHANTUMP, ITEM_NONE, SPECIES_TREVENANT, "Phantump -> Trevenant"},
        {SPECIES_ONIX, ITEM_METAL_COAT, SPECIES_STEELIX, "Onix holding a Metal Coat -> Steelix"},
        {SPECIES_SCYTHER, ITEM_METAL_COAT, SPECIES_SCIZOR, "Scyther holding a Metal Coat -> Scizor"},
        {SPECIES_POLIWHIRL, ITEM_KINGS_ROCK, SPECIES_POLITOED, "Poliwhirl holding a King's Rock -> Politoed"},
        {SPECIES_SLOWPOKE, ITEM_KINGS_ROCK, SPECIES_SLOWKING, "Slowpoke holding a King's Rock -> Slowking"},
        {SPECIES_SEADRA, ITEM_DRAGON_SCALE, SPECIES_KINGDRA, "Seadra holding a Dragon Scale -> Kingdra"},
        {SPECIES_PORYGON, ITEM_UPGRADE, SPECIES_PORYGON2, "Porygon holding an Up-Grade -> Porygon2"},
        {SPECIES_PORYGON2, ITEM_DUBIOUS_DISC, SPECIES_PORYGON_Z, "Porygon2 holding a Dubious Disc -> Porygon-Z"},
        {SPECIES_RHYDON, ITEM_PROTECTOR, SPECIES_RHYPERIOR, "Rhydon holding a Protector -> Rhyperior"},
        {SPECIES_ELECTABUZZ, ITEM_ELECTIRIZER, SPECIES_ELECTIVIRE, "Electabuzz holding an Electirizer -> Electivire"},
        {SPECIES_MAGMAR, ITEM_MAGMARIZER, SPECIES_MAGMORTAR, "Magmar holding a Magmarizer -> Magmortar"},
        {SPECIES_DUSCLOPS, ITEM_REAPER_CLOTH, SPECIES_DUSKNOIR, "Dusclops holding a Reaper Cloth -> Dusknoir"},
        {SPECIES_CLAMPERL, ITEM_DEEP_SEA_TOOTH, SPECIES_HUNTAIL, "Clamperl holding a Deep Sea Tooth -> Huntail"},
        {SPECIES_CLAMPERL, ITEM_DEEP_SEA_SCALE, SPECIES_GOREBYSS, "Clamperl holding a Deep Sea Scale -> Gorebyss"},
        {SPECIES_FEEBAS, ITEM_PRISM_SCALE, SPECIES_MILOTIC, "Feebas holding a Prism Scale -> Milotic"},
        {SPECIES_SWIRLIX, ITEM_WHIPPED_DREAM, SPECIES_SLURPUFF, "Swirlix holding a Whipped Dream -> Slurpuff"},
        {SPECIES_SPRITZEE, ITEM_SACHET, SPECIES_AROMATISSE, "Spritzee holding a Sachet -> Aromatisse"},
        {SPECIES_ONIX, ITEM_NONE, SPECIES_NONE, "Onix holding nothing: no evolution"},
        {SPECIES_CLAMPERL, ITEM_NONE, SPECIES_NONE, "Clamperl holding nothing: no evolution"},
        {SPECIES_SEADRA, ITEM_METAL_COAT, SPECIES_NONE, "Seadra holding a Metal Coat (another's item): no evolution"},
        {SPECIES_PORYGON, ITEM_DUBIOUS_DISC, SPECIES_NONE, "Porygon holding a Dubious Disc (Porygon2's): no evolution"},
        {SPECIES_HAUNTER, ITEM_EVERSTONE, SPECIES_NONE, "Haunter holding an Everstone: no evolution"},
        {SPECIES_KADABRA, ITEM_EVERSTONE, SPECIES_ALAKAZAM, "Kadabra holding an Everstone: still Alakazam (Gen 4+)"},
        {SPECIES_PIKACHU, ITEM_NONE, SPECIES_NONE, "Pikachu: no trade evolution"},
    };
    struct Pokemon partner;
    enum Species target;
    u32 i;
    char line[200];

    Setup();
    UseMap(MAP_ROUTE101);
    printf("selftest-evo-items: the Gen 1-6 trade evolutions and their items through GetEvolutionTargetSpecies\n");
    for (i = 0; i < ARRAY_COUNT(sTrades); i++)
    {
        target = TradeTarget(sTrades[i].species, sTrades[i].held, NULL);
        snprintf(line, sizeof(line), "%s (species %u, rule %u)", sTrades[i].what, (unsigned)target, sTrades[i].want);
        Expect(line, target == sTrades[i].want);
    }
    CreateMon(&partner, SPECIES_SHELMET, 30, GetMonPersonality(SPECIES_SHELMET, MON_MALE, NATURE_HARDY, 0), OTID_STRUCT_PLAYER_ID);
    target = TradeTarget(SPECIES_KARRABLAST, ITEM_NONE, &partner);
    snprintf(line, sizeof(line), "Karrablast traded for a Shelmet -> Escavalier (species %u)", (unsigned)target);
    Expect(line, target == SPECIES_ESCAVALIER);
    CreateMon(&partner, SPECIES_KARRABLAST, 30, GetMonPersonality(SPECIES_KARRABLAST, MON_MALE, NATURE_HARDY, 0), OTID_STRUCT_PLAYER_ID);
    target = TradeTarget(SPECIES_SHELMET, ITEM_NONE, &partner);
    snprintf(line, sizeof(line), "Shelmet traded for a Karrablast -> Accelgor (species %u)", (unsigned)target);
    Expect(line, target == SPECIES_ACCELGOR);
    CreateMon(&partner, SPECIES_PIKACHU, 30, GetMonPersonality(SPECIES_PIKACHU, MON_MALE, NATURE_HARDY, 0), OTID_STRUCT_PLAYER_ID);
    target = TradeTarget(SPECIES_KARRABLAST, ITEM_NONE, &partner);
    snprintf(line, sizeof(line), "Karrablast traded for a Pikachu: no evolution (species %u)", (unsigned)target);
    Expect(line, target == SPECIES_NONE);

    {
        static const struct { u16 species; u16 held; u8 hour; u16 want; const char *what; } sLevelUps[] = {
            {SPECIES_HAPPINY, ITEM_OVAL_STONE, 12, SPECIES_CHANSEY, "Happiny holding an Oval Stone, a level up at noon -> Chansey"},
            {SPECIES_HAPPINY, ITEM_OVAL_STONE, 22, SPECIES_NONE, "Happiny holding an Oval Stone at 22:00 (night): no evolution"},
            {SPECIES_HAPPINY, ITEM_NONE, 12, SPECIES_NONE, "Happiny holding nothing at noon: no evolution"},
            {SPECIES_SNEASEL, ITEM_RAZOR_CLAW, 22, SPECIES_WEAVILE, "Sneasel holding a Razor Claw, a level up at 22:00 -> Weavile"},
            {SPECIES_SNEASEL, ITEM_RAZOR_CLAW, 12, SPECIES_NONE, "Sneasel holding a Razor Claw at noon: no evolution"},
            {SPECIES_SNEASEL, ITEM_NONE, 22, SPECIES_NONE, "Sneasel holding nothing at 22:00: no evolution"},
            {SPECIES_GLIGAR, ITEM_RAZOR_FANG, 22, SPECIES_GLISCOR, "Gligar holding a Razor Fang, a level up at 22:00 -> Gliscor"},
            {SPECIES_GLIGAR, ITEM_RAZOR_FANG, 12, SPECIES_NONE, "Gligar holding a Razor Fang at noon: no evolution"},
            {SPECIES_GLIGAR, ITEM_RAZOR_CLAW, 22, SPECIES_NONE, "Gligar holding a Razor Claw (Sneasel's) at 22:00: no evolution"},
        };

        for (i = 0; i < ARRAY_COUNT(sLevelUps); i++)
        {
            SetTimeOfDay(sLevelUps[i].hour);
            target = LevelUpTarget(sLevelUps[i].species, sLevelUps[i].held);
            snprintf(line, sizeof(line), "%s (species %u, rule %u)", sLevelUps[i].what, (unsigned)target,
                     sLevelUps[i].want);
            Expect(line, target == sLevelUps[i].want);
        }
        SetTimeOfDay(0);
    }

    printf("selftest-evo-items: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// --selftest-ability-items (the full item audit, the Ability Capsule and the Ability Patch): what each does to a Pokemon's
// ability, by the rules the party menu checks before it sets MON_DATA_ABILITY_NUM (party_menu.c ItemUseCB_AbilityCapsule
// and Task_AbilityCapsule: the other of a species' two abilities, not where it has one or the Pokemon has its hidden
// ability; ItemUseCB_AbilityPatch and Task_AbilityPatch: the hidden ability, or from it back to the first, not where the
// species has none, nor on the PC for an Egg or a switch that changes nothing), against the species' data through
// GetMonAbility, and kept in the Pokemon's stored data. The rules are the tasks' own conditions copied here (the tasks
// ask and answer through the party menu's windows); the item leaving the bag is the menu's.

// Task_AbilityCapsule's check and its last step
static bool32 UseAbilityCapsule(struct Pokemon *mon)
{
    enum Species species = GetMonData(mon, MON_DATA_SPECIES);
    u8 abilityNum = GetMonData(mon, MON_DATA_ABILITY_NUM) ^ 1;

    if (GetSpeciesAbility(species, 0) == GetSpeciesAbility(species, 1) || GetSpeciesAbility(species, 1) == ABILITY_NONE
     || abilityNum > 1 || species == SPECIES_NONE)
        return FALSE;
    SetMonData(mon, MON_DATA_ABILITY_NUM, &abilityNum);
    return TRUE;
}

// Task_AbilityPatch's check (with its PC lines) and its last step
static bool32 UseAbilityPatch(struct Pokemon *mon)
{
    enum Species species = GetMonData(mon, MON_DATA_SPECIES);
    u8 abilityNum = GetMonData(mon, MON_DATA_ABILITY_NUM) == 2 ? 0 : 2;

    if (GetSpeciesAbility(species, abilityNum) == ABILITY_NONE || species == SPECIES_NONE
     || GetMonData(mon, MON_DATA_IS_EGG) || GetSpeciesAbility(species, abilityNum) == GetMonAbility(mon))
        return FALSE;
    SetMonData(mon, MON_DATA_ABILITY_NUM, &abilityNum);
    return TRUE;
}

static void ExpectAbility(const char *what, struct Pokemon *mon, bool32 used, bool32 wantUsed, enum Ability want)
{
    char line[200];
    enum Ability ability = GetMonAbility(mon);

    snprintf(line, sizeof(line), "%s: %s, ability %u, slot %u", what, used ? "used" : "no effect", (unsigned)ability,
             (unsigned)GetMonData(mon, MON_DATA_ABILITY_NUM));
    Expect(line, used == wantUsed && ability == want);
}

static struct Pokemon *AbilityMon(enum Species species, u8 abilityNum)
{
    struct Pokemon *mon = BagMon(species, 30, MON_MALE, 70);

    SetMonData(mon, MON_DATA_ABILITY_NUM, &abilityNum);
    return mon;
}

int NativeSelfTest_AbilityItems(void)
{
    struct Pokemon *mon;
    struct Pokemon stored;
    bool8 egg = TRUE;

    Setup();
    UseMap(MAP_ROUTE101);
    printf("selftest-ability-items: the Ability Capsule's and the Ability Patch's rules against the species' abilities\n");
    // Geodude: Rock Head, Sturdy, its hidden Sand Veil
    mon = AbilityMon(SPECIES_GEODUDE, 0);
    Expect("Geodude, its first ability: Rock Head", GetMonAbility(mon) == ABILITY_ROCK_HEAD);
    ExpectAbility("Ability Capsule: its second, Sturdy", mon, UseAbilityCapsule(mon), TRUE, ABILITY_STURDY);
    BoxMonToMon(&mon->box, &stored);
    Expect("Sturdy kept in its stored data (its box form, as saved)", GetMonAbility(&stored) == ABILITY_STURDY);
    ExpectAbility("Ability Capsule again: back to Rock Head", mon, UseAbilityCapsule(mon), TRUE, ABILITY_ROCK_HEAD);
    ExpectAbility("Ability Patch: its hidden Sand Veil", mon, UseAbilityPatch(mon), TRUE, ABILITY_SAND_VEIL);
    ExpectAbility("Ability Capsule with its hidden ability: no effect", mon, UseAbilityCapsule(mon), FALSE, ABILITY_SAND_VEIL);
    ExpectAbility("Ability Patch again: back to its first, Rock Head", mon, UseAbilityPatch(mon), TRUE, ABILITY_ROCK_HEAD);
    // Magnemite: Magnet Pull, Sturdy, its hidden Analytic
    mon = AbilityMon(SPECIES_MAGNEMITE, 1);
    ExpectAbility("Magnemite with Sturdy: Ability Capsule, Magnet Pull", mon, UseAbilityCapsule(mon), TRUE, ABILITY_MAGNET_PULL);
    ExpectAbility("then the Ability Patch: Analytic", mon, UseAbilityPatch(mon), TRUE, ABILITY_ANALYTIC);
    // One ability and a hidden one: Bulbasaur (Overgrow, Chlorophyll)
    mon = AbilityMon(SPECIES_BULBASAUR, 0);
    ExpectAbility("Bulbasaur (one ability): Ability Capsule, no effect", mon, UseAbilityCapsule(mon), FALSE, ABILITY_OVERGROW);
    ExpectAbility("Bulbasaur: Ability Patch, Chlorophyll", mon, UseAbilityPatch(mon), TRUE, ABILITY_CHLOROPHYLL);
    // One ability, no hidden one: Unown (Levitate), Shedinja (Wonder Guard)
    mon = AbilityMon(SPECIES_UNOWN, 0);
    ExpectAbility("Unown: Ability Capsule, no effect", mon, UseAbilityCapsule(mon), FALSE, ABILITY_LEVITATE);
    ExpectAbility("Unown (no hidden ability): Ability Patch, no effect", mon, UseAbilityPatch(mon), FALSE, ABILITY_LEVITATE);
    mon = AbilityMon(SPECIES_SHEDINJA, 0);
    ExpectAbility("Shedinja: Ability Capsule, no effect", mon, UseAbilityCapsule(mon), FALSE, ABILITY_WONDER_GUARD);
    ExpectAbility("Shedinja: Ability Patch, no effect", mon, UseAbilityPatch(mon), FALSE, ABILITY_WONDER_GUARD);
    // An Egg (the PC's rule: no Patch used on it)
    mon = AbilityMon(SPECIES_GEODUDE, 0);
    SetMonData(mon, MON_DATA_IS_EGG, &egg);
    ExpectAbility("a Geodude Egg: Ability Patch, no effect", mon, UseAbilityPatch(mon), FALSE, ABILITY_ROCK_HEAD);

    printf("selftest-ability-items: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// --selftest-rods (the full item audit, the Old, Good and Super Rods): a rod's chance of a bite (CalculateFishingBiteOdds,
// as Fishing_CheckForBite rolls it: the Gen 4+ odds this game is set to, doubled by a lead with Suction Cups or Sticky Hold,
// not by an Egg), the encounter slot each rod picks and how often (ChooseWildMonIndex_Fishing: the Old Rod slots 0-1 at
// 70/30, the Good Rod 2-4 at 60/20/20, the Super Rod 5-9 at 40/40/15/4/1; with a lure a fifth of them are swapped end for
// end), and every map's fishing table (each slot a Pokemon, its levels in order). Casting from the bag and the reeling
// screen are not part of it.

extern u32 MPFishing_TestBiteOdds(u32 rod);
extern u32 MPWild_TestFishingIndex(u8 rod);

static void ExpectBiteOdds(const char *who, const u32 *want)
{
    static const char *const sRods[] = {"Old Rod", "Good Rod", "Super Rod"};
    char line[200];

    for (u32 rod = OLD_ROD; rod <= SUPER_ROD; rod++)
    {
        u32 odds = MPFishing_TestBiteOdds(rod);

        snprintf(line, sizeof(line), "%s: the %s bites %u%% of the time (rule %u%%)", who, sRods[rod], (unsigned)odds, (unsigned)want[rod]);
        Expect(line, odds == want[rod]);
    }
}

// Each rod's slots from many bites: no slot outside its own, and each of its own at its rate (percent, slot by slot).
static void ExpectRodSlots(const char *rodName, u8 rod, u32 first, u32 count, const double *percent, bool32 lure)
{
    const u32 n = 200000;
    u32 hits[NUM_FISHING_MONS_ENCOUNTER_SLOTS] = {0}, outside = 0;
    char line[200];

    VarSet(VAR_REPEL_STEP_COUNT, lure ? (100 | REPEL_LURE_MASK) : 0);
    for (u32 i = 0; i < n; i++)
    {
        u32 slot = MPWild_TestFishingIndex(rod);

        if (slot >= first && slot < first + count)
            hits[slot]++;
        else
            outside++;
    }
    VarSet(VAR_REPEL_STEP_COUNT, 0);
    snprintf(line, sizeof(line), "%s%s: every bite in its slots %u-%u (%u outside)", rodName, lure ? " with a lure" : "",
             (unsigned)first, (unsigned)(first + count - 1), (unsigned)outside);
    Expect(line, outside == 0);
    for (u32 i = 0; i < count; i++)
    {
        snprintf(line, sizeof(line), "%s%s, slot %u", rodName, lure ? " with a lure" : "", (unsigned)(first + i));
        ExpectRate(line, hits[first + i], n, percent[i] / 100.0);
    }
}

int NativeSelfTest_Rods(void)
{
    static const double sOld[] = {70, 30}, sGood[] = {60, 20, 20}, sSuper[] = {40, 40, 15, 4, 1};
    // A lure swaps a fifth of the bites end for end: slot i becomes (first + last - i).
    static const double sOldLure[] = {70 * 0.8 + 30 * 0.2, 30 * 0.8 + 70 * 0.2};
    static const double sGoodLure[] = {60 * 0.8 + 20 * 0.2, 20, 20 * 0.8 + 60 * 0.2};
    static const double sSuperLure[] = {40 * 0.8 + 1 * 0.2, 40 * 0.8 + 4 * 0.2, 15, 4 * 0.8 + 40 * 0.2, 1 * 0.8 + 40 * 0.2};
    static const u32 sPlain[] = {25, 50, 75}, sBoosted[] = {50, 100, 100};
    u32 maps = 0, tables = 0, badSlots = 0;
    char line[200];
    bool8 egg = TRUE;

    Setup();
    UseMap(MAP_ROUTE119);
    printf("selftest-rods: the Old, Good and Super Rods: bites, slots, every map's fishing table\n");
    SetLead(SPECIES_ZIGZAGOON, 20, ABILITY_NONE, NATURE_HARDY, ITEM_NONE);
    ExpectBiteOdds("a lead without Suction Cups or Sticky Hold", sPlain);
    Expect("Lileep has Suction Cups", SetLead(SPECIES_LILEEP, 20, ABILITY_SUCTION_CUPS, NATURE_HARDY, ITEM_NONE));
    ExpectBiteOdds("a Lileep with Suction Cups in the lead", sBoosted);
    Expect("Gulpin has Sticky Hold", SetLead(SPECIES_GULPIN, 20, ABILITY_STICKY_HOLD, NATURE_HARDY, ITEM_NONE));
    ExpectBiteOdds("a Gulpin with Sticky Hold in the lead", sBoosted);
    SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_IS_EGG, &egg);
    ExpectBiteOdds("a Gulpin Egg in the lead", sPlain);

    ExpectRodSlots("Old Rod", OLD_ROD, 0, NUM_FISHING_MONS_OLD_ROD_ENCOUNTER_SLOTS, sOld, FALSE);
    ExpectRodSlots("Good Rod", GOOD_ROD, 2, NUM_FISHING_MONS_GOOD_ROD_ENCOUNTER_SLOTS, sGood, FALSE);
    ExpectRodSlots("Super Rod", SUPER_ROD, 5, NUM_FISHING_MONS_SUPER_ROD_ENCOUNTER_SLOTS, sSuper, FALSE);
    ExpectRodSlots("Old Rod", OLD_ROD, 0, NUM_FISHING_MONS_OLD_ROD_ENCOUNTER_SLOTS, sOldLure, TRUE);
    ExpectRodSlots("Good Rod", GOOD_ROD, 2, NUM_FISHING_MONS_GOOD_ROD_ENCOUNTER_SLOTS, sGoodLure, TRUE);
    ExpectRodSlots("Super Rod", SUPER_ROD, 5, NUM_FISHING_MONS_SUPER_ROD_ENCOUNTER_SLOTS, sSuperLure, TRUE);

    for (u32 i = 0; gWildMonHeaders[i].mapGroup != MAP_GROUP(MAP_UNDEFINED); i++)
    {
        bool32 fishable = FALSE;

        for (u32 time = 0; time < TIMES_OF_DAY_COUNT; time++)
        {
            const struct WildPokemonInfo *info = gWildMonHeaders[i].encounterTypes[time].fishingMonsInfo;

            if (info == NULL)
                continue;
            fishable = TRUE;
            tables++;
            for (u32 slot = 0; slot < NUM_FISHING_MONS_ENCOUNTER_SLOTS; slot++)
            {
                const struct WildPokemon *mon = &info->wildPokemon[slot];

                if (mon->species == SPECIES_NONE || mon->species >= NUM_SPECIES || mon->minLevel == 0 || mon->minLevel > mon->maxLevel)
                {
                    badSlots++;
                    printf("     map %u.%u, time %u, slot %u: species %u, levels %u-%u\n", gWildMonHeaders[i].mapGroup,
                           gWildMonHeaders[i].mapNum, (unsigned)time, (unsigned)slot, (unsigned)mon->species, mon->minLevel, mon->maxLevel);
                }
            }
        }
        if (fishable)
            maps++;
    }
    snprintf(line, sizeof(line), "every map's fishing table: %u maps, %u tables, every slot a Pokemon with its levels in order (%u bad)",
             (unsigned)maps, (unsigned)tables, (unsigned)badSlots);
    Expect(line, maps > 0 && badSlots == 0);

    printf("selftest-rods: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// --selftest-form-items (the full item audit, the Gen 3-6 key items that change a Pokemon's form from the Bag): the
// form each one gives, through TryFormChange(mon, FORM_CHANGE_ITEM_USE, ...) with the item in gSpecialVar_ItemId, the
// call the party menu makes (party_menu.c TryItemUseFormChange) before its animation: the Meteorite cycling Deoxys's four
// forms (ORAS), the Gracidea making Shaymin Sky by day and not at night or frozen, the Reveal Glass turning the forces of
// nature between their Incarnate and Therian forms both ways, the Prison Bottle unbinding Hoopa, and each item doing
// nothing for another species or an Egg. The DNA Splicers' fusion is a party-menu task of its own (two Pokemon and the
// PC's fusion storage) and is not part of it; the time of day is set with the overworld's own override (SetTimeOfDay).

static bool32 UseFormItem(struct Pokemon *mon, u16 item)
{
    bool32 changed;

    gSpecialVar_ItemId = item;
    changed = TryFormChange(mon, FORM_CHANGE_ITEM_USE, B_TRAINER_PLAYER);
    gSpecialVar_ItemId = ITEM_NONE;
    return changed;
}

static void ExpectForm(const char *what, struct Pokemon *mon, u16 item, bool32 wantChanged, enum Species want)
{
    char line[200];
    bool32 changed = UseFormItem(mon, item);
    enum Species species = GetMonData(mon, MON_DATA_SPECIES);

    snprintf(line, sizeof(line), "%s: %s, species %u (rule %u)", what, changed ? "changed" : "no effect",
             (unsigned)species, (unsigned)want);
    Expect(line, changed == wantChanged && species == want);
}

static struct Pokemon *FormMon(enum Species species)
{
    return BagMon(species, 50, MON_MALE, 70);
}

int NativeSelfTest_FormItems(void)
{
    static const struct { u16 incarnate, therian; const char *name; } sForces[] = {
        {SPECIES_TORNADUS_INCARNATE, SPECIES_TORNADUS_THERIAN, "Tornadus"},
        {SPECIES_THUNDURUS_INCARNATE, SPECIES_THUNDURUS_THERIAN, "Thundurus"},
        {SPECIES_LANDORUS_INCARNATE, SPECIES_LANDORUS_THERIAN, "Landorus"},
    };
    struct Pokemon *mon;
    struct Pokemon stored;
    char line[200];
    u32 attack, status = STATUS1_FREEZE;
    bool8 egg = TRUE;
    u32 i;

    Setup();
    UseMap(MAP_ROUTE101);
    printf("selftest-form-items: the key items that change a Pokemon's form, through TryFormChange\n");

    mon = FormMon(SPECIES_DEOXYS_NORMAL);
    attack = GetMonData(mon, MON_DATA_ATK);
    ExpectForm("Meteorite on Deoxys (Normal)", mon, ITEM_METEORITE, TRUE, SPECIES_DEOXYS_ATTACK);
    snprintf(line, sizeof(line), "its stats worked out again for the Attack form: Attack %u -> %u", (unsigned)attack,
             (unsigned)GetMonData(mon, MON_DATA_ATK));
    Expect(line, GetMonData(mon, MON_DATA_ATK) > attack);
    BoxMonToMon(&mon->box, &stored);
    Expect("the Attack form kept in its stored data (its box form, as saved)",
           GetMonData(&stored, MON_DATA_SPECIES) == SPECIES_DEOXYS_ATTACK);
    ExpectForm("again (Attack)", mon, ITEM_METEORITE, TRUE, SPECIES_DEOXYS_DEFENSE);
    ExpectForm("again (Defense)", mon, ITEM_METEORITE, TRUE, SPECIES_DEOXYS_SPEED);
    ExpectForm("again (Speed): back to Normal", mon, ITEM_METEORITE, TRUE, SPECIES_DEOXYS_NORMAL);

    SetTimeOfDay(12);
    mon = FormMon(SPECIES_SHAYMIN_LAND);
    ExpectForm("Gracidea on Shaymin at noon", mon, ITEM_GRACIDEA, TRUE, SPECIES_SHAYMIN_SKY);
    SetTimeOfDay(22);
    mon = FormMon(SPECIES_SHAYMIN_LAND);
    ExpectForm("Gracidea on Shaymin at 22:00 (night): no effect", mon, ITEM_GRACIDEA, FALSE, SPECIES_SHAYMIN_LAND);
    SetTimeOfDay(12);
    mon = FormMon(SPECIES_SHAYMIN_LAND);
    SetMonData(mon, MON_DATA_STATUS, &status);
    ExpectForm("Gracidea on a frozen Shaymin at noon: no effect", mon, ITEM_GRACIDEA, FALSE, SPECIES_SHAYMIN_LAND);
    SetTimeOfDay(0);

    for (i = 0; i < ARRAY_COUNT(sForces); i++)
    {
        char what[80];

        mon = FormMon(sForces[i].incarnate);
        snprintf(what, sizeof(what), "Reveal Glass on %s (Incarnate)", sForces[i].name);
        ExpectForm(what, mon, ITEM_REVEAL_GLASS, TRUE, sForces[i].therian);
        snprintf(what, sizeof(what), "Reveal Glass on %s (Therian): back", sForces[i].name);
        ExpectForm(what, mon, ITEM_REVEAL_GLASS, TRUE, sForces[i].incarnate);
    }

    mon = FormMon(SPECIES_HOOPA_CONFINED);
    ExpectForm("Prison Bottle on Hoopa (Confined)", mon, ITEM_PRISON_BOTTLE, TRUE, SPECIES_HOOPA_UNBOUND);
    // ORAS: the Prison Bottle also returns Hoopa Unbound to its Confined form; reported either way.
    {
        bool32 back = UseFormItem(mon, ITEM_PRISON_BOTTLE);

        printf("     Prison Bottle on Hoopa (Unbound): %s, species %u\n", back ? "changed" : "no effect",
               (unsigned)GetMonData(mon, MON_DATA_SPECIES));
    }

    mon = FormMon(SPECIES_PIKACHU);
    ExpectForm("Meteorite on Pikachu: no effect", mon, ITEM_METEORITE, FALSE, SPECIES_PIKACHU);
    ExpectForm("Gracidea on Pikachu: no effect", mon, ITEM_GRACIDEA, FALSE, SPECIES_PIKACHU);
    ExpectForm("Reveal Glass on Pikachu: no effect", mon, ITEM_REVEAL_GLASS, FALSE, SPECIES_PIKACHU);
    ExpectForm("Prison Bottle on Pikachu: no effect", mon, ITEM_PRISON_BOTTLE, FALSE, SPECIES_PIKACHU);
    ExpectForm("Reveal Glass on Shaymin: no effect", FormMon(SPECIES_SHAYMIN_LAND), ITEM_REVEAL_GLASS, FALSE,
               SPECIES_SHAYMIN_LAND);
    mon = FormMon(SPECIES_DEOXYS_NORMAL);
    SetMonData(mon, MON_DATA_IS_EGG, &egg);
    {
        bool32 changed = UseFormItem(mon, ITEM_METEORITE);

        snprintf(line, sizeof(line), "Meteorite on a Deoxys Egg: %s", changed ? "changed" : "no effect");
        Expect(line, !changed);
    }

    printf("selftest-form-items: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// --selftest-berry-water (the full item audit, the Wailmer Pail on a berry tree): a Pomeg Berry (Gen 3 yields, as this
// game is set to: 2 to 6) planted as the field plants one and grown stage by stage with the game's own BerryTreeGrow,
// watered with ObjectEventInteractionWaterBerryTree (what the tree's script and the Wailmer Pail from the Bag call, the
// tree reached through an object event as on the field) in none to all four of its watering stages, 500 trees each:
// the yields each number of watered stages gives (the minimum unwatered, a quarter of the range more for each stage),
// a second watering in one stage counting once, and a tree with its berries refusing water.

#define BERRY_WATER_TREE   0
#define BERRY_WATER_OBJECT 0
#define BERRY_WATER_TREES  500

static bool32 WaterTestTree(void)
{
    gObjectEvents[BERRY_WATER_OBJECT].trainerRange_berryTreeId = BERRY_WATER_TREE;
    gSelectedObjectEvent = BERRY_WATER_OBJECT;
    return ObjectEventInteractionWaterBerryTree();
}

// A Pomeg Berry planted and grown to its berries, watered in the first `watered` of its four watering stages (twice in
// each if `twice`): its yield (0 if it never reached its berries)
static u32 GrowPomeg(u32 watered, bool32 twice)
{
    struct BerryTree *tree = GetBerryTreeInfo(BERRY_WATER_TREE);
    u32 stage = 0;

    RemoveBerryTree(BERRY_WATER_TREE);
    PlantBerryTree(BERRY_WATER_TREE, ItemIdToBerryType(ITEM_POMEG_BERRY), BERRY_STAGE_PLANTED, TRUE);
    while (tree->stage != BERRY_STAGE_BERRIES)
    {
        if (stage++ < watered)
        {
            WaterTestTree();
            if (twice)
                WaterTestTree();
        }
        if (!BerryTreeGrow(tree) || stage > NUM_WATER_STAGES)
            break;
    }
    return tree->stage == BERRY_STAGE_BERRIES ? tree->berryYield : 0;
}

static void ExpectYields(const char *what, u32 watered, bool32 twice, u32 wantLow, u32 wantHigh)
{
    char line[200];
    u32 low = 255, high = 0, sum = 0;

    for (u32 i = 0; i < BERRY_WATER_TREES; i++)
    {
        u32 yield = GrowPomeg(watered, twice);

        low = min(low, yield);
        high = max(high, yield);
        sum += yield;
    }
    snprintf(line, sizeof(line), "%s: yields %u to %u, mean %.2f (rule %u to %u)", what, (unsigned)low, (unsigned)high,
             (double)sum / BERRY_WATER_TREES, (unsigned)wantLow, (unsigned)wantHigh);
    Expect(line, low == wantLow && high == wantHigh);
}

int NativeSelfTest_BerryWater(void)
{
    const struct BerryInfo *pomeg;
    struct BerryTree *tree;
    char line[200];
    u32 range;

    Setup();
    UseMap(MAP_ROUTE104);
    pomeg = GetBerryInfo(ItemIdToBerryType(ITEM_POMEG_BERRY));
    range = pomeg->maxYield - pomeg->minYield;
    printf("selftest-berry-water: a Pomeg Berry (yields %u to %u) grown %u times for each number of watered stages\n",
           (unsigned)pomeg->minYield, (unsigned)pomeg->maxYield, (unsigned)BERRY_WATER_TREES);
    snprintf(line, sizeof(line), "the Pomeg Berry's Gen 3 yields: %u to %u", (unsigned)pomeg->minYield,
             (unsigned)pomeg->maxYield);
    Expect(line, pomeg->minYield == 2 && pomeg->maxYield == 6 && range == NUM_WATER_STAGES);

    // Gen 3 (CalcBerryYieldInternal): w stages watered, the minimum plus a quarter of the range for each, rounded
    ExpectYields("never watered", 0, FALSE, 2, 2);
    ExpectYields("watered in 1 stage", 1, FALSE, 2, 3);
    ExpectYields("watered in 2 stages", 2, FALSE, 3, 4);
    ExpectYields("watered in 3 stages", 3, FALSE, 4, 5);
    ExpectYields("watered in all 4 stages", 4, FALSE, 5, 6);
    ExpectYields("watered twice in each of 2 stages (counted once each)", 2, TRUE, 3, 4);

    tree = GetBerryTreeInfo(BERRY_WATER_TREE);
    RemoveBerryTree(BERRY_WATER_TREE);
    PlantBerryTree(BERRY_WATER_TREE, ItemIdToBerryType(ITEM_POMEG_BERRY), BERRY_STAGE_PLANTED, TRUE);
    Expect("a planted Pomeg Berry takes water", WaterTestTree() && tree->watered == (1 << 0));
    while (tree->stage != BERRY_STAGE_BERRIES && BerryTreeGrow(tree))
        ;
    tree->watered = 0;
    Expect("a tree with its berries refuses water", !WaterTestTree() && tree->watered == 0);
    RemoveBerryTree(BERRY_WATER_TREE);
    Expect("soil with no berry planted refuses water", !WaterTestTree());

    printf("selftest-berry-water: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// --selftest-mail (the full item audit, the twelve Mail): what giving a Pokemon Mail and taking it back does to the
// save's mail slots, through the game's own functions (mail_data.c: GiveMailToMonByItemId, what the party menu's GIVE
// calls after the words are written; TakeMailFromMonAndSave, its TAKE when the Bag is full, the Mail kept in the PC):
// for each Mail, a party slot holding that Mail, the player's name and ID and the holder's species, the holder holding
// it; taken back, the same Mail in a PC slot and the holder empty-handed. The six party slots fill and a seventh is
// refused; a Potion is not Mail. The words' writing (the easy chat screen) is not part of it.

int NativeSelfTest_Mail(void)
{
    static const u16 sMail[] = {
        ITEM_ORANGE_MAIL, ITEM_HARBOR_MAIL, ITEM_GLITTER_MAIL, ITEM_MECH_MAIL, ITEM_WOOD_MAIL, ITEM_WAVE_MAIL,
        ITEM_BEAD_MAIL, ITEM_SHADOW_MAIL, ITEM_TROPIC_MAIL, ITEM_DREAM_MAIL, ITEM_FAB_MAIL, ITEM_RETRO_MAIL,
    };
    struct Pokemon *mon;
    char line[200];
    u32 i, slot, pcSlot;

    Setup();
    UseMap(MAP_ROUTE101);
    printf("selftest-mail: the twelve Mail given to a Pokemon and taken back, through mail_data.c\n");
    for (i = 0; i < ARRAY_COUNT(sMail); i++)
    {
        ClearAllMail();
        mon = BagMon(SPECIES_MUDKIP, 20, MON_MALE, 70);
        slot = GiveMailToMonByItemId(mon, sMail[i]);
        snprintf(line, sizeof(line), "Mail %u given: party slot %u holds it, the holder holds it, its writer and species set",
                 (unsigned)sMail[i], (unsigned)slot);
        Expect(line, ItemIsMail(sMail[i]) && slot < PARTY_SIZE && gSaveBlock1Ptr->mail[slot].itemId == sMail[i]
                     && GetMonData(mon, MON_DATA_HELD_ITEM) == sMail[i] && MonHasMail(mon)
                     && gSaveBlock1Ptr->mail[slot].trainerId[0] == gSaveBlock2Ptr->playerTrainerId[0]
                     && gSaveBlock1Ptr->mail[slot].species == SpeciesToMailSpecies(SPECIES_MUDKIP,
                                                                                    GetMonData(mon, MON_DATA_PERSONALITY))
                     && gSaveBlock1Ptr->mail[slot].words[0] == EC_EMPTY_WORD);
        pcSlot = TakeMailFromMonAndSave(mon);
        snprintf(line, sizeof(line), "  taken back into the PC: slot %u holds Mail %u, the party slot is free, the holder empty-handed",
                 (unsigned)pcSlot, (unsigned)(pcSlot < MAIL_COUNT ? gSaveBlock1Ptr->mail[pcSlot].itemId : 0));
        Expect(line, pcSlot >= PARTY_SIZE && pcSlot < MAIL_COUNT && gSaveBlock1Ptr->mail[pcSlot].itemId == sMail[i]
                     && gSaveBlock1Ptr->mail[slot].itemId == ITEM_NONE
                     && GetMonData(mon, MON_DATA_HELD_ITEM) == ITEM_NONE && !MonHasMail(mon));
    }

    ClearAllMail();
    for (i = 0; i < PARTY_SIZE; i++)
    {
        mon = BagMon(SPECIES_MUDKIP, 20, MON_MALE, 70);
        slot = GiveMailToMonByItemId(mon, ITEM_ORANGE_MAIL);
        if (slot != i)
            break;
    }
    snprintf(line, sizeof(line), "the six party mail slots fill in turn (%u filled)", (unsigned)i);
    Expect(line, i == PARTY_SIZE);
    mon = BagMon(SPECIES_MUDKIP, 20, MON_MALE, 70);
    Expect("a seventh Mail is refused while the six are taken", GiveMailToMonByItemId(mon, ITEM_ORANGE_MAIL) == MAIL_NONE
                                                                 && GetMonData(mon, MON_DATA_HELD_ITEM) == ITEM_NONE);
    Expect("a Potion is not Mail", !ItemIsMail(ITEM_POTION));
    ClearAllMail();

    printf("selftest-mail: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// --selftest-rare-goods (the full item audit, after the package: what the PC game's coin shop sells that no test checked,
// mp_rare_goods_prices.h): the Exp. Candies' experience and the Mochi's base points (PokemonUseItemEffects, what the
// party menu's USE calls), the Fresh-Start Mochi's reset (ExecuteTableBasedItemEffect, its ItemUseCB_ResetEVs), the
// Linking Cord's and the Black Augurite's evolutions (EVO_MODE_ITEM_USE), and the Rotom Catalog's and the Zygarde
// Cube's forms (TryFormChange in FORM_CHANGE_ITEM_USE_MULTICHOICE with the menu's choice in gSpecialVar_Result, as the
// party menu's catalog and cube menus call it; Rotom's move swap and the menus themselves are not part of it).

static void ExpectMultichoiceForm(const char *what, enum Species species, u16 item, u16 choice, bool32 wantChanged,
                                  enum Species want)
{
    struct Pokemon *mon = BagMon(species, 50, MON_MALE, 70);
    bool32 changed;
    char line[200];

    gSpecialVar_ItemId = item;
    gSpecialVar_Result = choice;
    changed = TryFormChange(mon, FORM_CHANGE_ITEM_USE_MULTICHOICE, B_TRAINER_PLAYER);
    gSpecialVar_ItemId = ITEM_NONE;
    snprintf(line, sizeof(line), "%s: %s, species %u (rule %u)", what, changed ? "changed" : "no change",
             (unsigned)GetMonData(mon, MON_DATA_SPECIES), (unsigned)want);
    Expect(line, changed == wantChanged && GetMonData(mon, MON_DATA_SPECIES) == want);
}

int NativeSelfTest_RareGoods(void)
{
    static const struct { u16 item; u32 exp; const char *name; } sCandies[] = {
        {ITEM_EXP_CANDY_XS, 100, "Exp. Candy XS"}, {ITEM_EXP_CANDY_S, 800, "Exp. Candy S"},
        {ITEM_EXP_CANDY_M, 3000, "Exp. Candy M"}, {ITEM_EXP_CANDY_L, 10000, "Exp. Candy L"},
        {ITEM_EXP_CANDY_XL, 30000, "Exp. Candy XL"},
    };
    static const struct { u16 item; u8 stat; const char *name; } sMochi[] = {
        {ITEM_HEALTH_MOCHI, STAT_HP, "Health Mochi"}, {ITEM_MUSCLE_MOCHI, STAT_ATK, "Muscle Mochi"},
        {ITEM_RESIST_MOCHI, STAT_DEF, "Resist Mochi"}, {ITEM_SWIFT_MOCHI, STAT_SPEED, "Swift Mochi"},
        {ITEM_GENIUS_MOCHI, STAT_SPATK, "Genius Mochi"}, {ITEM_CLEVER_MOCHI, STAT_SPDEF, "Clever Mochi"},
    };
    static const char *const sRotom[] = {"Rotom", "Heat", "Wash", "Frost", "Fan", "Mow"};
    static const u16 sRotomForms[] = {SPECIES_ROTOM, SPECIES_ROTOM_HEAT, SPECIES_ROTOM_WASH, SPECIES_ROTOM_FROST,
                                      SPECIES_ROTOM_FAN, SPECIES_ROTOM_MOW};
    struct Pokemon *mon;
    char line[200];
    u32 i, before, after, max;

    Setup();
    UseMap(MAP_ROUTE101);
    printf("selftest-rare-goods: the coin shop's Exp. Candies, Mochi, Linking Cord, Black Augurite, Rotom Catalog, Zygarde Cube\n");

    for (i = 0; i < ARRAY_COUNT(sCandies); i++)
    {
        bool8 noEffect;

        mon = BagMon(SPECIES_MUDKIP, 5, MON_MALE, 70);
        before = GetMonData(mon, MON_DATA_EXP);
        noEffect = PokemonUseItemEffects(mon, sCandies[i].item, 0, 0, FALSE);
        after = GetMonData(mon, MON_DATA_EXP);
        snprintf(line, sizeof(line), "%s on a level-5 Mudkip: experience %u -> %u (+%u, rule +%u), level %u",
                 sCandies[i].name, (unsigned)before, (unsigned)after, (unsigned)(after - before),
                 (unsigned)sCandies[i].exp, (unsigned)GetMonData(mon, MON_DATA_LEVEL));
        Expect(line, !noEffect && after - before == sCandies[i].exp
                     && GetMonData(mon, MON_DATA_LEVEL) == GetLevelFromMonExp(mon));
    }
    mon = BagMon(SPECIES_MUDKIP, MAX_LEVEL, MON_MALE, 70);
    max = GetMonData(mon, MON_DATA_EXP);
    Expect("Exp. Candy XL on a level-100 Pokemon: no effect, kept",
           PokemonUseItemEffects(mon, ITEM_EXP_CANDY_XL, 0, 0, FALSE) && GetMonData(mon, MON_DATA_EXP) == max);

    for (i = 0; i < ARRAY_COUNT(sMochi); i++)
        ExpectEVs(sMochi[i].name, sMochi[i].item, sMochi[i].stat, 0, 0, 10, TRUE);
    ExpectEVs("Health Mochi at 100 (a vitamin's limit): on to 110", ITEM_HEALTH_MOCHI, STAT_HP, 100, 0, 110, TRUE);
    ExpectEVs("Muscle Mochi at 245: to 252", ITEM_MUSCLE_MOCHI, STAT_ATK, 245, 0, 252, TRUE);
    ExpectEVs("Muscle Mochi at 252", ITEM_MUSCLE_MOCHI, STAT_ATK, 252, 0, 252, FALSE);
    ExpectEVs("Swift Mochi with 510 in the other stats", ITEM_SWIFT_MOCHI, STAT_SPEED, 0, 510, 0, FALSE);

    mon = BagMon(SPECIES_MUDKIP, 50, MON_MALE, 70);
    SetEVs(mon, STAT_HP, 100, 300);
    {
        bool8 noEffect = ExecuteTableBasedItemEffect(mon, ITEM_FRESH_START_MOCHI, 0, 0);
        u32 total = 0;

        for (i = 0; i < NUM_STATS; i++)
            total += GetMonData(mon, sEVData[i]);
        snprintf(line, sizeof(line), "Fresh-Start Mochi with 400 base points: %u left", (unsigned)total);
        Expect(line, !noEffect && total == 0);
        Expect("Fresh-Start Mochi with none: no effect, kept", ExecuteTableBasedItemEffect(mon, ITEM_FRESH_START_MOCHI, 0, 0));
    }

    // The Linking Cord stands in for a trade alone (the species' data): not for a trade with a held item
    ExpectStone("Linking Cord on Kadabra -> Alakazam", SPECIES_KADABRA, MON_MALE, ITEM_LINKING_CORD, ITEM_NONE, SPECIES_ALAKAZAM);
    ExpectStone("Linking Cord on Machoke -> Machamp", SPECIES_MACHOKE, MON_MALE, ITEM_LINKING_CORD, ITEM_NONE, SPECIES_MACHAMP);
    ExpectStone("Linking Cord on Graveler -> Golem", SPECIES_GRAVELER, MON_MALE, ITEM_LINKING_CORD, ITEM_NONE, SPECIES_GOLEM);
    ExpectStone("Linking Cord on Haunter -> Gengar", SPECIES_HAUNTER, MON_MALE, ITEM_LINKING_CORD, ITEM_NONE, SPECIES_GENGAR);
    ExpectStone("Linking Cord on Boldore -> Gigalith", SPECIES_BOLDORE, MON_MALE, ITEM_LINKING_CORD, ITEM_NONE, SPECIES_GIGALITH);
    ExpectStone("Linking Cord on Gurdurr -> Conkeldurr", SPECIES_GURDURR, MON_MALE, ITEM_LINKING_CORD, ITEM_NONE,
                SPECIES_CONKELDURR);
    ExpectStone("Linking Cord on Phantump -> Trevenant", SPECIES_PHANTUMP, MON_MALE, ITEM_LINKING_CORD, ITEM_NONE,
                SPECIES_TREVENANT);
    ExpectStone("Linking Cord on Onix holding a Metal Coat: no evolution (a held item's trade stays a trade)", SPECIES_ONIX,
                MON_MALE, ITEM_LINKING_CORD, ITEM_METAL_COAT, SPECIES_NONE);
    ExpectStone("Linking Cord on Onix holding nothing: no evolution", SPECIES_ONIX, MON_MALE, ITEM_LINKING_CORD, ITEM_NONE,
                SPECIES_NONE);
    ExpectStone("Linking Cord on Pikachu: no evolution", SPECIES_PIKACHU, MON_MALE, ITEM_LINKING_CORD, ITEM_NONE, SPECIES_NONE);
    ExpectStone("Black Augurite on Scyther -> Kleavor", SPECIES_SCYTHER, MON_MALE, ITEM_BLACK_AUGURITE, ITEM_NONE,
                SPECIES_KLEAVOR);
    ExpectStone("Black Augurite on Pikachu: no evolution", SPECIES_PIKACHU, MON_MALE, ITEM_BLACK_AUGURITE, ITEM_NONE,
                SPECIES_NONE);

    for (i = 1; i < ARRAY_COUNT(sRotomForms); i++)
    {
        char what[80];

        snprintf(what, sizeof(what), "Rotom Catalog on Rotom, choice %u (%s)", (unsigned)i, sRotom[i]);
        ExpectMultichoiceForm(what, SPECIES_ROTOM, ITEM_ROTOM_CATALOG, i, TRUE, sRotomForms[i]);
    }
    ExpectMultichoiceForm("Rotom Catalog on Rotom Heat, choice 0 (back to Rotom)", SPECIES_ROTOM_HEAT, ITEM_ROTOM_CATALOG, 0,
                          TRUE, SPECIES_ROTOM);
    ExpectMultichoiceForm("Rotom Catalog on Pikachu: no change", SPECIES_PIKACHU, ITEM_ROTOM_CATALOG, 1, FALSE, SPECIES_PIKACHU);
    ExpectMultichoiceForm("Zygarde Cube on Zygarde 50% (Aura Break), choice 0 -> 10% (Aura Break)", SPECIES_ZYGARDE_50,
                          ITEM_ZYGARDE_CUBE, 0, TRUE, SPECIES_ZYGARDE_10_AURA_BREAK);
    ExpectMultichoiceForm("Zygarde Cube on Zygarde 50% (Aura Break), choice 1 -> 50% (Power Construct)", SPECIES_ZYGARDE_50,
                          ITEM_ZYGARDE_CUBE, 1, TRUE, SPECIES_ZYGARDE_50_POWER_CONSTRUCT);
    ExpectMultichoiceForm("Zygarde Cube on Pikachu: no change", SPECIES_PIKACHU, ITEM_ZYGARDE_CUBE, 0, FALSE, SPECIES_PIKACHU);

    // The Nectars, not sold but held by a wild Oricorio: its style, through TryFormChange as for a form item
    {
        static const struct { u16 from; u16 item; u16 want; bool8 changed; const char *what; } sNectars[] = {
            {SPECIES_ORICORIO_BAILE, ITEM_YELLOW_NECTAR, SPECIES_ORICORIO_POM_POM, TRUE, "Yellow Nectar on Oricorio (Baile) -> Pom-Pom"},
            {SPECIES_ORICORIO_BAILE, ITEM_PINK_NECTAR, SPECIES_ORICORIO_PAU, TRUE, "Pink Nectar on Oricorio (Baile) -> Pa'u"},
            {SPECIES_ORICORIO_BAILE, ITEM_PURPLE_NECTAR, SPECIES_ORICORIO_SENSU, TRUE, "Purple Nectar on Oricorio (Baile) -> Sensu"},
            {SPECIES_ORICORIO_SENSU, ITEM_RED_NECTAR, SPECIES_ORICORIO_BAILE, TRUE, "Red Nectar on Oricorio (Sensu) -> Baile"},
            {SPECIES_ORICORIO_BAILE, ITEM_RED_NECTAR, SPECIES_ORICORIO_BAILE, FALSE, "Red Nectar on Oricorio (Baile): no change"},
            {SPECIES_PIKACHU, ITEM_RED_NECTAR, SPECIES_PIKACHU, FALSE, "Red Nectar on Pikachu: no change"},
        };

        for (i = 0; i < ARRAY_COUNT(sNectars); i++)
        {
            bool32 changed;

            mon = BagMon(sNectars[i].from, 50, MON_MALE, 70);
            gSpecialVar_ItemId = sNectars[i].item;
            changed = TryFormChange(mon, FORM_CHANGE_ITEM_USE, B_TRAINER_PLAYER);
            gSpecialVar_ItemId = ITEM_NONE;
            snprintf(line, sizeof(line), "%s: %s, species %u (rule %u)", sNectars[i].what, changed ? "changed" : "no change",
                     (unsigned)GetMonData(mon, MON_DATA_SPECIES), (unsigned)sNectars[i].want);
            Expect(line, changed == sNectars[i].changed && GetMonData(mon, MON_DATA_SPECIES) == sNectars[i].want);
        }
    }

    printf("selftest-rare-goods: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}
