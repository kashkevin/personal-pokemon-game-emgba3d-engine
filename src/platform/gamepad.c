#include "platform/gamepad.h"

// GBA key register bits (include/gba/io_reg.h), repeated so this file stands alone.
#define KEY_A      0x0001
#define KEY_B      0x0002
#define KEY_SELECT 0x0004
#define KEY_START  0x0008
#define KEY_RIGHT  0x0010
#define KEY_LEFT   0x0020
#define KEY_UP     0x0040
#define KEY_DOWN   0x0080
#define KEY_R      0x0100
#define KEY_L      0x0200

struct GamepadKeys Gamepad_MapKeys(const struct GamepadInput *input)
{
    struct GamepadKeys out = {0, false};
    uint16_t b = input->buttons;
    float x = (float)input->stickX / 32767.0f, y = (float)input->stickY / 32767.0f;

    if (b & GAMEPAD_A)              out.keys |= KEY_A;
    if (b & GAMEPAD_X)              out.keys |= KEY_B;      // the left face button is the GBA's B
    if (b & GAMEPAD_B)              out.keys |= KEY_B;      // and so is the right one: Cancel/Back on an Xbox pad (it did nothing)
    if (b & GAMEPAD_START)          out.keys |= KEY_START;
    if (b & GAMEPAD_BACK)           out.keys |= KEY_SELECT;
    if (b & GAMEPAD_LEFT_SHOULDER)  out.keys |= KEY_L;
    if (b & GAMEPAD_RIGHT_SHOULDER) out.keys |= KEY_R;
    if (b & GAMEPAD_DPAD_UP)        out.keys |= KEY_UP;
    if (b & GAMEPAD_DPAD_DOWN)      out.keys |= KEY_DOWN;
    if (b & GAMEPAD_DPAD_LEFT)      out.keys |= KEY_LEFT;
    if (b & GAMEPAD_DPAD_RIGHT)     out.keys |= KEY_RIGHT;
    out.details = (b & GAMEPAD_Y) != 0;
    if (x < -GAMEPAD_STICK_THRESHOLD) out.keys |= KEY_LEFT;
    if (x >  GAMEPAD_STICK_THRESHOLD) out.keys |= KEY_RIGHT;
    if (y < -GAMEPAD_STICK_THRESHOLD) out.keys |= KEY_DOWN;
    if (y >  GAMEPAD_STICK_THRESHOLD) out.keys |= KEY_UP;
    return out;
}

struct GamepadInput Gamepad_FromSdl(const struct GamepadSdlState *state)
{
    static const uint16_t sXInputBit[GAMEPAD_SDL_BUTTONS] = {
        [GAMEPAD_SDL_A] = GAMEPAD_A, [GAMEPAD_SDL_B] = GAMEPAD_B, [GAMEPAD_SDL_X] = GAMEPAD_X, [GAMEPAD_SDL_Y] = GAMEPAD_Y,
        [GAMEPAD_SDL_BACK] = GAMEPAD_BACK, [GAMEPAD_SDL_START] = GAMEPAD_START,
        [GAMEPAD_SDL_LEFT_SHOULDER] = GAMEPAD_LEFT_SHOULDER, [GAMEPAD_SDL_RIGHT_SHOULDER] = GAMEPAD_RIGHT_SHOULDER,
        [GAMEPAD_SDL_DPAD_UP] = GAMEPAD_DPAD_UP, [GAMEPAD_SDL_DPAD_DOWN] = GAMEPAD_DPAD_DOWN,
        [GAMEPAD_SDL_DPAD_LEFT] = GAMEPAD_DPAD_LEFT, [GAMEPAD_SDL_DPAD_RIGHT] = GAMEPAD_DPAD_RIGHT,
    };
    struct GamepadInput out = {0, 0, 0, 0};

    for (int i = 0; i < GAMEPAD_SDL_BUTTONS; i++)
    {
        if (state->buttons & (1u << i))
            out.buttons |= sXInputBit[i];
    }
    out.stickX = state->leftX;
    out.stickY = state->leftY == INT16_MIN ? INT16_MAX : (int16_t)-state->leftY;
    out.rightTrigger = state->rightTrigger > 0 ? (uint8_t)(state->rightTrigger >> 7) : 0;
    return out;
}
