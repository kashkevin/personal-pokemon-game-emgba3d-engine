// The gamepad mapping (include/platform/gamepad.h): XInput's: every button combination equals the
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

MT_TEST(the_right_trigger_no_longer_speeds_the_game_up)
{
    struct GamepadInput in = {0, 0, 0, 0};

    // Update 30: the right trigger no longer fast-forwards; it maps to no key at all
    in.rightTrigger = 255;  MT_CHECK(Gamepad_MapKeys(&in).keys == 0);
    in.rightTrigger = 0;    MT_CHECK(Gamepad_MapKeys(&in).keys == 0);
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

// SDL's controllers (a PlayStation or Switch Pro pad): every SDL button does what the same XInput button does, the
// stick's down (positive in SDL) is Down, and SDL's numbering is SDL2's own (checked against SDL's header).
#include <SDL2/SDL_gamecontroller.h>
#define SAME_BUTTON(ours, sdl) _Static_assert((int)(ours) == (int)(sdl), "SDL2's button numbering: " #sdl)
SAME_BUTTON(GAMEPAD_SDL_A, SDL_CONTROLLER_BUTTON_A);
SAME_BUTTON(GAMEPAD_SDL_B, SDL_CONTROLLER_BUTTON_B);
SAME_BUTTON(GAMEPAD_SDL_X, SDL_CONTROLLER_BUTTON_X);
SAME_BUTTON(GAMEPAD_SDL_Y, SDL_CONTROLLER_BUTTON_Y);
SAME_BUTTON(GAMEPAD_SDL_BACK, SDL_CONTROLLER_BUTTON_BACK);
SAME_BUTTON(GAMEPAD_SDL_GUIDE, SDL_CONTROLLER_BUTTON_GUIDE);
SAME_BUTTON(GAMEPAD_SDL_START, SDL_CONTROLLER_BUTTON_START);
SAME_BUTTON(GAMEPAD_SDL_LEFT_STICK, SDL_CONTROLLER_BUTTON_LEFTSTICK);
SAME_BUTTON(GAMEPAD_SDL_RIGHT_STICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK);
SAME_BUTTON(GAMEPAD_SDL_LEFT_SHOULDER, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
SAME_BUTTON(GAMEPAD_SDL_RIGHT_SHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
SAME_BUTTON(GAMEPAD_SDL_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_UP);
SAME_BUTTON(GAMEPAD_SDL_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
SAME_BUTTON(GAMEPAD_SDL_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
SAME_BUTTON(GAMEPAD_SDL_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);

static uint16_t SdlKeys(uint32_t buttons, int16_t x, int16_t y)
{
    struct GamepadSdlState state = {buttons, x, y, 0};
    struct GamepadInput in = Gamepad_FromSdl(&state);

    return Gamepad_MapKeys(&in).keys;
}

MT_TEST(an_sdl_pad_s_buttons_are_the_xinput_pad_s)
{
    struct { int sdl; uint16_t xinput; } table[] = {
        {GAMEPAD_SDL_A, GAMEPAD_A}, {GAMEPAD_SDL_B, GAMEPAD_B}, {GAMEPAD_SDL_X, GAMEPAD_X}, {GAMEPAD_SDL_Y, GAMEPAD_Y},
        {GAMEPAD_SDL_BACK, GAMEPAD_BACK}, {GAMEPAD_SDL_START, GAMEPAD_START},
        {GAMEPAD_SDL_LEFT_SHOULDER, GAMEPAD_LEFT_SHOULDER}, {GAMEPAD_SDL_RIGHT_SHOULDER, GAMEPAD_RIGHT_SHOULDER},
        {GAMEPAD_SDL_DPAD_UP, GAMEPAD_DPAD_UP}, {GAMEPAD_SDL_DPAD_DOWN, GAMEPAD_DPAD_DOWN},
        {GAMEPAD_SDL_DPAD_LEFT, GAMEPAD_DPAD_LEFT}, {GAMEPAD_SDL_DPAD_RIGHT, GAMEPAD_DPAD_RIGHT},
    };

    for (unsigned i = 0; i < sizeof(table) / sizeof(table[0]); i++)
    {
        struct GamepadSdlState state = {1u << table[i].sdl, 0, 0, 0};
        struct GamepadInput xinput = {table[i].xinput, 0, 0, 0};
        struct GamepadInput in = Gamepad_FromSdl(&state);

        MT_EQ(in.buttons, table[i].xinput);
        MT_EQ(Gamepad_MapKeys(&in).keys, Gamepad_MapKeys(&xinput).keys);
        MT_EQ(Gamepad_MapKeys(&in).details, Gamepad_MapKeys(&xinput).details);
    }
    MT_EQ(SdlKeys(1u << GAMEPAD_SDL_A, 0, 0), 0x0001);            // A (a PlayStation pad's cross) is A
    MT_EQ(SdlKeys(1u << GAMEPAD_SDL_B, 0, 0), 0x0002);            // B (circle) is B
    MT_EQ(SdlKeys(1u << GAMEPAD_SDL_X, 0, 0), 0x0002);            // X (square) is B too, as on an Xbox pad
    MT_EQ(SdlKeys(1u << GAMEPAD_SDL_Y, 0, 0), 0);                 // Y (triangle): the details key, no game button
    MT_EQ(SdlKeys((1u << GAMEPAD_SDL_GUIDE) | (1u << GAMEPAD_SDL_LEFT_STICK) | (1u << GAMEPAD_SDL_RIGHT_STICK), 0, 0), 0);
    MT_EQ(SdlKeys(0xFFFF8000u, 0, 0), 0);                         // SDL's later buttons (touchpad, paddles): nothing
}

MT_TEST(an_sdl_pad_s_stick_down_is_down)
{
    MT_EQ(SdlKeys(0, 0, 32767), 0x0080);      // SDL: down is positive
    MT_EQ(SdlKeys(0, 0, -32768), 0x0040);     // up (the most negative value, which XInput's up cannot hold, is up)
    MT_EQ(SdlKeys(0, -32768, 0), 0x0020);
    MT_EQ(SdlKeys(0, 32767, 0), 0x0010);
    MT_EQ(SdlKeys(0, 16383, -16383), 0);      // under half: nothing
    MT_EQ(SdlKeys(0, 16384, 16384), 0x0010 | 0x0080);

    struct GamepadSdlState trigger = {0, 0, 0, 32767};
    MT_EQ(Gamepad_FromSdl(&trigger).rightTrigger, 255);
    MT_EQ(SdlKeys(0, 0, 0), 0);
}

MT_TEST(random_sdl_input_never_sets_a_bit_outside_the_ten_game_keys)
{
    srand(11);
    for (int i = 0; i < 20000; i++)
    {
        struct GamepadSdlState state = {(uint32_t)rand() * 2654435761u, (int16_t)rand(), (int16_t)rand(), (int16_t)rand()};
        struct GamepadInput in = Gamepad_FromSdl(&state);

        MT_EQ(Gamepad_MapKeys(&in).keys & ~0x03FF, 0);
    }
}

int main(void)
{
    MT_RUN(all_4096_button_combinations_match_the_old_arithmetic);
    MT_RUN(each_button_is_the_key_it_always_was);
    MT_RUN(the_left_stick_is_a_dpad_beyond_half_deflection);
    MT_RUN(the_right_trigger_no_longer_speeds_the_game_up);
    MT_RUN(random_input_never_sets_a_bit_outside_the_ten_game_keys);
    MT_RUN(an_sdl_pad_s_buttons_are_the_xinput_pad_s);
    MT_RUN(an_sdl_pad_s_stick_down_is_down);
    MT_RUN(random_sdl_input_never_sets_a_bit_outside_the_ten_game_keys);
    return MT_REPORT();
}
