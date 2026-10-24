// Native PC build: Update 29 self-tests (wild encounters, Legendary/Mythical rolls, shiny rolls, coins) and the species
// data fingerprint. Project-owned. Everything here runs through the game's own code before the game starts and exits.
//
//   --dump-species-data FILE   one line per species: "id name fnv64", a hash of every field the update must not change
//                              (base stats, types, catch rate, experience, EVs, abilities, gender ratio, egg data,
//                              friendship, growth rate, flags, level-up / teachable / egg move lists, evolutions with
//                              their conditions). tools/pc_port/species_data_check.py compares it with the committed
//                              baseline: any difference fails the check.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "global.h"
#include "pokemon.h"
#include "constants/species.h"
#include "constants/moves.h"

static u64 Fnv(u64 hash, u32 value, u32 bytes)
{
    for (u32 i = 0; i < bytes; i++)
    {
        hash ^= (value >> (8 * i)) & 0xFF;
        hash *= 0x100000001B3ULL;
    }
    return hash;
}

static u64 SpeciesHash(enum Species species)
{
    const struct SpeciesInfo *info = &gSpeciesInfo[species];
    u64 h = 0xCBF29CE484222325ULL;
    const struct LevelUpMove *levelUp = GetSpeciesLevelUpLearnset(species);
    const u16 *list;
    const struct Evolution *evolutions = GetSpeciesEvolutions(species);

    h = Fnv(h, info->baseHP, 1); h = Fnv(h, info->baseAttack, 1); h = Fnv(h, info->baseDefense, 1);
    h = Fnv(h, info->baseSpeed, 1); h = Fnv(h, info->baseSpAttack, 1); h = Fnv(h, info->baseSpDefense, 1);
    h = Fnv(h, info->types[0], 1); h = Fnv(h, info->types[1], 1);
    h = Fnv(h, info->catchRate, 1); h = Fnv(h, info->expYield, 2);
    h = Fnv(h, info->evYield_HP | info->evYield_Attack << 2 | info->evYield_Defense << 4 | info->evYield_Speed << 6
              | info->evYield_SpAttack << 8 | info->evYield_SpDefense << 10, 2);
    h = Fnv(h, info->itemCommon, 2); h = Fnv(h, info->itemRare, 2);
    h = Fnv(h, info->genderRatio, 1); h = Fnv(h, info->eggCycles, 1); h = Fnv(h, info->friendship, 1);
    h = Fnv(h, info->growthRate, 1); h = Fnv(h, info->eggGroups[0], 1); h = Fnv(h, info->eggGroups[1], 1);
    for (u32 i = 0; i < NUM_ABILITY_SLOTS; i++)
        h = Fnv(h, info->abilities[i], 2);
    h = Fnv(h, info->safariZoneFleeRate, 1);
    h = Fnv(h, info->natDexNum, 2);
    h = Fnv(h, info->isRestrictedLegendary | info->isSubLegendary << 1 | info->isMythical << 2 | info->isUltraBeast << 3
              | info->isParadox << 4 | info->isMegaEvolution << 6 | info->isPrimalReversion << 7 | info->cannotBeTraded << 15
              | info->perfectIVCount << 16, 4);
    if (levelUp != NULL)
        for (u32 i = 0; i < 400 && levelUp[i].move != LEVEL_UP_MOVE_END; i++)
            h = Fnv(Fnv(h, levelUp[i].move, 2), levelUp[i].level, 2);
    h = Fnv(h, 0xFFFF, 2);
    list = GetSpeciesTeachableLearnset(species);
    if (list != NULL)
        for (u32 i = 0; i < 1000 && list[i] != MOVE_UNAVAILABLE; i++)
            h = Fnv(h, list[i], 2);
    h = Fnv(h, 0xFFFF, 2);
    list = GetSpeciesEggMoves(species);
    if (list != NULL)
        for (u32 i = 0; i < 100 && list[i] != MOVE_UNAVAILABLE; i++)
            h = Fnv(h, list[i], 2);
    h = Fnv(h, 0xFFFF, 2);
    if (evolutions != NULL)
        for (u32 i = 0; i < 20 && evolutions[i].method != EVOLUTIONS_END; i++)
        {
            h = Fnv(h, evolutions[i].method, 2); h = Fnv(h, evolutions[i].param, 2); h = Fnv(h, evolutions[i].targetSpecies, 2);
            if (evolutions[i].params != NULL)
                for (u32 j = 0; j < 12 && evolutions[i].params[j].condition != CONDITIONS_END; j++)
                {
                    h = Fnv(h, evolutions[i].params[j].condition, 2); h = Fnv(h, evolutions[i].params[j].arg1, 2);
                    h = Fnv(h, evolutions[i].params[j].arg2, 2); h = Fnv(h, evolutions[i].params[j].arg3, 2);
                }
        }
    return h;
}

