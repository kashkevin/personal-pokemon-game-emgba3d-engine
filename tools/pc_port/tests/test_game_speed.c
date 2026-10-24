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

// The window loop's timing (sdl2.c): each pass adds the time since the last present to the accumulator, runs the game
// frames it holds (dt: one game frame at the speed) and presents. Something counted at each frame boundary runs `speed`
// times a real frame in a battle between players; counted on the real frames only (Platform_RealFrame) it counts one
// frame a 1/60 s, each shown by a present before the next as at normal speed. The battle effects' pace (bfx_game.c,
// "frames the PC failed to show"), the native party screen's key repeat, the play time and the L=A hold count so.
struct Unseen
{
    unsigned frames, unseen;
};

static struct Unseen CountUnseen(unsigned speed, bool realOnly, double jitterMs)
{
    struct GameSpeed s;
    const double dt = 1.0 / 59.7275 / speed;
    double acc = 0.0;
    bool real = true, presented = true;
    struct Unseen u = {0, 0};
    unsigned seed = 12345;

    GameSpeed_Reset(&s);
    for (int pass = 0; pass < 6000; pass++)
    {
        seed = seed * 1103515245u + 12345u;   // a present a little early or late: -jitterMs..+jitterMs
        acc += 1.0 / 60.0 + jitterMs / 1000.0 * ((double)((seed >> 16) & 0x7FFF) / 0x7FFF * 2.0 - 1.0);
        while (acc >= dt)
        {
            if (real || !realOnly)            // the frame just run, at its boundary (VDraw)
            {
                u.frames++;
                u.unseen += !presented;
                presented = false;
            }
            GameSpeed_Frame(&s, speed);       // the coming frame's speed and place
            real = GameSpeed_RealFrame(&s);
            acc -= dt;
        }
        presented = true;                     // the present after the pass
    }
    return u;
}

MT_TEST(counted_on_real_frames_a_sped_up_battle_is_seen_as_at_normal_speed)
{
    struct Unseen normal = CountUnseen(1, false, 0.0);
    struct Unseen every = CountUnseen(4, false, 0.0);
    struct Unseen real = CountUnseen(4, true, 0.0);

    // steady 60 Hz presents: none unseen at normal speed, nor on the real frames at 4x (but the one where the speed
    // changed: the last normal frame and the first sped-up one end in the same pass); counting every game frame, about
    // three in four unseen
    MT_EQ(normal.unseen, 0);
    MT_CHECK(real.unseen <= 1);
    MT_CHECK(real.frames + 2 >= normal.frames && real.frames <= normal.frames + 2);
    MT_CHECK(every.unseen * 10 >= every.frames * 7);
    // presents a millisecond early or late: about as many real frames unseen at 4x as frames at normal speed
    normal = CountUnseen(1, false, 1.0);
    real = CountUnseen(4, true, 1.0);
    MT_CHECK(normal.unseen > 0);
    MT_CHECK(real.unseen * 2 <= normal.unseen * 3);
}

int main(void)
{
    MT_RUN(normal_speed_changes_nothing);
    MT_RUN(one_real_frame_in_speed_and_it_comes_first);
    MT_RUN(the_wanted_speed_is_clamped);
    MT_RUN(the_sound_card_gets_one_frame_a_real_frame);
    MT_RUN(a_played_frame_fades_in_from_the_sound_after_the_one_before);
    MT_RUN(a_small_sound_buffer_fades_whole_pairs_within_what_is_kept);
    MT_RUN(counted_on_real_frames_a_sped_up_battle_is_seen_as_at_normal_speed);
    return MT_REPORT();
}
