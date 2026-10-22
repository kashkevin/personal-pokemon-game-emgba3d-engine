// Native PC build: Battle Tower: Legends (docs/battle-tower-legends.md in the app). Project-owned.
//
//   --dump-tower-data FILE   the game's own data the floor generator needs (tools/tower_legends/generate.py), as JSON:
//                            every enabled species (stats, types, Abilities, flags, evolutions, form changes, level-up,
//                            teachable and egg moves, whether each picture is the game's question-mark placeholder),
//                            every move, item, Ability, trainer class and trainer picture. Numbers only; the generator
//                            names them from the constants headers.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "battle.h"
#include "data.h"
#include "item.h"
#include "move.h"
#include "pokemon.h"
#include "constants/abilities.h"
#include "constants/characters.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "constants/species.h"
#include "constants/trainers.h"

extern const u32 gMonFrontPic_CircledQuestionMark[];
extern const u32 gMonBackPic_CircledQuestionMark[];
extern const u16 gMonShinyPalette_CircledQuestionMark[];
extern const u8 gMonIcon_QuestionMark[];
#include "graphics.h"

// Game text as JSON-safe ASCII (letters, digits and common punctuation; anything else a space).
static const char *JsonText(const u8 *text, u32 limit)
{
    static char out[96];
    u32 n = 0;

    if (text == NULL)
        return "";
    for (u32 i = 0; i < limit && text[i] != EOS && n < sizeof(out) - 1; i++)
    {
        u8 c = text[i];

        if (c >= CHAR_A && c <= CHAR_Z)
            out[n++] = 'A' + (c - CHAR_A);
        else if (c >= CHAR_a && c <= CHAR_z)
            out[n++] = 'a' + (c - CHAR_a);
        else if (c >= CHAR_0 && c <= CHAR_9)
            out[n++] = '0' + (c - CHAR_0);
        else if (c == CHAR_EXTRA_SYMBOL || c == EXT_CTRL_CODE_BEGIN)
            i++; // a control code and its argument
        else
            out[n++] = c == CHAR_EXCL_MARK ? '!' : c == CHAR_QUESTION_MARK ? '?' : c == CHAR_PERIOD ? '.'
                     : c == CHAR_COMMA ? ',' : c == CHAR_HYPHEN ? '-' : c == CHAR_SGL_QUOTE_RIGHT ? '\''
                     : c == CHAR_COLON ? ':' : c == CHAR_MALE ? 'M' : c == CHAR_FEMALE ? 'F' : ' ';
    }
    out[n] = 0;
    return out;
}

static void DumpSpecies(FILE *out, enum Species species, bool32 first)
{
    const struct SpeciesInfo *info = &gSpeciesInfo[species];
    const struct LevelUpMove *levelUp = GetSpeciesLevelUpLearnset(species);
    const struct Evolution *evolutions = GetSpeciesEvolutions(species);
    const struct FormChange *forms = GetSpeciesFormChanges(species);
    const u16 *list;
    u32 i, j;

    fprintf(out, "%s\n{\"id\":%u,\"name\":\"%s\",\"dex\":%u", first ? "" : ",", species,
            JsonText(info->speciesName, POKEMON_NAME_LENGTH), info->natDexNum);
    fprintf(out, ",\"stats\":[%u,%u,%u,%u,%u,%u]", info->baseHP, info->baseAttack, info->baseDefense, info->baseSpeed,
            info->baseSpAttack, info->baseSpDefense);
    fprintf(out, ",\"types\":[%u,%u],\"abilities\":[%u,%u,%u],\"growth\":%u,\"gender\":%u,\"eggGroups\":[%u,%u]",
            info->types[0], info->types[1], info->abilities[0], info->abilities[1], info->abilities[2], info->growthRate,
            info->genderRatio, info->eggGroups[0], info->eggGroups[1]);
    fprintf(out, ",\"flags\":{\"restricted\":%u,\"sub\":%u,\"mythical\":%u,\"ultraBeast\":%u,\"paradox\":%u,\"totem\":%u,"
                 "\"mega\":%u,\"primal\":%u,\"ultraBurst\":%u,\"gmax\":%u,\"tera\":%u,\"alolan\":%u,\"galarian\":%u,"
                 "\"hisuian\":%u,\"paldean\":%u,\"noTrade\":%u,\"perfectIVs\":%u,\"frontierBanned\":%u}",
            info->isRestrictedLegendary, info->isSubLegendary, info->isMythical, info->isUltraBeast, info->isParadox,
            info->isTotem, info->isMegaEvolution, info->isPrimalReversion, info->isUltraBurst, info->isGigantamax,
            info->isTeraForm, info->isAlolanForm, info->isGalarianForm, info->isHisuianForm, info->isPaldeanForm,
            info->cannotBeTraded, info->perfectIVCount, info->isFrontierBanned);
    fprintf(out, ",\"pics\":{\"front\":%u,\"back\":%u,\"shiny\":%u,\"icon\":%u}",
            info->frontPic != NULL && info->frontPic != gMonFrontPic_CircledQuestionMark,
            info->backPic != NULL && info->backPic != gMonBackPic_CircledQuestionMark,
            info->shinyPalette != NULL && info->shinyPalette != gMonShinyPalette_CircledQuestionMark,
            info->iconSprite != NULL && info->iconSprite != gMonIcon_QuestionMark);
    fprintf(out, ",\"levelUp\":[");
    for (i = 0; levelUp != NULL && i < 400 && levelUp[i].move != LEVEL_UP_MOVE_END; i++)
        fprintf(out, "%s[%u,%u]", i ? "," : "", levelUp[i].level, levelUp[i].move);
    fprintf(out, "],\"teachable\":[");
    list = GetSpeciesTeachableLearnset(species);
    for (i = 0; list != NULL && i < 1000 && list[i] != MOVE_UNAVAILABLE; i++)
        fprintf(out, "%s%u", i ? "," : "", list[i]);
    fprintf(out, "],\"egg\":[");
    list = GetSpeciesEggMoves(species);
    for (i = 0; list != NULL && i < 100 && list[i] != MOVE_UNAVAILABLE; i++)
        fprintf(out, "%s%u", i ? "," : "", list[i]);
    fprintf(out, "],\"evolutions\":[");
    for (i = 0; evolutions != NULL && i < 20 && evolutions[i].method != EVOLUTIONS_END; i++)
    {
        fprintf(out, "%s{\"method\":%u,\"param\":%u,\"target\":%u,\"conditions\":[", i ? "," : "", evolutions[i].method,
                evolutions[i].param, evolutions[i].targetSpecies);
        for (j = 0; evolutions[i].params != NULL && j < 12 && evolutions[i].params[j].condition != CONDITIONS_END; j++)
            fprintf(out, "%s[%u,%u,%u,%u]", j ? "," : "", evolutions[i].params[j].condition, evolutions[i].params[j].arg1,
                    evolutions[i].params[j].arg2, evolutions[i].params[j].arg3);
        fprintf(out, "]}");
    }
    fprintf(out, "],\"forms\":[");
    for (i = 0; forms != NULL && i < 64 && forms[i].method != FORM_CHANGE_TERMINATOR; i++)
        fprintf(out, "%s[%u,%u,%u,%u,%u,%u]", i ? "," : "", forms[i].method, forms[i].targetSpecies, forms[i].param1,
                forms[i].param2, forms[i].param3, forms[i].param4);
    fprintf(out, "]}");
}

