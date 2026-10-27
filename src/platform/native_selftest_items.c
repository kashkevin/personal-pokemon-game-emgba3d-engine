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
#include "daycare.h"
#include "event_data.h"
#include "item.h"
#include "load_save.h"
#include "malloc.h"
#include "mp_wild_random.h"
#include "overworld.h"
#include "pokedex.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "random.h"
#include "wild_encounter.h"
#include "constants/abilities.h"
#include "constants/flags.h"
#include "constants/items.h"
#include "constants/layouts.h"
#include "constants/maps.h"
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

// The odds the rule gives: the game's integer steps with this multiplier / divider and flat bonus (catch rate at least 1)
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
    return odds == 0 ? 1 : odds;    // at least 1 (Gen 4-6; 0 would divide by zero in the shake check)
}

static void ExpectBall(const char *what, u32 ball, u32 multiplier, u32 divider, s32 flatBonus)
{
    u32 got = MPTest_CaptureOdds(ball, WILD, PLAYER), want = RuleOdds(multiplier, divider, flatBonus);
    char line[200];

    snprintf(line, sizeof(line), "%s: odds %u (rule %u: x%u/%u%s)", what, got, want, multiplier, divider,
             flatBonus ? ", catch rate changed" : "");
    Expect(line, got == want);
}

// The chance a throw catches (no critical capture): 1 at odds 255 or more, else three shakes each holding odds'/65536
static double CatchChance(u32 odds)
{
    double shake;

    if (odds == SURE_CATCH || odds > 254)
        return 1.0;
    if (odds == 0)
        return 0.0;
    shake = MPTest_BallShakeOdds(odds) / 65536.0;
    return shake * shake * shake;
}

int NativeSelfTest_Balls(void)
{
    static const struct { u16 ball; const char *name; } sPlainBalls[] = {
        {ITEM_POKE_BALL, "Poke Ball"}, {ITEM_PREMIER_BALL, "Premier Ball"}, {ITEM_LUXURY_BALL, "Luxury Ball"},
        {ITEM_HEAL_BALL, "Heal Ball"}, {ITEM_FRIEND_BALL, "Friend Ball"}, {ITEM_CHERISH_BALL, "Cherish Ball"},
        {ITEM_PARK_BALL, "Park Ball (outside the Pal Park, no effect)"}, {ITEM_DREAM_BALL, "Dream Ball (awake)"},
    };
    u32 i;

    Setup();
    printf("selftest-balls: every Poke Ball's capture odds, Gen 6 rules\n");
    UseMap(MAP_ROUTE101);

    // A catch rate 45 Pokemon at full HP (Bulbasaur), the player's level 50 Treecko
    SetBattle(SPECIES_BULBASAUR, 20, MON_MALE, SPECIES_TREECKO, 50, MON_MALE);
    for (i = 0; i < ARRAY_COUNT(sPlainBalls); i++)
        ExpectBall(sPlainBalls[i].name, sPlainBalls[i].ball, 100, 100, 0);
    ExpectBall("Great Ball x1.5", ITEM_GREAT_BALL, 150, 100, 0);
    ExpectBall("Ultra Ball x2", ITEM_ULTRA_BALL, 200, 100, 0);
    ExpectBall("Safari Ball x1.5 (Gen 2-7)", ITEM_SAFARI_BALL, 150, 100, 0);
    ExpectBall("Sport Ball x1.5 (Gen 2-7)", ITEM_SPORT_BALL, 150, 100, 0);
    Expect("Master Ball: a sure catch", MPTest_CaptureOdds(ITEM_MASTER_BALL, WILD, PLAYER) == SURE_CATCH);
    gBattleMons[WILD].status1 = STATUS1_SLEEP_TURN(3);
    ExpectBall("Dream Ball on a sleeping Pokemon: only the sleep bonus (x4 is Gen 8)", ITEM_DREAM_BALL, 100, 100, 0);
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
    // a light Pokemon whose catch rate is under 21: the rule's minimum of 1, never a sure catch
    SetBattle(SPECIES_LATIAS, 40, MON_FEMALE, SPECIES_TREECKO, 50, MON_MALE);
    ExpectBall("Heavy Ball, Latias 40 kg (catch rate 3): catch rate 1, not a sure catch", ITEM_HEAVY_BALL, 100, 100, -20);
    SetBattle(SPECIES_JIRACHI, 40, MON_GENDERLESS, SPECIES_TREECKO, 50, MON_MALE);
    ExpectBall("Heavy Ball, Jirachi 1.1 kg (catch rate 3): catch rate 1", ITEM_HEAVY_BALL, 100, 100, -20);
    SetBattle(SPECIES_UXIE, 40, MON_GENDERLESS, SPECIES_TREECKO, 50, MON_MALE);
    ExpectBall("Heavy Ball, Uxie 0.3 kg (catch rate 3): catch rate 1", ITEM_HEAVY_BALL, 100, 100, -20);
    gBattleMons[WILD].hp = 1;
    ExpectBall("Heavy Ball, Uxie at 1 HP: catch rate 1, odds still small", ITEM_HEAVY_BALL, 100, 100, -20);
    gBattleMons[WILD].hp = gBattleMons[WILD].maxHP;
    // the Beast Ball (Gen 7) on a Pokemon that is not an Ultra Beast: x410/4096, and odds of at least 1 (a catch rate 3
    // Pokemon at full HP made 0, and the shake check divided by it)
    SetBattle(SPECIES_LATIAS, 40, MON_FEMALE, SPECIES_TREECKO, 50, MON_MALE);
    ExpectBall("Beast Ball on Latias (not an Ultra Beast): x0.1, odds at least 1", ITEM_BEAST_BALL, 410, 4096, 0);
    Expect("a throw at the lowest odds (1) shakes with a chance above 0 (no division by zero)",
           MPTest_BallShakeOdds(1) > 0 && CatchChance(1) > 0.0 && CatchChance(1) < 0.02);

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