int NativeSelfTest_DumpSpeciesData(const char *outPath)
{
    FILE *out = fopen(outPath, "w");

    if (out == NULL)
    {
        fprintf(stderr, "dump-species-data: cannot open %s\n", outPath);
        return 2;
    }
    for (u32 species = 1; species < NUM_SPECIES; species++)
        if (IsSpeciesEnabled(species))
            fprintf(out, "%u %016llx\n", species, (unsigned long long)SpeciesHash(species)), fflush(out);
    fclose(out);
    printf("dump-species-data: written\n");
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// --selftest-legend (Update 29): the random Legendary / Mythical wild encounters, through the game's own code.
//   1. the pool is exactly the game's Legendary and Mythical species of the Gen 1-6 Pokedex (54): none missing, none extra,
//      no duplicate, the Mythicals included; each has a habitat and every habitat tag is used;
//   2. the roll is exact (boundary values of 2^32 / 6000 and 2^32 / 4000) and converges (60 million seeded rolls each) to a
//      combined 1/6,000 outside the Safari Zone and 1/4,000 inside it, the same total whatever the pool size;
//   3. habitats: examples from the brief (Kyogre only in the sea, Groudon on land and in caves, Rayquaza outdoors, Mewtwo in
//      caves and strange places, Articuno cold, Zapdos stormy, Moltres volcanic), the night-only species, and that every species
//      is eligible in at least one real encounter area of the game (no species unobtainable), and no real area has an empty pool;
//   4. the Safari Zone pool is all 54 species whatever the habitat or hour (one shared pool, no double counting);
//   5. the whole path: TryGenerateWildMon (the game's own encounter generator) then the roll of BattleSetup_StartWildBattle,
//      millions of times on Route 101, Route 105 (surfing) and a Safari Zone map: the level is always one the map's table
//      allows (a Legendary at level 2 or 3 on Route 101), the share is 1/6,000 and 1/4,000, every Safari species shows up, ordinary
//      Pokemon stay broadly available, a stale or foreign Pokemon never rolls, the shiny status and level survive the swap.
// ---------------------------------------------------------------------------------------------------------------------
#include "platform/mp_legend.h"
#include "battle.h"
#include "event_data.h"
#include "load_save.h"
#include "mp_wild_random.h"
#include "overworld.h"
#include "random.h"
#include "wild_encounter.h"
#include "safari_zone.h"
#include "malloc.h"
#include "constants/flags.h"
#include "constants/map_types.h"
#include "constants/maps.h"
#include "constants/region_map_sections.h"
#include "constants/weather.h"
#include "field_weather.h"

extern u32 MPWild_TestFamilyCount(void);
extern const struct MPWildFamily *MPWild_TestFamily(u32 index);
extern const struct MPWildMember *MPWild_TestMembers(void);
extern u32 MPWild_TestFamilyWeight(u32 index, u32 habitat, bool32 night, u8 weather);

#define TAG_FIELD    MP_LEGEND_FIELD
#define MP_LEGEND_SPECIES_COUNT_MAX 64

static int sLegChecks, sLegFailures;

static void ExpectLeg(const char *what, bool32 ok)
{
    sLegChecks++;
    if (!ok)
        sLegFailures++;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static u64 sSplit;
static u32 SplitMix32(void)
{
    u64 z = (sSplit += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return (u32)(z ^ (z >> 31));
}

static bool32 IsGameLegendary(u32 species)
{
    return gSpeciesInfo[species].isRestrictedLegendary || gSpeciesInfo[species].isSubLegendary || gSpeciesInfo[species].isMythical;
}

static bool32 InPool(u16 species)
{
    for (u32 i = 0; i < MPLegend_SpeciesCount(); i++)
        if (MPLegend_SpeciesAt(i)->species == species)
            return TRUE;
    return FALSE;
}

static void UseMapHeader(u16 map)
{
    gMapHeader = *Overworld_GetMapHeaderByGroupAndId(MAP_GROUP(map), MAP_NUM(map));
    gSaveBlock1Ptr->location.mapGroup = MAP_GROUP(map);
    gSaveBlock1Ptr->location.mapNum = MAP_NUM(map);
}

static const struct WildPokemonInfo *InfoFor(u16 map, enum WildPokemonArea area)
{
    for (u32 i = 0; gWildMonHeaders[i].mapGroup != MAP_GROUP(MAP_UNDEFINED); i++)
    {
        if (gWildMonHeaders[i].mapGroup == MAP_GROUP(map) && gWildMonHeaders[i].mapNum == MAP_NUM(map))
        {
            const struct WildEncounterTypes *types = &gWildMonHeaders[i].encounterTypes[TIME_MORNING];

            switch (area)
            {
            case WILD_AREA_LAND: return types->landMonsInfo;
            case WILD_AREA_WATER: return types->waterMonsInfo;
            case WILD_AREA_ROCKS: return types->rockSmashMonsInfo;
            default: return types->fishingMonsInfo;
            }
        }
    }
    return NULL;
}

static bool32 LevelInTable(const struct WildPokemonInfo *info, u32 count, u32 level)
{
    for (u32 i = 0; i < count; i++)
    {
        u32 lo = info->wildPokemon[i].minLevel, hi = info->wildPokemon[i].maxLevel;

        if (lo > hi)
        {
            u32 t = lo; lo = hi; hi = t;
        }
        if (level >= lo && level <= hi)
            return TRUE;
    }
    return FALSE;
}

static u32 MinLevel(const struct WildPokemonInfo *info, u32 count)
{
    u32 low = 255;

    for (u32 i = 0; i < count; i++)
        if (info->wildPokemon[i].minLevel < low)
            low = info->wildPokemon[i].minLevel;
    return low;
}

static u32 MaxLevel(const struct WildPokemonInfo *info, u32 count)
{
    u32 high = 0;

    for (u32 i = 0; i < count; i++)
        if (info->wildPokemon[i].maxLevel > high)
            high = info->wildPokemon[i].maxLevel;
    return high;
}

static bool32 InArea(u16 species, enum WildPokemonArea area, u8 mapsec, u8 mapType, u8 weather, bool32 night)
{
    u32 habitats = MPLegend_AreaHabitats(area, mapsec, mapType, weather);

    for (u32 i = 0, n = MPLegend_PoolSize(habitats, night, FALSE); i < n; i++)
        if (MPLegend_PoolSpecies(habitats, night, FALSE, i) == species)
            return TRUE;
    return FALSE;
}

static u32 CountSpeciesFlagged(void)
{
    u32 count = 0;

    for (u32 species = 1; species <= 721; species++)
        if (IsGameLegendary(species))
            count++;
    return count;
}

int NativeSelfTest_Legend(void)
{
    u32 i, j;
    u32 mythicalInGame = 0, mythicalInPool = 0;
    static const u8 sWeathers[] = {WEATHER_NONE, WEATHER_SUNNY, WEATHER_RAIN, WEATHER_DOWNPOUR, WEATHER_RAIN_THUNDERSTORM, WEATHER_SNOW,
                                   WEATHER_SANDSTORM, WEATHER_DROUGHT, WEATHER_FOG, WEATHER_FOG_HORIZONTAL, WEATHER_FOG_DIAGONAL};
    u32 timesObtainable[MP_LEGEND_SPECIES_COUNT_MAX];

    InitHeap(gHeap, HEAP_SIZE);
    SetSaveBlocksPointers(0);
    memset(gSaveBlock1Ptr, 0, sizeof(*gSaveBlock1Ptr));
    memset(gSaveBlock2Ptr, 0, sizeof(*gSaveBlock2Ptr));
    ZeroPlayerPartyMons();
    ZeroEnemyPartyMons();
    CreateMon(&gParties[B_TRAINER_PLAYER][0], SPECIES_TREECKO, 5, 0x12345678, OTID_STRUCT_PLAYER_ID);
    gPlayerPartyCount = 1;
    printf("selftest-legend: Update 29 random Legendary and Mythical encounters\n");

    // 1. the pool is the game's classification
    {
        u32 expected = CountSpeciesFlagged(), found = 0;
        bool32 allOk = TRUE, unique = TRUE, hasHabitat = TRUE;
        u32 used = 0;

        for (u32 species = 1; species <= 721; species++)
        {
            if (IsGameLegendary(species))
            {
                if (!InPool(species))
                    allOk = FALSE;
                if (gSpeciesInfo[species].isMythical)
                    mythicalInGame++;
            }
        }
        for (i = 0; i < MPLegend_SpeciesCount(); i++)
        {
            const struct MPLegendSpecies *entry = MPLegend_SpeciesAt(i);

            found++;
            if (entry->species == 0 || entry->species > 721 || !IsGameLegendary(entry->species))
                allOk = FALSE;
            if (gSpeciesInfo[entry->species].isMythical)
                mythicalInPool++;
            if (gSpeciesInfo[entry->species].isUltraBeast || gSpeciesInfo[entry->species].isParadox || gSpeciesInfo[entry->species].isMegaEvolution
             || gSpeciesInfo[entry->species].isPrimalReversion)
                allOk = FALSE;
            for (j = i + 1; j < MPLegend_SpeciesCount(); j++)
                if (MPLegend_SpeciesAt(j)->species == entry->species)
                    unique = FALSE;
            if (entry->habitats == 0)
                hasHabitat = FALSE;
            used |= entry->habitats;
        }
        printf("pool: %u species (game classification says %u), Mythicals %u of %u\n", found, expected, mythicalInPool, mythicalInGame);
        ExpectLeg("the pool is exactly the game's Legendary and Mythical species of the Gen 1-6 Pokedex", allOk && found == expected);
        ExpectLeg("no species twice", unique);
        ExpectLeg("every species has a habitat", hasHabitat);
        ExpectLeg("Mythical species are in the pool (all of the game's)", mythicalInPool == mythicalInGame && mythicalInPool > 0);
        ExpectLeg("every habitat tag is used by some species", used == (MP_LEGEND_MYSTIC << 1) - 1);
        ExpectLeg("the examples of the brief are all in the pool",
                  InPool(SPECIES_RAYQUAZA) && InPool(SPECIES_GROUDON) && InPool(SPECIES_KYOGRE) && InPool(SPECIES_MEWTWO) && InPool(SPECIES_MEW)
                  && InPool(SPECIES_CELEBI) && InPool(SPECIES_JIRACHI) && InPool(SPECIES_DEOXYS) && InPool(SPECIES_DARKRAI)
                  && InPool(SPECIES_ARCEUS) && InPool(SPECIES_VICTINI) && InPool(SPECIES_ARTICUNO) && InPool(SPECIES_ZAPDOS)
                  && InPool(SPECIES_MOLTRES));
        ExpectLeg("ordinary Pokemon (a starter, Pikachu, Magikarp, Eevee) are not in the pool",
                  !InPool(SPECIES_TREECKO) && !InPool(SPECIES_PIKACHU) && !InPool(SPECIES_MAGIKARP) && !InPool(SPECIES_EEVEE));
    }

    // 2. the roll is exact and converges
    {
        for (u32 pass = 0; pass < 2; pass++)
        {
            bool32 safari = pass == 1;
            u32 oneIn = safari ? MP_LEGEND_ONE_IN_SAFARI : MP_LEGEND_ONE_IN_REGULAR;
            u64 limit = (0x100000000ULL + oneIn - 1) / oneIn;     // the smallest value that misses: ceil(2^32 / oneIn)

            ExpectLeg(safari ? "Safari 1/4,000: 0 and ceil(2^32/4000) - 1 hit, ceil(2^32/4000) and 2^32 - 1 miss"
                             : "regular 1/6,000: 0 and ceil(2^32/6000) - 1 hit, ceil(2^32/6000) and 2^32 - 1 miss",
                      MPLegend_RollHits(0, safari) && MPLegend_RollHits((u32)(limit - 1), safari) && !MPLegend_RollHits((u32)limit, safari)
                      && !MPLegend_RollHits(0xFFFFFFFFu, safari));
        }
        for (u32 pass = 0; pass < 2; pass++)
        {
            bool32 safari = pass == 1;
            u32 oneIn = safari ? MP_LEGEND_ONE_IN_SAFARI : MP_LEGEND_ONE_IN_REGULAR;
            u32 n = 60000000, hits = 0;
            double expected = (double)n / oneIn, sigma, diff;

            sSplit = 0x1234567ULL + pass;
            for (i = 0; i < n; i++)
                if (MPLegend_RollHits(SplitMix32(), safari))
                    hits++;
            sigma = __builtin_sqrt(expected * (1.0 - 1.0 / oneIn));
            diff = (double)hits - expected;
            printf("%s: %u rolls, %u hits, expected %.1f (sigma %.1f), observed 1 in %.0f\n", safari ? "Safari" : "regular", n, hits, expected, sigma,
                   hits ? (double)n / hits : 0.0);
            ExpectLeg(safari ? "60M Safari rolls converge to 1/4,000 (within 4 sigma)" : "60M regular rolls converge to 1/6,000 (within 4 sigma)",
                      diff > -4 * sigma && diff < 4 * sigma);
        }
    }

    // 3. habitats
    {
        u8 secForest = MAPSEC_ROUTE_101;
        u32 landField = MPLegend_AreaHabitats(WILD_AREA_LAND, MAPSEC_ROUTE_101, MAP_TYPE_ROUTE, WEATHER_SUNNY);
        u32 sea = MPLegend_AreaHabitats(WILD_AREA_WATER, MAPSEC_ROUTE_105, MAP_TYPE_ROUTE, WEATHER_SUNNY);
        u32 fish = MPLegend_AreaHabitats(WILD_AREA_FISHING, MAPSEC_ROUTE_105, MAP_TYPE_ROUTE, WEATHER_SUNNY);
        u32 chimney = MPLegend_AreaHabitats(WILD_AREA_LAND, MAPSEC_MT_CHIMNEY, MAP_TYPE_ROUTE, WEATHER_SUNNY);
        u32 fiery = MPLegend_AreaHabitats(WILD_AREA_LAND, MAPSEC_FIERY_PATH, MAP_TYPE_UNDERGROUND, WEATHER_SUNNY);
        u32 shoal = MPLegend_AreaHabitats(WILD_AREA_LAND, MAPSEC_SHOAL_CAVE, MAP_TYPE_UNDERGROUND, WEATHER_SUNNY);
        u32 granite = MPLegend_AreaHabitats(WILD_AREA_LAND, MAPSEC_GRANITE_CAVE, MAP_TYPE_UNDERGROUND, WEATHER_SUNNY);
        u32 r110 = MPLegend_AreaHabitats(WILD_AREA_LAND, MAPSEC_ROUTE_110, MAP_TYPE_ROUTE, WEATHER_SUNNY);
        u32 newMauville = MPLegend_AreaHabitats(WILD_AREA_LAND, MAPSEC_NEW_MAUVILLE, MAP_TYPE_UNDERGROUND, WEATHER_SUNNY);
        u32 sky = MPLegend_AreaHabitats(WILD_AREA_LAND, MAPSEC_SKY_PILLAR, MAP_TYPE_ROUTE, WEATHER_SUNNY);

        (void)secForest;
        ExpectLeg("Kyogre: surfing and fishing on a sea route, never on Route 101's grass",
                  InArea(SPECIES_KYOGRE, WILD_AREA_WATER, MAPSEC_ROUTE_105, MAP_TYPE_ROUTE, WEATHER_SUNNY, FALSE)
                  && InArea(SPECIES_KYOGRE, WILD_AREA_FISHING, MAPSEC_ROUTE_105, MAP_TYPE_ROUTE, WEATHER_SUNNY, FALSE)
                  && !InArea(SPECIES_KYOGRE, WILD_AREA_LAND, MAPSEC_ROUTE_101, MAP_TYPE_ROUTE, WEATHER_SUNNY, FALSE));
        ExpectLeg("Groudon: land and caves (Mt. Chimney, Granite Cave), not at sea",
                  InArea(SPECIES_GROUDON, WILD_AREA_LAND, MAPSEC_MT_CHIMNEY, MAP_TYPE_ROUTE, WEATHER_SUNNY, FALSE)
                  && InArea(SPECIES_GROUDON, WILD_AREA_LAND, MAPSEC_GRANITE_CAVE, MAP_TYPE_UNDERGROUND, WEATHER_SUNNY, FALSE)
                  && !InArea(SPECIES_GROUDON, WILD_AREA_WATER, MAPSEC_ROUTE_105, MAP_TYPE_ROUTE, WEATHER_SUNNY, FALSE));
        ExpectLeg("Rayquaza: outdoors (Route 101) and at the Sky Pillar, not in an ordinary cave",
                  InArea(SPECIES_RAYQUAZA, WILD_AREA_LAND, MAPSEC_ROUTE_101, MAP_TYPE_ROUTE, WEATHER_SUNNY, FALSE)
                  && InArea(SPECIES_RAYQUAZA, WILD_AREA_LAND, MAPSEC_SKY_PILLAR, MAP_TYPE_ROUTE, WEATHER_SUNNY, FALSE)
                  && !InArea(SPECIES_RAYQUAZA, WILD_AREA_LAND, MAPSEC_GRANITE_CAVE, MAP_TYPE_UNDERGROUND, WEATHER_SUNNY, FALSE));
        ExpectLeg("Mewtwo: caves and mystic places, not the grass of Route 101",
                  InArea(SPECIES_MEWTWO, WILD_AREA_LAND, MAPSEC_GRANITE_CAVE, MAP_TYPE_UNDERGROUND, WEATHER_SUNNY, FALSE)
                  && InArea(SPECIES_MEWTWO, WILD_AREA_LAND, MAPSEC_MT_PYRE, MAP_TYPE_ROUTE, WEATHER_SUNNY, FALSE)
                  && !InArea(SPECIES_MEWTWO, WILD_AREA_LAND, MAPSEC_ROUTE_101, MAP_TYPE_ROUTE, WEATHER_SUNNY, FALSE));
        ExpectLeg("Articuno: Shoal Cave and snow, not Route 101 in clear weather",
                  InArea(SPECIES_ARTICUNO, WILD_AREA_LAND, MAPSEC_SHOAL_CAVE, MAP_TYPE_UNDERGROUND, WEATHER_SUNNY, FALSE)
                  && InArea(SPECIES_ARTICUNO, WILD_AREA_LAND, MAPSEC_ROUTE_101, MAP_TYPE_ROUTE, WEATHER_SNOW, FALSE)
                  && !InArea(SPECIES_ARTICUNO, WILD_AREA_LAND, MAPSEC_ROUTE_101, MAP_TYPE_ROUTE, WEATHER_SUNNY, FALSE));
        ExpectLeg("Zapdos: stormy weather, Route 110 and New Mauville, not a calm Route 101",
                  InArea(SPECIES_ZAPDOS, WILD_AREA_LAND, MAPSEC_ROUTE_101, MAP_TYPE_ROUTE, WEATHER_RAIN_THUNDERSTORM, FALSE)
                  && InArea(SPECIES_ZAPDOS, WILD_AREA_LAND, MAPSEC_ROUTE_110, MAP_TYPE_ROUTE, WEATHER_SUNNY, FALSE)
                  && InArea(SPECIES_ZAPDOS, WILD_AREA_LAND, MAPSEC_NEW_MAUVILLE, MAP_TYPE_UNDERGROUND, WEATHER_SUNNY, FALSE)
                  && !InArea(SPECIES_ZAPDOS, WILD_AREA_LAND, MAPSEC_ROUTE_101, MAP_TYPE_ROUTE, WEATHER_SUNNY, FALSE));
        ExpectLeg("Moltres: volcanic places (Fiery Path, Mt. Chimney) and harsh sun, not Route 101 in clear weather",
                  InArea(SPECIES_MOLTRES, WILD_AREA_LAND, MAPSEC_FIERY_PATH, MAP_TYPE_UNDERGROUND, WEATHER_SUNNY, FALSE)
                  && InArea(SPECIES_MOLTRES, WILD_AREA_LAND, MAPSEC_MT_CHIMNEY, MAP_TYPE_ROUTE, WEATHER_SUNNY, FALSE)
                  && InArea(SPECIES_MOLTRES, WILD_AREA_LAND, MAPSEC_ROUTE_101, MAP_TYPE_ROUTE, WEATHER_DROUGHT, FALSE)
                  && !InArea(SPECIES_MOLTRES, WILD_AREA_LAND, MAPSEC_ROUTE_101, MAP_TYPE_ROUTE, WEATHER_SUNNY, FALSE));
        ExpectLeg("Darkrai and Cresselia: night only",
                  !InArea(SPECIES_DARKRAI, WILD_AREA_LAND, MAPSEC_PETALBURG_WOODS, MAP_TYPE_ROUTE, WEATHER_SUNNY, FALSE)
                  && InArea(SPECIES_DARKRAI, WILD_AREA_LAND, MAPSEC_PETALBURG_WOODS, MAP_TYPE_ROUTE, WEATHER_SUNNY, TRUE)
                  && !InArea(SPECIES_CRESSELIA, WILD_AREA_LAND, MAPSEC_ROUTE_101, MAP_TYPE_ROUTE, WEATHER_SUNNY, FALSE)
                  && InArea(SPECIES_CRESSELIA, WILD_AREA_LAND, MAPSEC_ROUTE_101, MAP_TYPE_ROUTE, WEATHER_SUNNY, TRUE));
        ExpectLeg("underground maps ignore the weather", MPLegend_AreaHabitats(WILD_AREA_LAND, MAPSEC_GRANITE_CAVE, MAP_TYPE_UNDERGROUND, WEATHER_RAIN_THUNDERSTORM)
                                                       == MPLegend_AreaHabitats(WILD_AREA_LAND, MAPSEC_GRANITE_CAVE, MAP_TYPE_UNDERGROUND, WEATHER_SUNNY));
        (void)landField; (void)sea; (void)fish; (void)chimney; (void)fiery; (void)shoal; (void)granite; (void)r110; (void)newMauville; (void)sky;

        // every species obtainable somewhere real; no real area has an empty pool
        memset(timesObtainable, 0, sizeof(timesObtainable));
        {
            u32 areas = 0, emptyPools = 0, maps = 0;

            for (i = 0; gWildMonHeaders[i].mapGroup != MAP_GROUP(MAP_UNDEFINED); i++)
            {
                const struct MapHeader *header = Overworld_GetMapHeaderByGroupAndId(gWildMonHeaders[i].mapGroup, gWildMonHeaders[i].mapNum);
                const struct WildEncounterTypes *types = &gWildMonHeaders[i].encounterTypes[TIME_MORNING];

                if (header == NULL || header->regionMapSectionId == MAPSEC_SAFARI_ZONE || header->regionMapSectionId == MAPSEC_KANTO_SAFARI_ZONE)
                    continue;
                maps++;
                for (u32 method = 0; method < 4; method++)
                {
                    const struct WildPokemonInfo *info = method == 0 ? types->landMonsInfo : method == 1 ? types->waterMonsInfo
                                                       : method == 2 ? types->rockSmashMonsInfo : types->fishingMonsInfo;
                    enum WildPokemonArea area = method == 0 ? WILD_AREA_LAND : method == 1 ? WILD_AREA_WATER : method == 2 ? WILD_AREA_ROCKS : WILD_AREA_FISHING;

                    if (info == NULL)
                        continue;
                    areas++;
                    for (u32 w = 0; w < ARRAY_COUNT(sWeathers); w++)
                        for (u32 night = 0; night < 2; night++)
                        {
                            u32 habitats = MPLegend_AreaHabitats(area, header->regionMapSectionId, header->mapType, sWeathers[w]);
                            u32 size = MPLegend_PoolSize(habitats, night, FALSE);

                            if (size == 0)
                                emptyPools++;
                            if (w == 0 || w == 1)   // the map's usual conditions (no weather / clear)
                                for (j = 0; j < MPLegend_SpeciesCount(); j++)
                                    if (InArea(MPLegend_SpeciesAt(j)->species, area, header->regionMapSectionId, header->mapType, sWeathers[w], night))
                                        timesObtainable[j]++;
                        }
                }
            }
            {
                u32 unobtainable = 0, minAreas = 0xFFFFFFFF, maxAreas = 0;

                for (j = 0; j < MPLegend_SpeciesCount(); j++)
                {
                    if (timesObtainable[j] == 0)
                    {
                        unobtainable++;
                        printf("  never eligible: species %u\n", MPLegend_SpeciesAt(j)->species);
                    }
                    if (timesObtainable[j] < minAreas)
                        minAreas = timesObtainable[j];
                    if (timesObtainable[j] > maxAreas)
                        maxAreas = timesObtainable[j];
                }
                printf("areas: %u maps, %u encounter areas (map x method); a species is eligible in %u to %u area/hour combinations\n", maps, areas,
                       minAreas, maxAreas);
                ExpectLeg("every Legendary and Mythical species is eligible in some real area (none unobtainable)", unobtainable == 0);
                ExpectLeg("no real encounter area, weather or hour has an empty pool", emptyPools == 0);
            }
        }
        ExpectLeg("an empty habitat has an empty pool and no pick (no crash)", MPLegend_PoolSize(0, FALSE, FALSE) == 0 && MPLegend_Pick(0, FALSE, FALSE, 12345) == SPECIES_NONE);
        ExpectLeg("night adds exactly the two night-only species", MPLegend_PoolSize(MP_LEGEND_ALL_TAGS, TRUE, FALSE) == MPLegend_PoolSize(MP_LEGEND_ALL_TAGS, FALSE, FALSE) + 2);
    }

    // 4. the Safari pool
    {
        ExpectLeg("Safari Zone: all species, whatever the habitat",
                  MPLegend_PoolSize(0, FALSE, TRUE) == MPLegend_SpeciesCount() && MPLegend_PoolSize(MP_LEGEND_OCEAN, TRUE, TRUE) == MPLegend_SpeciesCount()
                  && MPLegend_PoolSize(MP_LEGEND_FIELD, FALSE, TRUE) == MPLegend_SpeciesCount());
        {
            bool32 each = TRUE, distinct = TRUE;
            u16 seen[MP_LEGEND_SPECIES_COUNT_MAX];

            for (j = 0; j < MPLegend_SpeciesCount(); j++)
            {
                seen[j] = MPLegend_PoolSpecies(0, FALSE, TRUE, j);
                if (seen[j] == SPECIES_NONE || !InPool(seen[j]))
                    each = FALSE;
                for (u32 k = 0; k < j; k++)
                    if (seen[k] == seen[j])
                        distinct = FALSE;
            }
            ExpectLeg("the Safari pool lists each species once (no duplicate weighting)", each && distinct);
        }
        {
            u32 counts[MP_LEGEND_SPECIES_COUNT_MAX];
            u32 draws = 5400000, low = 0xFFFFFFFF, high = 0;

            memset(counts, 0, sizeof(counts));
            sSplit = 99;
            for (i = 0; i < draws; i++)
            {
                u16 s = MPLegend_Pick(0, FALSE, TRUE, SplitMix32());

                for (j = 0; j < MPLegend_SpeciesCount(); j++)
                    if (MPLegend_SpeciesAt(j)->species == s)
                        counts[j]++;
            }
            for (j = 0; j < MPLegend_SpeciesCount(); j++)
            {
                if (counts[j] < low) low = counts[j];
                if (counts[j] > high) high = counts[j];
            }
            printf("Safari pick: %u draws over %u species, each between %u and %u (expected %u)\n", draws, MPLegend_SpeciesCount(), low, high,
                   draws / MPLegend_SpeciesCount());
            ExpectLeg("the Safari pick is uniform (every species within 5% of its share)",
                      low > draws / MPLegend_SpeciesCount() * 95 / 100 && high < draws / MPLegend_SpeciesCount() * 105 / 100);
        }
    }

    // 5. the whole path
    {
        struct Run { u32 total, legend, outOfTable, legendOutOfTable, legendIneligible; u32 ordinaryDistinct; u32 legendDistinct, minLegendLevel, maxLegendLevel;
                     u32 minLevel, maxLevel; u8 seenOrdinary[1500]; u8 seenLegend[1500]; u32 fireOrdinary, waterOrdinary; };
        static struct Run run;
        static const struct { const char *name; u16 map; enum WildPokemonArea area; bool32 safari; u32 count; } sRuns[] =
        {
            {"Route 101, grass", MAP_ROUTE101, WILD_AREA_LAND, FALSE, 6000000},
            {"Route 105, surfing", MAP_ROUTE105, WILD_AREA_WATER, FALSE, 3000000},
            {"Safari Zone South", MAP_SAFARI_ZONE_SOUTH, WILD_AREA_LAND, TRUE, 3600000},
        };

        for (u32 r = 0; r < ARRAY_COUNT(sRuns); r++)
        {
            const struct WildPokemonInfo *info = InfoFor(sRuns[r].map, sRuns[r].area);
            u32 slots = sRuns[r].area == WILD_AREA_LAND ? NUM_LAND_MONS_ENCOUNTER_SLOTS : NUM_WATER_MONS_ENCOUNTER_SLOTS;
            u32 oneIn = MPLegend_OneIn(sRuns[r].safari);
            double expected, sigma, diff;
            clock_t started = clock();

            memset(&run, 0, sizeof(run));
            run.minLegendLevel = 255;
            run.minLevel = 255;
            UseMapHeader(sRuns[r].map);
            FlagClear(FLAG_SYS_SAFARI_MODE);
            if (sRuns[r].safari)
                FlagSet(FLAG_SYS_SAFARI_MODE);
            SeedRng(0x2900 + r);
            ExpectLeg("the map has the encounter table the run uses", info != NULL);
            if (info == NULL)
                continue;
            for (i = 0; i < sRuns[r].count; i++)
            {
                u16 species;
                u32 level;
                bool32 night;
                enum TimeOfDay tod = GetTimeOfDay();

                night = tod == TIME_EVENING || tod == TIME_NIGHT;
                ZeroEnemyPartyMons();
                TryGenerateWildMon(info, sRuns[r].area, 0);
                MPLegend_ApplyToPendingWildBattle();
                species = GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_SPECIES);
                level = GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_LEVEL);
                run.total++;
                if (level < run.minLevel) run.minLevel = level;
                if (level > run.maxLevel) run.maxLevel = level;
                if (!LevelInTable(info, slots, level))
                    run.outOfTable++;
                if (InPool(species))
                {
                    run.legend++;
                    if (!run.seenLegend[species]) { run.seenLegend[species] = 1; run.legendDistinct++; }
                    if (level < run.minLegendLevel) run.minLegendLevel = level;
                    if (level > run.maxLegendLevel) run.maxLegendLevel = level;
                    if (!LevelInTable(info, slots, level))
                        run.legendOutOfTable++;
                    if (!sRuns[r].safari && !InArea(species, sRuns[r].area, gMapHeader.regionMapSectionId, gMapHeader.mapType, GetCurrentWeather(), night))
                        run.legendIneligible++;
                }
                else if (species < 1500 && !run.seenOrdinary[species])
                {
                    run.seenOrdinary[species] = 1;
                    run.ordinaryDistinct++;
                    if (gSpeciesInfo[species].types[0] == TYPE_FIRE || gSpeciesInfo[species].types[1] == TYPE_FIRE) run.fireOrdinary++;
                    if (gSpeciesInfo[species].types[0] == TYPE_WATER || gSpeciesInfo[species].types[1] == TYPE_WATER) run.waterOrdinary++;
                }
            }
            expected = (double)run.total / oneIn;
            sigma = __builtin_sqrt(expected * (1.0 - 1.0 / oneIn));
            diff = (double)run.legend - expected;
            printf("%s: %u encounters through the game's own generator in %.1f s: %u Legendary/Mythical (expected %.0f +- %.0f, 1 in %.0f), levels %u-%u"
                   " (Legendary %u-%u), %u distinct Legendaries, %u distinct ordinary species (%u Fire-type, %u Water-type)\n",
                   sRuns[r].name, run.total, (double)(clock() - started) / CLOCKS_PER_SEC, run.legend, expected, sigma,
                   run.legend ? (double)run.total / run.legend : 0.0, run.minLevel, run.maxLevel,
                   run.legend ? run.minLegendLevel : 0, run.legend ? run.maxLegendLevel : 0, run.legendDistinct, run.ordinaryDistinct, run.fireOrdinary,
                   run.waterOrdinary);
            ExpectLeg("every encounter's level is one of the map's own table levels (no universal level, no species-based level)", run.outOfTable == 0);
            ExpectLeg("the Legendary/Mythical share converges to the documented rate (within 4.5 sigma)", diff > -4.5 * sigma && diff < 4.5 * sigma);
            ExpectLeg("every Legendary/Mythical encounter has a level the map's table allows (no override)", run.legendOutOfTable == 0);
            if (sRuns[r].safari)
            {
                ExpectLeg("Safari: every Legendary and Mythical species shows up (one shared pool)", run.legendDistinct == MPLegend_SpeciesCount());
                ExpectLeg("Safari: the levels are the Safari table's (25 to 31), also for the Legendaries",
                          run.minLevel >= MinLevel(info, slots) && run.maxLevel <= MaxLevel(info, slots));
            }
            else
            {
                ExpectLeg("outside the Safari Zone every Legendary/Mythical fits the area's habitat", run.legendIneligible == 0);
                {
                    enum TimeOfDay tod = GetTimeOfDay();
                    bool32 night = tod == TIME_EVENING || tod == TIME_NIGHT;
                    u32 poolSize = MPLegend_PoolSize(MPLegend_AreaHabitats(sRuns[r].area, gMapHeader.regionMapSectionId, gMapHeader.mapType,
                                                                          GetCurrentWeather()), night, FALSE);

                    printf("  the area's pool holds %u species; %u of them appeared\n", poolSize, run.legendDistinct);
                    ExpectLeg("the area's pool is restricted (fewer species than the Safari pool) and all of it appears", poolSize < MPLegend_SpeciesCount()
                              && run.legendDistinct == poolSize);
                }
            }
            if (sRuns[r].map == MAP_ROUTE101)
            {
                ExpectLeg("Route 101: a Legendary or Mythical is as low-level as the route (level 2 or 3), never 50/70/100",
                          run.legend > 0 && run.minLegendLevel >= 2 && run.maxLegendLevel <= 3);
                ExpectLeg("Route 101: ordinary species stay broadly available (over 150 distinct species, with Fire- and Water-types)",
                          run.ordinaryDistinct > 150 && run.fireOrdinary > 10 && run.waterOrdinary > 10);
            }
            if (sRuns[r].map == MAP_ROUTE105)
                ExpectLeg("Route 105 (surfing): the Legendaries are the sea's (Kyogre can appear, Groudon cannot)",
                          run.seenLegend[SPECIES_KYOGRE] && !run.seenLegend[SPECIES_GROUDON]);
        }
        FlagClear(FLAG_SYS_SAFARI_MODE);
    }

    // 6. what must never roll
    {
        const struct WildPokemonInfo *info = InfoFor(MAP_ROUTE101, WILD_AREA_LAND);
        u16 before, after;
        u32 shiny = 1, level;

        UseMapHeader(MAP_ROUTE101);
        FlagClear(FLAG_SYS_SAFARI_MODE);
        MPLegend_TestForce(1);
        ZeroEnemyPartyMons();
        TryGenerateWildMon(info, WILD_AREA_LAND, 0);
        before = GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_SPECIES);
        level = GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_LEVEL);
        SetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_IS_SHINY, &shiny);
        MPLegend_ApplyToPendingWildBattle();
        after = GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_SPECIES);
        ExpectLeg("(forced roll) a noted encounter becomes a Legendary or Mythical of the area, at the same level, shiny status kept",
                  InPool(after) && after != before && GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_LEVEL) == level
                  && GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_IS_SHINY) == 1);
        ExpectLeg("a legendary made this way has its own species' data (stats at its level, a legal move set)",
                  GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_MAX_HP) > 0 && GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_MOVE1) != MOVE_NONE);
        before = after;
        MPLegend_ApplyToPendingWildBattle();
        ExpectLeg("(forced roll) the same encounter never rolls twice", GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_SPECIES) == before);
        ZeroEnemyPartyMons();
        CreateWildMon(SPECIES_ZIGZAGOON, 3);            // a Pokemon made by something else (DexNav, a script): never noted
        MPLegend_ApplyToPendingWildBattle();
        ExpectLeg("(forced roll) a Pokemon nothing noted does not roll (DexNav, scripted, roaming, outbreak, Feebas)",
                  GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_SPECIES) == SPECIES_ZIGZAGOON);
        TryGenerateWildMon(info, WILD_AREA_LAND, 0);     // noted...
        CreateWildMon(SPECIES_ZIGZAGOON, 3);             // ...then another Pokemon takes the slot
        MPLegend_ApplyToPendingWildBattle();
        ExpectLeg("(forced roll) a stale note does not roll a different Pokemon",
                  GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_SPECIES) == SPECIES_ZIGZAGOON);
        UseMapHeader(MAP_BATTLE_FRONTIER_BATTLE_PYRAMID_FLOOR);
        TryGenerateWildMon(info, WILD_AREA_LAND, 0);
        before = GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_SPECIES);
        MPLegend_ApplyToPendingWildBattle();
        ExpectLeg("(forced roll) the Battle Pyramid and Pike never roll",
                  GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_SPECIES) == before);
        UseMapHeader(MAP_ROUTE101);
        MPLegend_TestForce(-1);
        TryGenerateWildMon(info, WILD_AREA_LAND, 0);
        before = GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_SPECIES);
        MPLegend_ApplyToPendingWildBattle();
        ExpectLeg("(never-hit roll) the encounter stays ordinary", GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_SPECIES) == before);
        MPLegend_TestForce(0);
    }

    // 6. Safari-only ordinary Pokemon (Kangaskhan, Tauros, Happiny, Scyther, Pinsir, Heracross, Mime Jr., Farfetch'd): weight 0
    //    outside the Zone, a normal weight inside it, found in the Zone's grass and never on an ordinary route
    {
        u32 families = MPWild_TestFamilyCount(), exclusives = 0, rareBoosted = 0, rareTotal = 0, outside = 0, inside = 0;
        static u8 isExclusive[1500];
#define MP_WILD_SAFARI_RARE_BOOST_TEST 3
        const struct MPWildMember *members = MPWild_TestMembers();
        u32 insideAll = 0, outsideAll = 0;

        memset(isExclusive, 0, sizeof(isExclusive));
        for (u32 f = 0; f < families; f++)
        {
            const struct MPWildFamily *family = MPWild_TestFamily(f);
            u32 out, in;

            UseMapHeader(MAP_ROUTE101);
            FlagClear(FLAG_SYS_SAFARI_MODE);
            out = MPWild_TestFamilyWeight(f, MP_WILD_HABITAT_LAND, FALSE, WEATHER_NONE);
            UseMapHeader(MAP_SAFARI_ZONE_SOUTH);
            FlagSet(FLAG_SYS_SAFARI_MODE);
            in = MPWild_TestFamilyWeight(f, MP_WILD_HABITAT_LAND, FALSE, WEATHER_NONE);
            outsideAll += out;
            insideAll += in;
            if (out == 0 && in > 0)
            {
                for (u32 m = 0; m < family->memberCount; m++)
                    isExclusive[members[family->firstMember + m].species] = 1;
                exclusives++;
            }
            else if (family->tier >= MP_WILD_TIER_RARE && out > 0)
            {
                rareTotal++;
                if (in == out * MP_WILD_SAFARI_RARE_BOOST_TEST)
                    rareBoosted++;
            }
        }
        ExpectLeg("exactly eight families (the Safari-only ones) have weight 0 outside the Zone and a weight inside it", exclusives == 8);
        ExpectLeg("(Safari) every other rare and very rare family is three times as likely inside the Zone", rareTotal > 0 && rareBoosted == rareTotal);
        ExpectLeg("(Safari) the Zone's pool is wider than the routes' (exclusives plus the boosted rares)", insideAll > outsideAll);
        SeedRng(0x2961);
        FlagClear(FLAG_SYS_SAFARI_MODE);
        UseMapHeader(MAP_ROUTE101);
        for (i = 0; i < 1500000; i++)
        {
            u16 species = MPWild_RandomizeSpecies(SPECIES_ZIGZAGOON, 3, WILD_AREA_LAND);

            if (species < ARRAY_COUNT(isExclusive) && isExclusive[species])
                outside++;
        }
        ExpectLeg("(1.5M encounters on Route 101) no Safari-only Pokemon ever appears outside the Safari Zone", outside == 0);
        UseMapHeader(MAP_SAFARI_ZONE_SOUTH);
        FlagSet(FLAG_SYS_SAFARI_MODE);
        for (i = 0; i < 3000000; i++)
        {
            u16 species = MPWild_RandomizeSpecies(SPECIES_ZIGZAGOON, 28, WILD_AREA_LAND);

            if (species < ARRAY_COUNT(isExclusive) && isExclusive[species])
                inside++;
        }
        ExpectLeg("(3M encounters in the Safari Zone) the Safari-only Pokemon do appear, as a few percent of encounters", inside > 3000000 / 200 && inside < 3000000 / 4);
        FlagClear(FLAG_SYS_SAFARI_MODE);
        UseMapHeader(MAP_ROUTE101);
    }

    printf("selftest-legend: %d checks, %d failed\n", sLegChecks, sLegFailures);
    return sLegFailures ? 1 : 0;
}

