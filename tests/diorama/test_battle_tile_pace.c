#include <assert.h>
#include <stdio.h>

#include "diorama/battle_tile_pace.h"

#define BUDGET (1000.0f / 60.0f)

static unsigned Redraws(struct TilePace *pace, unsigned count, float ms)
{
    unsigned changes = 0;

    for (unsigned i = 0; i < count; i++)
        changes += TilePace_Redrawn(pace, ms, BUDGET);
    return changes;
}

int main(void)
{
    struct TilePace pace;

    /* A quick PC: the glide's frames keep within the budget; every change redraws. */
    TilePace_Begin(&pace, -1);
    TilePace_Decide(&pace, 15.0f, 90, BUDGET);
    assert(pace.step == 1);
    assert(!TilePace_Due(&pace, 0) && TilePace_Due(&pace, 1));

    /* The owner's report: a few slow moments in a battle (saves, the bridge,
     * a window drag) used to stop the water for the rest of the battle. They
     * may stop it for a while; it comes back once frames are quick again. */
    unsigned changes = 0;
    while (pace.step != 0 && changes < 10)
        changes += TilePace_Redrawn(&pace, 120.0f, BUDGET);
    assert(pace.step == 0 && changes == 6);              /* 1, 2, 4, 8, 16, 32, then still */
    assert(!TilePace_Due(&pace, TILE_PACE_PROBE_FIRST - 1));
    assert(TilePace_Due(&pace, TILE_PACE_PROBE_FIRST));
    assert(TilePace_Redrawn(&pace, 15.0f, BUDGET));          /* the try kept within: moving again */
    assert(pace.step == TILE_PACE_STEP_MAX);
    for (unsigned want = TILE_PACE_STEP_MAX / 2; want >= 1; want /= 2)
    {
        assert(Redraws(&pace, TILE_PACE_GOOD_REDRAWS, 15.0f) == 1);
        assert(pace.step == want);
        if (want == 1)
            break;
    }
    assert(Redraws(&pace, 50, 15.0f) == 0 && pace.step == 1);

    /* One slow frame counts once: it spaces the redraws once, quick frames
     * after it bring them back. */
    assert(TilePace_Redrawn(&pace, 200.0f, BUDGET) && pace.step == 2);
    assert(Redraws(&pace, TILE_PACE_GOOD_REDRAWS - 1, 15.0f) == 0 && pace.step == 2);
    assert(Redraws(&pace, 1, 15.0f) == 1 && pace.step == 1);

    /* Frames a little over the budget leave the step as it is. */
    assert(Redraws(&pace, 40, BUDGET * 1.15f) == 0 && pace.step == 1);

    /* A slow PC (software graphics): the water stands still and is tried less
     * and less often, never more than once a minute after a while. */
    TilePace_Begin(&pace, -1);
    TilePace_Decide(&pace, 120.0f, 90, BUDGET);
    assert(pace.step == 0);
    for (unsigned wait = TILE_PACE_PROBE_FIRST; wait < TILE_PACE_PROBE_MAX; wait *= 2)
    {
        assert(!TilePace_Due(&pace, wait - 1) && TilePace_Due(&pace, wait));
        assert(!TilePace_Redrawn(&pace, 110.0f, BUDGET));
    }
    assert(pace.probeEvery == TILE_PACE_PROBE_MAX);
    assert(!TilePace_Redrawn(&pace, 110.0f, BUDGET) && pace.probeEvery == TILE_PACE_PROBE_MAX);

    /* A glide between two and four times the budget: every 16 game frames;
     * none measured (3D chosen in the battle): every 16 too. */
    TilePace_Begin(&pace, -1);
    TilePace_Decide(&pace, BUDGET * 3.0f, 90, BUDGET);
    assert(pace.step == 16);
    TilePace_Begin(&pace, -1);
    TilePace_Decide(&pace, 0.0f, 0, BUDGET);
    assert(pace.step == 16);

    /* The test switches: PKMN_DIORAMA_BATTLE_TILES=0 never, =1 always, whatever the times. */
    TilePace_Begin(&pace, 0);
    TilePace_Decide(&pace, 10.0f, 90, BUDGET);
    assert(pace.step == 0 && !TilePace_Due(&pace, 100000) && !TilePace_Redrawn(&pace, 10.0f, BUDGET));
    TilePace_Begin(&pace, 1);
    TilePace_Decide(&pace, 500.0f, 90, BUDGET);
    assert(pace.step == 1 && TilePace_Due(&pace, 1));
    assert(Redraws(&pace, 20, 500.0f) == 0 && pace.step == 1);

    puts("diorama battle tile pace tests passed");
    return 0;
}
