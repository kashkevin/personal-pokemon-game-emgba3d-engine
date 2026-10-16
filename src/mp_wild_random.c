#include "global.h"
#include "mp_wild_random.h"
#include "random.h"
#include "rtc.h"
#include "constants/layouts.h"
#include "constants/map_types.h"
#include "constants/region_map_sections.h"
#if defined(PLATFORM_SDL2)
#include "field_weather.h"
#include "constants/weather.h"
#include "event_data.h"
#include "constants/flags.h"
#endif
#line 8 // gba-line

#include "data/mp_wild_pool.h"

// Per-species weights by rarity tier: one common family is ten times as likely
// as one very rare family (owner-approved recommended defaults; see
// docs/balance-defaults.md). Weights are scaled by 3 so the 1/3 time-of-day
// reduction stays an integer.
static const u8 sTierWeight[MP_WILD_TIER_COUNT] =
{
    [MP_WILD_TIER_COMMON]    = 10 * 3,
    [MP_WILD_TIER_UNCOMMON]  = 5 * 3,
    [MP_WILD_TIER_RARE]      = 2 * 3,
    [MP_WILD_TIER_VERY_RARE] = 1 * 3,
};

// Chance (percent) that an evolved family picks one stage below the highest
// stage its level allows, keeping early-stage Pokemon common on later routes.
#define MP_WILD_LOWER_STAGE_CHANCE 35

#define TYPE_BIT(t) (1u << (t))

static u32 GetAreaTypeBoost(void)
{
    switch (gMapHeader.regionMapSectionId)
    {
    case MAPSEC_PETALBURG_WOODS:
        return TYPE_BIT(TYPE_BUG) | TYPE_BIT(TYPE_GRASS);
    case MAPSEC_FIERY_PATH:
    case MAPSEC_FIERY_PATH2:
    case MAPSEC_MT_CHIMNEY:
    case MAPSEC_JAGGED_PASS:
    case MAPSEC_JAGGED_PASS2:
        return TYPE_BIT(TYPE_FIRE) | TYPE_BIT(TYPE_ROCK) | TYPE_BIT(TYPE_GROUND);
    case MAPSEC_SHOAL_CAVE:
        return TYPE_BIT(TYPE_ICE) | TYPE_BIT(TYPE_WATER);
    case MAPSEC_MT_PYRE:
        return TYPE_BIT(TYPE_GHOST) | TYPE_BIT(TYPE_PSYCHIC);
    case MAPSEC_NEW_MAUVILLE:
        return TYPE_BIT(TYPE_ELECTRIC) | TYPE_BIT(TYPE_STEEL);
    case MAPSEC_METEOR_FALLS:
    case MAPSEC_METEOR_FALLS2:
        return TYPE_BIT(TYPE_ROCK) | TYPE_BIT(TYPE_DRAGON) | TYPE_BIT(TYPE_PSYCHIC);
    case MAPSEC_ABANDONED_SHIP:
        return TYPE_BIT(TYPE_GHOST) | TYPE_BIT(TYPE_WATER);
    case MAPSEC_SKY_PILLAR:
        return TYPE_BIT(TYPE_DRAGON) | TYPE_BIT(TYPE_FLYING) | TYPE_BIT(TYPE_GHOST);
    case MAPSEC_VICTORY_ROAD:
        return TYPE_BIT(TYPE_FIGHTING) | TYPE_BIT(TYPE_ROCK) | TYPE_BIT(TYPE_STEEL) | TYPE_BIT(TYPE_DRAGON);
    case MAPSEC_SEAFLOOR_CAVERN:
        return TYPE_BIT(TYPE_WATER) | TYPE_BIT(TYPE_ROCK);
    case MAPSEC_ROUTE_119:
    case MAPSEC_ROUTE_120:
        return TYPE_BIT(TYPE_GRASS) | TYPE_BIT(TYPE_BUG) | TYPE_BIT(TYPE_WATER);
    case MAPSEC_ROUTE_113:
        return TYPE_BIT(TYPE_FIRE) | TYPE_BIT(TYPE_GROUND) | TYPE_BIT(TYPE_STEEL);
    case MAPSEC_ROUTE_111:
        return TYPE_BIT(TYPE_GROUND) | TYPE_BIT(TYPE_ROCK);
    }
    if (gMapHeader.mapType == MAP_TYPE_UNDERGROUND)
        return TYPE_BIT(TYPE_ROCK) | TYPE_BIT(TYPE_GROUND) | TYPE_BIT(TYPE_DARK)
             | TYPE_BIT(TYPE_GHOST) | TYPE_BIT(TYPE_POISON) | TYPE_BIT(TYPE_STEEL);
    return 0;
}

