#ifndef GUARD_PLATFORM_MP_BRIDGE_H
#define GUARD_PLATFORM_MP_BRIDGE_H

/*
 * Native multiplayer bridge (Milestone C): the native build's replacement for
 * the app repository's bridge/mgba.lua. It moves the same frames between the
 * game's EWRAM mailboxes and the local Python companion over the same loopback
 * protocol, with the same checks, so the companion and the host need no change:
 *
 *   companion <-> bridge   TCP 127.0.0.1:port, frames of a big-endian u32 length
 *                          and a payload (at most 256 bytes inbound); the first
 *                          frame is "H" and the 32-byte launch token.
 *   bridge <-> game        the mailboxes' inbound halves, written odd generation
 *                          first and the even one last, as the ROM expects.
 *
 * Where mgba.lua reads the mailbox addresses and the ROM's capability markers
 * from the verified build manifest, this file is part of the build: it takes
 * them from the game's own symbols. The game thread is parked in
 * VBlankIntrWait whenever the bridge runs (sdl2.c, frame boundary), as the
 * emulator is paused during the Lua frame callback.
 *
 * Started with `--bridge FILE`; FILE holds `port=N` (1-65535) and `token=` 64
 * lowercase hex digits, and the manifest's explicit opt-ins it has, each on
 * its own line: `trade_effect_prepare=1`, `trade_effect_save=1`,
 * `trade_effect_choice=1`. The launcher writes it into its private launch
 * folder as it writes bridge.lua for mGBA.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Platform API (sdl2.c). */
bool MpBridge_Configure(const char *path); /* false: refuse to start the game */
bool MpBridge_IsEnabled(void);
void MpBridge_Frame(void);                  /* once per game frame, game thread parked */
uint16_t MpBridge_FilterKeys(uint16_t keys); /* the multiplayer runtime's reset-chord guard */
bool MpBridge_SaveCommandPending(void);     /* a save command is in the game, its reply not yet read */
void MpBridge_Shutdown(void);

/*
 * The bridge's core, separated from sockets and game symbols so the unit tests
 * (tools/pc_port/tests/test_mp_bridge.c) can drive it as tests/test_mgba_bridge.lua
 * drives mgba.lua: with fake mailboxes and a fake transport.
 */
enum MpRegion
{
    MP_REGION_PRESENCE, /* gMultiplayerMailbox, 128 bytes */
    MP_REGION_SAVE,     /* gMultiplayerSaveMailbox, 512 */
    MP_REGION_BATTLE,   /* gMultiplayerBattleMailbox, 512 */
    MP_REGION_MENU,     /* gMultiplayerMenuMailbox, 512 */
    MP_REGION_RESET,    /* gMultiplayerResetMailbox, 128 */
    MP_REGION_WORLD,    /* gMultiplayerWorldMailbox, 256 */
    MP_REGION_WINDOW,   /* gSaveDataBuffer, 4096 (read only) */
    MP_REGION_COUNT
};

/* The ROM's capability markers (0 = absent), as mgba.lua's cfg receives them. */
struct MpBridgeCaps
{
    uint32_t presenceAnimation;      /* 1 */
    uint32_t save;                   /* 1 */
    uint32_t trade;                  /* 1 */
    uint32_t emergencySave;          /* 1 or 2 */
    uint32_t battle;                 /* 1..3 */
    uint32_t battleResume;           /* 1 */
    uint32_t battleExecution;        /* 1 */
    uint32_t battleAdmissionCleanup; /* 1 */
    uint32_t saveWindow;             /* 1 */
    uint32_t menu;                   /* 1 or 2 */
    uint32_t tradeEffectPrepare;     /* 1 */
    uint32_t tradeEffectSave;        /* 1 */
    uint32_t tradeEffectChoice;      /* 1 */
    uint32_t reset;                  /* 1 or 2 */
    uint32_t resetInput;             /* 1 */
    uint32_t world;                  /* 1 */
};

enum
{
    MP_IO_AGAIN = -2,  /* nothing now (EAGAIN) */
    MP_IO_ERROR = -1,
};

