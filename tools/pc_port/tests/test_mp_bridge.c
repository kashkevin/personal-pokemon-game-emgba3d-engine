// Unit tests for the native multiplayer bridge core (src/platform/mp_bridge.c),
// after the app repository's tests/test_mgba_bridge.lua (fake mailboxes, fake
// transport) and tests/native/windows-input-guard.c (reset-chord guard).
// Built and run by: make -f Makefile_pc NATIVE_LINUX=1 check-native
#include <stdlib.h>
#include <string.h>

#include "platform/mp_bridge.h"
#include "minitest.h"

static uint8_t sMemory[MP_REGION_COUNT][4096];
static const uint32_t sSizes[MP_REGION_COUNT] = {128, 512, 512, 512, 128, 256, 4096, 384, 384};

struct FakeTransport
{
    bool again;          // the next send says EAGAIN once
    size_t chunk;        // bytes a send takes at most (0: all of them)
    bool closed;
    bool hangup;         // the next receive sees the companion close (0 bytes)
    int connects;
    int errors;
    uint8_t sent[16384];
    size_t sentLength;
    uint8_t received[64][512];
    size_t receivedLength[64];
    int receivedCount;
    bool chordRouted;
};

static struct FakeTransport sNet;
static struct MpBridge sBridge;

static bool FakeConnect(void *context, uint16_t port)
{
    (void)context;
    if (port != 23456)
        return false;
    sNet.connects++;
    sNet.closed = false;
    return true;
}

static int FakeSend(void *context, const uint8_t *data, size_t length)
{
    size_t n = sNet.chunk != 0 && length > sNet.chunk ? sNet.chunk : length;

    (void)context;
    if (sNet.again)
    {
        sNet.again = false;
        return MP_IO_AGAIN;
    }
    memcpy(sNet.sent + sNet.sentLength, data, n);
    sNet.sentLength += n;
    return (int)n;
}

static int FakeReceive(void *context, uint8_t *data, size_t max)
{
    size_t n;

    (void)context;
    if (sNet.hangup)
    {
        sNet.hangup = false;
        return 0;
    }
    if (sNet.receivedCount == 0)
        return MP_IO_AGAIN;
    n = sNet.receivedLength[0];
    if (n > max)
        abort();
    memcpy(data, sNet.received[0], n);
    sNet.receivedCount--;
    memmove(sNet.received[0], sNet.received[1], sizeof(sNet.received[0]) * sNet.receivedCount);
    memmove(sNet.receivedLength, sNet.receivedLength + 1, sizeof(sNet.receivedLength[0]) * sNet.receivedCount);
    return (int)n;
}

static void FakeClose(void *context)
{
    (void)context;
    sNet.closed = true;
}

static void FakeLog(void *context, bool error, const char *message)
{
    (void)context;
    if (error)
    {
        sNet.errors++;
        if (getenv("MP_TEST_VERBOSE") != NULL)
            fprintf(stderr, "  bridge: %s\n", message);
    }
}

static void FakeRoute(void *context, bool enabled)
{
    (void)context;
    sNet.chordRouted = enabled;
}

static void PutU32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void PutU16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static uint32_t GetU32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t GetBE32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// epoch 1, frame 10, x 5, y 7, group 0, map 9, direction 1, elevation 3,
// gender 0, mode, activity 1, reserved 0
static void MakePose(uint8_t pose[20], uint8_t mode)
{
    memset(pose, 0, 20);
    PutU32(pose, 1);
    PutU32(pose + 4, mode == 5 ? 11 : 10);
    PutU16(pose + 8, mode == 5 ? 6 : 5);
    PutU16(pose + 10, 7);
    pose[13] = 9;
    pose[14] = mode == 5 ? 4 : 1;
    pose[15] = 3;
    pose[17] = mode;
    pose[18] = 1;
}

static void Fresh(const struct MpBridgeCaps *caps)
{
    struct MpBridgeOps ops;
    struct MpBridgeCaps none;
    uint8_t token[32];
    int i;

    memset(sMemory, 0, sizeof(sMemory));
    memset(&sNet, 0, sizeof(sNet));
    memset(&ops, 0, sizeof(ops));
    memset(&none, 0, sizeof(none));
    for (i = 0; i < MP_REGION_COUNT; i++)
    {
        ops.region[i] = sMemory[i];
        ops.regionSize[i] = sSizes[i];
    }
    ops.connect = FakeConnect;
    ops.send = FakeSend;
    ops.receive = FakeReceive;
    ops.close = FakeClose;
    ops.log = FakeLog;
    ops.routeResetChord = FakeRoute;
    memset(token, 0xab, sizeof(token));
    sNet.chunk = 3; // partial sends, as the Lua test's transport
    MpBridge_Init(&sBridge, &ops, caps != NULL ? caps : &none, 23456, token);
}

static void Ready(uint8_t mode)
{
    uint8_t *p = sMemory[MP_REGION_PRESENCE];

    PutU32(p, 0x31504D45);
    PutU16(p + 4, 1);
    PutU16(p + 6, 128);
    MakePose(p + 16, mode);
    PutU32(p + 8, GetU32(p + 20));
}

static void Step(int frames)
{
    while (frames-- > 0)
        MpBridge_Step(&sBridge);
}

// One frame, delivered `chunk` bytes per receive.
static void Feed(const uint8_t *bytes, size_t length, size_t chunk)
{
    size_t at;

    for (at = 0; at < length; at += chunk)
    {
        size_t n = length - at < chunk ? length - at : chunk;

        if (sNet.receivedCount == 64 || n > 512)
            abort();
        memcpy(sNet.received[sNet.receivedCount], bytes + at, n);
        sNet.receivedLength[sNet.receivedCount++] = n;
    }
}

static size_t FrameOf(uint8_t *out, uint8_t kind, const uint8_t *payload, size_t length)
{
    size_t size = 1 + length;

    out[0] = (uint8_t)(size >> 24);
    out[1] = (uint8_t)(size >> 16);
    out[2] = (uint8_t)(size >> 8);
    out[3] = (uint8_t)size;
    out[4] = kind;
    memcpy(out + 5, payload, length);
    return 4 + size;
}

// Finds the n-th complete outbound frame of `kind`; its payload length or -1.
static long SentFrame(uint8_t kind, int nth, const uint8_t **payload)
{
    size_t at = 0;

    while (at + 5 <= sNet.sentLength)
    {
        uint32_t size = GetBE32(sNet.sent + at);

        if (at + 4 + size > sNet.sentLength)
            break;
        if (sNet.sent[at + 4] == kind && nth-- == 0)
        {
            *payload = sNet.sent + at + 5;
            return (long)size - 1;
        }
        at += 4 + size;
    }
    return -1;
}

static void MakeSnapshot(uint8_t out[88], uint8_t count, uint32_t secondToken, uint8_t mode)
{
    memset(out, 0, 88);
    PutU32(out, 1);
    PutU32(out + 4, 2);
    PutU32(out + 8, 1);
    out[13] = count;
    PutU32(out + 16, 2);
    MakePose(out + 20, mode);
    if (count > 1)
    {
        PutU32(out + 40, secondToken);
        MakePose(out + 44, mode);
    }
}

MT_TEST(rom_startup_does_not_write)
{
    static uint8_t before[MP_REGION_COUNT][4096];

    Fresh(NULL);
    memcpy(before, sMemory, sizeof(before));
    Step(4);
    MT_EQ(sNet.connects, 0);
    MT_CHECK(memcmp(before, sMemory, sizeof(before)) == 0);
}

