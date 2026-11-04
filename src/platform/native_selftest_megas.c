// Native PC build: self-test of two Mega Evolutions that did not work (KNOWN_ISSUES MS-39 and MS-38). Project-owned.
// Runs the game's own code on Pokemon and battle state built here, then exits.
//   --selftest-megas
//  - MS-39, Mega Rayquaza: Rayquaza learns Dragon Ascent at level 1 (as in the Gen 8 and Gen 9 learnsets), so the move
//    relearners' level-up list offers it to any Rayquaza; the Sky Pillar's level 70 Rayquaza still comes with its own
//    latest four moves; with Dragon Ascent known (and the Mega Ring) CanMegaEvolve says yes and the form-change table
//    gives Mega Rayquaza, and not without it;
//  - MS-38, Mega Lucario Z's Aura Guard: Lucario holding Lucarionite Z Mega Evolves into it and every one of its Ability
//    slots is Aura Guard; the battle's own damage calculation (CalculateMoveDamage, the same random roll and no critical
//    hit for both) halves the damage of moves that make contact, physical or special, and leaves the others as they
//    were; an attacker with Long Reach (no contact) does full damage; Mold Breaker ignores it (breakable).
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "battle.h"
#include "battle_util.h"
#include "battle_util2.h"
#include "item.h"
#include "load_save.h"
#include "malloc.h"
#include "move_relearner.h"
#include "pokemon.h"
#include "random.h"
#include "constants/abilities.h"
#include "constants/battle.h"
#include "constants/hold_effects.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "constants/species.h"

static int sChecks, sFailures;

static void Expect(const char *what, bool32 ok)
{
    sChecks++;
    if (!ok)
        sFailures++;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    fflush(stdout);
}

static bool32 Knows(struct Pokemon *mon, enum Move move)
{
    for (u32 i = 0; i < MAX_MON_MOVES; i++)
    {
        if (GetMonData(mon, MON_DATA_MOVE1 + i) == move)
            return TRUE;
    }
    return FALSE;
}

static void Setup(void)
{
    InitHeap(gHeap, HEAP_SIZE);
    SetSaveBlocksPointers(0);
    memset(gSaveBlock1Ptr, 0, sizeof(*gSaveBlock1Ptr));
    memset(gSaveBlock2Ptr, 0, sizeof(*gSaveBlock2Ptr));
    gSaveBlock2Ptr->playerTrainerId[0] = 0x39;
    gSaveBlock2Ptr->playerTrainerId[1] = 0x30;
    SetBagItemsPointers();
    ClearBag();
    ZeroPlayerPartyMons();
    ZeroEnemyPartyMons();
}

// A trainer battle, the player's Pokemon battler 0 and the opponent's battler 1 (each its party's first).
static void SinglesBattle(void)
{
    gBattleTypeFlags = BATTLE_TYPE_TRAINER;
    AllocateBattleResources();
    gMain.inBattle = TRUE;
    gBattlersCount = 2;
    gBattlerPositions[0] = B_POSITION_PLAYER_LEFT;
    gBattlerPositions[1] = B_POSITION_OPPONENT_LEFT;
    gAbsentBattlerFlags = 0;
    gBattlerPartyIndexes[0] = 0;
    gBattlerPartyIndexes[1] = 0;
}

static void EndBattle(void)
{
    gMain.inBattle = FALSE;
    FreeBattleResources();
    gBattleTypeFlags = 0;
}

static void MakeMon(struct Pokemon *mon, enum Species species, u32 level, u32 personality, u16 item)
{
    CreateMon(mon, species, level, personality, OTID_STRUCT_PLAYER_ID);
    SetMonData(mon, MON_DATA_HELD_ITEM, &item);
    CalculateMonStats(mon);
}

