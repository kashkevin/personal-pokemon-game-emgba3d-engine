// Native PC build: self-test of move switching as in Legends Z-A (MOVES in the party menu and the summary screen's
// relearner; src/move_relearner.c, the end of the file). Project-owned. Runs the game's own code on save state built
// here, then exits:
//   --selftest-move-switch
// What a Pokemon can switch in (level-up moves to its level with its pre-evolutions', egg moves only if it hatched and
// from its own line, the TMs and HMs the player has, the moves of the one-time tutors used), TMs never used up, an HM's
// move switched out only when the HM can switch it back in, the warnings before a move goes (can't be switched back in,
// PP Ups lost), and the places with no move switching (Battle Pike, Battle Pyramid, Battle Factory, Slateport Battle
// Tent).
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "event_data.h"
#include "item.h"
#include "load_save.h"
#include "malloc.h"
#include "move_relearner.h"
#include "overworld.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "string_util.h"
#include "constants/characters.h"
#include "constants/flags.h"
#include "constants/items.h"
#include "constants/layouts.h"
#include "constants/moves.h"
#include "constants/species.h"
#include "constants/region_map_sections.h"

bool32 PartyMenuTest_OffersMoves(u8 slot);
bool32 SummaryTest_MoveSwitching(void *mons, u8 monIndex, u8 maxMonIndex, u8 slot, u16 newMove);

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
        else if (c == CHAR_EXTRA_SYMBOL || c == EXT_CTRL_CODE_BEGIN || c == PLACEHOLDER_BEGIN)
            text++; // a control code and its argument
        else
            out[n++] = c == CHAR_SGL_QUOTE_RIGHT ? '\'' : c == CHAR_QUESTION_MARK ? '?' : c == CHAR_PERIOD ? '.' : ' ';
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
    memset(gPokemonStoragePtr, 0, sizeof(*gPokemonStoragePtr));
    SetBagItemsPointers();
    ClearBag();
    ZeroPlayerPartyMons();
    gSaveBlock2Ptr->playerTrainerId[0] = 0x39;
    gSaveBlock2Ptr->playerTrainerId[1] = 0x30;
    gMapHeader.mapLayoutId = LAYOUT_LITTLEROOT_TOWN;
    gMapHeader.regionMapSectionId = MAPSEC_LITTLEROOT_TOWN;
    gRelearnMode = RELEARN_MODE_NONE;
    gSpecialVar_0x8004 = 0;
}

// The party's first Pokemon: `species` at `level` with its level-up moveset, met at that level (hatched: met at 0)
static struct BoxPokemon *Lead(u32 species, u32 level, bool32 hatched)
{
    struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][0];
    u32 metLevel = hatched ? 0 : level;

    CreateMon(mon, species, level, 0x1234, OTID_STRUCT_PLAYER_ID);
    GiveMonInitialMoveset(mon);
    SetMonData(mon, MON_DATA_MET_LEVEL, &metLevel);
    CalculatePlayerPartyCount();
    return &mon->box;
}

static bool32 SpeciesLearnsByLevel(u32 species, enum Move move, u32 level)
{
    const struct LevelUpMove *learnset = GetSpeciesLevelUpLearnset(species);

    for (u32 i = 0; learnset[i].move != LEVEL_UP_MOVE_END && learnset[i].level <= level; i++)
    {
        if (learnset[i].move == move)
            return TRUE;
    }
    return FALSE;
}

static void TestTmsReusable(void)
{
    Expect("TMs are never used up on PC: TM03 Water Pulse is important (I_REUSABLE_TMS)",
           GetItemImportance(ITEM_TM_WATER_PULSE) != 0);
    Expect("... TM39 Rock Tomb (Roxanne's, given once) too", GetItemImportance(ITEM_TM_ROCK_TOMB) != 0);
    Expect("HMs stay key items", GetItemImportance(ITEM_HM_SURF) != 0);
}