MT_TEST(hello_survives_partial_sends_then_pose)
{
    const uint8_t *payload;
    uint8_t hello[37];
    long length;

    Fresh(NULL);
    Ready(0);
    sNet.again = true;
    Step(40);
    hello[0] = 0;
    hello[1] = 0;
    hello[2] = 0;
    hello[3] = 33;
    hello[4] = 'H';
    memset(hello + 5, 0xab, 32);
    MT_EQ(sNet.connects, 1);
    MT_CHECK(sNet.sentLength >= sizeof(hello) && memcmp(sNet.sent, hello, sizeof(hello)) == 0);
    length = SentFrame('P', 0, &payload);
    MT_EQ(length, 20);
    MT_CHECK(length == 20 && memcmp(payload, sMemory[MP_REGION_PRESENCE] + 16, 20) == 0);
}

MT_TEST(snapshot_commits_after_complete_frame)
{
    uint8_t snapshot[88];
    uint8_t frame[128];
    size_t n;
    uint8_t *p = sMemory[MP_REGION_PRESENCE];

    Fresh(NULL);
    Ready(0);
    Step(1);
    MakeSnapshot(snapshot, 1, 0, 0);
    n = FrameOf(frame, 'S', snapshot, sizeof(snapshot));
    Feed(frame, n, 7);
    Step(5);
    MT_EQ(GetU32(p + 36), 2);   // odd generation, then the even one
    MT_EQ(GetU32(p + 40), 1);   // flags
    MT_EQ(GetU32(p + 56), 2);   // the peer's token
    MT_CHECK(memcmp(p + 40, snapshot, 88) == 0);
    MT_EQ(sNet.errors, 0);
}

// The PC game says why it is off the field in its pose's last byte (the presence detail capability): the bridge sends
// it with the pose only while the pose is unavailable, only up to the transition code and only with the capability.
// The other players' poses in a snapshot keep the byte zero (the game's own check refuses anything else).
MT_TEST(presence_detail_rides_the_local_pose_only)
{
    struct MpBridgeCaps caps;
    const uint8_t *payload;
    uint8_t snapshot[88];
    uint8_t frame[128];
    size_t n;
    uint8_t *p = sMemory[MP_REGION_PRESENCE];
    static const struct { uint8_t capability, activity, detail; bool sent; } cases[] = {
        {1, 0, 1, true}, {1, 0, 2, true}, {1, 0, 3, true}, {1, 0, 0, true}, {1, 1, 0, true},
        {1, 0, 4, false},  /* no such detail */
        {1, 1, 1, false},  /* on the field there is nothing to explain */
        {0, 0, 1, false},  /* a build without the capability */
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        memset(&caps, 0, sizeof(caps));
        caps.presenceDetail = cases[i].capability;
        Fresh(&caps);
        sNet.chunk = 0;
        Ready(0);
        p[16 + 18] = cases[i].activity;
        p[16 + 19] = cases[i].detail;
        Step(12);
        if (cases[i].sent)
            MT_CHECK(SentFrame('P', 0, &payload) == 20 && memcmp(payload, p + 16, 20) == 0);
        else
            MT_CHECK(SentFrame('P', 0, &payload) < 0);
    }
    for (i = 0; i < 2; i++)
    {
        memset(&caps, 0, sizeof(caps));
        caps.presenceDetail = 1;
        Fresh(&caps);
        Ready(0);
        Step(1);
        MakeSnapshot(snapshot, 1, 0, 0);
        snapshot[20 + 18] = 0;
        snapshot[20 + 19] = i == 0 ? 1 : 0; // a peer's pose with a detail, then without
        n = FrameOf(frame, 'S', snapshot, sizeof(snapshot));
        Feed(frame, n, 100);
        Step(2);
        MT_EQ(GetU32(p + 40), i == 0 ? 0 : 1);
    }
}

MT_TEST(malformed_length_stops_and_clears)
{
    uint8_t snapshot[88];
    uint8_t frame[128];
    uint8_t bad[4] = {0, 0, 1, 1}; // 257
    size_t n;
    uint8_t *p = sMemory[MP_REGION_PRESENCE];
    uint32_t generation;
    int connects;

    Fresh(NULL);
    Ready(0);
    Step(1);
    MakeSnapshot(snapshot, 1, 0, 0);
    n = FrameOf(frame, 'S', snapshot, sizeof(snapshot));
    Feed(frame, n, 100);
    Step(1);
    MT_EQ(GetU32(p + 40), 1);
    Feed(bad, sizeof(bad), 4);
    Step(1);
    MT_CHECK(sNet.closed);
    MT_EQ(sNet.errors, 1);
    MT_EQ(GetU32(p + 40), 0); // rejection clears old actors
    generation = GetU32(p + 36);
    connects = sNet.connects;
    Step(300);
    MT_EQ(GetU32(p + 36), generation); // a fatal stop never reconnects
    MT_EQ(sNet.connects, connects);
}

MT_TEST(duplicate_peer_tokens_refused)
{
    uint8_t snapshot[88];
    uint8_t frame[128];
    size_t n;

    Fresh(NULL);
    Ready(0);
    Step(1);
    MakeSnapshot(snapshot, 2, 2, 0);
    n = FrameOf(frame, 'S', snapshot, sizeof(snapshot));
    Feed(frame, n, 100);
    Step(1);
    MT_CHECK(sNet.closed);
    MT_EQ(GetU32(sMemory[MP_REGION_PRESENCE] + 40), 0);
}

MT_TEST(foreign_mailbox_never_written)
{
    static uint8_t before[MP_REGION_COUNT][4096];

    Fresh(NULL);
    PutU32(sMemory[MP_REGION_PRESENCE], 0xDEADBEEF);
    memcpy(before, sMemory, sizeof(before));
    Step(1);
    MT_EQ(sNet.connects, 0);
    MT_EQ(sNet.errors, 1);
    MT_CHECK(memcmp(before, sMemory, sizeof(before)) == 0);
}

MT_TEST(running_needs_the_animation_capability)
{
    struct MpBridgeCaps caps;
    uint8_t snapshot[88];
    uint8_t frame[128];
    const uint8_t *payload;
    size_t n;

    Fresh(NULL);
    Ready(5);
    Step(40);
    MT_EQ(sNet.sentLength, 37); // the hello only: no running pose published
    memset(&caps, 0, sizeof(caps));
    caps.presenceAnimation = 1;
    Fresh(&caps);
    Ready(5);
    Step(40);
    MT_EQ(SentFrame('P', 0, &payload), 20);
    MakeSnapshot(snapshot, 1, 0, 5);
    n = FrameOf(frame, 'S', snapshot, sizeof(snapshot));
    Feed(frame, n, 7);
    Step(5);
    MT_CHECK(!sNet.closed);
    MT_EQ(GetU32(sMemory[MP_REGION_PRESENCE] + 36), 2);
}

MT_TEST(crc32_matches_zlib)
{
    MT_EQ(MpBridge_Crc32((const uint8_t *)"123456789", 9), 0xCBF43926u);
    MT_EQ(MpBridge_Crc32(NULL, 0), 0);
}

static void SaveReady(void)
{
    uint8_t *s = sMemory[MP_REGION_SAVE];

    PutU32(s, 0x31534D45);
    PutU16(s + 4, 1);
    PutU16(s + 6, 512);
}

