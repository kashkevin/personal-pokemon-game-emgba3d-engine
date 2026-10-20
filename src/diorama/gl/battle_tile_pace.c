/* The step between a 3D battle's tile redraws (battle_tile_pace.h). */
#include "diorama/battle_tile_pace.h"

void TilePace_Begin(struct TilePace *pace, int mode)
{
    pace->mode = mode;
    pace->step = 0;
    pace->goodRedraws = 0;
    pace->probeEvery = TILE_PACE_PROBE_FIRST;
    pace->pace = 0.0f;
}

static void SetStep(struct TilePace *pace, unsigned step)
{
    pace->step = step;
    pace->goodRedraws = 0;
    pace->pace = 0.0f;               /* a fresh measurement for the new step */
    pace->probeEvery = TILE_PACE_PROBE_FIRST;
}

void TilePace_Decide(struct TilePace *pace, float glideAverage, uint32_t count, float allowance)
{
    if (pace->mode == 1)
        SetStep(pace, 1);
    else if (pace->mode == 0)
        SetStep(pace, 0);
    else if (count == 0)
        SetStep(pace, 16);   /* no glide (3D chosen mid-battle): try now and then */
    else if (glideAverage <= allowance * 1.1f)
        SetStep(pace, 1);
    else if (glideAverage <= allowance * 2.0f)
        SetStep(pace, 4);
    else if (glideAverage <= allowance * 4.0f)
        SetStep(pace, 16);
    else
        SetStep(pace, 0);
}

bool TilePace_Due(const struct TilePace *pace, uint32_t sinceRedraw)
{
    if (pace->mode == 0)
        return false;
    if (pace->step == 0)
        return pace->mode == -1 && sinceRedraw >= pace->probeEvery;
    return sinceRedraw >= pace->step;
}

bool TilePace_Redrawn(struct TilePace *pace, float ms, float allowance)
{
    if (pace->mode != -1)
        return false;
    if (pace->step == 0)
    {
        /* A try while the water stood still: back to the widest step if the
         * frame kept within the allowance, else a longer wait for the next try. */
        if (ms <= allowance * 1.25f)
        {
            SetStep(pace, TILE_PACE_STEP_MAX);
            return true;
        }
        pace->probeEvery = pace->probeEvery * 2 > TILE_PACE_PROBE_MAX ? TILE_PACE_PROBE_MAX : pace->probeEvery * 2;
        return false;
    }
    pace->pace = pace->pace > 0.0f ? pace->pace * 0.7f + ms * 0.3f : ms;
    if (pace->pace > allowance * 1.25f)
    {
        SetStep(pace, pace->step * 2 > TILE_PACE_STEP_MAX ? 0 : pace->step * 2);
        return true;
    }
    if (pace->pace <= allowance * 1.05f)
    {
        if (++pace->goodRedraws >= TILE_PACE_GOOD_REDRAWS && pace->step > 1)
        {
            SetStep(pace, pace->step / 2);
            return true;
        }
        return false;
    }
    pace->goodRedraws = 0;
    return false;
}