static void TestLevelUp(void)
{
    struct BoxPokemon *mon;
    const struct LevelUpMove *learnset;
    enum Move above = MOVE_NONE, azurillOnly = MOVE_NONE;

    Setup();
    gRelearnMode = RELEARN_MODE_PARTY_MENU;
    mon = Lead(SPECIES_MUDKIP, 20, FALSE);
    Expect("Mudkip Lv 20: Tackle (Lv 1) can be switched back in", MoveRelearner_CanSwitchBackIn(mon, MOVE_TACKLE));
    learnset = GetSpeciesLevelUpLearnset(SPECIES_MUDKIP);
    for (u32 i = 0; learnset[i].move != LEVEL_UP_MOVE_END && above == MOVE_NONE; i++)
    {
        if (learnset[i].level > 20 && !SpeciesLearnsByLevel(SPECIES_MUDKIP, learnset[i].move, 20)
         && GetTMHMItemIdFromMoveId(learnset[i].move) == ITEM_NONE)
            above = learnset[i].move;
    }
    Expect("Mudkip Lv 20: a move it learns only after Lv 20 is not offered",
           above != MOVE_NONE && !MoveRelearner_CanSwitchBackIn(mon, above));

    // A pre-evolution's level-up move (Azurill's, not Marill's or Azumarill's) up to the Pokemon's level
    learnset = GetSpeciesLevelUpLearnset(SPECIES_AZURILL);
    for (u32 i = 0; learnset[i].move != LEVEL_UP_MOVE_END && learnset[i].level <= 30 && azurillOnly == MOVE_NONE; i++)
    {
        enum Move move = learnset[i].move;

        if (!SpeciesLearnsByLevel(SPECIES_AZUMARILL, move, 30) && !SpeciesLearnsByLevel(SPECIES_MARILL, move, 30)
         && GetTMHMItemIdFromMoveId(move) == ITEM_NONE)
            azurillOnly = move;
    }
    mon = Lead(SPECIES_AZUMARILL, 30, FALSE);
    if (azurillOnly != MOVE_NONE)
        Expect("Azumarill Lv 30: a move only Azurill learns by level (by Lv 30) is offered (its pre-evolution's)",
               MoveRelearner_CanSwitchBackIn(mon, azurillOnly) && HasMoveToRelearn(mon, MOVE_RELEARNER_LEVEL_UP_MOVES));
    else
        printf("skip Azurill has no level-up move of its own by Lv 30 in this data\n");
    gRelearnMode = RELEARN_MODE_NONE;
}

