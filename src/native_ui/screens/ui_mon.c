#ifdef ENABLE_NATIVE_UI
/*
 * One Pokémon as the native UI's screens show it (see native_ui/ui_mon.h).
 * Called from screens' capture only (the game is parked). Project-owned.
 */
#include <string.h>

#include "global.h"
#include "item.h"
#include "move.h"
#include "party_menu.h"
#include "pokemon.h"
#include "string_util.h"
#include "constants/characters.h"
#include "constants/moves.h"
#include "constants/party_menu.h"
#include "constants/species.h"
#include "native_ui/ui_mon.h"
#include "native_ui/ui_text.h"

int UiMon_Type(int engineType)
{
    switch (engineType)
    {
    case TYPE_NORMAL: return UI_TYPE_NORMAL;
    case TYPE_FIGHTING: return UI_TYPE_FIGHTING;
    case TYPE_FLYING: return UI_TYPE_FLYING;
    case TYPE_POISON: return UI_TYPE_POISON;
    case TYPE_GROUND: return UI_TYPE_GROUND;
    case TYPE_ROCK: return UI_TYPE_ROCK;
    case TYPE_BUG: return UI_TYPE_BUG;
    case TYPE_GHOST: return UI_TYPE_GHOST;
    case TYPE_STEEL: return UI_TYPE_STEEL;
    case TYPE_FIRE: return UI_TYPE_FIRE;
    case TYPE_WATER: return UI_TYPE_WATER;
    case TYPE_GRASS: return UI_TYPE_GRASS;
    case TYPE_ELECTRIC: return UI_TYPE_ELECTRIC;
    case TYPE_PSYCHIC: return UI_TYPE_PSYCHIC;
    case TYPE_ICE: return UI_TYPE_ICE;
    case TYPE_DRAGON: return UI_TYPE_DRAGON;
    case TYPE_DARK: return UI_TYPE_DARK;
    case TYPE_FAIRY: return UI_TYPE_FAIRY;
    default: return -1;
    }
}

void UiMon_FromPokemon(struct UiMon *out, struct Pokemon *mon)
{
    u8 gender;
    u8 ppBonuses;

    memset(out, 0, sizeof(*out));
    out->types[0] = out->types[1] = -1;
    out->species = GetMonData(mon, MON_DATA_SPECIES);
    if (out->species == SPECIES_NONE)
        return;
    out->personality = GetMonData(mon, MON_DATA_PERSONALITY);
    out->isEgg = GetMonData(mon, MON_DATA_IS_EGG);
    out->shiny = GetMonData(mon, MON_DATA_IS_SHINY);
    GetMonData(mon, MON_DATA_NICKNAME, out->nickname);
    StringGet_Nickname(out->nickname);
    out->gender = UI_MON_GENDERLESS;
    if (out->isEgg)
        return;
    gender = GetMonGender(mon);
    /* the game leaves the symbol out for a Nidoran still named after its species */
    if (!((out->species == SPECIES_NIDORAN_M || out->species == SPECIES_NIDORAN_F)
          && StringCompare(out->nickname, GetSpeciesName(out->species)) == 0))
        out->gender = gender == MON_MALE ? UI_MON_MALE : gender == MON_FEMALE ? UI_MON_FEMALE : UI_MON_GENDERLESS;
    out->level = GetMonData(mon, MON_DATA_LEVEL);
    out->hp = GetMonData(mon, MON_DATA_HP);
    out->maxHP = GetMonData(mon, MON_DATA_MAX_HP);
    out->ailment = GetMonAilment(mon);
    out->toxic = (GetMonData(mon, MON_DATA_STATUS) & STATUS1_TOXIC_POISON) != 0;
    out->heldItem = GetMonData(mon, MON_DATA_HELD_ITEM);
    out->heldItemName = out->heldItem != ITEM_NONE ? GetItemName(out->heldItem) : NULL;
    out->ability = GetMonAbility(mon);
    out->abilityName = gAbilitiesInfo[out->ability].name;
    out->types[0] = (s8)UiMon_Type(GetSpeciesType(out->species, 0));
    out->types[1] = (s8)UiMon_Type(GetSpeciesType(out->species, 1));
    if (out->types[1] == out->types[0])
        out->types[1] = -1;
    ppBonuses = GetMonData(mon, MON_DATA_PP_BONUSES);
    for (int i = 0; i < MAX_MON_MOVES; i++)
    {
        u16 move = GetMonData(mon, MON_DATA_MOVE1 + i);

        if (move == MOVE_NONE)
            continue;
        out->moves[out->moveCount] = move;
        out->moveNames[out->moveCount] = GetMoveName(move);
        out->moveTypes[out->moveCount] = (s8)UiMon_Type(GetMoveType(move));
        out->pp[out->moveCount] = GetMonData(mon, MON_DATA_PP1 + i);
        out->maxPP[out->moveCount] = CalculatePPWithBonus(move, ppBonuses, i);
        out->moveCount++;
    }
    if (out->level < MAX_LEVEL)
    {
        u8 growth = gSpeciesInfo[out->species].growthRate;
        u32 exp = GetMonData(mon, MON_DATA_EXP);
        u32 lo = gExperienceTables[growth][out->level];
        u32 hi = gExperienceTables[growth][out->level + 1];

        if (hi > lo && exp >= lo)
            out->expFrac = exp >= hi ? 1.0f : (float)(exp - lo) / (float)(hi - lo);
    }
}

const char *UiMon_StatusLabel(const struct UiMon *m, UiRGBA *colour)
{
    switch (m->ailment)
    {
    case AILMENT_PSN: *colour = m->toxic ? UI_P_STATUS_TOX : UI_P_STATUS_PSN; return m->toxic ? "TOX" : "PSN";
    case AILMENT_PRZ: *colour = UI_P_STATUS_PAR; return "PAR";
    case AILMENT_SLP: *colour = UI_P_STATUS_SLP; return "SLP";
    case AILMENT_FRZ: *colour = UI_P_STATUS_FRZ; return "FRZ";
    case AILMENT_FRB: *colour = UI_P_STATUS_FRZ; return "FRB";
    case AILMENT_BRN: *colour = UI_P_STATUS_BRN; return "BRN";
    case AILMENT_PKRS: *colour = UI_P_STATUS_PKRS; return "PKRS";
    case AILMENT_FNT: *colour = UI_P_STATUS_FNT; return "FNT";
    default: return NULL;
    }
}

void UiMon_LevelText(u8 *dst, int size, int level)
{
    static const u8 sLv[] = _("{LV}");

    dst[0] = EOS;
    UiText_Append(dst, size, sLv);
    UiText_AppendNumber(dst, size, level, 1, false);
}
#endif // ENABLE_NATIVE_UI