static void MakeSaveControl(uint8_t out[240], uint8_t opcode)
{
    memset(out, 0, 240);
    PutU32(out, 7);        // incarnation
    PutU32(out + 4, 3);    // epoch
    memset(out + 8, 0x5a, 16); // request
    PutU32(out + 24, 1);   // sequence
    out[40] = opcode;
    PutU32(out + 44, MpBridge_Crc32(NULL, 0));
}

MT_TEST(save_command_and_response)
{
    struct MpBridgeCaps caps;
    uint8_t command[240];
    uint8_t frame[260];
    uint8_t capability[8] = {1, 0, 0, 2, 240, 0, 192, 0};
    const uint8_t *payload;
    uint8_t *s = sMemory[MP_REGION_SAVE];
    size_t n;

    memset(&caps, 0, sizeof(caps));
    caps.save = 1;
    Fresh(&caps);
    sNet.chunk = 0;
    Ready(0);
    SaveReady();
    Step(10);
    MT_EQ(SentFrame('V', 0, &payload), 8);
    MT_CHECK(memcmp(payload, capability, 8) == 0);
    MakeSaveControl(command, 1);
    n = FrameOf(frame, 'C', command, sizeof(command));
    Feed(frame, n, 200);
    Step(2);
    MT_EQ(GetU32(s + 24), 2);
    MT_CHECK(memcmp(s + 28, command, 240) == 0);
    // the game answers: the same correlation, status 0, no data
    memcpy(s + 272, command, 240);
    PutU32(s + 268, 2);
    Step(30);
    MT_EQ(SentFrame('R', 0, &payload), 240);
    MT_CHECK(memcmp(payload, command, 240) == 0);
    MT_EQ(sNet.errors, 0);
    // after the response a new command is accepted; another one while that
    // is pending is refused (opcode 2 is not one of the recovery controls)
    MakeSaveControl(command, 2);
    command[24] = 2;
    n = FrameOf(frame, 'C', command, sizeof(command));
    Feed(frame, n, 200);
    Step(2);
    MT_EQ(GetU32(s + 24), 4);
    MT_CHECK(!sNet.closed);
    command[24] = 3;
    n = FrameOf(frame, 'C', command, sizeof(command));
    Feed(frame, n, 200);
    Step(2);
    MT_CHECK(sNet.closed); // multiple outstanding save commands
}

MT_TEST(save_command_with_bad_crc_refused)
{
    struct MpBridgeCaps caps;
    uint8_t command[240];
    uint8_t frame[260];
    size_t n;

    memset(&caps, 0, sizeof(caps));
    caps.save = 1;
    Fresh(&caps);
    sNet.chunk = 0;
    Ready(0);
    SaveReady();
    Step(2);
    MakeSaveControl(command, 1);
    PutU32(command + 44, 1);
    n = FrameOf(frame, 'C', command, sizeof(command));
    Feed(frame, n, 200);
    Step(2);
    MT_CHECK(sNet.closed);
    MT_EQ(GetU32(sMemory[MP_REGION_SAVE] + 24), 0);
}

MT_TEST(world_inbound_validated)
{
    struct MpBridgeCaps caps;
    uint8_t record[208];
    uint8_t frame[230];
    uint8_t *w = sMemory[MP_REGION_WORLD];
    const uint8_t *payload;
    size_t n;

    memset(&caps, 0, sizeof(caps));
    caps.world = 1;
    Fresh(&caps);
    sNet.chunk = 0;
    Ready(0);
    PutU32(w, 0x31574D45);
    PutU16(w + 4, 1);
    PutU16(w + 6, 256);
    memset(w + 12, 0x11, 32); // the game's outbound record
    Step(12);
    MT_EQ(SentFrame('G', 0, &payload), 32);
    memset(record, 0, sizeof(record));
    PutU32(record, 1);
    memcpy(record + 116, "Hello trainers", 14);
    n = FrameOf(frame, 'N', record, sizeof(record));
    Feed(frame, n, 200);
    Step(2);
    MT_EQ(GetU32(w + 44), 2);
    MT_CHECK(memcmp(w + 48, record, 208) == 0);
    record[116] = 7; // not printable
    n = FrameOf(frame, 'N', record, sizeof(record));
    Feed(frame, n, 200);
    Step(2);
    MT_CHECK(sNet.closed);
}

// Update 31's rival record (bytes 200-207) reaches the game: the record the companion sent in the reproduction of the
// owner's report (only the valid flag) stopped the bridge here, so the game left the session. Malformed records and
// anything in 192-199 still close the connection.
MT_TEST(world_rival_record_accepted)
{
    struct MpBridgeCaps caps;
    uint8_t record[208];
    uint8_t frame[230];
    uint8_t *w = sMemory[MP_REGION_WORLD];
    static const uint8_t good[][8] = {
        {1, 0, 0, 0, 0, 0, 0, 0},       /* the reproduction's record: valid, nothing done, at no checkpoint */
        {1, 0x1F, 0x03, 0, 0, 0, 0, 0}, /* every checkpoint completed, two cleared */
        {3, 0, 0, 1, 1, 0, 0, 0},       /* present at checkpoint 1, open */
        {7, 0, 0, 5, 3, 0, 0, 0},       /* own match battling at checkpoint 5 */
        {0, 0, 0, 0, 0, 0, 0, 0},       /* no record */
    };
    static const uint8_t bad[][8] = {
        {0, 1, 0, 0, 0, 0, 0, 0},       /* data without the valid flag */
        {9, 0, 0, 0, 0, 0, 0, 0},       /* unknown flag */
        {1, 0x20, 0, 0, 0, 0, 0, 0},    /* a sixth checkpoint */
        {1, 0, 0, 6, 1, 0, 0, 0},       /* checkpoint 6 */
        {1, 0, 0, 1, 5, 0, 0, 0},       /* state 5 */
        {1, 0, 0, 1, 0, 0, 0, 0},       /* a checkpoint without its state */
        {3, 0, 0, 0, 0, 0, 0, 0},       /* present nowhere */
        {1, 0, 0, 0, 0, 0, 0, 1},       /* reserved byte */
    };
    size_t n, i, count = sizeof(good) / 8 + sizeof(bad) / 8 + 1;

    for (i = 0; i < count; i++)
    {
        int accepted = i < sizeof(good) / 8;

        memset(&caps, 0, sizeof(caps));
        caps.world = 1;
        Fresh(&caps);
        sNet.chunk = 0;
        Ready(0);
        PutU32(w, 0x31574D45);
        PutU16(w + 4, 1);
        PutU16(w + 6, 256);
        Step(12);
        memset(record, 0, sizeof(record));
        PutU32(record, 1);
        if (i < sizeof(good) / 8)
            memcpy(record + 200, good[i], 8);
        else if (i < count - 1)
            memcpy(record + 200, bad[i - sizeof(good) / 8], 8);
        else
            record[195] = 1; /* not an outbreak marker: data without its valid byte */
        n = FrameOf(frame, 'N', record, sizeof(record));
        Feed(frame, n, 200);
        Step(2);
        MT_CHECK(accepted ? !sNet.closed : sNet.closed);
        if (accepted)
            MT_CHECK(memcmp(w + 48, record, 208) == 0);
    }
}