static void TestEggMoves(void)
{
    struct BoxPokemon *mon;

    Setup();
    Expect("(data) Leaf Storm is a Treecko egg move", SpeciesHasEggMove(SPECIES_TREECKO, MOVE_LEAF_STORM));
    mon = Lead(SPECIES_TREECKO, 5, FALSE);
    Expect("a caught Lv 5 Treecko (met at Lv 5): no egg moves at all", !HasMoveToRelearn(mon, MOVE_RELEARNER_EGG_MOVES)
           && !CanBoxMonRelearnMoves(mon, MOVE_RELEARNER_EGG_MOVES));
    Expect("... Leaf Storm is not offered", !MoveRelearner_CanSwitchBackIn(mon, MOVE_LEAF_STORM));
    mon = Lead(SPECIES_TREECKO, 5, TRUE);
    Expect("a Treecko that hatched from an Egg (met at Lv 0): its egg moves (Leaf Storm)",
           CanBoxMonRelearnMoves(mon, MOVE_RELEARNER_EGG_MOVES) && MoveRelearner_CanSwitchBackIn(mon, MOVE_LEAF_STORM));

    // The incense babies: an Egg made without Sea Incense / Full Incense hatches as Marill / Snorlax
    Expect("(data) Belly Drum is Marill's egg move, not Azurill's",
           SpeciesHasEggMove(SPECIES_MARILL, MOVE_BELLY_DRUM) && !SpeciesHasEggMove(SPECIES_AZURILL, MOVE_BELLY_DRUM));
    mon = Lead(SPECIES_AZUMARILL, 30, TRUE);
    Expect("a hatched Azumarill: Marill's egg moves (Belly Drum, Aqua Jet, Superpower)",
           MoveRelearner_CanSwitchBackIn(mon, MOVE_BELLY_DRUM) && MoveRelearner_CanSwitchBackIn(mon, MOVE_AQUA_JET)
           && MoveRelearner_CanSwitchBackIn(mon, MOVE_SUPERPOWER));
    Expect("... and Azurill's (Encore: it may have hatched as Azurill)", MoveRelearner_CanSwitchBackIn(mon, MOVE_ENCORE));
    mon = Lead(SPECIES_MARILL, 15, TRUE);
    Expect("a hatched Marill: Belly Drum", MoveRelearner_CanSwitchBackIn(mon, MOVE_BELLY_DRUM));
    mon = Lead(SPECIES_AZUMARILL, 30, FALSE);
    Expect("a caught Azumarill: no egg moves (Belly Drum not offered)",
           !MoveRelearner_CanSwitchBackIn(mon, MOVE_BELLY_DRUM) && !HasMoveToRelearn(mon, MOVE_RELEARNER_EGG_MOVES));
    Expect("(data) Fissure and Power-Up Punch are Snorlax's egg moves, not Munchlax's",
           SpeciesHasEggMove(SPECIES_SNORLAX, MOVE_FISSURE) && !SpeciesHasEggMove(SPECIES_MUNCHLAX, MOVE_FISSURE));
    mon = Lead(SPECIES_SNORLAX, 40, TRUE);
    Expect("a hatched Snorlax: its own egg moves (Fissure, Power-Up Punch)",
           MoveRelearner_CanSwitchBackIn(mon, MOVE_FISSURE) && MoveRelearner_CanSwitchBackIn(mon, MOVE_POWER_UP_PUNCH));
    mon = Lead(SPECIES_SWAMPERT, 40, TRUE);
    Expect("a hatched Swampert: Mudkip's egg moves (Mirror Coat)", SpeciesHasEggMove(SPECIES_MUDKIP, MOVE_MIRROR_COAT)
           && MoveRelearner_CanSwitchBackIn(mon, MOVE_MIRROR_COAT));
}

static void TestTutors(void)
{
    struct BoxPokemon *mon;

    Setup();
    gRelearnMode = RELEARN_MODE_PARTY_MENU;
    mon = Lead(SPECIES_MUDKIP, 20, FALSE);
    Expect("(data) Mudkip can learn Double-Edge and Snore from tutors",
           CanLearnTeachableMove(SPECIES_MUDKIP, MOVE_DOUBLE_EDGE) && CanLearnTeachableMove(SPECIES_MUDKIP, MOVE_SNORE));
    Expect("no tutor used: no tutor moves (the old list offered all 30 from the start)",
           !HasMoveToRelearn(mon, MOVE_RELEARNER_TUTOR_MOVES) && !MoveRelearner_CanSwitchBackIn(mon, MOVE_DOUBLE_EDGE));
    FlagSet(FLAG_MOVE_TUTOR_TAUGHT_DOUBLE_EDGE);
    Expect("the Sootopolis tutor used (its flag): Double-Edge offered",
           HasMoveToRelearn(mon, MOVE_RELEARNER_TUTOR_MOVES) && MoveRelearner_CanSwitchBackIn(mon, MOVE_DOUBLE_EDGE));
    for (u32 flag = FLAG_MOVE_TUTOR_TAUGHT_SWAGGER; flag <= FLAG_MOVE_TUTOR_TAUGHT_EXPLOSION; flag++)
        FlagSet(flag);
    Expect("every one-time tutor used: a Battle Frontier tutor's move (Snore, for Battle Points) still not offered",
           !MoveRelearner_CanSwitchBackIn(mon, MOVE_SNORE));
    gRelearnMode = RELEARN_MODE_NONE;
}

