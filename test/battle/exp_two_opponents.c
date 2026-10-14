#include "global.h"
#include "test/battle.h"

// KNOWN_ISSUES G14 (2026-10-13 audit): givenExpMons has two entries but is
// indexed with GetBattlerTrainer(battler) & BIT_FLANK, which is 2 for the
// second opposing trainer (B_TRAINER_OPPONENT_B = 3): that reads and writes
// expSentInMons. Each opposing Pokemon gives experience once, so the order in
// which the two trainers' Pokemon faint must not change the total. (AI tests:
// the test runner's scripted opponents are recorded battles, which give none.)
AI_ONE_VS_TWO_BATTLE_TEST("Two opponents: both trainers' Pokemon give experience in either fainting order", s32 exp)
{
    u32 speedA = 0, speedB = 0;
    PARAMETRIZE { speedA = 1; speedB = 2; } // the second trainer's Pokemon faints first (upstream's test)
    PARAMETRIZE { speedA = 2; speedB = 1; } // the first trainer's Pokemon faints first
    GIVEN {
        WITH_CONFIG(B_SCALED_EXP, GEN_3);
        WITH_CONFIG(B_UNEVOLVED_EXP_MULTIPLIER, GEN_3);
        PLAYER(SPECIES_METAPOD) { Level(1); Speed(3); }
        PLAYER(SPECIES_WOBBUFFET) { Level(100); Speed(3); }
        OPPONENT_B(SPECIES_WYNAUT) { Moves(MOVE_MEMENTO); Speed(speedB); }
        OPPONENT_B(SPECIES_WYNAUT) { Moves(MOVE_CELEBRATE); Speed(1); }
        OPPONENT_A(SPECIES_WOBBUFFET) { Moves(MOVE_MEMENTO); Speed(speedA); }
        OPPONENT_A(SPECIES_WOBBUFFET) { Moves(MOVE_CELEBRATE); Speed(1); }
    } WHEN {
        TURN { }
    } THEN {
        results[i].exp = GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_EXP);
    } FINALLY {
        EXPECT_EQ(results[0].exp, results[1].exp);
    }
}