// ------------------------------------------------------------------------------------------------- MS-39: Rayquaza
static void CheckRayquaza(void)
{
    const struct LevelUpMove *learnset = GetSpeciesLevelUpLearnset(SPECIES_RAYQUAZA);
    struct Pokemon *rayquaza = &gParties[B_TRAINER_PLAYER][0];
    u16 moves[MAX_RELEARNER_MOVES];
    u32 count, i;
    bool32 atLevel1 = FALSE, listed = FALSE;
    char line[200];

    printf("MS-39: Mega Rayquaza\n");
    for (i = 0; learnset[i].move != LEVEL_UP_MOVE_END; i++)
    {
        if (learnset[i].move == MOVE_DRAGON_ASCENT && learnset[i].level == 1)
            atLevel1 = TRUE;
    }
    Expect("Rayquaza's level-up learnset has Dragon Ascent at level 1 (the Gen 8 and Gen 9 learnsets' level)", atLevel1);

    MakeMon(rayquaza, SPECIES_RAYQUAZA, 70, 0x00C0FFEE, ITEM_NONE);
    GiveMonInitialMoveset(rayquaza); // the moves every wild Pokemon is given
    snprintf(line, sizeof(line), "the Sky Pillar's level 70 Rayquaza comes with its latest four moves (%u %u %u %u), not "
             "Dragon Ascent", (unsigned)GetMonData(rayquaza, MON_DATA_MOVE1), (unsigned)GetMonData(rayquaza, MON_DATA_MOVE2),
             (unsigned)GetMonData(rayquaza, MON_DATA_MOVE3), (unsigned)GetMonData(rayquaza, MON_DATA_MOVE4));
    Expect(line, !Knows(rayquaza, MOVE_DRAGON_ASCENT) && GetMonData(rayquaza, MON_DATA_MOVE4) != MOVE_NONE);

    count = MoveRelearner_GetListMoves(&rayquaza->box, MOVE_RELEARNER_LEVEL_UP_MOVES, moves);
    for (i = 0; i < count; i++)
    {
        if (moves[i] == MOVE_DRAGON_ASCENT)
            listed = TRUE;
    }
    snprintf(line, sizeof(line), "the move relearners' level-up list offers it Dragon Ascent (%u moves listed)", (unsigned)count);
    Expect(line, listed);

    SinglesBattle();
    AddBagItem(ITEM_MEGA_RING, 1);
    PokemonToBattleMon(rayquaza, &gBattleMons[0]);
    Expect("in battle without Dragon Ascent (Mega Ring in the Bag): it cannot Mega Evolve", !CanMegaEvolve(0));
    SetMonMoveSlot(rayquaza, MOVE_DRAGON_ASCENT, 0); // relearned in place of its first move
    PokemonToBattleMon(rayquaza, &gBattleMons[0]);
    Expect("with Dragon Ascent: it can Mega Evolve", CanMegaEvolve(0));
    Expect("... into Mega Rayquaza (the form-change table's Dragon Ascent entry)",
           GetBattleFormChangeTargetSpecies(0, FORM_CHANGE_BATTLE_MEGA_EVOLUTION_MOVE, GetBattlerAbility(0))
               == SPECIES_RAYQUAZA_MEGA);
    ClearBag();
    Expect("without the Mega Ring: it cannot", !CanMegaEvolve(0));
    EndBattle();
}

// ----------------------------------------------------------------------------------- MS-38: Mega Lucario Z's Aura Guard
// The damage battler 0 deals battler 1 with `move` as the battle works it out (CalculateMoveDamage: type effectiveness,
// the random roll, every modifier), with the given Abilities. The roll is the same for every call (the same seed), and
// Lucky Chant on battler 1's side rules out a critical hit.
static s32 Damage(enum Move move, enum Ability attacker, enum Ability defender)
{
    struct DamageContext ctx = {
        .battlerAtk = 0,
        .battlerDef = 1,
        .move = move,
        .chosenMove = move,
        .baseMove = move,
        .moveType = GetBattleMoveType(move),
        .weather = GetWeather(),
        .randomFactor = TRUE,
        .updateFlags = FALSE,
    };

    gBattleMons[0].ability = attacker;
    gBattleMons[1].ability = defender;
    ctx.abilities[0] = attacker;
    ctx.abilities[1] = defender;
    ctx.holdEffects[0] = HOLD_EFFECT_NONE;
    ctx.holdEffects[1] = HOLD_EFFECT_NONE;
    gSideStatuses[B_SIDE_OPPONENT] |= SIDE_STATUS_LUCKY_CHANT;
    SeedRng(0x4D533338);
    return CalculateMoveDamage(&ctx);
}