// --dump-legend-pool FILE: the game's own Legendary/Mythical pool for every map with land encounters, every weather and both
// times of day, so scripts/multiplayer/check_legend_pool.py can compare it with the host's port (legend_rules.py).
//   "L group number weather night count species..."  (land encounter area of that map)
int NativeSelfTest_DumpLegendPool(const char *outPath)
{
    FILE *out = fopen(outPath, "w");

    if (out == NULL)
        return 1;
    InitHeap(gHeap, HEAP_SIZE);
    SetSaveBlocksPointers(0);
    memset(gSaveBlock1Ptr, 0, sizeof(*gSaveBlock1Ptr));
    memset(gSaveBlock2Ptr, 0, sizeof(*gSaveBlock2Ptr));
    for (u32 i = 0; gWildMonHeaders[i].mapGroup != MAP_GROUP(MAP_UNDEFINED); i++)
    {
        const struct MapHeader *header = Overworld_GetMapHeaderByGroupAndId(gWildMonHeaders[i].mapGroup, gWildMonHeaders[i].mapNum);

        if (header == NULL)
            continue;
        {
            bool32 hasLand = FALSE;

            for (u32 t = 0; t < TIMES_OF_DAY_COUNT; t++)
                if (gWildMonHeaders[i].encounterTypes[t].landMonsInfo != NULL)
                    hasLand = TRUE;
            if (!hasLand)
                continue;
        }
        for (u32 weather = 0; weather <= WEATHER_ROUTE119_CYCLE; weather++)
            for (u32 night = 0; night < 2; night++)
            {
                u32 habitats = MPLegend_AreaHabitats(WILD_AREA_LAND, header->regionMapSectionId, header->mapType, weather);
                u32 size = MPLegend_PoolSize(habitats, night, FALSE);

                fprintf(out, "L %u %u %u %u %u", gWildMonHeaders[i].mapGroup, gWildMonHeaders[i].mapNum, weather, night, size);
                for (u32 k = 0; k < size; k++)
                    fprintf(out, " %u", MPLegend_PoolSpecies(habitats, night, FALSE, k));
                fputc('\n', out);
            }
    }
    fclose(out);
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// --selftest-shiny (Update 29): the shiny rolls of a wild Pokemon, through the game's own ComputePlayerShinyOdds.
// Every roll is an independent 1/4096 (16/65536). The number of rolls is 1, plus 2 with the Shiny Charm, plus 1 inside the
// Safari Zone, so the odds are exactly 1 - (4095/4096)^rolls:
//     normal 1/4096       Charm 3 rolls 1/1365.6      Safari 2 rolls 1/2048.5      Safari + Charm 4 rolls 1/1024.4
// The test runs 20 million creations of each and compares with these (4.5 sigma), checks that the Charm is the hunter's own
// (a bag without it gets nothing), that it leaves the Legendary/Mythical share alone, and that stored Pokemon keep their status.
// ---------------------------------------------------------------------------------------------------------------------
#include "item.h"
#include "multiplayer_spawns.h"

int NativeSelfTest_Shiny(void)
{
    const u32 n = 20000000;
    const double p = 16.0 / 65536.0, f = 41848.0 / 65536.0;
    // Update 30 targets: 1/4096; Charm about 1/2500 (one extra roll made with chance f); Safari 1/2048.3 (two rolls); Safari + Charm 1/1024.4 (four)
    const struct { const char *name; bool32 charm, safari; double odds; } sCases[] =
    {
        {"normal wild Pokemon", FALSE, FALSE, p},
        {"normal wild Pokemon with the Shiny Charm", TRUE, FALSE, p + f * p * (1.0 - p)},
        {"Safari Zone", FALSE, TRUE, 1.0 - (1.0 - p) * (1.0 - p)},
        {"Safari Zone with the Shiny Charm", TRUE, TRUE, 1.0 - __builtin_pow(1.0 - p, 4)},
    };

    InitHeap(gHeap, HEAP_SIZE);
    SetSaveBlocksPointers(0);
    memset(gSaveBlock1Ptr, 0, sizeof(*gSaveBlock1Ptr));
    memset(gSaveBlock2Ptr, 0, sizeof(*gSaveBlock2Ptr));
    SetBagItemsPointers();
    ClearBag();
    printf("selftest-shiny: Update 29 shiny rolls\n");
    ExpectLeg("the Shiny Charm is a Key Item the bag can hold and the game's shiny code reads from the hunter's own bag",
              AddBagItem(ITEM_SHINY_CHARM, 1) && CheckBagHasItem(ITEM_SHINY_CHARM, 1));
    ClearBag();
    for (u32 c = 0; c < ARRAY_COUNT(sCases); c++)
    {
        u32 hits = 0, i;
        double odds = sCases[c].odds, expected = n * odds, sigma = __builtin_sqrt(expected * (1.0 - odds)), diff;
        char what[160];

        ClearBag();
        if (sCases[c].charm)
            AddBagItem(ITEM_SHINY_CHARM, 1);
        FlagClear(FLAG_SYS_SAFARI_MODE);
        if (sCases[c].safari)
            FlagSet(FLAG_SYS_SAFARI_MODE);
        gSaveBlock2Ptr->playerTrainerId[0] = 0x39; gSaveBlock2Ptr->playerTrainerId[1] = 0x30;     // some trainer id
        SeedRng(0x5110 + c);
        for (i = 0; i < n; i++)
            if (ComputePlayerShinyOdds(Random32(), 0x00123456))
                hits++;
        diff = (double)hits - expected;
        printf("%s: %u shiny of %u (expected %.0f +- %.0f), observed 1 in %.1f, documented 1 in %.1f\n", sCases[c].name,
               hits, n, expected, sigma, hits ? (double)n / hits : 0.0, 1.0 / odds);
        snprintf(what, sizeof(what), "%s: shiny odds are 1 in %.1f (within 4.5 sigma)", sCases[c].name, 1.0 / odds);
        ExpectLeg(what, diff > -4.5 * sigma && diff < 4.5 * sigma);
    }
    // the Charm belongs to the bag it is in: with another save's empty bag a hunter gets the plain odds (checked above by case 1)
    FlagClear(FLAG_SYS_SAFARI_MODE);
    ClearBag();
    ExpectLeg("without the Charm in the bag the Safari flag alone gives two rolls, not four",
              (FlagSet(FLAG_SYS_SAFARI_MODE), !CheckBagHasItem(ITEM_SHINY_CHARM, 1)));
    FlagClear(FLAG_SYS_SAFARI_MODE);

    // the Charm does not touch the Legendary/Mythical probability: 6 million Route 101 encounters with the Charm
    {
        const struct WildPokemonInfo *info = InfoFor(MAP_ROUTE101, WILD_AREA_LAND);
        u32 legend = 0, total = 6000000;
        double expected = total / 6000.0, sigma = __builtin_sqrt(expected), diff;

        AddBagItem(ITEM_SHINY_CHARM, 1);
        ZeroPlayerPartyMons();
        CreateMon(&gParties[B_TRAINER_PLAYER][0], SPECIES_TREECKO, 5, 0x12345678, OTID_STRUCT_PLAYER_ID);
        UseMapHeader(MAP_ROUTE101);
        SeedRng(0xC4A8);
        for (u32 i = 0; i < total; i++)
        {
            ZeroEnemyPartyMons();
            TryGenerateWildMon(info, WILD_AREA_LAND, 0);
            MPLegend_ApplyToPendingWildBattle();
            if (InPool(GetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_SPECIES)))
                legend++;
        }
        diff = (double)legend - expected;
        printf("with the Shiny Charm in the bag: %u Legendary/Mythical in %u Route 101 encounters (expected %.0f +- %.0f)\n", legend, total, expected, sigma);
        ExpectLeg("the Shiny Charm does not change the Legendary/Mythical probability", diff > -4.5 * sigma && diff < 4.5 * sigma);
    }
    ClearBag();

    // shared (multiplayer) spawns: the Charm's two extra rolls are a pure function of the Pokemon, so they cannot be rerolled
    {
        u32 hits = 0, total = 20000000, again = 0, mixed = 0;
        double q = 669574.0 / 4294967296.0, expected = total * q, sigma = __builtin_sqrt(expected), diff;

        SeedRng(0x5A4ED);
        for (u32 i = 0; i < total; i++)
        {
            u32 id = 1 + (SplitMix32() % 1000000), personality = SplitMix32();
            bool32 first = MultiplayerSpawns_CharmMakesShiny(id, personality);

            if (first)
                hits++;
            if (MultiplayerSpawns_CharmMakesShiny(id, personality) != first)
                again++;      // a second claim of the same Pokemon must give the same answer
            if (MultiplayerSpawns_CharmMakesShiny(id + 1, personality) != first)
                mixed++;
        }
        diff = (double)hits - expected;
        printf("shared spawns, Charm bonus: %u shiny in %u Pokemon (expected %.0f +- %.0f, 1 in %.1f); total with the host's 1/4096: 1 in %.1f\n",
               hits, total, expected, sigma, total / (double)hits, 1.0 / (1.0 - (4095.0 / 4096.0) * (1.0 - q)));
        ExpectLeg("(shared) the Charm bonus on a shared spawn converges to the 1-in-2500 target", diff > -4.5 * sigma && diff < 4.5 * sigma);
        ExpectLeg("(shared) claiming the same Pokemon again always gives the same answer (nothing to reroll)", again == 0);
        ExpectLeg("(shared) different Pokemon give different answers (the hash is not constant)", mixed > 0);
    }

    // stored Pokemon keep their shininess: the stored predicate and the modifier bit are unchanged
    {
        struct Pokemon mon;
        u32 shiny = 1, notShiny = 0;

        CreateMon(&mon, SPECIES_PIKACHU, 20, 0x11112222, OTID_STRUCT_PLAYER_ID);
        SetMonData(&mon, MON_DATA_IS_SHINY, &shiny);
        ExpectLeg("a stored shiny Pokemon stays shiny after being written and read", GetMonData(&mon, MON_DATA_IS_SHINY) == 1);
        SetMonData(&mon, MON_DATA_IS_SHINY, &notShiny);
        ExpectLeg("a stored ordinary Pokemon stays ordinary", GetMonData(&mon, MON_DATA_IS_SHINY) == 0);
    }
    printf("selftest-shiny: %d checks, %d failed\n", sLegChecks, sLegFailures);
    return sLegFailures ? 1 : 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// --selftest-coins (Update 29): the 32-bit Coin Case, the table stake, the Mega Stone price list, the coin shop's payment and
// the script specials the Game Corner clerks use.
// ---------------------------------------------------------------------------------------------------------------------
#include "coins.h"
#include "money.h"
#include "constants/coins.h"
#include "constants/multiplayer_balance.h"
#include "platform/coin_wallet.h"
#include "platform/coin_shop.h"
#include "mp_mega_prices.h"
#include "mp_rare_goods_prices.h"
#ifdef ENABLE_NATIVE_UI
#include "native_ui/ui_engine.h"
#endif
#include "battle_util.h"
extern bool32 DoesSpeciesUseHoldItemToChangeForm(enum Species species, enum Item heldItemId);

extern u16 gSpecialVar_0x8004;
extern u16 gSpecialVar_0x8005;

int NativeSelfTest_Coins(void)
{
    static const u32 sAmounts[] = {0, 1, 50, 500, 5000, 9998, 9999, 10000, 25000, 30000, 32767, 32768, 65535, 65536, 65537, 99999, 100000,
                                   123456, 500000, 999998, 999999};

    InitHeap(gHeap, HEAP_SIZE);
    SetSaveBlocksPointers(0);
    memset(gSaveBlock1Ptr, 0, sizeof(*gSaveBlock1Ptr));
    memset(gSaveBlock2Ptr, 0, sizeof(*gSaveBlock2Ptr));
    SetBagItemsPointers();
    ClearBag();
    printf("selftest-coins: Update 29 coin economy\n");

    // 1. the wallet
    {
        bool32 roundTrip = TRUE, keyed = TRUE, capped;
        u32 key;

        gSaveBlock2Ptr->encryptionKey = 0xA5C3F00D;
        for (u32 i = 0; i < ARRAY_COUNT(sAmounts); i++)
        {
            SetCoins(sAmounts[i]);
            if (GetCoins() != sAmounts[i])
                roundTrip = FALSE;
        }
        ExpectLeg("the wallet round-trips every amount from 0 to 999,999 (the 16-bit edges included)", roundTrip);
        for (u32 i = 0; i < ARRAY_COUNT(sAmounts); i++)
        {
            // the game re-encrypts the low half when the save key changes (load_save.c); the total must not move
            u16 *low = &gSaveBlock1Ptr->coins;

            SetCoins(sAmounts[i]);
            key = gSaveBlock2Ptr->encryptionKey;
            *low ^= key;
            *low ^= 0x1234BEEF;
            gSaveBlock2Ptr->encryptionKey = 0x1234BEEF;
            if (GetCoins() != sAmounts[i])
                keyed = FALSE;
            gSaveBlock2Ptr->encryptionKey = key;
        }
        ExpectLeg("a new save encryption key keeps every balance (the high half is plain, the low half re-encrypted)", keyed);
        SetCoins(MAX_COINS_WALLET + 5000);
        capped = GetCoins() == MAX_COINS_WALLET;
        ExpectLeg("the wallet caps at 999,999", capped);
        SetCoins(999990);
        ExpectLeg("AddCoins fills the wallet to its cap and stops", AddCoins(50) && GetCoins() == MAX_COINS_WALLET && !AddCoins(1));
        SetCoins(100);
        ExpectLeg("RemoveCoins refuses more than the wallet holds, and takes exactly what it is asked",
                  !RemoveCoins(101) && GetCoins() == 100 && RemoveCoins(100) && GetCoins() == 0);
        // a save from before this update: the old u16 balance, the two spare bytes zero
        gSaveBlock2Ptr->encryptionKey = 0x00C0FFEE;
        memset(gSaveBlock1Ptr->unused_9C2, 0, sizeof(gSaveBlock1Ptr->unused_9C2));
        gSaveBlock1Ptr->coins = (u16)(7777 ^ gSaveBlock2Ptr->encryptionKey);
        ExpectLeg("a save from before the update keeps its balance (7,777 stays 7,777)", GetCoins() == 7777);
        SetCoins(65535);
        ExpectLeg("the old u16 field alone cannot hold 100,000 (the high half carries it)", (SetCoins(100000), GetCoins() == 100000
                  && (u16)(gSaveBlock1Ptr->coins ^ gSaveBlock2Ptr->encryptionKey) == 34464));
    }

    // 2. the Slot Machine / Roulette stake
    {
        bool32 stakes = CoinWallet_StakeFor(0) == 0 && CoinWallet_StakeFor(100) == 100 && CoinWallet_StakeFor(9998) == 9998
                     && CoinWallet_StakeFor(9999) == COIN_TABLE_STAKE && CoinWallet_StakeFor(10000) == COIN_TABLE_STAKE
                     && CoinWallet_StakeFor(999999) == COIN_TABLE_STAKE;

        ExpectLeg("a wallet below 9,999 is staked whole (as in the vanilla game); at 9,999 and above the table gets 5,000", stakes);
        SetCoins(50000);
        ExpectLeg("a table takes 5,000 of 50,000 and remembers 45,000", CoinWallet_BeginTable() == 5000 && CoinWallet_Reserve() == 45000
                  && CoinWallet_TableGet() == 5000);
        CoinWallet_TableSet(8200);
        ExpectLeg("leaving with 8,200 on the table gives a wallet of 53,200", GetCoins() == 53200 && CoinWallet_TableGet() == 8200);
        CoinWallet_TableSet(0);
        ExpectLeg("leaving with nothing keeps the reserve", GetCoins() == 45000);
        SetCoins(7000);
        ExpectLeg("a small wallet is staked whole and comes back whole", CoinWallet_BeginTable() == 7000 && CoinWallet_Reserve() == 0
                  && (CoinWallet_TableSet(7400), GetCoins() == 7400));
        SetCoins(999999);
        CoinWallet_BeginTable();
        CoinWallet_TableSet(9999);
        ExpectLeg("a full wallet cannot overflow", GetCoins() == MAX_COINS_WALLET);
    }

    // 3. the price list
    {
        extern const u16 MauvilleCity_Mart_Pokemart_MegaStones[];
        u32 tiers[8] = {0}, listed = 0;
        bool32 allInList = TRUE, pricesOk = TRUE, unique = TRUE, held = TRUE;
        static const u32 sTierValues[] = {30000, 35000, 40000, 60000, 80000, 90000, 100000};

        for (u32 i = 0; MauvilleCity_Mart_Pokemart_MegaStones[i] != ITEM_NONE; i++)
        {
            u16 item = MauvilleCity_Mart_Pokemart_MegaStones[i];
            u32 coins = CoinShop_StonePrice(item), t;

            listed++;
            if (coins == 0)
                allInList = FALSE;
            for (t = 0; t < ARRAY_COUNT(sTierValues) && sTierValues[t] != coins; t++)
                ;
            if (t == ARRAY_COUNT(sTierValues))
                pricesOk = FALSE;
            else
                tiers[t]++;
            if (GetItemHoldEffect(item) != HOLD_EFFECT_MEGA_STONE)
                held = FALSE;
            for (u32 j = 0; j < i; j++)
                if (MauvilleCity_Mart_Pokemart_MegaStones[j] == item)
                    unique = FALSE;
        }
        printf("Mega Stones for sale: %u; by price:", listed);
        for (u32 t = 0; t < ARRAY_COUNT(sTierValues); t++)
            printf(" %u x %u coins;", tiers[t], sTierValues[t]);
        printf("\n");
        ExpectLeg("every Mega Stone the dealer sells has a coin price, and only one", listed == MP_MEGA_STONE_COUNT && allInList && unique);
        ExpectLeg("every price is one of the seven tiers: 30,000 / 35,000 / 40,000 (common), 60,000 / 80,000 (strong), 90,000 / 100,000", pricesOk);
        ExpectLeg("the cheapest stone costs 30,000 and the dearest 100,000", MP_MEGA_STONE_PRICE_MIN == 30000 && MP_MEGA_STONE_PRICE_MAX == 100000);
        {
            // Mega flow: every stone sold is the one a Pokemon of the game holds to Mega Evolve (the form change table), so a
            // bought stone, held by its Pokemon with the Mega Ring in the bag, is what CanMegaEvolve looks for
            u32 withUser = 0;

            for (u32 i = 0; MauvilleCity_Mart_Pokemart_MegaStones[i] != ITEM_NONE; i++)
            {
                bool32 found = FALSE;

                for (u32 species = 1; species < NUM_SPECIES && !found; species++)
                    if (IsSpeciesEnabled(species) && DoesSpeciesUseHoldItemToChangeForm(species, MauvilleCity_Mart_Pokemart_MegaStones[i]))
                        found = TRUE;
                withUser += found;
            }
            ExpectLeg("every Mega Stone sold has a Pokemon that Mega Evolves with it", withUser == listed);
        }
        ExpectLeg("every priced item is a real Mega Stone (hold effect)", held);
        ExpectLeg("Mewtwonite X and Y cost 100,000 coins; Venusaurite (a standard Mega) 60,000; Beedrillite (the lightest) 30,000",
                  CoinShop_StonePrice(ITEM_MEWTWONITE_X) == 100000 && CoinShop_StonePrice(ITEM_MEWTWONITE_Y) == 100000
                  && CoinShop_StonePrice(ITEM_VENUSAURITE) == 60000 && CoinShop_StonePrice(ITEM_BEEDRILLITE) == 30000);
        ExpectLeg("a stone that is not on the list has no price", CoinShop_StonePrice(ITEM_POTION) == 0);
        ExpectLeg("the Mega Ring costs 25,000 coins and the Shiny Charm 100,000", MP_MEGA_RING_COINS == 25000 && MP_SHINY_CHARM_COINS == 100000);
        ExpectLeg("the large coin pack is the same 20 yen a coin as the others (5,000 for 100,000)",
                  MP_COINS_PACK_LARGE * 20 == MP_COINS_PACK_LARGE_YEN);
    }

    // 4. the dealer's coin shop pays in coins, every other shop in yen
    {
        u32 money = 1000000, coins = 90000;

        SetMoney(&gSaveBlock1Ptr->money, money);
        SetCoins(coins);
        gCoinShopActive = FALSE;
        ExpectLeg("(yen shop) the price is the item's yen price and the balance the money",
                  CoinShop_ItemPrice(ITEM_POTION) == GetItemPrice(ITEM_POTION) && CoinShop_Balance(&gSaveBlock1Ptr->money) == money);
        CoinShop_Pay(&gSaveBlock1Ptr->money, 300);
        ExpectLeg("(yen shop) paying takes yen and leaves the coins", GetMoney(&gSaveBlock1Ptr->money) == money - 300 && GetCoins() == coins);
        CoinShop_OnOpen(MauvilleCity_Mart_Pokemart_MegaStones);
        ExpectLeg("the dealer's list switches the shop to coins", gCoinShopActive);
        ExpectLeg("(coin shop) the price is the stone's coin price", CoinShop_ItemPrice(ITEM_GENGARITE) == CoinShop_StonePrice(ITEM_GENGARITE)
                  && CoinShop_ItemPrice(ITEM_GENGARITE) >= 30000);
        ExpectLeg("(coin shop) the balance is the coins; the POKeNEWS sale does not apply", CoinShop_Balance(&gSaveBlock1Ptr->money) == coins && CoinShop_SaleShift(0) == 0);
        ExpectLeg("(coin shop) an item without a coin price is out of reach, never free", CoinShop_ItemPrice(ITEM_POTION) > 999999);
        ExpectLeg("(coin shop) 90,000 coins can pay 80,000 and not 90,001",
                  CoinShop_CanPay(&gSaveBlock1Ptr->money, 80000) && CoinShop_CanPay(&gSaveBlock1Ptr->money, 90000) && !CoinShop_CanPay(&gSaveBlock1Ptr->money, 90001));
        CoinShop_Pay(&gSaveBlock1Ptr->money, 60000);
        ExpectLeg("(coin shop) paying takes coins and leaves the money alone", GetCoins() == 30000 && GetMoney(&gSaveBlock1Ptr->money) == money - 300);
        ExpectLeg("(coin shop) 30,000 coins cannot pay a 60,000-coin stone", !CoinShop_CanPay(&gSaveBlock1Ptr->money, 60000));
        CoinShop_OnOpen(NULL);
        ExpectLeg("any other shop list switches back to yen", !gCoinShopActive);
        // The Game Corner's RARE GOODS (the owner's request of 2026-10-24): each of the clerk's lists is a coin shop, every
        // item it sells has a coin price, the Ability Patch and the Gold Bottle Cap at 200,000-500,000
        {
            extern const u16 MauvilleCity_GameCorner_Pokemart_RareGoodsHeld[], MauvilleCity_GameCorner_Pokemart_RareGoodsOther[];
            extern const u16 MauvilleCity_GameCorner_Pokemart_RareGoodsTraining[];
            bool32 priced = TRUE;
            u32 listed = 0;

            CoinShop_OnOpen(MauvilleCity_GameCorner_Pokemart_RareGoodsOther);
            ExpectLeg("the RARE GOODS lists switch the shop to coins", gCoinShopActive);
            for (const u16 *item = MauvilleCity_GameCorner_Pokemart_RareGoodsHeld; *item != ITEM_NONE; item++, listed++)
                priced &= CoinShop_ItemPrice(*item) >= 200 && CoinShop_ItemPrice(*item) <= 500000;
            for (const u16 *item = MauvilleCity_GameCorner_Pokemart_RareGoodsTraining; *item != ITEM_NONE; item++, listed++)
                priced &= CoinShop_ItemPrice(*item) >= 200 && CoinShop_ItemPrice(*item) <= 500000;
            ExpectLeg("(RARE GOODS) every item listed has a coin price", priced && listed > 0);
            ExpectLeg("(RARE GOODS) the Ability Patch and the Gold Bottle Cap cost 200,000 to 500,000",
                      CoinShop_ItemPrice(ITEM_ABILITY_PATCH) >= 200000 && CoinShop_ItemPrice(ITEM_ABILITY_PATCH) <= 500000
                      && CoinShop_ItemPrice(ITEM_GOLD_BOTTLE_CAP) >= 200000 && CoinShop_ItemPrice(ITEM_GOLD_BOTTLE_CAP) <= 500000);
            ExpectLeg("(RARE GOODS) an obtainable item is not for sale", CoinShop_ItemPrice(ITEM_POTION) > 999999);
            // the review of 2026-10-24: every list whole on the native shop screen, the Mints in the owner's band, the
            // Charms and form-change key items sold, nothing that does nothing here, Misty Seed (no one can get it) sold
            {
                extern const u16 MauvilleCity_GameCorner_Pokemart_RareGoodsBerries[], MauvilleCity_GameCorner_Pokemart_RareGoodsEvolution[];
                const u16 *const lists[] = {MauvilleCity_GameCorner_Pokemart_RareGoodsHeld, MauvilleCity_GameCorner_Pokemart_RareGoodsBerries,
                                            MauvilleCity_GameCorner_Pokemart_RareGoodsTraining, MauvilleCity_GameCorner_Pokemart_RareGoodsEvolution,
                                            MauvilleCity_GameCorner_Pokemart_RareGoodsOther, MauvilleCity_Mart_Pokemart_MegaStones};
                u32 longest = 0, total = 0;

                priced = TRUE;
                for (u32 l = 0; l < ARRAY_COUNT(lists); l++)
                {
                    u32 n = 0;

                    for (const u16 *item = lists[l]; *item != ITEM_NONE; item++, n++)
                        priced &= CoinShop_ItemPrice(*item) >= 200 && CoinShop_ItemPrice(*item) <= 500000;
                    longest = max(longest, n);
                    total += l < ARRAY_COUNT(lists) - 1 ? n : 0;
                }
                ExpectLeg("(RARE GOODS) every item of all five lists (and every stone) has a coin price", priced);
                ExpectLeg("(RARE GOODS) the lists hold every good of the price table", total == MP_RARE_GOODS_COUNT);
#ifdef ENABLE_NATIVE_UI
                ExpectLeg("(RARE GOODS) every coin shop list fits the native shop screen whole (its CANCEL is CANCEL)",
                          longest <= NATIVE_SHOP_MAX_ITEMS);
#endif
                ExpectLeg("(RARE GOODS) the Mints cost 200,000 to 250,000 (the owner's band for trait changers)",
                          CoinShop_ItemPrice(ITEM_ADAMANT_MINT) >= 200000 && CoinShop_ItemPrice(ITEM_ADAMANT_MINT) <= 250000
                          && CoinShop_ItemPrice(ITEM_SERIOUS_MINT) >= 200000 && CoinShop_ItemPrice(ITEM_SERIOUS_MINT) <= 250000);
                ExpectLeg("(RARE GOODS) the Oval, Exp. and Catching Charms cost 100,000 (the Shiny Charm's price), one each",
                          CoinShop_ItemPrice(ITEM_OVAL_CHARM) == MP_SHINY_CHARM_COINS && CoinShop_ItemPrice(ITEM_EXP_CHARM) == MP_SHINY_CHARM_COINS
                          && CoinShop_ItemPrice(ITEM_CATCHING_CHARM) == MP_SHINY_CHARM_COINS && GetItemImportance(ITEM_OVAL_CHARM));
                ExpectLeg("(RARE GOODS) Gracidea, Reveal Glass, DNA Splicers, Prison Bottle, Zygarde Cube and Rotom Catalog are sold",
                          CoinShop_ItemPrice(ITEM_GRACIDEA) <= 500000 && CoinShop_ItemPrice(ITEM_REVEAL_GLASS) <= 500000
                          && CoinShop_ItemPrice(ITEM_DNA_SPLICERS) <= 500000 && CoinShop_ItemPrice(ITEM_PRISON_BOTTLE) <= 500000
                          && CoinShop_ItemPrice(ITEM_ZYGARDE_CUBE) <= 500000 && CoinShop_ItemPrice(ITEM_ROTOM_CATALOG) <= 500000);
                ExpectLeg("(RARE GOODS) Peat Block, the Aux items and the Legends foods (no effect here) are not sold",
                          CoinShop_ItemPrice(ITEM_PEAT_BLOCK) > 999999 && CoinShop_ItemPrice(ITEM_AUX_GUARD) > 999999
                          && CoinShop_ItemPrice(ITEM_AUX_POWERGUARD) > 999999 && CoinShop_ItemPrice(ITEM_CHOICE_DUMPLING) > 999999
                          && CoinShop_ItemPrice(ITEM_SWAP_SNACK) > 999999 && CoinShop_ItemPrice(ITEM_TWICE_SPICED_RADISH) > 999999);
                ExpectLeg("(RARE GOODS) Misty Seed (only Galarian Weezing and Comfey hold it) is sold", CoinShop_ItemPrice(ITEM_MISTY_SEED) <= 500000);
            }
            CoinShop_OnOpen(NULL);
        }
    }

    // 5. the Game Corner clerks' specials (32-bit amounts in two script variables)
    {
        SetCoins(130000);
        gSpecialVar_0x8004 = MP_SHINY_CHARM_COINS >> 16;
        gSpecialVar_0x8005 = MP_SHINY_CHARM_COINS & 0xFFFF;
        ExpectLeg("(specials) the 100,000-coin price splits into 1 and 34,464", gSpecialVar_0x8004 == 1 && gSpecialVar_0x8005 == 34464);
        ExpectLeg("(specials) a wallet of 130,000 has the Charm's 100,000", CoinWallet_Has() == 1);
        ExpectLeg("(specials) paying takes exactly 100,000", CoinWallet_Spend() == 1 && GetCoins() == 30000);
        ExpectLeg("(specials) 30,000 no longer has it, and a failed payment takes nothing", CoinWallet_Has() == 0 && CoinWallet_Spend() == 0 && GetCoins() == 30000);
        gSpecialVar_0x8004 = MP_MEGA_RING_COINS >> 16;
        gSpecialVar_0x8005 = MP_MEGA_RING_COINS & 0xFFFF;
        ExpectLeg("(specials) the Mega Ring's 25,000 is within 30,000, and leaves 5,000", CoinWallet_Has() == 1 && CoinWallet_Spend() == 1 && GetCoins() == 5000);
        SetCoins(999000);
        gSpecialVar_0x8004 = 0;
        gSpecialVar_0x8005 = 999;
        ExpectLeg("(specials) room for 999 more at 999,000, none for 1,000", CoinWallet_HasRoom() == 1 && (gSpecialVar_0x8005 = 1000, CoinWallet_HasRoom() == 0));
        gSpecialVar_0x8005 = 1000;
        ExpectLeg("(specials) giving more than there is room for is refused whole", CoinWallet_Give() == 0 && GetCoins() == 999000);
        gSpecialVar_0x8005 = 500;
        ExpectLeg("(specials) giving 500 coins works", CoinWallet_Give() == 1 && GetCoins() == 999500);
        gSpecialVar_0x8004 = 1;
        gSpecialVar_0x8005 = 0;
        SetCoins(0);
        ExpectLeg("(specials) the pack of 65,536 and more is also exact (high half 1, low 0)", CoinWallet_Give() == 1 && GetCoins() == 65536);
    }
    printf("selftest-coins: %d checks, %d failed\n", sLegChecks, sLegFailures);
    return sLegFailures ? 1 : 0;
}
