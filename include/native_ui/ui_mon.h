#ifndef GUARD_NATIVE_UI_MON_H
#define GUARD_NATIVE_UI_MON_H

/*
 * One Pokémon as the native UI's screens show it (party, summary, storage,
 * battle): copied from the game at the frame boundary by a screen's capture.
 * Names point at the game's own read-only strings. Only what the game has:
 * an Egg carries no level, HP, ability, item or moves.
 */
#include <stdbool.h>

#include "global.h"
#include "native_ui/ui_kit.h"

enum UiMonGender
{
    UI_MON_MALE,
    UI_MON_FEMALE,
    UI_MON_GENDERLESS,
};

struct UiMon
{
    u16 species;                       /* SPECIES_NONE: an empty slot */
    u32 personality;
    bool8 isEgg;
    bool8 shiny;
    u8 gender;                         /* enum UiMonGender; GENDERLESS also where the game leaves the symbol out */
    u8 nickname[POKEMON_NAME_BUFFER_SIZE];
    u8 level;
    u16 hp;
    u16 maxHP;
    u8 ailment;                        /* AILMENT_* (constants/party_menu.h) */
    bool8 toxic;                       /* badly poisoned (shown as TOX) */
    u16 heldItem;
    const u8 *heldItemName;
    u16 ability;
    const u8 *abilityName;
    s8 types[2];                       /* enum UiTypeId; -1 none */
    u8 moveCount;
    u16 moves[MAX_MON_MOVES];
    const u8 *moveNames[MAX_MON_MOVES];
    s8 moveTypes[MAX_MON_MOVES];
    u8 pp[MAX_MON_MOVES];
    u8 maxPP[MAX_MON_MOVES];
    float expFrac;                     /* toward the next level (0 at the last level) */
};

void UiMon_FromPokemon(struct UiMon *out, struct Pokemon *mon);
/* the engine's TYPE_* as the design's enum UiTypeId (-1: none, e.g. Mystery or Stellar) */
int UiMon_Type(int engineType);
/* the status badge (W.statusBadge): its label and colour; NULL when none */
const char *UiMon_StatusLabel(const struct UiMon *m, UiRGBA *colour);
/* "{LV}12" in engine bytes */
void UiMon_LevelText(u8 *dst, int size, int level);

/* ---- the Pokémon group's shared pieces (PK.pokemon in the design's summary.js) */

/* the pkui bevelled plate for small labels (H.tag); returns the right edge */
int UiMon_Tag(int x, int y, const char *label, UiRGBA colour);
/* the shiny star, 9x9 */
void UiMon_Star(int x, int y);
/* a sparkle centred at x, y (big: 7x7, else 5x5); NULL palette: the blue one */
void UiMon_Sparkle(int x, int y, bool big, const UiRGBA *pal3);
/* the four markings (bit i set: mark i on); returns the right edge */
int UiMon_Markings(int x, int y, u8 marks, UiRGBA on, UiRGBA off);
/* a photo-studio stage tinted by two types (enum UiTypeId); fills where the feet go */
void UiMon_Stage(int x, int y, int w, int h, int type0, int type1, int floor, int shadow, int *feetY, int *cx);

#endif // GUARD_NATIVE_UI_MON_H