// Update 18's outbreak marker (192-199), as the companion packs it while the host runs a mass outbreak, reaches the
// game; before, the first outbreak (8-15 active minutes into a world) stopped every game's bridge for good. Anything
// else in those bytes still closes the connection.
MT_TEST(world_outbreak_marker_accepted)
{
    struct MpBridgeCaps caps;
    uint8_t record[208];
    uint8_t frame[230];
    uint8_t *w = sMemory[MP_REGION_WORLD];
    static const uint8_t good[][8] = {
        {1, 0, 29, 0, 0, 0, 0, 0},      /* world_mailbox.py: valid, group, number, 0, species 0 */
        {1, 0, 16, 0, 0, 0, 0, 0},      /* Route 101 */
        {1, 255, 255, 0, 0, 0, 0, 0},   /* an unknown map: the Town Map leaves it out (RegionMap_PortableLocate) */
        {1, 0, 29, 0, 0xA4, 0x06, 0, 0}, /* a species word (1700), not read by the game */
        {0, 0, 0, 0, 0, 0, 0, 0},       /* no outbreak */
    };
    static const uint8_t bad[][8] = {
        {2, 0, 29, 0, 0, 0, 0, 0},      /* unknown valid byte */
        {0, 0, 29, 0, 0, 0, 0, 0},      /* data without the valid byte */
        {1, 0, 29, 1, 0, 0, 0, 0},      /* the zero byte */
        {1, 0, 29, 0, 0xA5, 0x06, 0, 0}, /* species 1701 */
        {1, 0, 29, 0, 0, 0, 1, 0},      /* reserved byte */
        {1, 0, 29, 0, 0, 0, 0, 1},      /* reserved byte */
    };
    static const uint8_t rival[8] = {3, 0x01, 0x01, 2, 1, 0, 0, 0};
    size_t n, i, count = sizeof(good) / 8 + sizeof(bad) / 8 + 1;

    for (i = 0; i < count; i++)
    {
        int accepted = i < sizeof(good) / 8 || i == count - 1;

        memset(&caps, 0, sizeof(caps));
        caps.world = 1;
        Fresh(&caps);
        sNet.chunk = 0;
        Ready(0);
        PutU32(w, 0x31574D45);
        PutU16(w + 4, 1);
        PutU16(w + 6, 256);
        Step(12);
        memset(record, 0, sizeof(record));
        PutU32(record, 1);
        if (i < sizeof(good) / 8)
            memcpy(record + 192, good[i], 8);
        else if (i < count - 1)
            memcpy(record + 192, bad[i - sizeof(good) / 8], 8);
        else
        {
            memcpy(record + 192, good[0], 8); /* an outbreak and a rival record in the same frame */
            memcpy(record + 200, rival, 8);
        }
        n = FrameOf(frame, 'N', record, sizeof(record));
        Feed(frame, n, 200);
        Step(2);
        MT_CHECK(accepted ? !sNet.closed : sNet.closed);
        if (accepted)
            MT_CHECK(memcmp(w + 48, record, 208) == 0);
    }
}

// The wager wallet accepts money (+-999,999) and, for the Battle Frontier, reward words (bit 30, kind 1-3): anything
// else above the money range closes the connection like any other malformed world record.
MT_TEST(world_wallet_money_and_reward_words)
{
    struct MpBridgeCaps caps;
    uint8_t record[208];
    uint8_t frame[230];
    uint8_t *w = sMemory[MP_REGION_WORLD];
    const uint32_t good[] = {1, 999999, 0xFFF0BDC1u /* -999999 */, 0x41000005u /* 5 Battle Points */,
                             0x42006601u /* an item */, 0x43000010u /* a shop debit */,
                             0x44000007u /* keep a catch */, 0x45000007u /* release a catch */};
    const uint32_t bad[] = {1000000, 0x40000005u /* no kind */, 0x46000005u /* kind 6 */, 0x61000005u, 0x7F000005u, 0xC1000005u};
    size_t n, i;

    for (i = 0; i < sizeof(good) / sizeof(good[0]) + sizeof(bad) / sizeof(bad[0]); i++)
    {
        int accepted = i < sizeof(good) / sizeof(good[0]);

        memset(&caps, 0, sizeof(caps));
        caps.world = 1;
        Fresh(&caps);
        sNet.chunk = 0;
        Ready(0);
        PutU32(w, 0x31574D45);
        PutU16(w + 4, 1);
        PutU16(w + 6, 256);
        Step(12);
        memset(record, 0, sizeof(record));
        PutU32(record, 1);
        PutU32(record + 180, 7);                                      /* wallet sequence */
        PutU32(record + 184, accepted ? good[i] : bad[i - sizeof(good) / sizeof(good[0])]);
        n = FrameOf(frame, 'N', record, sizeof(record));
        Feed(frame, n, 200);
        Step(2);
        MT_CHECK(accepted ? !sNet.closed : sNet.closed);
    }
}

// A slow game (2-3 frames a second in the 3D view on software OpenGL) still
// takes everything the companion sent since the last frame: 20 snapshots
// (1.8 KiB, more than mgba.lua's 1 KiB per frame) and the save command behind
// them reach their mailboxes in one frame.
MT_TEST(slow_frame_drains_the_backlog)
{
    struct MpBridgeCaps caps;
    uint8_t snapshot[88];
    uint8_t command[240];
    uint8_t backlog[2400];
    size_t n = 0;
    int i;
    uint8_t *p = sMemory[MP_REGION_PRESENCE];
    uint8_t *s = sMemory[MP_REGION_SAVE];

    memset(&caps, 0, sizeof(caps));
    caps.save = 1;
    Fresh(&caps);
    sNet.chunk = 0;
    Ready(0);
    SaveReady();
    Step(10);
    for (i = 0; i < 20; i++)
    {
        MakeSnapshot(snapshot, 1, 0, 0);
        PutU32(snapshot + 4, 2 + i); // heartbeat
        n += FrameOf(backlog + n, 'S', snapshot, sizeof(snapshot));
    }
    MakeSaveControl(command, 1);
    n += FrameOf(backlog + n, 'C', command, sizeof(command));
    MT_CHECK(n > 1024);
    Feed(backlog, n, 256);
    Step(1);
    MT_EQ(sNet.receivedCount, 0);
    MT_EQ(GetU32(p + 44), 21);  // the last snapshot's heartbeat
    MT_EQ(GetU32(s + 24), 2);   // the save command is in its mailbox
    MT_CHECK(memcmp(s + 28, command, 240) == 0);
    MT_EQ(sNet.errors, 0);
}

static uint32_t sNowMs;

static uint32_t FakeMilliseconds(void *context)
{
    (void)context;
    return sNowMs;
}

static int CountSent(uint8_t kind)
{
    const uint8_t *payload;
    int n = 0;

    while (SentFrame(kind, n, &payload) >= 0)
        n++;
    return n;
}

