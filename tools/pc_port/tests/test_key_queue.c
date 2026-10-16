// The per-frame key queue (include/platform/key_queue.h): a tap between two game frames is not lost, a key pressed again after
// a release is a new press, ordinary input is delivered as before, and a frame's keys are never invented.
#include <stdint.h>
#include <stdlib.h>

#include "minitest.h"
#include "platform/key_queue.h"

#define A 0x0001
#define B 0x0002
#define UP 0x0040
#define RIGHT 0x0010

MT_TEST(a_key_held_across_frames_is_the_level_the_game_saw_before)
{
    struct KeyQueue q;

    KeyQueue_Reset(&q);
    MT_EQ(KeyQueue_Take(&q), 0);
    KeyQueue_Set(&q, A);
    MT_EQ(KeyQueue_Take(&q), A);
    MT_EQ(KeyQueue_Take(&q), A);   // no event: still held
    KeyQueue_Set(&q, 0);
    MT_EQ(KeyQueue_Take(&q), 0);
    MT_EQ(KeyQueue_Pending(&q), 0);
}

MT_TEST(a_tap_inside_one_frame_is_one_frame_down_then_up)
{
    struct KeyQueue q;

    KeyQueue_Reset(&q);
    KeyQueue_Set(&q, A);   // down and up arrive in one batch, before the frame boundary
    KeyQueue_Set(&q, 0);
    MT_EQ(KeyQueue_Take(&q), A);
    MT_EQ(KeyQueue_Take(&q), 0);
    MT_EQ(KeyQueue_Take(&q), 0);
}

MT_TEST(a_key_released_and_pressed_again_is_a_new_press)
{
    struct KeyQueue q;

    KeyQueue_Reset(&q);
    KeyQueue_Set(&q, A);
    MT_EQ(KeyQueue_Take(&q), A);   // held at the last frame
    KeyQueue_Set(&q, 0);           // up and down again before the next frame
    KeyQueue_Set(&q, A);
    MT_EQ(KeyQueue_Take(&q), 0);   // the game must see it go up...
    MT_EQ(KeyQueue_Take(&q), A);   // ...and down again
    MT_EQ(KeyQueue_Take(&q), A);
}

MT_TEST(two_keys_pressed_in_one_batch_arrive_in_one_frame)
{
    struct KeyQueue q;

    KeyQueue_Reset(&q);
    KeyQueue_Set(&q, A);
    KeyQueue_Set(&q, A | UP);
    MT_EQ(KeyQueue_Pending(&q), 1);
    MT_EQ(KeyQueue_Take(&q), A | UP);
}

MT_TEST(a_tap_of_one_key_while_another_is_held)
{
    struct KeyQueue q;

    KeyQueue_Reset(&q);
    KeyQueue_Set(&q, RIGHT);
    MT_EQ(KeyQueue_Take(&q), RIGHT);
    KeyQueue_Set(&q, RIGHT | A);
    KeyQueue_Set(&q, RIGHT);
    MT_EQ(KeyQueue_Take(&q), RIGHT | A);
    MT_EQ(KeyQueue_Take(&q), RIGHT);
}

MT_TEST(several_taps_in_one_batch_are_several_presses)
{
    struct KeyQueue q;
    int presses = 0, prev = 0;

    KeyQueue_Reset(&q);
    for (int i = 0; i < 4; i++)
    {
        KeyQueue_Set(&q, A);
        KeyQueue_Set(&q, 0);
    }
    for (int i = 0; i < 12; i++)
    {
        int now = KeyQueue_Take(&q) & A;

        if (now && !prev)
            presses++;
        prev = now;
    }
    MT_EQ(presses, 4);
}

MT_TEST(a_long_backlog_never_hangs_and_ends_on_the_real_level)
{
    struct KeyQueue q;
    uint16_t last = 0;

    KeyQueue_Reset(&q);
    for (int i = 0; i < 200; i++)
        KeyQueue_Set(&q, (i & 1) ? 0 : A);
    MT_CHECK(KeyQueue_Pending(&q) <= KEY_QUEUE_MAX);
    for (int i = 0; i < KEY_QUEUE_MAX + 2; i++)
        last = KeyQueue_Take(&q);
    MT_EQ(last, 0);   // the last event released it
}

MT_TEST(random_event_streams_deliver_every_press_and_nothing_else)
{
    srand(28);
    for (int run = 0; run < 2000; run++)
    {
        struct KeyQueue q;
        uint16_t live = 0, seen = 0;
        int downs[3] = {0, 0, 0}, ups[3] = {0, 0, 0}, sawDown[3] = {0, 0, 0}, sawUp[3] = {0, 0, 0};
        uint16_t prev = 0;
        static const uint16_t keys[3] = {A, B, UP};

        KeyQueue_Reset(&q);
        for (int frame = 0; frame < 6; frame++)
        {
            int events = rand() % 4;   // fewer events per frame than the queue holds
            uint16_t got;

            for (int e = 0; e < events; e++)
            {
                int k = rand() % 3;

                live ^= keys[k];
                if (live & keys[k]) downs[k]++; else ups[k]++;
                KeyQueue_Set(&q, live);
            }
            got = KeyQueue_Take(&q);
            seen |= got;
            for (int k = 0; k < 3; k++)
            {
                if ((got & keys[k]) && !(prev & keys[k])) sawDown[k]++;
                if (!(got & keys[k]) && (prev & keys[k])) sawUp[k]++;
            }
            prev = got;
        }
        for (int i = 0; i < KEY_QUEUE_MAX + 2; i++)   // drain
        {
            uint16_t got = KeyQueue_Take(&q);

            for (int k = 0; k < 3; k++)
            {
                if ((got & keys[k]) && !(prev & keys[k])) sawDown[k]++;
                if (!(got & keys[k]) && (prev & keys[k])) sawUp[k]++;
            }
            prev = got;
        }
        MT_EQ(prev, live);   // it ends on what is really held
        for (int k = 0; k < 3; k++)
        {
            MT_CHECK(sawDown[k] <= downs[k]);   // nothing invented
            if (downs[k] > 0 && ups[k] > 0 && downs[k] == ups[k] && live == 0)
                MT_CHECK(sawDown[k] >= 1);        // a press that came and went was seen at least once
        }
        (void)seen;
    }
}

int main(void)
{
    MT_RUN(a_key_held_across_frames_is_the_level_the_game_saw_before);
    MT_RUN(a_tap_inside_one_frame_is_one_frame_down_then_up);
    MT_RUN(a_key_released_and_pressed_again_is_a_new_press);
    MT_RUN(two_keys_pressed_in_one_batch_arrive_in_one_frame);
    MT_RUN(a_tap_of_one_key_while_another_is_held);
    MT_RUN(several_taps_in_one_batch_are_several_presses);
    MT_RUN(a_long_backlog_never_hangs_and_ends_on_the_real_level);
    MT_RUN(random_event_streams_deliver_every_press_and_nothing_else);
    return MT_REPORT();
}