static void TestTms(void)
{
    struct BoxPokemon *mon;

    Setup();
    gRelearnMode = RELEARN_MODE_PARTY_MENU;
    mon = Lead(SPECIES_MUDKIP, 20, FALSE);
    Expect("no TM03: Water Pulse not offered", !MoveRelearner_CanSwitchBackIn(mon, MOVE_WATER_PULSE));
    AddBagItem(ITEM_TM_WATER_PULSE, 1);
    Expect("TM03 in the Bag: Water Pulse offered", MoveRelearner_CanSwitchBackIn(mon, MOVE_WATER_PULSE)
           && HasMoveToRelearn(mon, MOVE_RELEARNER_TM_MOVES));
    RemoveBagItem(ITEM_TM_WATER_PULSE, 1);
    AddPCItem(ITEM_TM_WATER_PULSE, 1);
    Expect("TM03 in the PC's item storage (an older save): Water Pulse offered",
           MoveRelearner_CanSwitchBackIn(mon, MOVE_WATER_PULSE));
    gRelearnMode = RELEARN_MODE_NONE;
}

static void TestHms(void)
{
    struct Pokemon *lead = &gParties[B_TRAINER_PLAYER][0];
    struct BoxPokemon *mon;

    Setup();
    mon = Lead(SPECIES_MUDKIP, 20, FALSE);
    SetMonMoveSlot(lead, MOVE_SURF, 0);
    AddBagItem(ITEM_HM_SURF, 1);
    Expect("no move switching (a move learned by level up): Surf stays (P_CAN_FORGET_HIDDEN_MOVE)",
           !MoveRelearner_CanForgetHiddenMove(mon, MOVE_SURF)
           && !SummaryTest_MoveSwitching(gParties[B_TRAINER_PLAYER], 0, 0, 0, MOVE_TACKLE));
    gRelearnMode = RELEARN_MODE_PARTY_MENU;
    Expect("MOVES, HM03 in the Bag: Surf can be switched out (the summary lets it be chosen)",
           MoveRelearner_CanForgetHiddenMove(mon, MOVE_SURF)
           && SummaryTest_MoveSwitching(gParties[B_TRAINER_PLAYER], 0, 0, 0, MOVE_TACKLE));
    gRelearnMode = RELEARN_MODE_PSS_PAGE_BATTLE_MOVES;
    Expect("the summary's relearner, HM03 in the Bag: the same", MoveRelearner_CanForgetHiddenMove(mon, MOVE_SURF));
    gRelearnMode = RELEARN_MODE_PARTY_MENU;
    RemoveBagItem(ITEM_HM_SURF, 1);
    Expect("MOVES with no HM03 (a traded-in Surf): Surf stays, it could not come back",
           !MoveRelearner_CanForgetHiddenMove(mon, MOVE_SURF)
           && !SummaryTest_MoveSwitching(gParties[B_TRAINER_PLAYER], 0, 0, 0, MOVE_TACKLE));
    gRelearnMode = RELEARN_MODE_NONE;
}

static void TestWarnings(void)
{
    struct Pokemon *lead = &gParties[B_TRAINER_PLAYER][0];
    struct BoxPokemon *mon;
    const u8 *warning;
    u8 bonus;

    Setup();
    mon = Lead(SPECIES_MUDKIP, 20, FALSE);
    SetMonMoveSlot(lead, MOVE_TACKLE, 0);
    SetMonMoveSlot(lead, MOVE_SNORE, 1);
    gRelearnMode = RELEARN_MODE_PARTY_MENU;
    Expect("Tackle (a level-up move), no PP Ups: switched out with no question", MoveRelearner_ForgetWarning(mon, 0) == NULL);
    warning = MoveRelearner_ForgetWarning(mon, 1);
    Expect("Snore (a Battle Frontier tutor's): \"can't be switched back in later\"",
           warning != NULL && strstr(Ascii(warning), "can't be") != NULL && strstr(Ascii(warning), "PP Ups") == NULL);
    printf("     %s\n", Ascii(warning));
    bonus = 1 << 0; // one PP Up on the first slot
    SetMonData(lead, MON_DATA_PP_BONUSES, &bonus);
    warning = MoveRelearner_ForgetWarning(mon, 0);
    Expect("Tackle with a PP Up: \"PP Ups will be lost\"",
           warning != NULL && strstr(Ascii(warning), "PP Ups") != NULL && strstr(Ascii(warning), "can't be") == NULL);
    printf("     %s\n", Ascii(warning));
    bonus = 3 << 2; // PP Max on the second slot
    SetMonData(lead, MON_DATA_PP_BONUSES, &bonus);
    warning = MoveRelearner_ForgetWarning(mon, 1);
    Expect("Snore with PP Max: both", warning != NULL && strstr(Ascii(warning), "PP Ups") != NULL
           && strstr(Ascii(warning), "can't be") != NULL);
    printf("     %s\n", Ascii(warning));
    Expect("... a PP bonus on another slot is not this one's", MoveRelearner_ForgetWarning(mon, 0) == NULL);
    gRelearnMode = RELEARN_MODE_NONE;
    Expect("not move switching (a move learned by level up, the Move Reminder): no question, as on the GBA",
           MoveRelearner_ForgetWarning(mon, 1) == NULL);
    gRelearnMode = RELEARN_MODE_SCRIPT;
    Expect("... the Move Reminder's relearner (RELEARN_MODE_SCRIPT) neither", MoveRelearner_ForgetWarning(mon, 1) == NULL);
    gRelearnMode = RELEARN_MODE_NONE;
}

