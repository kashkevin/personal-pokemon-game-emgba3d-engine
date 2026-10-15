#ifndef GUARD_PLATFORM_GAMEPAD_H
#define GUARD_PLATFORM_GAMEPAD_H

// The Windows gamepad (XInput) mapped to the GBA's keys, as a pure function so it can be tested
// on any machine. The bit values are XInput's XINPUT_GAMEPAD_* masks and the GBA key register's bits.
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
    bool fastForward;        // right trigger held
};

struct GamepadKeys Gamepad_MapKeys(const struct GamepadInput *input);

#endif // GUARD_PLATFORM_GAMEPAD_H