struct MpBridgeOps
{
    void *context;
    volatile uint8_t *region[MP_REGION_COUNT];
    uint32_t regionSize[MP_REGION_COUNT];
    /* Connects to 127.0.0.1:port; true when connected. */
    bool (*connect)(void *context, uint16_t port);
    /* Bytes sent (0..len), MP_IO_AGAIN or MP_IO_ERROR. */
    int (*send)(void *context, const uint8_t *data, size_t len);
    /* Bytes read (1..max), 0 when the companion closed, MP_IO_AGAIN or MP_IO_ERROR. */
    int (*receive)(void *context, uint8_t *data, size_t max);
    void (*close)(void *context);
    void (*log)(void *context, bool error, const char *message);
    /* mgba.lua's emeraldResetInput: the game may take the reset chord itself. */
    void (*routeResetChord)(void *context, bool enabled);
    /* Monotonic milliseconds (wrapping), or NULL. With a clock the pose and the
     * shared-world record are published every 100 ms of real time, so a game
     * running below 60 frames a second still looks live to the companion;
     * without one, every 6 frames as mgba.lua does (mp_bridge.c, RECORD_MS). */
    uint32_t (*milliseconds)(void *context);
};

#define MP_BRIDGE_MAX_FRAME (4 + 1 + 240 + 4096) /* "F" response with its 4 KiB window */
#define MP_BRIDGE_CONTROL_LIMIT 4

struct MpBridgeSave
{
    bool pending;
    uint32_t incarnation, epoch, sequence, opcode, revision, offset, size, status;
    uint8_t request[16];
    uint8_t payload[240];
};

struct MpBridgeBattle
{
    bool pending;
    uint32_t opcode;
    uint8_t correlation[40];
    uint8_t payload[240];
};

struct MpBridge
{
    struct MpBridgeOps ops;
    struct MpBridgeCaps caps;
    uint16_t port;
    uint8_t token[32];
    bool configured;
    /* mgba.lua's locals */
    bool connected;
    uint8_t rx[1024];
    size_t rxLength;
    uint8_t tx[MP_BRIDGE_MAX_FRAME];
    size_t txLength, txSent;
    uint8_t controls[MP_BRIDGE_CONTROL_LIMIT][MP_BRIDGE_MAX_FRAME];
    size_t controlLength[MP_BRIDGE_CONTROL_LIMIT];
    unsigned controlCount, controlBurst;
    uint8_t latestPose[4 + 1 + 20];
    bool hasLatestPose;
    uint8_t latestWorld[4 + 1 + 32];
    bool hasLatestWorld, poseSentLast;
    bool posePublished, worldPublished; /* RecordDue with a clock */
    uint32_t poseAt, worldAt;
    struct MpBridgeSave save;
    bool hasSaveGeneration;
    uint32_t saveGeneration;
    bool capabilitySent, windowCapabilitySent, emergencyCapabilitySent;
    bool tradeEffectPrepareCapabilitySent, tradeEffectSaveCapabilitySent, tradeEffectChoiceCapabilitySent;
    struct MpBridgeBattle battle;
    bool hasBattleGeneration;
    uint32_t battleGeneration;
    bool battleCapabilitySent;
    bool menuCapabilitySent, hasMenuSequence;
    uint32_t menuSequence;
    bool resetCapabilitySent, hasResetSequence, hasResetReceipt;
    uint32_t resetSequence;
    uint8_t resetReceipt[32];
    bool observedReset, hasPreviousResetFrame;
    uint32_t previousResetFrame;
    bool observedMenu, hasPreviousMenuFrame;
    uint32_t previousMenuFrame;
    bool observedBattle, hasPreviousBattleFrame;
    uint32_t previousBattleFrame, previousBattleState;
    bool observedPresence, hasPreviousFrame;
    uint32_t previousFrame;
    bool observedSave, hasPreviousSaveFrame;
    uint32_t previousSaveFrame, previousSaveState;
    int64_t tick, lastConnect;
    bool stopped;
};

/* Checks the capability set as mgba.lua's start-up asserts do; NULL when
 * consistent, otherwise why not. */
const char *MpBridge_CheckCaps(const struct MpBridgeCaps *caps);
void MpBridge_Init(struct MpBridge *bridge, const struct MpBridgeOps *ops,
                   const struct MpBridgeCaps *caps, uint16_t port, const uint8_t token[32]);
void MpBridge_Step(struct MpBridge *bridge);   /* mgba.lua's onFrame */
void MpBridge_Stop(struct MpBridge *bridge);   /* mgba.lua's stop() without a reason */
uint32_t MpBridge_Crc32(const uint8_t *data, size_t length);

/* The reset-chord guard of the app's native/windows-runtime/sdl-multiplayer.h. */
struct MpBridgeInput
{
    uint32_t keys;
    bool chordHeld;
    bool nativeChordEnabled;
    bool chordRouted;
};
uint32_t MpBridge_FilterInput(struct MpBridgeInput *input, uint32_t keys);
void MpBridge_EnableNativeChord(struct MpBridgeInput *input, bool enabled);

#endif // GUARD_PLATFORM_MP_BRIDGE_H