int NativeSelfTest_DumpTowerData(const char *outPath)
{
    FILE *out = fopen(outPath, "w");
    bool32 first = TRUE;
    u32 i, count = 0;

    if (out == NULL)
    {
        fprintf(stderr, "dump-tower-data: cannot open %s\n", outPath);
        return 2;
    }
    fprintf(out, "{\"version\":1,\"counts\":{\"species\":%u,\"moves\":%u,\"items\":%u,\"abilities\":%u,"
                 "\"trainerClasses\":%u,\"trainerPics\":%u,\"trainers\":%u},\n\"species\":[",
            NUM_SPECIES, MOVES_COUNT, ITEMS_COUNT, ABILITIES_COUNT, TRAINER_CLASS_COUNT, TRAINER_PIC_TABLE_COUNT,
            TRAINERS_COUNT);
    for (i = 1; i < NUM_SPECIES; i++)
    {
        if (!IsSpeciesEnabled(i))
            continue;
        DumpSpecies(out, i, first);
        first = FALSE;
        count++;
    }
    fprintf(out, "],\n\"moves\":[");
    for (i = 1; i < MOVES_COUNT; i++)
    {
        const struct MoveInfo *move = &gMovesInfo[i];

        fprintf(out, "%s\n{\"id\":%u,\"name\":\"%s\",\"effect\":%u,\"type\":%u,\"category\":%u,\"power\":%u,"
                     "\"accuracy\":%u,\"target\":%u,\"pp\":%u,\"priority\":%d,\"strikes\":%u,\"multiHit\":%u,\"explosion\":%u,"
                     "\"contact\":%u,\"healing\":%u,\"sound\":%u,\"sketchBanned\":%u,\"metronomeBanned\":%u}",
                i == 1 ? "" : ",", i, JsonText(move->name, 32), move->effect, move->type, move->category, move->power,
                move->accuracy, move->target, move->pp, move->priority, move->strikeCount, move->multiHit, move->explosion,
                move->makesContact, move->healingMove, move->soundMove, move->sketchBanned, move->metronomeBanned);
    }
    fprintf(out, "],\n\"items\":[");
    for (i = 1; i < ITEMS_COUNT; i++)
    {
        const struct ItemInfo *item = &gItemsInfo[i];

        fprintf(out, "%s\n{\"id\":%u,\"name\":\"%s\",\"holdEffect\":%u,\"holdParam\":%u,\"pocket\":%u,\"price\":%u,"
                     "\"secondaryId\":%u,\"icon\":%u}",
                i == 1 ? "" : ",", i, JsonText(item->name, ITEM_NAME_LENGTH), item->holdEffect, item->holdEffectParam,
                item->pocket, item->price, item->secondaryId, item->iconPic != NULL && item->iconPic != gItemIcon_QuestionMark);
    }
    fprintf(out, "],\n\"abilities\":[");
    for (i = 1; i < ABILITIES_COUNT; i++)
        fprintf(out, "%s\n{\"id\":%u,\"name\":\"%s\"}", i == 1 ? "" : ",", i, JsonText(gAbilitiesInfo[i].name, ABILITY_NAME_LENGTH));
    fprintf(out, "],\n\"trainerClasses\":[");
    for (i = 0; i < TRAINER_CLASS_COUNT; i++)
        fprintf(out, "%s\n{\"id\":%u,\"name\":\"%s\",\"money\":%u}", i ? "," : "", i,
                JsonText(gTrainerClasses[i].name, 32), gTrainerClasses[i].money);
    fprintf(out, "],\n\"trainerPics\":[");
    for (i = 0; i < TRAINER_PIC_TABLE_COUNT; i++)
    {
        const struct TrainerFrontPicInfo *front = gTrainerPicInfo[i].frontPic;

        fprintf(out, "%s\n{\"id\":%u,\"front\":%u,\"palette\":%u}", i ? "," : "", i,
                front != NULL && front->imageData != NULL, front != NULL && front->paletteData != NULL);
    }
    fprintf(out, "]}\n");
    fclose(out);
    printf("dump-tower-data: %u species, %u moves, %u items\n", count, MOVES_COUNT - 1, ITEMS_COUNT - 1);
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// --selftest-tower: Battle Tower: Legends through the game's own code (the floor table against the game's data, the
// parties as the battle builds them, the experience cap, the record and its words, the milestone Pokemon, the menu's
// command and style, the Duo code), before the game starts.
#include "event_data.h"
#include "item.h"
#include "battle_setup.h"
#include "load_save.h"
#include "malloc.h"
#include "multiplayer_ladder.h"
#include "multiplayer_world.h"
#include "pokemon_storage_system.h"
#include "tower_legends.h"
#include "constants/flags.h"
#include "constants/hold_effects.h"
#include "constants/opponents.h"

static int sTowerChecks, sTowerFailures;

static void ExpectTower(const char *what, bool32 ok)
{
    sTowerChecks++;
    if (!ok)
    {
        sTowerFailures++;
        printf("  FAILED: %s\n", what);
    }
}

static u32 TowerWord(u32 kind, u32 payload)
{
    return MP_REWARD_FLAG | (kind << 24) | payload;
}

static bool32 SpeciesHasAbility(enum Species species, enum Ability ability)
{
    for (u32 i = 0; i < NUM_ABILITY_SLOTS; i++)
        if (GetSpeciesAbility(species, i) == ability)
            return TRUE;
    return FALSE;
}

// The Mega form a held stone makes for this species (SPECIES_NONE: none).
static enum Species MegaFormOf(enum Species species, enum Item item)
{
    const struct FormChange *forms = GetSpeciesFormChanges(species);

    for (u32 i = 0; forms != NULL && i < 64 && forms[i].method != FORM_CHANGE_TERMINATOR; i++)
        if (forms[i].method == FORM_CHANGE_BATTLE_MEGA_EVOLUTION_ITEM && forms[i].param1 == item)
            return forms[i].targetSpecies;
    return SPECIES_NONE;
}

static const u8 sTowerCheckpoints[] = {10, 20, 25, 30, 40, 50, 60, 70, 75, 80, 90, 100};

static u32 ExpectedKind(u32 floor)
{
    if (floor == 100)
        return TOWER_FLOOR_FINAL;
    if (floor == 25 || floor == 50 || floor == 75)
        return TOWER_FLOOR_SPECIAL;
    if (floor % 10 == 0)
        return TOWER_FLOOR_BOSS;
    return TOWER_FLOOR_REGULAR;
}

static EWRAM_DATA struct Pokemon sTowerParty[PARTY_SIZE] = {0};

// One trainer of a floor: the table's data, then its party exactly as the battle creates it.
static void CheckTowerTrainer(u32 floor, const char *role, u32 index, u32 maxSize, u32 *megas, u32 *mons)
{
    const struct Trainer *trainer = &gTowerLegendsTrainers[DIFFICULTY_NORMAL][index];
    char what[200];
    u32 i, j, stones = 0;
    bool32 ok = TRUE;

    snprintf(what, sizeof(what), "floor %u %s: a named trainer of a real class and picture with 1-%u Pokemon", floor, role, maxSize);
    ExpectTower(what, trainer->trainerName[0] != EOS && trainer->trainerClass < TRAINER_CLASS_COUNT
                && trainer->trainerClass != TRAINER_CLASS_RIVAL_EARLY_FRLG && trainer->trainerClass != TRAINER_CLASS_RIVAL_LATE_FRLG
                && trainer->trainerPic < TRAINER_PIC_TABLE_COUNT && gTrainerPicInfo[trainer->trainerPic].frontPic != NULL
                && gTrainerPicInfo[trainer->trainerPic].frontPic->imageData != NULL
                && trainer->party != NULL && trainer->partySize >= 1 && trainer->partySize <= maxSize);
    for (i = 0; trainer->party != NULL && i < trainer->partySize; i++)
    {
        const struct TrainerMon *mon = &trainer->party[i];
        bool32 damaging = FALSE;
        u32 evTotal = 0;

        ok = mon->species != SPECIES_NONE && mon->species < NUM_SPECIES && IsSpeciesEnabled(mon->species)
          && !gSpeciesInfo[mon->species].isMegaEvolution && mon->lvl == floor && mon->heldItem < ITEMS_COUNT
          && (mon->ability == ABILITY_NONE || SpeciesHasAbility(mon->species, mon->ability));
        for (j = 0; j < MAX_MON_MOVES; j++)
        {
            if (mon->moves[j] >= MOVES_COUNT)
                ok = FALSE;
            else if (mon->moves[j] != MOVE_NONE && GetMoveCategory(mon->moves[j]) != DAMAGE_CATEGORY_STATUS)
                damaging = TRUE;
        }
        if (mon->ev != NULL)
            for (j = 0; j < NUM_STATS; j++)
                evTotal += mon->ev[j];
        if (mon->heldItem != ITEM_NONE && GetItemHoldEffect(mon->heldItem) == HOLD_EFFECT_MEGA_STONE)
        {
            enum Species mega = MegaFormOf(mon->species, mon->heldItem);

            stones++;
            ok = ok && mega != SPECIES_NONE && IsSpeciesEnabled(mega) && floor >= 30;
        }
        snprintf(what, sizeof(what), "floor %u %s mon %u (species %u): enabled, level %u, real moves with an attack, "
                 "its own Ability, a real item, EVs <= 510, a stone only for its own Mega from floor 30",
                 floor, role, i + 1, mon->species, floor);
        ExpectTower(what, ok && damaging && evTotal <= MAX_TOTAL_EVS);
        (*mons)++;
    }
    snprintf(what, sizeof(what), "floor %u %s: at most one Mega Stone", floor, role);
    ExpectTower(what, stones <= 1);
    *megas += stones;

    // The party exactly as a battle builds it.
    memset(sTowerParty, 0, sizeof(sTowerParty));
    CreateNPCTrainerPartyFromTrainer(sTowerParty, trainer);
    ok = TRUE;
    for (i = 0; trainer->party != NULL && i < trainer->partySize; i++)
    {
        const struct TrainerMon *mon = &trainer->party[i];
        struct Pokemon *built = &sTowerParty[i];

        ok = ok && GetMonData(built, MON_DATA_SPECIES) == mon->species && GetMonData(built, MON_DATA_LEVEL) == floor
                && GetMonData(built, MON_DATA_HELD_ITEM) == mon->heldItem
                && (mon->ability == ABILITY_NONE || GetMonAbility(built) == mon->ability)
                && GetMonData(built, MON_DATA_HP) == GetMonData(built, MON_DATA_MAX_HP) && GetMonData(built, MON_DATA_HP) != 0;
        for (j = 0; j < MAX_MON_MOVES; j++)
            if (mon->moves[j] != MOVE_NONE && GetMonData(built, MON_DATA_MOVE1 + j) != mon->moves[j])
                ok = FALSE;
    }
    snprintf(what, sizeof(what), "floor %u %s: the built party has every species, level %u, item, Ability and move", floor, role, floor);
    ExpectTower(what, ok);
}

static void SetBadges(u32 count, bool32 champion)
{
    for (u32 i = 0; i < NUM_BADGES; i++)
    {
        if (i < count)
            FlagSet(gBadgeFlags[i]);
        else
            FlagClear(gBadgeFlags[i]);
    }
    if (champion)
        FlagSet(FLAG_SYS_GAME_CLEAR);
    else
        FlagClear(FLAG_SYS_GAME_CLEAR);
}

int NativeSelfTest_Tower(void)
{
    static const u8 sCaps[] = {15, 20, 25, 30, 40, 45, 50, 55, 60};
    u32 floor, i, megas = 0, mons = 0, revision, amount, personality, ivs;
    struct TowerLegendsCommand command;
    struct Pokemon *mon;
    u8 result;
    bool32 ok;

    printf("Battle Tower: Legends\n");
    InitHeap(gHeap, HEAP_SIZE);
    SetSaveBlocksPointers(0);
    SetBagItemsPointers();
    ClearBag();
    ZeroPlayerPartyMons();
    ResetPokemonStorageSystem();
    memset(&gSaveBlock3Ptr->tower, 0, sizeof(gSaveBlock3Ptr->tower));
    TowerLegends_TestReset();

    // 1. The table, against the game's own data, and every party as a battle builds it.
    for (floor = 1; floor <= TOWER_FLOOR_COUNT; floor++)
    {
        const struct TowerLegendsFloor *f = &gTowerLegendsFloors[floor - 1];
        bool32 checkpoint = FALSE;
        char what[120];

        for (i = 0; i < ARRAY_COUNT(sTowerCheckpoints); i++)
            if (sTowerCheckpoints[i] == floor)
                checkpoint = TRUE;
        snprintf(what, sizeof(what), "floor %u: kind, checkpoint, difficulty, music and texts", floor);
        ExpectTower(what, f->kind == ExpectedKind(floor) && f->checkpoint == checkpoint && f->difficulty >= 1 && f->difficulty <= 5
                    && f->battleMusic != 0 && f->introText != NULL && f->defeatText != NULL && f->defeatTextB != NULL
                    && (f->kind == TOWER_FLOOR_REGULAR || f->victoryText != NULL)
                    && f->solo < TOWER_LEGENDS_TRAINER_COUNT && f->duoA < TOWER_LEGENDS_TRAINER_COUNT
                    && f->duoB < TOWER_LEGENDS_TRAINER_COUNT);
        CheckTowerTrainer(floor, "solo", f->solo, PARTY_SIZE, &megas, &mons);
        CheckTowerTrainer(floor, "Duo A", f->duoA, 3, &megas, &mons);
        CheckTowerTrainer(floor, "Duo B", f->duoB, 3, &megas, &mons);
    }
    printf("  %u floors, %u Pokemon, %u Mega Stones\n", TOWER_FLOOR_COUNT, mons, megas);

    // 2. The trainer hook answers only for the two Tower ids while a Tower battle is set up.
    ExpectTower("no Tower battle: the hook answers nothing", TowerLegends_GetTrainer(TRAINER_TOWER_A) == NULL
                && GetTrainerStructFromId(TRAINER_TOWER_A) == &gTrainers[DIFFICULTY_NORMAL][TRAINER_RED]);
    TowerLegends_PrepareCoop(TowerLegends_EncodeDuo(37, TRUE));
    ExpectTower("a Duo floor: trainers A and B are the floor's Duo pair, other ids unchanged",
                GetTrainerStructFromId(TRAINER_TOWER_A) == &gTowerLegendsTrainers[DIFFICULTY_NORMAL][gTowerLegendsFloors[36].duoA]
                && GetTrainerStructFromId(TRAINER_TOWER_B) == &gTowerLegendsTrainers[DIFFICULTY_NORMAL][gTowerLegendsFloors[36].duoB]
                && GetTrainerStructFromId(TRAINER_WALLACE) == &gTrainers[DIFFICULTY_NORMAL][TRAINER_WALLACE]
                && TowerLegends_CoopMusic(TowerLegends_EncodeDuo(37, TRUE)) == gTowerLegendsFloors[36].battleMusic);
    TowerLegends_EndCoop();
    ExpectTower("after the Duo battle the hook answers nothing again", TowerLegends_GetTrainer(TRAINER_TOWER_A) == NULL);

    // 3. The Duo code: the ladder layout with family 1, refused by the ladder's own checks otherwise.
    ExpectTower("Duo codes encode and decode", TowerLegends_IsValidCode(TowerLegends_EncodeDuo(1, TRUE))
                && TowerLegends_IsValidCode(TowerLegends_EncodeDuo(100, FALSE)) && TowerLegends_CodeFloor(TowerLegends_EncodeDuo(64, TRUE)) == 64
                && MultiplayerLadder_ExpAllowed(TowerLegends_EncodeDuo(5, TRUE)) && !MultiplayerLadder_ExpAllowed(TowerLegends_EncodeDuo(5, FALSE)));
    ExpectTower("bad Duo codes are refused", !TowerLegends_IsValidCode(TowerLegends_EncodeDuo(0, TRUE))
                && !TowerLegends_IsValidCode(TowerLegends_EncodeDuo(101, TRUE)) && !TowerLegends_IsValidCode(TowerLegends_EncodeDuo(9, TRUE) ^ 1)
                && !TowerLegends_IsValidCode(TowerLegends_EncodeDuo(9, TRUE) | (5u << 10)) && !TowerLegends_IsValidCode(MultiplayerLadder_Encode(TRAINER_WALLACE, TRAINER_STEVEN, 30, TRUE)));
    ExpectTower("the ladder takes a Tower code as a valid co-op battle of the Tower's two trainers",
                MultiplayerLadder_IsValid(TowerLegends_EncodeDuo(44, TRUE)) && MultiplayerLadder_TrainerA(TowerLegends_EncodeDuo(44, TRUE)) == TRAINER_TOWER_A
                && MultiplayerLadder_TrainerB(TowerLegends_EncodeDuo(44, TRUE)) == TRAINER_TOWER_B
                && !MultiplayerLadder_IsValid(TowerLegends_EncodeDuo(44, TRUE) | (1u << 8))
                && MultiplayerLadder_IsValid(MultiplayerLadder_Encode(TRAINER_WALLACE, TRAINER_STEVEN, 30, TRUE)));

    // 4. The experience cap: this game's badges, a Champion 100; nothing at or above it, and stopping exactly at it.
    for (i = 0; i <= NUM_BADGES; i++)
    {
        SetBadges(i, FALSE);
        ExpectTower("the cap of 0-8 badges is 15/20/25/30/40/45/50/55/60", TowerLegends_ExpCap() == sCaps[i]);
    }
    SetBadges(NUM_BADGES, TRUE);
    ExpectTower("a Champion's cap is 100", TowerLegends_ExpCap() == MAX_LEVEL);
    mon = &gParties[B_TRAINER_PLAYER][0];
    CreateMon(mon, SPECIES_ZIGZAGOON, 14, 0x1234, OTID_STRUCT_PLAYER_ID);
    {
        u32 exp = GetMonData(mon, MON_DATA_EXP), capExp = gExperienceTables[gSpeciesInfo[SPECIES_ZIGZAGOON].growthRate][15];

        ExpectTower("below the cap the award stops exactly at the cap's level", TowerLegends_ClampExp(mon, 100000, 15) == capExp - exp
                    && TowerLegends_ClampExp(mon, 3, 15) == 3);
    }
    CreateMon(mon, SPECIES_ZIGZAGOON, 15, 0x1234, OTID_STRUCT_PLAYER_ID);
    ExpectTower("at the cap nothing", TowerLegends_ClampExp(mon, 500, 15) == 0);
    CreateMon(mon, SPECIES_ZIGZAGOON, 40, 0x1234, OTID_STRUCT_PLAYER_ID);
    ExpectTower("above the cap nothing (and the level is kept)", TowerLegends_ClampExp(mon, 500, 15) == 0 && GetMonData(mon, MON_DATA_LEVEL) == 40);
    ZeroPlayerPartyMons();

    // 5. The record and the host's words.
    ExpectTower("an older save has no record: floor 1, nothing claimed", !TowerLegends_RecordValid() && TowerLegends_Progress() == 1
                && TowerLegends_Highest() == 0 && TowerLegends_Claims() == 0);
    amount = MultiplayerWorld_ApplyRewardWord((s32)TowerWord(MP_REWARD_TOWER_STATE, (12 << 15) | (13 << 8)), &result);
    ExpectTower("PROGRESS sets H and P and starts the record (receipt = revision 1)", amount == 1 && TowerLegends_RecordValid()
                && TowerLegends_Highest() == 12 && TowerLegends_Progress() == 13);
    gSaveBlock3Ptr->tower.pendingOutcome = TOWER_RESULT_LOST;
    gSaveBlock3Ptr->tower.pendingTicket = 0x0000A5C3;
    gSaveBlock3Ptr->tower.pendingFloor = 13;
    gSaveBlock3Ptr->tower.crc32 = 0; // a hand-made change breaks the CRC: the record reads as damaged
    ExpectTower("a damaged record is not trusted", !TowerLegends_RecordValid() && TowerLegends_Progress() == 1);
    memset(&gSaveBlock3Ptr->tower, 0, sizeof(gSaveBlock3Ptr->tower));
    MultiplayerWorld_ApplyRewardWord((s32)TowerWord(MP_REWARD_TOWER_STATE, (12 << 15) | (13 << 8)), &result);
    TowerLegends_TestSetPending(TOWER_RESULT_LOST, 0x0000A5C3, 13);
    ExpectTower("an unsettled result is kept", TowerLegends_HasPendingResult());
    MultiplayerWorld_ApplyRewardWord((s32)TowerWord(MP_REWARD_TOWER_STATE, (12 << 15) | (11 << 8) | 0x99), &result);
    ExpectTower("a PROGRESS for another ticket leaves it unsettled", TowerLegends_HasPendingResult() && TowerLegends_Progress() == 11);
    revision = gSaveBlock3Ptr->tower.revision;
    amount = MultiplayerWorld_ApplyRewardWord((s32)TowerWord(MP_REWARD_TOWER_STATE, (12 << 15) | (11 << 8) | 0xC3), &result);
    ExpectTower("the settling PROGRESS clears it; the receipt is the new revision", !TowerLegends_HasPendingResult()
                && amount == revision + 1 && gSaveBlock3Ptr->tower.revision == revision + 1);
    MultiplayerWorld_ApplyRewardWord((s32)TowerWord(MP_REWARD_TOWER_STATE, (1u << 22) | 0x0005), &result);
    MultiplayerWorld_ApplyRewardWord((s32)TowerWord(MP_REWARD_TOWER_STATE, (1u << 22) | 0x1002), &result);
    ExpectTower("CLAIMS are OR-ed and never cleared", TowerLegends_Claims() == 0x1007);
    ok = TRUE;
    {
        u32 bad[] = {(101 << 15) | (13 << 8), (12 << 15) | (0 << 8), (12 << 15) | (102 << 8), (1u << 22) | (1 << 16), (2u << 22), (3u << 22)};
        for (i = 0; i < ARRAY_COUNT(bad); i++)
            if (MultiplayerWorld_ApplyRewardWord((s32)TowerWord(MP_REWARD_TOWER_STATE, bad[i]), &result) != 0)
                ok = FALSE;
    }
    ExpectTower("malformed Tower words change nothing (receipt 0)", ok && TowerLegends_Claims() == 0x1007 && TowerLegends_Progress() == 11);

    // 6. The milestone Pokemon: made from the word and the trainer's ID, the same each time; party, then PC.
    gSaveBlock3Ptr->tower.claims = 0; gSaveBlock3Ptr->tower.crc32 = 0; // a fresh record below
    memset(&gSaveBlock3Ptr->tower, 0, sizeof(gSaveBlock3Ptr->tower));
    amount = MultiplayerWorld_ApplyRewardWord((s32)TowerWord(MP_REWARD_TOWER_MON, (0u << 22) | (SPECIES_ARCANINE << 11) | (25 << 4)), &result);
    mon = &gParties[B_TRAINER_PLAYER][0];
    personality = GetMonData(mon, MON_DATA_PERSONALITY);
    ivs = GetMonData(mon, MON_DATA_HP_IV) | GetMonData(mon, MON_DATA_ATK_IV) << 5 | GetMonData(mon, MON_DATA_SPEED_IV) << 10;
    ExpectTower("floor 25's prize: in the party, the species, level 25, shiny, the player's, a Cherish Ball, full HP",
                amount == 1 && GetMonData(mon, MON_DATA_SPECIES) == SPECIES_ARCANINE && GetMonData(mon, MON_DATA_LEVEL) == 25
                && GetMonData(mon, MON_DATA_IS_SHINY) && GetMonData(mon, MON_DATA_OT_ID) == READ_OTID_FROM_SAVE
                && GetMonData(mon, MON_DATA_POKEBALL) == BALL_CHERISH && GetMonData(mon, MON_DATA_HP) == GetMonData(mon, MON_DATA_MAX_HP)
                && GetMonData(mon, MON_DATA_MOVE1) != MOVE_NONE);
    ExpectTower("the save's record marks it delivered", (TowerLegends_Claims() >> TOWER_CLAIM_MON_SHIFT) == 1);
    ZeroPlayerPartyMons();
    amount = MultiplayerWorld_ApplyRewardWord((s32)TowerWord(MP_REWARD_TOWER_MON, (0u << 22) | (SPECIES_ARCANINE << 11) | (25 << 4)), &result);
    ExpectTower("the same word again answers 2 and makes nothing", amount == 2
                && GetMonData(mon, MON_DATA_SPECIES) == SPECIES_NONE);
    memset(&gSaveBlock3Ptr->tower, 0, sizeof(gSaveBlock3Ptr->tower)); // a save from before the delivery
    MultiplayerWorld_ApplyRewardWord((s32)TowerWord(MP_REWARD_TOWER_MON, (0u << 22) | (SPECIES_ARCANINE << 11) | (25 << 4)), &result);
    ExpectTower("from a save before the delivery, the very same Pokemon (no reroll)", GetMonData(mon, MON_DATA_PERSONALITY) == personality
                && (GetMonData(mon, MON_DATA_HP_IV) | GetMonData(mon, MON_DATA_ATK_IV) << 5 | GetMonData(mon, MON_DATA_SPEED_IV) << 10) == ivs);
    ZeroPlayerPartyMons();
    MultiplayerWorld_ApplyRewardWord((s32)TowerWord(MP_REWARD_TOWER_MON, (2u << 22) | (SPECIES_DRAGONITE << 11) | (75 << 4)), &result);
    ok = GetMonData(mon, MON_DATA_IS_SHINY);
    for (i = 0; i < NUM_STATS; i++)
        if (GetMonData(mon, MON_DATA_HP_IV + i) != MAX_PER_STAT_IVS)
            ok = FALSE;
    ExpectTower("floor 75's prize: shiny with every IV 31", ok && GetMonData(mon, MON_DATA_LEVEL) == 75);
    ZeroPlayerPartyMons();
    MultiplayerWorld_ApplyRewardWord((s32)TowerWord(MP_REWARD_TOWER_MON, (1u << 22) | (SPECIES_GARCHOMP << 11) | (50 << 4)), &result);
    ok = TRUE;
    for (i = 0; i < NUM_STATS; i++)
        if (GetMonData(mon, MON_DATA_HP_IV + i) != MAX_PER_STAT_IVS)
            ok = FALSE;
    ExpectTower("floor 50's prize: every IV 31, shiny only by the odds", ok && GetMonData(mon, MON_DATA_IS_SHINY)
                == (GET_SHINY_VALUE(READ_OTID_FROM_SAVE, GetMonData(mon, MON_DATA_PERSONALITY)) < MP_GEN6_SHINY_ODDS));
    MultiplayerWorld_ApplyRewardWord((s32)TowerWord(MP_REWARD_TOWER_STATE, (1u << 22) | (1u << (TOWER_CLAIM_MON_SHIFT + 3))), &result);
    ExpectTower("a delivered milestone answers 2 and makes nothing", MultiplayerWorld_ApplyRewardWord(
                (s32)TowerWord(MP_REWARD_TOWER_MON, (3u << 22) | (SPECIES_RAYQUAZA << 11) | (70 << 4)), &result) == 2
                && GetMonData(&gParties[B_TRAINER_PLAYER][1], MON_DATA_SPECIES) == SPECIES_NONE);
    for (i = 0; i < PARTY_SIZE; i++)
        CreateMon(&gParties[B_TRAINER_PLAYER][i], SPECIES_ZIGZAGOON, 5, i, OTID_STRUCT_PLAYER_ID);
    CalculatePlayerPartyCount();
    memset(&gSaveBlock3Ptr->tower, 0, sizeof(gSaveBlock3Ptr->tower));
    amount = MultiplayerWorld_ApplyRewardWord((s32)TowerWord(MP_REWARD_TOWER_MON, (0u << 22) | (SPECIES_LAPRAS << 11) | (25 << 4)), &result);
    ExpectTower("a full party sends it to the PC", amount == 1 && GetBoxMonDataAt(0, 0, MON_DATA_SPECIES) == SPECIES_LAPRAS);
    ok = TRUE;
    {
        u32 bad[] = {(0u << 22) | (SPECIES_LAPRAS << 11) | (25 << 4) | 1, (0u << 22) | (0 << 11) | (25 << 4),
                     (0u << 22) | (SPECIES_LAPRAS << 11) | (0 << 4), (0u << 22) | (SPECIES_LAPRAS << 11) | (101 << 4)};
        for (i = 0; i < ARRAY_COUNT(bad); i++)
            if (MultiplayerWorld_ApplyRewardWord((s32)TowerWord(MP_REWARD_TOWER_MON, bad[i]), &result) != 0)
                ok = FALSE;
    }
    ExpectTower("malformed milestone words make nothing", ok);

    // 7. The menu's command and style.
    memset(&command, 0, sizeof(command));
    command.magic = TOWER_COMMAND_MAGIC; command.ticket = 0x00012345; command.tableHash = TOWER_LEGENDS_TABLE_HASH;
    command.floor = 12; command.mode = TOWER_COMMAND_MODE_SOLO;
    ExpectTower("a solo command is valid", TowerLegends_ValidCommand(&command));
    command.ticket = 0x00012300;
    ExpectTower("a ticket whose low byte is zero is refused", !TowerLegends_ValidCommand(&command));
    command.ticket = 0x00012345; command.floor = 101;
    ExpectTower("floor 101 is refused", !TowerLegends_ValidCommand(&command));
    command.floor = 12; command.reserved[17] = 1;
    ExpectTower("a reserved byte is refused", !TowerLegends_ValidCommand(&command));
    ExpectTower("styles 0-8, arguments 0-100, none without a style", TowerLegends_ValidStyle(0, 0) && TowerLegends_ValidStyle(8, 100)
                && !TowerLegends_ValidStyle(9, 0) && !TowerLegends_ValidStyle(3, 101) && !TowerLegends_ValidStyle(0, 4));

    // 8. Held items after a solo floor: each Pokemon's own back once, none for a Shedinja the battle's evolution of a
    // Nincada made (same personality), none an evolution used (review of 2026-10-21: both came back as copies).
    {
        static const u16 sItems[] = {ITEM_ORAN_BERRY, ITEM_RAZOR_FANG, ITEM_LIGHT_BALL, ITEM_LEFTOVERS};
        static const u16 sSpecies[] = {SPECIES_NINCADA, SPECIES_GLIGAR, SPECIES_PIKACHU, SPECIES_ZIGZAGOON};
        enum Item none = ITEM_NONE;
        struct Pokemon swap;

        ZeroPlayerPartyMons();
        for (i = 0; i < ARRAY_COUNT(sSpecies); i++)
        {
            CreateMon(&gParties[B_TRAINER_PLAYER][i], sSpecies[i], 20, 0x100 + i, OTID_STRUCT_PLAYER_ID);
            SetMonData(&gParties[B_TRAINER_PLAYER][i], MON_DATA_HELD_ITEM, &sItems[i]);
        }
        CalculatePlayerPartyCount();
        TowerLegends_TestSaveHeldItems();
        // The battle: the Oran Berry eaten, the Light Ball knocked off, the Leftovers' holder switched to the lead.
        SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_HELD_ITEM, &none);
        SetMonData(&gParties[B_TRAINER_PLAYER][2], MON_DATA_HELD_ITEM, &none);
        swap = gParties[B_TRAINER_PLAYER][3]; gParties[B_TRAINER_PLAYER][3] = gParties[B_TRAINER_PLAYER][0];
        gParties[B_TRAINER_PLAYER][0] = swap;
        // After it, the evolutions as the evolution scene does them: Nincada (now slot 3) into Ninjask with a Shedinja
        // copy at the end, holding nothing; Gligar into Gliscor at night, the Razor Fang used.
        i = SPECIES_NINJASK;
        SetMonData(&gParties[B_TRAINER_PLAYER][3], MON_DATA_SPECIES, &i);
        CopyMon(&gParties[B_TRAINER_PLAYER][4], &gParties[B_TRAINER_PLAYER][3], sizeof(struct Pokemon));
        i = SPECIES_SHEDINJA;
        SetMonData(&gParties[B_TRAINER_PLAYER][4], MON_DATA_SPECIES, &i);
        SetMonData(&gParties[B_TRAINER_PLAYER][4], MON_DATA_HELD_ITEM, &none);
        i = SPECIES_GLISCOR;
        SetMonData(&gParties[B_TRAINER_PLAYER][1], MON_DATA_SPECIES, &i);
        SetMonData(&gParties[B_TRAINER_PLAYER][1], MON_DATA_HELD_ITEM, &none);
        CalculatePlayerPartyCount();
        TowerLegends_TestRestoreHeldItems();
        ExpectTower("held items: the Zigzagoon moved to the lead keeps its Leftovers",
                    GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_HELD_ITEM) == ITEM_LEFTOVERS);
        ExpectTower("held items: the eaten Berry and the knocked-off Light Ball come back to their holders",
                    GetMonData(&gParties[B_TRAINER_PLAYER][3], MON_DATA_HELD_ITEM) == ITEM_ORAN_BERRY
                    && GetMonData(&gParties[B_TRAINER_PLAYER][2], MON_DATA_HELD_ITEM) == ITEM_LIGHT_BALL);
        ExpectTower("held items: the new Shedinja (the Nincada's personality) holds nothing",
                    GetMonData(&gParties[B_TRAINER_PLAYER][4], MON_DATA_HELD_ITEM) == ITEM_NONE);
        ExpectTower("held items: the Razor Fang Gliscor's evolution used stays used",
                    GetMonData(&gParties[B_TRAINER_PLAYER][1], MON_DATA_HELD_ITEM) == ITEM_NONE);
        ZeroPlayerPartyMons();
    }

    printf("selftest-tower: %d checks, %d failed\n", sTowerChecks, sTowerFailures);
    return sTowerFailures ? 1 : 0;
}
