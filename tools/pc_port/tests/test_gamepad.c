// The Windows gamepad mapping (include/platform/gamepad.h): every button combination equals the
// shift arithmetic the game used before the mapping became a function, sticks and the trigger
// behave at their thresholds, and nothing a pad does can set a bit outside the GBA's ten keys.
#include <stdint.h>
#include <stdlib.h>

#include "minitest.h"
#include "platform/gamepad.h"

static const uint16_t sButtons[] = {
    GAMEPAD_DPAD_UP, GAMEPAD_DPAD_DOWN, GAMEPAD_DPAD_LEFT, GAMEPAD_DPAD_RIGHT, GAMEPAD_START, GAMEPAD_BACK,
    GAMEPAD_LEFT_SHOULDER, GAMEPAD_RIGHT_SHOULDER, GAMEPAD_A, GAMEPAD_B, GAMEPAD_X, GAMEPAD_Y,
};

// The mapping as sdl2.c wrote it before (buttons only), kept here as the oracle, plus Update 24's one addition (B = the B key).
static uint16_t OldButtons(uint16_t w)
{
    uint16_t k = 0;

    k |= (w & GAMEPAD_A) >> 12;
    k |= (w & GAMEPAD_X) >> 13;
    k |= (w & GAMEPAD_B) >> 12;   // Update 24: the right face button is the GBA's B as well (it used to do nothing)
    k |= (w & GAMEPAD_START) >> 1;
    k |= (w & GAMEPAD_BACK) >> 3;
    k |= (w & GAMEPAD_LEFT_SHOULDER) << 1;
    k |= (w & GAMEPAD_RIGHT_SHOULDER) >> 1;
    k |= (w & GAMEPAD_DPAD_UP) << 6;
    k |= (w & GAMEPAD_DPAD_DOWN) << 6;
    k |= (w & GAMEPAD_DPAD_LEFT) << 3;
    k |= (w & GAMEPAD_DPAD_RIGHT) << 1;
    return k;
}

MT_TEST(all_4096_button_combinations_match_the_old_arithmetic)
{
    for (unsigned mask = 0; mask < (1u << 12); mask++)
    {
        uint16_t w = 0;
        struct GamepadInput in = {0, 0, 0, 0};

        for (unsigned bit = 0; bit < 12; bit++)
        {
            if (mask & (1u << bit))
                w |= sButtons[bit];
        }
        in.buttons = w;
        MT_EQ(Gamepad_MapKeys(&in).keys, OldButtons(w));
        MT_EQ(Gamepad_MapKeys(&in).details, (w & GAMEPAD_Y) != 0);
    }
}

MT_TEST(each_button_is_the_key_it_always_was)
{
    struct { uint16_t pad; uint16_t key; } table[] = {
        {GAMEPAD_A, 0x0001}, {GAMEPAD_X, 0x0002}, {GAMEPAD_B, 0x0002}, {GAMEPAD_BACK, 0x0004}, {GAMEPAD_START, 0x0008},
        {GAMEPAD_DPAD_RIGHT, 0x0010}, {GAMEPAD_DPAD_LEFT, 0x0020}, {GAMEPAD_DPAD_UP, 0x0040},
        {GAMEPAD_DPAD_DOWN, 0x0080}, {GAMEPAD_RIGHT_SHOULDER, 0x0100}, {GAMEPAD_LEFT_SHOULDER, 0x0200},
    };

    for (unsigned i = 0; i < sizeof(table) / sizeof(table[0]); i++)
    {
        struct GamepadInput in = {table[i].pad, 0, 0, 0};

        MT_EQ(Gamepad_MapKeys(&in).keys, table[i].key);
    }
    struct GamepadInput unused = {0x0040 | 0x0080, 0, 0, 0};   // the thumb clicks: no game button
    MT_EQ(Gamepad_MapKeys(&unused).keys, 0);
}

MT_TEST(the_left_stick_is_a_dpad_beyond_half_deflection)
{
    struct GamepadInput in = {0, 0, 0, 0};

    in.stickX = 16383;  MT_EQ(Gamepad_MapKeys(&in).keys, 0);        // just under half
    in.stickX = 16384;  MT_EQ(Gamepad_MapKeys(&in).keys, 0x0010);   // just over: right
    in.stickX = -16384; MT_EQ(Gamepad_MapKeys(&in).keys, 0x0020);   // left
    in.stickX = 0; in.stickY = 32767;  MT_EQ(Gamepad_MapKeys(&in).keys, 0x0040);   // up is positive
    in.stickY = -32768; MT_EQ(Gamepad_MapKeys(&in).keys, 0x0080);   // down
    in.stickX = 32767; in.stickY = 32767; MT_EQ(Gamepad_MapKeys(&in).keys, 0x0010 | 0x0040);  // diagonals work
    in.stickX = -32768; in.stickY = -32768; MT_EQ(Gamepad_MapKeys(&in).keys, 0x0020 | 0x0080);
    in.buttons = GAMEPAD_DPAD_LEFT; in.stickX = 32767; in.stickY = 0;
    MT_EQ(Gamepad_MapKeys(&in).keys, 0x0020 | 0x0010);              // pad and stick together: both, as before
}

MT_TEST(the_right_trigger_speeds_the_game_up_past_its_threshold)
{
    struct GamepadInput in = {0, 0, 0, 0};

    in.rightTrigger = 0x80; MT_CHECK(!Gamepad_MapKeys(&in).fastForward);
    in.rightTrigger = 0x81; MT_CHECK(Gamepad_MapKeys(&in).fastForward);
    in.rightTrigger = 255;  MT_CHECK(Gamepad_MapKeys(&in).fastForward);
    in.rightTrigger = 0;    MT_CHECK(!Gamepad_MapKeys(&in).fastForward);
    MT_EQ(Gamepad_MapKeys(&in).keys, 0);   // the trigger is never a game button
}

MT_TEST(random_input_never_sets_a_bit_outside_the_ten_game_keys)
{
    srand(7);
    for (int i = 0; i < 20000; i++)
    {
        struct GamepadInput in = {(uint16_t)rand(), (int16_t)rand(), (int16_t)rand(), (uint8_t)rand()};

        MT_EQ(Gamepad_MapKeys(&in).keys & ~0x03FF, 0);
    }
}

int main(void)
{
    MT_RUN(all_4096_button_combinations_match_the_old_arithmetic);
    MT_RUN(each_button_is_the_key_it_always_was);
    MT_RUN(the_left_stick_is_a_dpad_beyond_half_deflection);
    MT_RUN(the_right_trigger_speeds_the_game_up_past_its_threshold);
    MT_RUN(random_input_never_sets_a_bit_outside_the_ten_game_keys);
    return MT_REPORT();
}
