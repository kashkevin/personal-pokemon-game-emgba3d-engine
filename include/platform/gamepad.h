#ifndef GUARD_PLATFORM_GAMEPAD_H
#define GUARD_PLATFORM_GAMEPAD_H

// The gamepad (XInput on Windows; SDL's controllers where XInput has none) mapped to the GBA's keys, as pure functions so
// they can be tested on any machine. The bit values are XInput's XINPUT_GAMEPAD_* masks and the GBA key register's bits.
#include <stdbool.h>
#include <stdint.h>

#define GAMEPAD_DPAD_UP        0x0001
#define GAMEPAD_DPAD_DOWN      0x0002
#define GAMEPAD_DPAD_LEFT      0x0004
#define GAMEPAD_DPAD_RIGHT     0x0008
#define GAMEPAD_START          0x0010
#define GAMEPAD_BACK           0x0020
#define GAMEPAD_LEFT_SHOULDER  0x0100
#define GAMEPAD_RIGHT_SHOULDER 0x0200
#define GAMEPAD_A              0x1000
#define GAMEPAD_B              0x2000
#define GAMEPAD_X              0x4000
#define GAMEPAD_Y              0x8000

#define GAMEPAD_STICK_THRESHOLD 0.5f
#define GAMEPAD_TRIGGER_THRESHOLD 0x80 // the right trigger past this speeds the game up

struct GamepadInput
{
    uint16_t buttons;        // XINPUT_GAMEPAD_* bits
    int16_t stickX, stickY;  // left stick, -32768..32767 (up is positive)
    uint8_t rightTrigger;
};

struct GamepadKeys
{
    uint16_t keys;           // GBA key bits (A_BUTTON... DPAD_DOWN)
    bool details;            // Y: no game button, the native UI's details view
};

struct GamepadKeys Gamepad_MapKeys(const struct GamepadInput *input);

// Controllers XInput does not read (a PlayStation or Switch Pro controller, most other USB and Bluetooth pads) through
// SDL's game controller support: SDL2's button numbering (SDL_GameControllerButton, fixed for SDL2; sdl2.c checks it at
// compile time), as bits of GamepadSdlState.buttons. SDL names the face buttons by position (A at the bottom: an Xbox
// pad's A, a PlayStation pad's cross), except a Nintendo pad's, which it names by their labels (its A is the right one).
enum
{
    GAMEPAD_SDL_A, GAMEPAD_SDL_B, GAMEPAD_SDL_X, GAMEPAD_SDL_Y, GAMEPAD_SDL_BACK, GAMEPAD_SDL_GUIDE, GAMEPAD_SDL_START,
    GAMEPAD_SDL_LEFT_STICK, GAMEPAD_SDL_RIGHT_STICK, GAMEPAD_SDL_LEFT_SHOULDER, GAMEPAD_SDL_RIGHT_SHOULDER,
    GAMEPAD_SDL_DPAD_UP, GAMEPAD_SDL_DPAD_DOWN, GAMEPAD_SDL_DPAD_LEFT, GAMEPAD_SDL_DPAD_RIGHT,
    GAMEPAD_SDL_BUTTONS
};

struct GamepadSdlState
{
    uint32_t buttons;        // bit n: SDL button n held
    int16_t leftX, leftY;    // the left stick as SDL reports it: -32768..32767, down is positive
    int16_t rightTrigger;    // 0..32767
};

// The SDL controller in XInput's terms, for Gamepad_MapKeys: the same buttons do the same things on every pad (the
// guide button and the stick clicks nothing, as on XInput), the stick's up positive.
struct GamepadInput Gamepad_FromSdl(const struct GamepadSdlState *state);

#endif // GUARD_PLATFORM_GAMEPAD_H
