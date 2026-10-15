#include "global.h"
#include "battle.h"
#include "battle_ffa.h"
#include "battle_controllers.h"
#include "battle_util.h"
#include "pokemon.h"
#include "random.h"

#if MP_FFA_ENABLED

static EWRAM_DATA u8 sWinnerPlusOne = 0; // 0: no winner (draw, or not decided)

bool32 BattlerHasAlly(enum BattlerId battler)
{
    if (IsFreeForAllBattle())
        return FALSE;
    return IsDoubleBattle();
}

enum BattlerId GetOtherBattler(enum BattlerId battler, u32 n)
{
    for (enum BattlerId other = 0; other < gBattlersCount; other++)
    {
        if (other == battler)
            continue;
        if (n-- == 0)
            return other;
    }
    return gBattlersCount;
}

u32 CountAliveFoes(enum BattlerId battler)
{
    u32 count = 0;

    for (enum BattlerId other = 0; other < gBattlersCount; other++)
    {
        if (!IsBattlerAlly(battler, other) && IsBattlerAlive(other))
            count++;
    }
    return count;
}

enum BattlerId PickRandomAliveFoe(enum BattlerId battler)
{
    u32 count = CountAliveFoes(battler);

    if (count == 0)
        return battler;

    u32 pick = RandomUniform(RNG_RANDOM_TARGET, 0, count - 1);
    for (enum BattlerId other = 0; other < gBattlersCount; other++)
    {
        if (IsBattlerAlly(battler, other) || !IsBattlerAlive(other))
            continue;
        if (pick-- == 0)
            return other;
    }
    return battler;
}

bool32 FFA_IsEliminated(enum BattlerId battler)
{
    struct Pokemon *party = GetBattlerParty(battler);

    for (u32 i = 0; i < PARTY_SIZE; i++)
    {
        if (GetMonData(&party[i], MON_DATA_SPECIES)
         && GetMonData(&party[i], MON_DATA_HP) > 0
         && !GetMonData(&party[i], MON_DATA_IS_EGG))
            return FALSE;
    }
    return TRUE;
}

u32 FFA_CountTrainersLeft(void)
{
    u32 left = 0;

    for (enum BattlerId battler = 0; battler < gBattlersCount; battler++)
    {
        if (!FFA_IsEliminated(battler))
            left++;
    }
    return left;
}

enum BattlerId FFA_GetSoleSurvivor(void)
{
    enum BattlerId survivor = gBattlersCount;

    for (enum BattlerId battler = 0; battler < gBattlersCount; battler++)
    {
        if (FFA_IsEliminated(battler))
            continue;
        if (survivor != gBattlersCount)
            return gBattlersCount;
        survivor = battler;
    }
    return survivor;
}

enum BattlerId FFA_GetLocalBattler(void)
{
    for (enum BattlerId battler = 0; battler < gBattlersCount; battler++)
    {
        if (gBattlerBattleController[battler] == BATTLE_CONTROLLER_PLAYER
         || gBattlerBattleController[battler] == BATTLE_CONTROLLER_RECORDED_PLAYER)
            return battler;
    }
    return B_BATTLER_0;
}

u8 FFA_GetWinnerByte(void)
{
    return sWinnerPlusOne == 0 ? 0xFF : sWinnerPlusOne - 1;
}

u8 FFA_RecordWinner(enum BattlerId winner)
{
    sWinnerPlusOne = winner < gBattlersCount ? winner + 1 : 0;
    return FFA_GetWinnerByte();
}

u32 FFA_GetOutcomeForWinner(u32 winnerByte)
{
    if (winnerByte >= gBattlersCount)
        return B_OUTCOME_DREW;
    return winnerByte == FFA_GetLocalBattler() ? B_OUTCOME_WON : B_OUTCOME_LOST;
}

#endif // MP_FFA_ENABLED