static u32 GetFamilyWeight(const struct MPWildFamily *family, u32 habitat, bool32 night, u32 typeBoost)
{
    u32 weight;

    if (!(family->habitat & habitat))
        return 0;
    weight = sTierWeight[family->tier];
    if (family->time == MP_WILD_TIME_NOCTURNAL)
        weight = night ? weight * 3 : weight / 3;
    else if (family->time == MP_WILD_TIME_DIURNAL)
        weight = night ? weight / 3 : weight * 3;
    if (typeBoost & (TYPE_BIT(family->type1) | TYPE_BIT(family->type2)))
        weight *= 3;
    if (weight == 0)
        weight = 1;
    return weight;
}

#if defined(PLATFORM_SDL2)
// Weather (PC builds; the GBA ROM keeps its pinned weights). Owner's table:
// rain -> Water, heavy rain -> Water plus the rarer families, snow -> Ice,
// sun -> Fire and some Grass, sandstorm -> Ground and Rock, fog -> Ghost and some
// Psychic, thunder -> Electric. Clear weather changes nothing. The multipliers
// are configurable: set MP_WILD_WEATHER_ENABLED to 0 to turn the table off.
// "Sun" is the harsh sunlight of WEATHER_DROUGHT: WEATHER_SUNNY is Emerald's
// ordinary clear sky (most routes, towns and the sea), so it is clear weather
// here (KNOWN_ISSUES G12).
#ifndef MP_WILD_WEATHER_ENABLED
#define MP_WILD_WEATHER_ENABLED 1
#endif
#ifndef MP_WILD_WEATHER_PRIMARY_BOOST
#define MP_WILD_WEATHER_PRIMARY_BOOST 3      // x weight for the weather's main type
#endif
#ifndef MP_WILD_WEATHER_SECONDARY_BOOST
#define MP_WILD_WEATHER_SECONDARY_BOOST 2    // x weight for its secondary type
#endif
#ifndef MP_WILD_WEATHER_HEAVY_RARE_BOOST
#define MP_WILD_WEATHER_HEAVY_RARE_BOOST 2   // x weight for rare families in heavy rain
#endif

#include "platform/mp_wild_safari.h"

// Safari Zone (PC builds): the families flagged in mp_wild_safari.h (Kangaskhan, Tauros, ...) are found only inside the
// Safari Zone, where every rare and very rare family is also MP_WILD_SAFARI_RARE_BOOST times as likely. Outside it the
// exclusives have weight 0, so they never leave the Zone; inside it nothing is removed from the ordinary pool.
static u32 SafariAdjust(const struct MPWildFamily *family, u32 weight)
{
    bool32 safari = FlagGet(FLAG_SYS_SAFARI_MODE);

    if (sMPWildSafariFlags[family - sMPWildFamilies] != 0 && MP_WILD_SAFARI_EXCLUSIVE)
        return safari ? weight * MP_WILD_SAFARI_RARE_BOOST : 0;
    if (safari && family->tier >= MP_WILD_TIER_RARE)
        weight *= MP_WILD_SAFARI_RARE_BOOST;
    return weight;
}

