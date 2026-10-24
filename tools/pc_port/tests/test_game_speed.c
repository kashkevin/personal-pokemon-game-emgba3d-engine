// Battle speed (include/platform/game_speed.h): the coming game frames' speed, the real frames the multiplayer timeouts
// count, and the sound card fed one frame of sound per real frame, cross-faded, at every speed.
#include <math.h>
#include <stdlib.h>

#include "minitest.h"
#include "platform/game_speed.h"

#define FRAME 1402   // one game frame of stereo sound at 42060 Hz (sdl2.c), in floats

static void Fill(float *frame, int index)
{
    for (int i = 0; i < FRAME; i++)
        frame[i] = (float)index + (float)i / FRAME;   // each frame's samples rise from its index to the next
}

MT_TEST(normal_speed_changes_nothing)
{
    struct GameSpeed s;
    float in[FRAME], out[FRAME];

    GameSpeed_Reset(&s);
    for (int f = 0; f < 10; f++)
    {
        MT_EQ(GameSpeed_Frame(&s, 1), 1);
        MT_CHECK(GameSpeed_RealFrame(&s));
        Fill(in, f);
        MT_EQ(GameSpeed_Sound(&s, in, FRAME, out), FRAME);
        MT_CHECK(memcmp(in, out, sizeof(in)) == 0);
    }
}

MT_TEST(one_real_frame_in_speed_and_it_comes_first)
{
    struct GameSpeed s;

    for (unsigned speed = 2; speed <= GAME_SPEED_MAX; speed++)
    {
        int real = 0;

        GameSpeed_Reset(&s);
        MT_EQ(GameSpeed_Frame(&s, speed), speed);
        MT_CHECK(GameSpeed_RealFrame(&s));              // the first sped-up frame ends a real frame
        for (int f = 1; f < 120 * (int)speed; f++)
        {
            GameSpeed_Frame(&s, speed);
            real += GameSpeed_RealFrame(&s);
        }
        MT_EQ(real + 1, 120);                           // 120 real frames: two seconds of timeouts at any speed
        MT_EQ(GameSpeed_Frame(&s, 1), 1);                // back to normal: every frame is real again
        MT_CHECK(GameSpeed_RealFrame(&s));
    }
}

MT_TEST(the_wanted_speed_is_clamped)
{
    struct GameSpeed s;

    GameSpeed_Reset(&s);
    MT_EQ(GameSpeed_Frame(&s, 0), 1);
    MT_EQ(GameSpeed_Frame(&s, 99), GAME_SPEED_MAX);
}

MT_TEST(the_sound_card_gets_one_frame_a_real_frame)
{
    struct GameSpeed s;
    float in[FRAME], out[FRAME];

    for (unsigned speed = 2; speed <= GAME_SPEED_MAX; speed++)
    {
        size_t sent = 0;

        GameSpeed_Reset(&s);
        for (int f = 0; f < 60 * (int)speed; f++)
        {
            GameSpeed_Frame(&s, speed);
            Fill(in, f);
            size_t n = GameSpeed_Sound(&s, in, FRAME, out);
            MT_CHECK(n == 0 || n == FRAME);
            MT_EQ(n == FRAME, GameSpeed_RealFrame(&s));
            sent += n;
        }
        MT_EQ(sent, 60 * FRAME);                        // one second of sound for one second of real time
    }
}

MT_TEST(a_played_frame_fades_in_from_the_sound_after_the_one_before)
{
    struct GameSpeed s;
    float in[FRAME], out[FRAME];
    size_t fade = FRAME / 4 & ~(size_t)1;

    GameSpeed_Reset(&s);
    GameSpeed_Frame(&s, 4);
    Fill(in, 0);
    MT_EQ(GameSpeed_Sound(&s, in, FRAME, out), FRAME);
    MT_CHECK(memcmp(in, out, sizeof(in)) == 0);         // the first frame has nothing to fade from
    for (int f = 1; f < 4; f++)
    {
        GameSpeed_Frame(&s, 4);
        Fill(in, f);
        MT_EQ(GameSpeed_Sound(&s, in, FRAME, out), 0);
    }
    GameSpeed_Frame(&s, 4);
    Fill(in, 4);
    MT_EQ(GameSpeed_Sound(&s, in, FRAME, out), FRAME);
    // It starts next to where the frame played before ended (frame 1's start, about 1.0), ends as frame 4 (4.x), and
    // never jumps between two samples by more than the fade's share of the gap.
    MT_CHECK(fabsf(out[0] - 1.0f) < 0.05f);
    MT_CHECK(out[fade] == in[fade]);
    MT_CHECK(out[FRAME - 1] == in[FRAME - 1]);
    for (size_t i = 1; i < fade; i++)
        MT_CHECK(fabsf(out[i] - out[i - 1]) < 0.03f);
    // Changing speed starts over: nothing to fade from.
    GameSpeed_Frame(&s, 2);
    Fill(in, 9);
    MT_EQ(GameSpeed_Sound(&s, in, FRAME, out), FRAME);
    MT_CHECK(memcmp(in, out, sizeof(in)) == 0);
}

MT_TEST(a_small_sound_buffer_fades_whole_pairs_within_what_is_kept)
{
    struct GameSpeed s;
    float in[8192], out[8192];

    GameSpeed_Reset(&s);
    for (int f = 0; f < 6; f++)
    {
        GameSpeed_Frame(&s, 2);
        for (int i = 0; i < 8192; i++)
            in[i] = (float)f;
        size_t n = GameSpeed_Sound(&s, in, (f % 3) == 0 ? 6 : 8192, out);   // sizes change: never past what is kept
        MT_CHECK(n == 0 || n == 6 || n == 8192);
    }
}

int main(void)
{
    MT_RUN(normal_speed_changes_nothing);
    MT_RUN(one_real_frame_in_speed_and_it_comes_first);
    MT_RUN(the_wanted_speed_is_clamped);
    MT_RUN(the_sound_card_gets_one_frame_a_real_frame);
    MT_RUN(a_played_frame_fades_in_from_the_sound_after_the_one_before);
    MT_RUN(a_small_sound_buffer_fades_whole_pairs_within_what_is_kept);
    return MT_REPORT();
}