// With a clock the pose and the shared-world record follow real time: every
// 100 ms at 60 frames a second (as every 6 frames), and every frame when the
// game runs at 2.5 frames a second, so the companion never sees a stale pose.
// All due frames leave in the same game frame.
MT_TEST(clock_paces_records_by_real_time)
{
    struct MpBridgeCaps caps;
    uint8_t *w = sMemory[MP_REGION_WORLD];
    uint32_t start;
    int i;

    memset(&caps, 0, sizeof(caps));
    caps.world = 1;
    Fresh(&caps);
    sBridge.ops.milliseconds = FakeMilliseconds;
    sNet.chunk = 0;
    sNowMs = 0xFFFFFF00u; // wraps during the test
    Ready(0);
    PutU32(w, 0x31574D45);
    PutU16(w + 4, 1);
    PutU16(w + 6, 256);
    Step(1);
    MT_EQ(sNet.connects, 1);
    MT_EQ(CountSent('H'), 1);
    MT_EQ(CountSent('P'), 1); // hello, pose and world in the first frame
    MT_EQ(CountSent('G'), 1);
    sNet.sentLength = 0;
    start = sNowMs;
    for (i = 0; i < 60; i++) // one second at 60 frames a second
    {
        sNowMs = start + (uint32_t)((i + 1) * 1000 / 60);
        Step(1);
    }
    MT_EQ(CountSent('P'), 10);
    MT_EQ(CountSent('G'), 10);
    sNet.sentLength = 0;
    for (i = 0; i < 5; i++) // two seconds at 2.5 frames a second
    {
        sNowMs += 400;
        Step(1);
    }
    MT_EQ(CountSent('P'), 5);
    MT_EQ(CountSent('G'), 5);
    MT_EQ(sNet.errors, 0);
}

// Without a clock (mgba.lua's cadence) the pose still leaves every 6 frames.
MT_TEST(no_clock_keeps_the_six_frame_cadence)
{
    Fresh(NULL);
    sNet.chunk = 0;
    Ready(0);
    Step(1);
    sNet.sentLength = 0;
    Step(60);
    MT_EQ(CountSent('P'), 10);
}

MT_TEST(battle_capability_waits_for_its_mailbox)
{
    struct MpBridgeCaps caps;

    memset(&caps, 0, sizeof(caps));
    caps.save = 1;
    caps.trade = 1;
    caps.battle = 3;
    MT_CHECK(MpBridge_CheckCaps(&caps) == NULL);
    Fresh(&caps);
    Ready(0);
    SaveReady();
    Step(200);
    MT_EQ(sNet.connects, 0); // runtimeReady holds until the battle mailbox is up
    caps.trade = 0;
    MT_CHECK(MpBridge_CheckCaps(&caps) != NULL);
}

// The battle looks extension (mp_bridge.h): 'A' after the battle capability
// when the game takes looks, 'B' only after it and only whole.
static struct
{
    int calls;
    uint8_t generation[16];
    uint8_t looks[MP_BRIDGE_BATTLE_LOOKS * 8];
} sLooks;

static void FakeBattleLooks(void *context, const uint8_t *generation, const uint8_t *looks)
{
    (void)context;
    sLooks.calls++;
    memcpy(sLooks.generation, generation, sizeof(sLooks.generation));
    memcpy(sLooks.looks, looks, sizeof(sLooks.looks));
}

static void BattleReady(void)
{
    uint8_t *m = sMemory[MP_REGION_BATTLE];

    PutU32(m, 0x31424D45);
    PutU16(m + 4, 3);
    PutU16(m + 6, 512);
}

static void LooksBridge(bool withLooks)
{
    struct MpBridgeCaps caps;

    memset(&caps, 0, sizeof(caps));
    caps.save = 1;
    caps.trade = 1;
    caps.battle = 3;
    Fresh(&caps);
    if (withLooks)
        sBridge.ops.battleLooks = FakeBattleLooks;
    memset(&sLooks, 0, sizeof(sLooks));
    sNet.chunk = 0;
    Ready(0);
    SaveReady();
    BattleReady();
    Step(20);
}

MT_TEST(battle_looks_follow_their_announcement)
{
    uint8_t body[16 + MP_BRIDGE_BATTLE_LOOKS * 8];
    uint8_t frame[80];
    const uint8_t *payload;
    size_t n;
    int i;

    LooksBridge(true);
    MT_EQ(SentFrame('W', 0, &payload), 8);
    MT_EQ(SentFrame('A', 0, &payload), 4);
    MT_EQ(payload[0], 1);
    MT_EQ(payload[1], 0);
    MT_EQ(payload[2], MP_BRIDGE_BATTLE_LOOKS);
    MT_EQ(payload[3], 0);
    for (i = 0; i < (int)sizeof(body); i++)
        body[i] = (uint8_t)(i * 7 + 1);
    n = FrameOf(frame, 'B', body, sizeof(body));
    Feed(frame, n, 30);
    Step(2);
    MT_EQ(sLooks.calls, 1);
    MT_CHECK(memcmp(sLooks.generation, body, 16) == 0);
    MT_CHECK(memcmp(sLooks.looks, body + 16, sizeof(sLooks.looks)) == 0);
    MT_CHECK(!sNet.closed);
    MT_EQ(sNet.errors, 0);
    // a short frame stops the bridge and reaches no game
    n = FrameOf(frame, 'B', body, sizeof(body) - 1);
    Feed(frame, n, 200);
    Step(2);
    MT_EQ(sLooks.calls, 1);
    MT_CHECK(sNet.closed);
}

MT_TEST(battle_looks_need_the_game)
{
    uint8_t body[16 + MP_BRIDGE_BATTLE_LOOKS * 8];
    uint8_t frame[80];
    const uint8_t *payload;
    size_t n;

    LooksBridge(false); // a game without characters: mgba.lua's frames only
    MT_EQ(SentFrame('W', 0, &payload), 8);
    MT_EQ(SentFrame('A', 0, &payload), -1);
    memset(body, 0, sizeof(body));
    n = FrameOf(frame, 'B', body, sizeof(body));
    Feed(frame, n, 200);
    Step(2);
    MT_EQ(sLooks.calls, 0);
    MT_CHECK(sNet.closed);
}

// Native battle commands by phase: 2 singles, 3 full Multi, 4 co-op Gym (two
// cores). A co-op command once stopped the bridge ("invalid battle command").
static void MakeBattleControl(uint8_t out[240], uint8_t opcode, uint8_t phase)
{
    memset(out, 0, 240);
    PutU32(out, 7);            // incarnation
    PutU32(out + 4, 3);        // epoch
    memset(out + 8, 0x5a, 16); // battle id
    PutU32(out + 24, 1);       // sequence
    out[36] = opcode;
    out[38] = phase;
    PutU32(out + 44, MpBridge_Crc32(NULL, 0));
}

static bool BattleCommandAccepted(uint8_t phase)
{
    uint8_t command[240];
    uint8_t frame[260];
    uint8_t *m = sMemory[MP_REGION_BATTLE];
    size_t n;

    LooksBridge(false);
    MakeBattleControl(command, 2, phase); // a native command without data
    n = FrameOf(frame, 'D', command, sizeof(command));
    Feed(frame, n, 200);
    Step(2);
    return !sNet.closed && GetU32(m + 24) == 2 && memcmp(m + 28, command, 240) == 0;
}

MT_TEST(native_battle_phases_reach_the_game)
{
    MT_CHECK(BattleCommandAccepted(2)); // singles
    MT_CHECK(BattleCommandAccepted(3)); // full Multi
    MT_CHECK(BattleCommandAccepted(4)); // co-op Gym
    MT_CHECK(!BattleCommandAccepted(5));
    MT_CHECK(sNet.closed);
}