static u32 GetWeatherWeight(const struct MPWildFamily *family, u32 weight, u8 weather)
{
    u32 primary = 0, secondary = 0, familyTypes = TYPE_BIT(family->type1) | TYPE_BIT(family->type2);
    bool32 heavy = FALSE;

#if MP_WILD_WEATHER_ENABLED
    switch (weather)
    {
    case WEATHER_RAIN:
        primary = TYPE_BIT(TYPE_WATER);
        break;
    case WEATHER_DOWNPOUR:
        primary = TYPE_BIT(TYPE_WATER);
        heavy = TRUE;
        break;
    case WEATHER_RAIN_THUNDERSTORM:
        primary = TYPE_BIT(TYPE_ELECTRIC);
        secondary = TYPE_BIT(TYPE_WATER);
        break;
    case WEATHER_SNOW:
        primary = TYPE_BIT(TYPE_ICE);
        break;
    case WEATHER_DROUGHT:
        primary = TYPE_BIT(TYPE_FIRE);
        secondary = TYPE_BIT(TYPE_GRASS);
        break;
    case WEATHER_SANDSTORM:
        primary = TYPE_BIT(TYPE_GROUND);
        secondary = TYPE_BIT(TYPE_ROCK);
        break;
    case WEATHER_FOG:
    case WEATHER_FOG_HORIZONTAL:
    case WEATHER_FOG_DIAGONAL:
        primary = TYPE_BIT(TYPE_GHOST);
        secondary = TYPE_BIT(TYPE_PSYCHIC);
        break;
    default:
        break;
    }
#endif

    if (primary & familyTypes)
        weight *= MP_WILD_WEATHER_PRIMARY_BOOST;
    if (secondary & familyTypes)
        weight *= MP_WILD_WEATHER_SECONDARY_BOOST;
    if (heavy && family->tier >= MP_WILD_TIER_RARE)
        weight *= MP_WILD_WEATHER_HEAVY_RARE_BOOST;
    return SafariAdjust(family, weight);
}
#endif

#if defined(PLATFORM_SDL2)
#define FAMILY_WEIGHT(family, habitat, night, typeBoost) \
    GetWeatherWeight(family, GetFamilyWeight(family, habitat, night, typeBoost), weather)
#else
#define FAMILY_WEIGHT(family, habitat, night, typeBoost) \
    GetFamilyWeight(family, habitat, night, typeBoost)
#endif
#line 89 // gba-line

static u16 ChooseMember(const struct MPWildFamily *family, u8 level)
{
    const struct MPWildMember *members = &sMPWildMembers[family->firstMember];
    u32 i, maxDepth = 0, targetDepth, eligible = 0, pick;

    for (i = 0; i < family->memberCount; i++)
    {
        if (members[i].minLevel <= level && members[i].depth > maxDepth)
            maxDepth = members[i].depth;
    }
    targetDepth = maxDepth;
    if (maxDepth > 0 && (Random() % 100) < MP_WILD_LOWER_STAGE_CHANCE)
        targetDepth = maxDepth - 1;

    for (i = 0; i < family->memberCount; i++)
    {
        if (members[i].depth == targetDepth && members[i].minLevel <= level)
            eligible++;
    }
    if (eligible == 0)
        return members[0].species;

    pick = Random() % eligible;
    for (i = 0; i < family->memberCount; i++)
    {
        if (members[i].depth == targetDepth && members[i].minLevel <= level)
        {
            if (pick == 0)
                return members[i].species;
            pick--;
        }
    }
    return members[0].species;
}