static void TestPlaces(void)
{
    static const struct
    {
        const char *name;
        u16 layout;
        bool8 offered;
    } sPlaces[] =
    {
        {"Littleroot Town", LAYOUT_LITTLEROOT_TOWN, TRUE},
        {"the Battle Pike's rooms", LAYOUT_BATTLE_FRONTIER_BATTLE_PIKE_ROOM_NORMAL, FALSE},
        {"the Battle Pyramid's floors", LAYOUT_BATTLE_FRONTIER_BATTLE_PYRAMID_FLOOR, FALSE},
        {"the Battle Pyramid's top", LAYOUT_BATTLE_FRONTIER_BATTLE_PYRAMID_TOP, FALSE},
        {"the Battle Factory's battle room", LAYOUT_BATTLE_FRONTIER_BATTLE_FACTORY_BATTLE_ROOM, FALSE},
    };
    char what[160];

    Setup();
    Lead(SPECIES_MUDKIP, 20, FALSE);
    for (u32 i = 0; i < ARRAY_COUNT(sPlaces); i++)
    {
        gMapHeader.mapLayoutId = sPlaces[i].layout;
        snprintf(what, sizeof(what), "%s: MOVES in the party menu %s", sPlaces[i].name, sPlaces[i].offered ? "offered" : "hidden");
        Expect(what, PartyMenuTest_OffersMoves(0) == sPlaces[i].offered);
        snprintf(what, sizeof(what), "%s: the summary's relearner (START on a move page) %s", sPlaces[i].name,
                 sPlaces[i].offered ? "offered" : "hidden");
        Expect(what, SummaryTest_MoveSwitching(gParties[B_TRAINER_PLAYER], 0, 0, 0, MOVE_NONE) == sPlaces[i].offered);
    }
    gMapHeader.mapLayoutId = LAYOUT_BATTLE_TENT_BATTLE_ROOM;
    gMapHeader.regionMapSectionId = MAPSEC_SLATEPORT_CITY;
    Expect("the Slateport Battle Tent: MOVES hidden", !PartyMenuTest_OffersMoves(0));
    Expect("the Slateport Battle Tent: the summary's relearner hidden",
           !SummaryTest_MoveSwitching(gParties[B_TRAINER_PLAYER], 0, 0, 0, MOVE_NONE));
    gMapHeader.regionMapSectionId = MAPSEC_VERDANTURF_TOWN;
    Expect("the Verdanturf Battle Tent (the player's own Pokemon, healed between battles): MOVES offered",
           PartyMenuTest_OffersMoves(0));
}

int NativeSelfTest_MoveSwitch(void)
{
    printf("selftest-move-switch: move switching as in Legends Z-A (MOVES, the summary's relearner)\n");
    TestTmsReusable();
    TestLevelUp();
    TestEggMoves();
    TestTutors();
    TestTms();
    TestHms();
    TestWarnings();
    TestPlaces();
    printf("selftest-move-switch: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}
