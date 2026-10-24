/*
 * Battle speed: see include/platform/game_speed.h. Project-owned.
 */
#include <string.h>

#include "platform/game_speed.h"

void GameSpeed_Reset(struct GameSpeed *s)
{
    s->speed = 1;
    s->subFrame = 0;
    s->tailHeld = false;
    s->fadeCount = 0;
}

unsigned GameSpeed_Frame(struct GameSpeed *s, unsigned wanted)
{
    if (wanted < 1)
        wanted = 1;
    if (wanted > GAME_SPEED_MAX)
        wanted = GAME_SPEED_MAX;
    if (wanted != s->speed)
    {
        // A new speed starts on a real frame, with nothing to fade from.
        s->speed = wanted;
        s->subFrame = 0;
        s->tailHeld = false;
    }
    else
    {
        s->subFrame = (s->subFrame + 1) % s->speed;
    }
    return s->speed;
}

bool GameSpeed_RealFrame(const struct GameSpeed *s)
{
    return s->subFrame == 0;
}

size_t GameSpeed_Sound(struct GameSpeed *s, const float *in, size_t count, float *out)
{
    size_t fade = count / 4;

    if (fade > GAME_SPEED_FADE_MAX)
        fade = GAME_SPEED_FADE_MAX;
    fade &= ~(size_t)1;   // whole left/right pairs
    if (s->speed == 1)
    {
        memcpy(out, in, count * sizeof(float));
        return count;
    }
    if (s->subFrame == 0)
    {
        // The frame played: its start fades in over the sound that followed the frame played before.
        memcpy(out, in, count * sizeof(float));
        if (s->tailHeld)
        {
            size_t n = s->fadeCount < fade ? s->fadeCount : fade;

            for (size_t i = 0; i < n; i++)
            {
                float w = (float)(i + 1) / (float)(n + 1);

                out[i] = s->fade[i] * (1.0f - w) + in[i] * w;
            }
        }
        s->tailHeld = false;
        return count;
    }
    if (s->subFrame == 1)
    {
        // The sound right after the frame played: kept for the next one's fade.
        s->fadeCount = fade;
        memcpy(s->fade, in, fade * sizeof(float));
        s->tailHeld = true;
    }
    return 0;
}
