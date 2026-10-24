#ifndef GUARD_PLATFORM_GAME_SPEED_H
#define GUARD_PLATFORM_GAME_SPEED_H

// Battle speed (PC only; the owner's request of 2026-10-24, "make battles as fast as possible").
//
// While a battle between players is on, the window loop runs the game's frames this many times as fast. Everything the
// battle shows (animations, HP bars, text) finishes sooner, and the game answers the host's battle commands sooner too
// (it reads them once a game frame). Three things must not speed up with it:
//  - the game's own patience: the multiplayer code counts frames for its timeouts (the battle core's deadline, the
//    companion's heartbeat, the save mailbox's hold). Those counters advance only on a real frame, one in `speed`, so a
//    slow connection gets the same seconds at any speed (Platform_RealFrame);
//  - the sound card: it plays 60 frames of sound a second. One game frame's sound in `speed` is played, its start
//    cross-faded with the sound that followed the one played before, so the music keeps its pitch and nothing clicks;
//  - the player's own battles against the game (and everything outside battles): normal speed.
#include <stdbool.h>
#include <stddef.h>

#define GAME_SPEED_MAX 4
#define GAME_SPEED_DEFAULT 4
#define GAME_SPEED_FADE_MAX 512   // floats of the cross-fade kept between two played frames

struct GameSpeed
{
    unsigned speed;      // game frames a real frame (1: normal)
    unsigned subFrame;   // the coming game frame's place among them (0: it ends a real frame)
    bool tailHeld;       // fade holds the start of the sound that followed the frame played last
    size_t fadeCount;
    float fade[GAME_SPEED_FADE_MAX];
};

void GameSpeed_Reset(struct GameSpeed *s);
// At a frame boundary: the speed the coming game frame runs at (wanted is clamped to 1..GAME_SPEED_MAX).
unsigned GameSpeed_Frame(struct GameSpeed *s, unsigned wanted);
// Whether the coming game frame ends a real 1/60 s (always at speed 1).
bool GameSpeed_RealFrame(const struct GameSpeed *s);
// The game frame's sound (count floats): how many floats of out to send to the sound card (0 or count).
size_t GameSpeed_Sound(struct GameSpeed *s, const float *in, size_t count, float *out);

#endif // GUARD_PLATFORM_GAME_SPEED_H
