#ifndef GUARD_PLATFORM_MP_BRIDGE_H
#define GUARD_PLATFORM_MP_BRIDGE_H

/*
 * Native multiplayer bridge (Milestone C): the native build's replacement for
 * the app repository's bridge/mgba.lua. It moves the same frames between the
 * game's EWRAM mailboxes and the local Python companion over the same loopback
 * protocol, with the same checks, so the companion and the host need no change:
 *
 *   companion <-> bridge   TCP 127.0.0.1:port, frames of a big-endian u32 length
 *                          and a payload (at most 256 bytes inbound, but
 *                          for 'i' below); the first
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
 * One extension mgba.lua does not have, for the native game's character
 * selection: after its battle capability the bridge announces 'A' (version 1,
 * MP_BRIDGE_BATTLE_LOOKS slots), and the companion then sends 'B' for each
 * battle the game binds: the battle's generation and one 8-byte look per
 * binding slot (the looks the host's roster already carries for the overworld).
 * The game draws the other players' characters from them; the battle's own
 * records stay exactly the ROM's. A companion that does not know 'A' never
 * meets it: the launcher pairs this game with its own companion.
 *
 * A second native extension, the shared wild Pokemon of EmeraldTogether Phase 3
 * (multiplayer_spawns.h): once the world mailbox is ready the bridge announces
 * 'w' (version 1, 12 slots, 384 bytes, 20-byte slots), publishes the game's
 * 116-byte spawn record as 'g' (as 'G' for the world record) and accepts the
 * companion's 252-byte spawn inbound record as 'n' (only after 'w').
 *
 * A third native extension, multiplayer Secret Bases (multiplayer_extras.h,
 * mailbox EMX1): once the world mailbox is ready the bridge announces 'e'
 * (version 1, 384 bytes, 160-byte base records), publishes the game's own base
 * record (168 bytes) as 'x' whenever its number changes and after every
 * connection, and accepts one 176-byte base hand-over from the companion as 'y'
 * (only after 'e').
 *
 * A fourth native extension, the trade import window (PC builds only, marker
 * gMultiplayerSaveImportWindowAbiVersion): the companion may send a save
 * command WRITE_WINDOW (opcode 30) as 'i', the 240-byte command followed by
 * the 4096 bytes of one received save sector; the bridge writes the sector to
 * gMultiplayerSaveImportWindow and then the command, which names its CRC. One
 * frame instead of 32 WRITEs per sector. The companion learns the capability
 * from the build manifest; nothing is announced, and 'C' never carries it.
 *
 * Started with `--bridge FILE`; FILE holds `port=N` (1-65535) and `token=` 64
 * lowercase hex digits, and the manifest's explicit opt-ins it has, each on
 * its own line: `trade_effect_prepare=1`, `trade_effect_save=1`,
 * `trade_effect_choice=1`, and (Update 32) `menu_open_page=1`. The launcher writes it into its private launch
 * folder as it writes bridge.lua for mGBA.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Platform API (sdl2.c). */