// Shared wild Pokemon (the second native extension, mp_bridge.h).
static void SpawnBridge(bool spawnMailbox)
{
    struct MpBridgeCaps caps;
    uint8_t *w = sMemory[MP_REGION_WORLD];
    uint8_t *v = sMemory[MP_REGION_SPAWN];

    memset(&caps, 0, sizeof(caps));
    caps.world = 1;
    caps.spawns = 1;
    Fresh(&caps);
    sNet.chunk = 0;
    Ready(0);
    PutU32(w, 0x31574D45);
    PutU16(w + 4, 1);
    PutU16(w + 6, 256);
    if (spawnMailbox)
    {
        PutU32(v, 0x31564D45);
        PutU16(v + 4, 1);
        PutU16(v + 6, 384);
    }
}

static void MakeSpawnInbound(uint8_t record[252], uint8_t count)
{
    uint8_t i;

    memset(record, 0, 252);
    PutU32(record, count != 0 ? 1 : 0);
    PutU32(record + 4, 7);   // action ack
    record[8] = 0;           // map group
    record[9] = 16;          // map number
    record[10] = count;
    for (i = 0; i < count; i++)
    {
        uint8_t *slot = record + 12 + i * 20;
        PutU32(slot, 100 + i);          // spawn id
        PutU32(slot + 4, 0xDEADBEEF);   // personality
        PutU16(slot + 8, 263);          // species
        slot[10] = 3;                   // level
        slot[11] = i == 0 ? 1 : 0;      // the first one is shiny
        PutU16(slot + 12, 10 + i);
        PutU16(slot + 14, 7);
        slot[16] = 1;
    }
}

MT_TEST(spawn_capability_waits_for_its_mailbox)
{
    const uint8_t *payload;

    MT_CHECK(MpBridge_CheckCaps(&(struct MpBridgeCaps){.spawns = 1}) != NULL); // needs the world mailbox
    SpawnBridge(false);
    Step(12);
    MT_EQ(SentFrame('w', 0, &payload), -1);
    PutU32(sMemory[MP_REGION_SPAWN], 0x31564D45);
    PutU16(sMemory[MP_REGION_SPAWN] + 4, 1);
    PutU16(sMemory[MP_REGION_SPAWN] + 6, 384);
    Step(2);
    MT_EQ(SentFrame('w', 0, &payload), 8);
    MT_EQ(payload[0], 1);
    MT_EQ(payload[2], 12);
    MT_EQ(payload[4] | (payload[5] << 8), 384);
    MT_EQ(payload[6], 20);
    MT_EQ(SentFrame('w', 1, &payload), -1); // announced once
}

MT_TEST(spawn_record_and_inbound_round_trip)
{
    uint8_t record[252];
    uint8_t frame[260];
    uint8_t *v = sMemory[MP_REGION_SPAWN];
    const uint8_t *payload;
    size_t n;

    SpawnBridge(true);
    memset(v + 12, 0x22, 116); // the game's spawn record (claims ride on it)
    Step(12);
    MT_EQ(SentFrame('w', 0, &payload), 8);
    MT_EQ(SentFrame('g', 0, &payload), 116);
    MT_EQ(payload[0], 0x22);
    MT_EQ(payload[115], 0x22);
    MakeSpawnInbound(record, 3);
    n = FrameOf(frame, 'n', record, sizeof(record));
    Feed(frame, n, 100);
    Step(2);
    MT_CHECK(!sNet.closed);
    MT_EQ(GetU32(v + 128), 2);                  // odd while writing, even after
    MT_CHECK(memcmp(v + 132, record, 252) == 0);
    MT_EQ(GetU32(v + 144), 100);                // the first slot's spawn id
    MakeSpawnInbound(record, 0);                // disconnected: off, no slots
    n = FrameOf(frame, 'n', record, sizeof(record));
    Feed(frame, n, 100);
    Step(2);
    MT_EQ(GetU32(v + 128), 4);
    MT_EQ(GetU32(v + 132), 0);
    MT_CHECK(!sNet.closed);
}

MT_TEST(spawn_inbound_is_validated)
{
    uint8_t record[252];
    uint8_t frame[260];
    size_t n;
    int bad;

    for (bad = 0; bad < 4; bad++)
    {
        SpawnBridge(true);
        Step(12);
        MakeSpawnInbound(record, 2);
        if (bad == 0)
            record[10] = 13;             // more than twelve slots
        else if (bad == 1)
            PutU32(record + 12 + 20, 0); // a listed slot without a spawn id
        else if (bad == 2)
            record[12 + 2 * 20] = 1;     // an unused slot that is not zero
        else
            PutU32(record, 0);           // slots without "enabled"
        n = FrameOf(frame, 'n', record, sizeof(record));
        Feed(frame, n, 100);
        Step(2);
        MT_CHECK(sNet.closed);
        MT_EQ(GetU32(sMemory[MP_REGION_SPAWN] + 128), 0); // nothing reached the game
    }
}

MT_TEST(spawn_inbound_needs_the_announcement)
{
    uint8_t record[252];
    uint8_t frame[260];
    size_t n;

    SpawnBridge(false); // a game without the spawn mailbox
    Step(12);
    MakeSpawnInbound(record, 1);
    n = FrameOf(frame, 'n', record, sizeof(record));
    Feed(frame, n, 100);
    Step(2);
    MT_CHECK(sNet.closed);
}

// Multiplayer Secret Bases (the third native extension, mp_bridge.h).
static void ExtrasBridge(bool mailbox)
{
    struct MpBridgeCaps caps;
    uint8_t *w = sMemory[MP_REGION_WORLD];
    uint8_t *x = sMemory[MP_REGION_EXTRAS];

    memset(&caps, 0, sizeof(caps));
    caps.world = 1;
    caps.extras = 1;
    Fresh(&caps);
    sNet.chunk = 0;
    Ready(0);
    PutU32(w, 0x31574D45);
    PutU16(w + 4, 1);
    PutU16(w + 6, 256);
    if (mailbox)
    {
        PutU32(x, 0x31584D45);
        PutU16(x + 4, 1);
        PutU16(x + 6, 384);
    }
}

// flags, baseAck, recordSeq, source slot, three zeros, the 160-byte base
static void MakeExtrasInbound(uint8_t record[176], uint32_t seq, uint8_t fill)
{
    memset(record, 0, 176);
    PutU32(record, 1);
    PutU32(record + 4, 5);
    PutU32(record + 8, seq);
    record[12] = 2;
    memset(record + 16, fill, 160);
}

MT_TEST(extras_capability_waits_for_its_mailbox_and_the_world)
{
    const uint8_t *payload;

    MT_CHECK(MpBridge_CheckCaps(&(struct MpBridgeCaps){.extras = 1}) != NULL); // needs the world mailbox
    MT_CHECK(MpBridge_CheckCaps(&(struct MpBridgeCaps){.world = 1, .extras = 2}) != NULL);
    ExtrasBridge(false);
    Step(12);
    MT_EQ(SentFrame('e', 0, &payload), -1);
    PutU32(sMemory[MP_REGION_EXTRAS], 0x31584D45);
    PutU16(sMemory[MP_REGION_EXTRAS] + 4, 1);
    PutU16(sMemory[MP_REGION_EXTRAS] + 6, 384);
    Step(2);
    MT_EQ(SentFrame('e', 0, &payload), 8);
    MT_EQ(payload[0], 1);
    MT_EQ(payload[2] | (payload[3] << 8), 384);
    MT_EQ(payload[4], 160);
    MT_EQ(SentFrame('e', 1, &payload), -1); // announced once
}