u16 MPWild_RandomizeSpecies(u16 vanillaSpecies, u8 level, enum WildPokemonArea area)
{
    u32 habitat, typeBoost, total = 0, roll, i;
    bool32 night;
    enum TimeOfDay timeOfDay;
#if defined(PLATFORM_SDL2)
    u8 weather;
#endif
#line 130 // gba-line

    // Battle Frontier facilities keep their curated tables.
    if (gMapHeader.mapLayoutId == LAYOUT_BATTLE_FRONTIER_BATTLE_PIKE_ROOM_WILD_MONS
     || gMapHeader.mapLayoutId == LAYOUT_BATTLE_FRONTIER_BATTLE_PYRAMID_FLOOR)
        return vanillaSpecies;

    switch (area)
    {
    case WILD_AREA_WATER:
        habitat = MP_WILD_HABITAT_SURF;
        break;
    case WILD_AREA_FISHING:
        habitat = MP_WILD_HABITAT_FISH;
        break;
    case WILD_AREA_ROCKS:
        habitat = MP_WILD_HABITAT_ROCK;
        break;
    default:
        habitat = MP_WILD_HABITAT_LAND;
        break;
    }

    timeOfDay = GetTimeOfDay();
    night = (timeOfDay == TIME_EVENING || timeOfDay == TIME_NIGHT);
    typeBoost = GetAreaTypeBoost();
#if defined(PLATFORM_SDL2)
    // Underground maps keep their tables: no weather there.
    weather = (gMapHeader.mapType == MAP_TYPE_UNDERGROUND) ? WEATHER_NONE : GetCurrentWeather();
#endif
#line 155 // gba-line

    for (i = 0; i < MP_WILD_FAMILY_COUNT; i++)
        total += FAMILY_WEIGHT(&sMPWildFamilies[i], habitat, night, typeBoost);
    if (total == 0)
        return vanillaSpecies;

    roll = Random32() % total;
    for (i = 0; i < MP_WILD_FAMILY_COUNT; i++)
    {
        u32 weight = FAMILY_WEIGHT(&sMPWildFamilies[i], habitat, night, typeBoost);
        if (roll < weight)
            return ChooseMember(&sMPWildFamilies[i], level);
        roll -= weight;
    }
    return vanillaSpecies;
}

u8 MPWild_GetSpeciesTier(u16 species)
{
    u32 i, j;

    for (i = 0; i < MP_WILD_FAMILY_COUNT; i++)
    {
        const struct MPWildFamily *family = &sMPWildFamilies[i];
        for (j = 0; j < family->memberCount; j++)
        {
            if (sMPWildMembers[family->firstMember + j].species == species)
                return family->tier;
        }
    }
    return MP_WILD_TIER_COUNT;
}

#if defined(PLATFORM_SDL2)
// Test hooks (PC builds only, never called by the game): the native self-test
// --selftest-wild-weather and the weight dump --dump-wild-weights, which the
// app's scripts/multiplayer/check_engine_spawn_weights.py compares with the
// host's shared spawn director. MPWild_TestFamilyWeight is the weight
// MPWild_RandomizeSpecies gives a family on the current map (gMapHeader) for a
// habitat, time of day and weather, by the same steps (no weather underground);
// the self-test checks it against the game's own rolls.
u32 MPWild_TestFamilyCount(void)
{
    return MP_WILD_FAMILY_COUNT;
}

const struct MPWildFamily *MPWild_TestFamily(u32 index)
{
    return index < MP_WILD_FAMILY_COUNT ? &sMPWildFamilies[index] : NULL;
}

const struct MPWildMember *MPWild_TestMembers(void)
{
    return sMPWildMembers;
}

bool32 MPWild_TestSafariOnly(u32 index)
{
    return index < MP_WILD_FAMILY_COUNT && MP_WILD_SAFARI_EXCLUSIVE && sMPWildSafariFlags[index] != 0;
}

u32 MPWild_TestFamilyWeight(u32 index, u32 habitat, bool32 night, u8 weather)
{
    const struct MPWildFamily *family;

    if (index >= MP_WILD_FAMILY_COUNT)
        return 0;
    family = &sMPWildFamilies[index];
    if (gMapHeader.mapType == MAP_TYPE_UNDERGROUND)
        weather = WEATHER_NONE;
    return GetWeatherWeight(family, GetFamilyWeight(family, habitat, night, GetAreaTypeBoost()), weather);
}
#endif