bool MpBridge_Configure(const char *path); /* false: refuse to start the game */
bool MpBridge_IsEnabled(void);
bool MpBridge_MenuOpenPageEnabled(void); /* Update 32: a Players menu OPEN may name its page (menu_open_page=1) */
bool MpBridge_TowerEnabled(void); /* Battle Tower: Legends (tower=1) */
bool MpBridge_HostSavePageEnabled(void); /* MS-61: an in-game SAVE in a world asks for the host save (host_save_page=1) */
enum MpLinkState
{
    MP_LINK_OFFLINE,       /* started without a bridge: single play on this PC */
    MP_LINK_SETUP,         /* the launcher's character setup (--setup): offline by design until saved and closed */
    MP_LINK_CONNECTING,    /* the bridge is up, the world session not yet reported */
    MP_LINK_NOT_CONNECTED, /* still no world session after a while: the launcher's window says why */
    MP_LINK_CONNECTED,     /* the companion reports the world session (hosting or joined) */
    MP_LINK_LOST,          /* it was connected and is not now */
};
void MpBridge_SetSetupSession(void);        /* --setup on the command line */
enum MpLinkState MpBridge_LinkState(void);
void MpBridge_SetRole(const char *role);    /* --link-role host|join on the command line */
char MpBridge_Role(void);                   /* 'h' hosting, 'j' joined, 0 not said */
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
    MP_REGION_SPAWN,    /* gMultiplayerSpawnMailbox, 384 (native extension) */
    MP_REGION_EXTRAS,   /* gMultiplayerExtrasMailbox, 384 (native extension) */
    MP_REGION_IMPORT,   /* gMultiplayerSaveImportWindow, 4096 (native extension, written only by 'i') */
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
    uint32_t spawns;                 /* 1: shared wild Pokemon (native extension, needs world) */
    uint32_t extras;                 /* 1: multiplayer Secret Bases (native extension, needs world) */
    uint32_t presenceDetail;         /* 1: why the game is off the field, in its pose's last byte (native extension) */
    uint32_t battleFfa;              /* 1: the four-player free-for-all battle phase 5 (needs battle 3) */
    uint32_t battleFfa3;             /* 1: the three-player free-for-all (1v1v1) battle phase 6 (needs battleFfa 1) */
    uint32_t menuRelease;            /* 1: a menu screen with flags 3 ends a refused Enter battle latch (needs menu 2, resume 1) */
    uint32_t saveImportWindow;       /* 1: WRITE_WINDOW, a trade's save a 4 KiB sector per 'i' frame (needs the save window) */
    uint32_t menuOpenPage;           /* 1: a menu OPEN may name a page (1: soft reset) (needs menu 2; launcher opt-in) */
    uint32_t tower;                  /* 1: Battle Tower: Legends - OPEN page 2, screen flags 4/5 and style bytes, reward
                                        kinds 6/7 (needs menuOpenPage 1 and the world; launcher opt-in tower=1) */
    uint32_t savePartyQuery;         /* 1: QUERY_PARTY (save opcode 31), what a battle preparation checks (KNOWN_ISSUES
                                        MS-60; needs the save mailbox) */
    uint32_t hostSavePage;           /* 1: OPEN page 3, the host save after an in-game SAVE (MS-61; needs menuOpenPage 1;
                                        launcher opt-in host_save_page=1) */
    uint32_t battlePeek;             /* 1: battle opcode 25 PEEK, a query with the outbound block's bytes, and an ACK_OUT
                                        without READ_OUT (KNOWN_ISSUES MS-62; needs battle 3; launcher opt-in battle_peek=1) */
    uint32_t battleReceiveMany;      /* 1: battle opcode 26 RECEIVE_MANY, a batch's small blocks for one core in one command
                                        (MS-62, the 2v2s; needs battle 3; launcher opt-in battle_receive_many=1) */
};

enum
{
    MP_IO_AGAIN = -2,  /* nothing now (EAGAIN) */
    MP_IO_ERROR = -1,
};

#define MP_BRIDGE_BATTLE_LOOKS 4 /* binding slots in a 'B' frame, 8 bytes each */

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
    /* The looks of a bound battle's players (frame 'B'): its 16-byte generation
     * and MP_BRIDGE_BATTLE_LOOKS looks of 8 bytes, by binding slot. NULL: the
     * game shows no characters, and the bridge never announces 'A'. */
    void (*battleLooks)(void *context, const uint8_t *generation, const uint8_t *looks);
    /* KNOWN_ISSUES MS-62: called at the frame boundary while a battle command is
     * outstanding, before its answer is polled: the game answers its transport
     * commands at once (MultiplayerBattle_ServiceTransport). NULL: the game's next
     * frame answers, a frame later. */
    void (*serviceBattle)(void *context);
};

#define MP_BRIDGE_MAX_FRAME (4 + 1 + 240 + 4096) /* "F" response with its 4 KiB window */
#define MP_BRIDGE_IMPORT_FRAME (1 + 240 + 4096) /* inbound 'i': a save command and its import sector */
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
    uint8_t rx[4 + MP_BRIDGE_IMPORT_FRAME + 256]; /* a partial 'i' frame and one more read */
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
    uint8_t latestSpawn[4 + 1 + 116];
    bool hasLatestSpawn, spawnTurn, spawnPublished, spawnCapabilitySent;
    uint32_t spawnAt;
    uint8_t latestExtras[4 + 1 + 168];
    bool hasLatestExtras, extrasPublished, extrasCapabilitySent, extrasSentValid;
    uint32_t extrasAt, extrasSentSeq;
    struct MpBridgeSave save;
    bool hasSaveGeneration;
    uint32_t saveGeneration;
    bool capabilitySent, windowCapabilitySent, emergencyCapabilitySent;
    bool tradeEffectPrepareCapabilitySent, tradeEffectSaveCapabilitySent, tradeEffectChoiceCapabilitySent;
    struct MpBridgeBattle battle;
    bool hasBattleGeneration;
    uint32_t battleGeneration;
    bool battleCapabilitySent, battleLooksCapabilitySent;
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