MT_TEST(extras_local_base_goes_out_when_it_changes_and_after_a_reconnect)
{
    uint8_t *x = sMemory[MP_REGION_EXTRAS];
    const uint8_t *payload;

    ExtrasBridge(true);
    Step(12);
    MT_EQ(SentFrame('x', 0, &payload), -1);      // nothing published yet (sequence 0)
    PutU32(x + 8, 2);                            // the game's local generation (even)
    PutU32(x + 12, 77);                          // base sequence
    x[16] = 1;                                   // has a base
    memset(x + 20, 0x5A, 160);
    Step(12);
    MT_EQ(SentFrame('x', 0, &payload), 168);
    MT_EQ(GetU32(payload), 77);
    MT_EQ(payload[4], 1);
    MT_EQ(payload[8], 0x5A);
    MT_EQ(payload[167], 0x5A);
    Step(40);
    MT_EQ(SentFrame('x', 1, &payload), -1);      // unchanged: not sent again
    PutU32(x + 12, 78);
    memset(x + 20, 0x6B, 160);
    Step(12);
    MT_EQ(SentFrame('x', 1, &payload), 168);
    MT_EQ(GetU32(payload), 78);
    MT_EQ(payload[8], 0x6B);
    PutU32(x + 8, 3);                            // odd: the game is writing: nothing is read
    PutU32(x + 12, 79);
    Step(12);
    MT_EQ(SentFrame('x', 2, &payload), -1);
}

MT_TEST(extras_inbound_base_reaches_the_game)
{
    uint8_t record[176];
    uint8_t frame[200];
    uint8_t *x = sMemory[MP_REGION_EXTRAS];
    size_t n;

    ExtrasBridge(true);
    Step(12);
    MakeExtrasInbound(record, 9, 0x33);
    n = FrameOf(frame, 'y', record, sizeof(record));
    Feed(frame, n, 100);
    Step(2);
    MT_CHECK(!sNet.closed);
    MT_EQ(GetU32(x + 180), 2);                   // odd while writing, even after
    MT_CHECK(memcmp(x + 184, record, 176) == 0);
    MT_EQ(GetU32(x + 192), 9);                   // the record number
    MT_EQ(x[200], 0x33);
    MT_EQ(x[359], 0x33);
    MakeExtrasInbound(record, 10, 0x44);
    n = FrameOf(frame, 'y', record, sizeof(record));
    Feed(frame, n, 100);
    Step(2);
    MT_EQ(GetU32(x + 180), 4);
    MT_EQ(GetU32(x + 192), 10);
}

MT_TEST(extras_inbound_is_validated)
{
    uint8_t record[176];
    uint8_t frame[200];
    size_t n;
    int bad;

    for (bad = 0; bad < 4; bad++)
    {
        ExtrasBridge(true);
        Step(12);
        MakeExtrasInbound(record, 3, 0x11);
        if (bad == 0)
            PutU32(record, 2);          // a flag the ABI does not have
        else if (bad == 1)
            record[12] = 4;             // a roster slot beyond four
        else if (bad == 2)
            record[14] = 1;             // padding that is not zero
        else
            n = 0;                      // a short frame, below
        n = FrameOf(frame, 'y', record, bad == 3 ? 100 : sizeof(record));
        Feed(frame, n, 100);
        Step(2);
        MT_CHECK(sNet.closed);
        MT_EQ(GetU32(sMemory[MP_REGION_EXTRAS] + 180), 0); // nothing reached the game
    }
}

MT_TEST(extras_inbound_needs_the_announcement)
{
    uint8_t record[176];
    uint8_t frame[200];
    size_t n;

    ExtrasBridge(false); // a game without the extras mailbox
    Step(12);
    MakeExtrasInbound(record, 1, 0x11);
    n = FrameOf(frame, 'y', record, sizeof(record));
    Feed(frame, n, 100);
    Step(2);
    MT_CHECK(sNet.closed);
}

// Position of the first complete outbound frame of `kind` among all frames
// sent, or -1.
static int FrameIndex(uint8_t kind)
{
    size_t at = 0;
    int index = 0;

    while (at + 5 <= sNet.sentLength)
    {
        uint32_t size = GetBE32(sNet.sent + at);

        if (at + 4 + size > sNet.sentLength)
            break;
        if (sNet.sent[at + 4] == kind)
            return index;
        at += 4 + size;
        index++;
    }
    return -1;
}

// A game that is already running when its companion (re)starts announces all
// its capabilities at once while its records are due every frame (a slow 3D
// frame on software OpenGL). The spawn record still never overtakes its 'w'
// announcement: the companion drops a connection whose first 'g' precedes it.
MT_TEST(spawn_record_never_overtakes_its_announcement)
{
    struct MpBridgeCaps caps;
    uint8_t *w = sMemory[MP_REGION_WORLD];
    uint8_t *v = sMemory[MP_REGION_SPAWN];
    int round, i;

    // The native game's capability set (its log: "save 1 trade 1 emergency 2
    // battle 3/1/1 window 1 menu 2 effects 0/0/0 reset 2/1 world 1 spawns 1").
    memset(&caps, 0, sizeof(caps));
    caps.save = 1;
    caps.trade = 1;
    caps.emergencySave = 2;
    caps.battle = 3;
    caps.battleResume = 1;
    caps.battleExecution = 1;
    caps.battleAdmissionCleanup = 1;
    caps.saveWindow = 1;
    caps.menu = 2;
    caps.reset = 2;
    caps.resetInput = 1;
    caps.world = 1;
    caps.spawns = 1;
    MT_CHECK(MpBridge_CheckCaps(&caps) == NULL);
    Fresh(&caps);
    sBridge.ops.milliseconds = FakeMilliseconds;
    sBridge.ops.battleLooks = FakeBattleLooks;
    sNet.chunk = 0;
    sNowMs = 1000;
    Ready(0);
    SaveReady();
    BattleReady();
    PutU32(sMemory[MP_REGION_MENU], 0x31554D45);
    PutU16(sMemory[MP_REGION_MENU] + 4, 2);
    PutU16(sMemory[MP_REGION_MENU] + 6, 512);
    PutU32(sMemory[MP_REGION_RESET], 0x31524D45);
    PutU16(sMemory[MP_REGION_RESET] + 4, 2);
    PutU16(sMemory[MP_REGION_RESET] + 6, 128);
    PutU32(w, 0x31574D45);
    PutU16(w + 4, 1);
    PutU16(w + 6, 256);
    PutU32(v, 0x31564D45);
    PutU16(v + 4, 1);
    PutU16(v + 6, 384);
    memset(v + 12, 0x22, 116);
    for (round = 0; round < 2; round++)
    {
        sNet.sentLength = 0;
        for (i = 0; i < 8; i++) // 2.5 frames a second: every record is due every frame
        {
            sNowMs += 400;
            Step(1);
        }
        MT_EQ(sNet.connects, round + 1);
        MT_CHECK(!sNet.closed);
        MT_EQ(FrameIndex('H'), 0);
        MT_CHECK(FrameIndex('w') > 0);
        MT_CHECK(FrameIndex('g') > FrameIndex('w'));
        MT_CHECK(FrameIndex('P') > 0);
        MT_EQ(sNet.errors, 0);
        // The companion restarts: the game sees it close, then reconnects.
        sNet.hangup = true;
        sNowMs += 400;
        Step(1);
        MT_CHECK(sNet.closed);
        MT_EQ(sNet.errors, 1);
        sNet.errors = 0;
        Step(119);
        MT_EQ(sNet.connects, round + 1);
    }
}

