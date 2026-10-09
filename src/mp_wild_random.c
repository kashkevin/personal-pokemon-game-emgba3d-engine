#include "global.h"
#include "mp_wild_random.h"
#include "random.h"
#include "rtc.h"
#include "constants/layouts.h"
#include "constants/map_types.h"
#include "constants/region_map_sections.h"

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

    for (i = 0; i < MP_WILD_FAMILY_COUNT; i++)
        total += GetFamilyWeight(&sMPWildFamilies[i], habitat, night, typeBoost);
    if (total == 0)
        return vanillaSpecies;

    roll = Random32() % total;
    for (i = 0; i < MP_WILD_FAMILY_COUNT; i++)
    {
        u32 weight = GetFamilyWeight(&sMPWildFamilies[i], habitat, night, typeBoost);
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