static void ExpectDamage(const char *what, enum Move move, enum Ability attacker, bool32 halved)
{
    s32 plain = Damage(move, attacker, ABILITY_NONE), guarded = Damage(move, attacker, ABILITY_AURA_GUARD);
    char line[240];
    bool32 ok;

    if (halved) // the modifier is applied once, rounded half down: half of the plain damage, give or take one
        ok = plain >= 2 && 2 * guarded >= plain - 1 && 2 * guarded <= plain + 1;
    else
        ok = plain > 0 && guarded == plain;
    snprintf(line, sizeof(line), "%s: %d without Aura Guard, %d with it (%s)", what, (int)plain, (int)guarded,
             halved ? "halved" : "unchanged");
    Expect(line, ok);
}

static void CheckAuraGuard(void)
{
    struct Pokemon *lucario = &gParties[B_TRAINER_PLAYER][0];
    struct Pokemon *attacker = &gParties[B_TRAINER_PLAYER][0], *defender = &gParties[B_TRAINER_OPPONENT_A][0];
    const struct SpeciesInfo *info = &gSpeciesInfo[SPECIES_LUCARIO_MEGA_Z];

    printf("MS-38: Mega Lucario Z's Aura Guard\n");
    Expect("Aura Guard is one Mold Breaker ignores (breakable)", gAbilitiesInfo[ABILITY_AURA_GUARD].breakable);
    Expect("every one of Mega Lucario Z's Ability slots is Aura Guard", info->abilities[0] == ABILITY_AURA_GUARD
           && info->abilities[1] == ABILITY_AURA_GUARD && info->abilities[2] == ABILITY_AURA_GUARD);

    SinglesBattle();
    AddBagItem(ITEM_MEGA_RING, 1);
    MakeMon(lucario, SPECIES_LUCARIO, 50, 0x00C0FFEE, ITEM_LUCARIONITE_Z);
    PokemonToBattleMon(lucario, &gBattleMons[0]);
    Expect("Lucario holding Lucarionite Z can Mega Evolve", CanMegaEvolve(0));
    Expect("... into Mega Lucario Z",
           GetBattleFormChangeTargetSpecies(0, FORM_CHANGE_BATTLE_MEGA_EVOLUTION_ITEM, GetBattlerAbility(0))
               == SPECIES_LUCARIO_MEGA_Z);
    EndBattle();

    ZeroPlayerPartyMons();
    SinglesBattle();
    MakeMon(attacker, SPECIES_SNORLAX, 50, 0x00000010, ITEM_NONE);
    MakeMon(defender, SPECIES_LUCARIO_MEGA_Z, 50, 0x00000020, ITEM_NONE);
    PokemonToBattleMon(attacker, &gBattleMons[0]);
    PokemonToBattleMon(defender, &gBattleMons[1]);
    Expect("Mega Lucario Z in battle has Aura Guard", gBattleMons[1].ability == ABILITY_AURA_GUARD);
    ExpectDamage("Tackle (physical, contact)", MOVE_TACKLE, ABILITY_NONE, TRUE);
    ExpectDamage("Draining Kiss (special, contact)", MOVE_DRAINING_KISS, ABILITY_NONE, TRUE);
    ExpectDamage("Earthquake (physical, no contact)", MOVE_EARTHQUAKE, ABILITY_NONE, FALSE);
    ExpectDamage("Flamethrower (special, no contact)", MOVE_FLAMETHROWER, ABILITY_NONE, FALSE);
    ExpectDamage("Tackle from an attacker with Long Reach (no contact)", MOVE_TACKLE, ABILITY_LONG_REACH, FALSE);

    gBattleMons[1].ability = ABILITY_AURA_GUARD;
    gBattlerAttacker = 0;
    gBattleStruct->moldBreakerActive = FALSE;
    Expect("the battle reads Aura Guard for it", GetBattlerAbility(1) == ABILITY_AURA_GUARD);
    gBattleStruct->moldBreakerActive = TRUE;
    Expect("... and none while an attacker's Mold Breaker is at work (its damage as without Aura Guard)",
           GetBattlerAbility(1) == ABILITY_NONE);
    gBattleStruct->moldBreakerActive = FALSE;
    EndBattle();
}

int NativeSelfTest_Megas(void)
{
    Setup();
    printf("selftest-megas: Mega Rayquaza (MS-39) and Mega Lucario Z's Aura Guard (MS-38)\n");
    CheckRayquaza();
    CheckAuraGuard();
    printf("selftest-megas: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}