// tests/native/windows-input-guard.c
static uint32_t AddKeys(struct MpBridgeInput *s, uint32_t keys)
{
    return MpBridge_FilterInput(s, s->keys | keys);
}

static uint32_t ClearKeys(struct MpBridgeInput *s, uint32_t keys)
{
    return MpBridge_FilterInput(s, s->keys & ~keys);
}

MT_TEST(reset_chord_guard_masks)
{
    uint32_t keys;

    for (keys = 0; keys < 1024; keys++)
    {
        struct MpBridgeInput state = {0};
        uint32_t expected = (keys & 15) == 15 ? keys & ~15u : keys;

        MT_EQ(MpBridge_FilterInput(&state, keys), expected);
        MT_EQ(MpBridge_FilterInput(&state, keys), expected);
        MT_EQ(ClearKeys(&state, 1023), 0);
        MT_CHECK(!state.chordHeld);
    }
}

MT_TEST(reset_chord_guard_orders)
{
    static const uint32_t bits[] = {1, 2, 4, 8};
    unsigned a, b, c, d, e, f, g, h, i;
    uint32_t other;
    int failures = 0;

    for (a = 0; a < 4; a++)
    for (b = 0; b < 4; b++)
    for (c = 0; c < 4; c++)
    for (d = 0; d < 4; d++)
    {
        unsigned down[4];

        if (a == b || a == c || a == d || b == c || b == d || c == d)
            continue;
        down[0] = a; down[1] = b; down[2] = c; down[3] = d;
        for (e = 0; e < 4; e++)
        for (f = 0; f < 4; f++)
        for (g = 0; g < 4; g++)
        for (h = 0; h < 4; h++)
        {
            unsigned up[4];

            if (e == f || e == g || e == h || f == g || f == h || g == h)
                continue;
            up[0] = e; up[1] = f; up[2] = g; up[3] = h;
            for (other = 0; other < 1024; other += 16)
            {
                struct MpBridgeInput state = {0};
                uint32_t raw = other;

                failures += MpBridge_FilterInput(&state, other) != other;
                for (i = 0; i < 4; i++)
                {
                    raw |= bits[down[i]];
                    failures += AddKeys(&state, bits[down[i]]) != (i == 3 ? other : raw);
                }
                failures += !state.chordHeld;
                failures += AddKeys(&state, 15) != other;
                failures += MpBridge_FilterInput(&state, raw) != other;
                for (i = 0; i < 4; i++)
                {
                    raw &= ~bits[up[i]];
                    failures += ClearKeys(&state, bits[up[i]]) != other;
                    failures += state.chordHeld != (i != 3);
                }
                failures += AddKeys(&state, 1) != (other | 1);
                failures += ClearKeys(&state, 1) != other;
            }
        }
    }
    MT_EQ(failures, 0);
}

MT_TEST(reset_chord_guard_native_capability)
{
    struct MpBridgeInput state = {0};

    MT_EQ(MpBridge_FilterInput(&state, 15), 0);
    MT_EQ(ClearKeys(&state, 1), 0);
    MT_EQ(AddKeys(&state, 1), 0);
    MT_EQ(ClearKeys(&state, 7), 0);
    MT_EQ(AddKeys(&state, 16), 16);
    MT_EQ(ClearKeys(&state, 8), 16);
    MT_CHECK(!state.chordHeld);
    MT_EQ(AddKeys(&state, 8), 24); // ordinary Start
    MT_EQ(ClearKeys(&state, 8), 16);
    MT_EQ(MpBridge_FilterInput(&state, 15), 0);
    MT_EQ(MpBridge_FilterInput(&state, 4), 0); // no synthetic Select
    MT_EQ(MpBridge_FilterInput(&state, 0), 0);
    MT_EQ(MpBridge_FilterInput(&state, 4), 4); // fresh Select

    memset(&state, 0, sizeof(state));
    MpBridge_EnableNativeChord(&state, true);
    MT_EQ(MpBridge_FilterInput(&state, 31), 31);
    MT_CHECK(state.chordHeld && state.chordRouted);
    MT_EQ(ClearKeys(&state, 1), 16);
    MT_EQ(AddKeys(&state, 1), 16);
    MT_EQ(ClearKeys(&state, 15), 16);
    MT_EQ(AddKeys(&state, 15), 31);
    MpBridge_EnableNativeChord(&state, false);
    MT_EQ(MpBridge_FilterInput(&state, 31), 16);
    MpBridge_EnableNativeChord(&state, true);
    MT_EQ(MpBridge_FilterInput(&state, 31), 16);
    MT_EQ(ClearKeys(&state, 15), 16);
    MT_EQ(AddKeys(&state, 15), 31);
    MT_EQ(ClearKeys(&state, 15), 16);
    MpBridge_EnableNativeChord(&state, false);
    MT_EQ(AddKeys(&state, 15), 16);
    MpBridge_EnableNativeChord(&state, true);
    MT_EQ(MpBridge_FilterInput(&state, 31), 16);
}

int main(void)
{
    MT_RUN(rom_startup_does_not_write);
    MT_RUN(hello_survives_partial_sends_then_pose);
    MT_RUN(snapshot_commits_after_complete_frame);
    MT_RUN(presence_detail_rides_the_local_pose_only);
    MT_RUN(malformed_length_stops_and_clears);
    MT_RUN(duplicate_peer_tokens_refused);
    MT_RUN(foreign_mailbox_never_written);
    MT_RUN(running_needs_the_animation_capability);
    MT_RUN(crc32_matches_zlib);
    MT_RUN(save_command_and_response);
    MT_RUN(save_command_with_bad_crc_refused);
    MT_RUN(world_inbound_validated);
    MT_RUN(world_rival_record_accepted);
    MT_RUN(world_outbreak_marker_accepted);
    MT_RUN(world_wallet_money_and_reward_words);
    MT_RUN(slow_frame_drains_the_backlog);
    MT_RUN(clock_paces_records_by_real_time);
    MT_RUN(no_clock_keeps_the_six_frame_cadence);
    MT_RUN(battle_capability_waits_for_its_mailbox);
    MT_RUN(battle_looks_follow_their_announcement);
    MT_RUN(battle_looks_need_the_game);
    MT_RUN(native_battle_phases_reach_the_game);
    MT_RUN(spawn_capability_waits_for_its_mailbox);
    MT_RUN(spawn_record_and_inbound_round_trip);
    MT_RUN(spawn_inbound_is_validated);
    MT_RUN(spawn_inbound_needs_the_announcement);
    MT_RUN(spawn_record_never_overtakes_its_announcement);
    MT_RUN(extras_capability_waits_for_its_mailbox_and_the_world);
    MT_RUN(extras_local_base_goes_out_when_it_changes_and_after_a_reconnect);
    MT_RUN(extras_inbound_base_reaches_the_game);
    MT_RUN(extras_inbound_is_validated);
    MT_RUN(extras_inbound_needs_the_announcement);
    MT_RUN(reset_chord_guard_masks);
    MT_RUN(reset_chord_guard_orders);
    MT_RUN(reset_chord_guard_native_capability);
    return MT_REPORT();
}
