/*
 * Native multiplayer bridge: see include/platform/mp_bridge.h.
 *
 * A line-for-line port of the app repository's bridge/mgba.lua (the production
 * emulator bridge): every check and every order of operations is that file's,
 * so the companion sees the same bridge it has been tested against. Comments
 * name the Lua function each C function ports. Offsets below are 0-based; the
 * Lua string offsets they come from are 1-based.
 *
 * This object is a preserved object (Makefile_pc PRESERVED_OBJS): the
 * connection survives a soft reset as it does in the emulator, and the bridge
 * then sees the mailboxes reinitialized and reconnects (runtimeRestart).
 */
#include <string.h>

#include "platform/mp_bridge.h"

#define MP_PRESENCE_MAGIC 0x31504D45u /* EMP1 */
#define MP_SAVE_MAGIC     0x31534D45u /* EMS1 */
#define MP_BATTLE_MAGIC   0x31424D45u /* EMB1 */
#define MP_MENU_MAGIC     0x31554D45u /* EMU1 */
#define MP_RESET_MAGIC    0x31524D45u /* EMR1 */
#define MP_WORLD_MAGIC    0x31574D45u /* EMW1 */
#define MP_SPAWN_MAGIC    0x31564D45u /* EMV1: shared wild Pokemon (native extension) */
#define MP_SPAWN_BYTES    384
#define MP_SPAWN_LOCAL    116
#define MP_SPAWN_INBOUND  252

#define MP_EXTRAS_MAGIC   0x31584D45u /* EMX1: multiplayer Secret Bases (native extension) */
#define MP_EXTRAS_BYTES   384
#define MP_EXTRAS_LOCAL   168
#define MP_EXTRAS_INBOUND 176

#define RECONNECT_FRAMES 120
#define MAX_INBOUND_FRAME 256
#define RX_LIMIT 260

/*
 * Where this port deliberately differs from mgba.lua: frame rate. mGBA always
 * runs the game at 60 frames a second, so mgba.lua can bound its work per
 * emulated frame (1 KiB received, one frame sent) and publish the pose every 6
 * frames. The native game slows down when drawing is slow (the 3D view on a
 * slow graphics card: 2-5 frames a second on the cloud computer's software
 * OpenGL), while the companion keeps sending snapshots by the clock (10 a
 * second) and judges a pose stale after 0.5 s. At 2.4 frames a second the Lua
 * budgets let snapshots back up in the socket, every save command waited 6-12 s
 * behind them and a live checkpoint timed out (2 clients, 3D build), and a pose
 * every 6 frames (2.5 s) reads as a paused game. So, per game frame:
 */
#define RECEIVE_ROUNDS 64   /* 256-byte reads: up to 16 KiB (mgba.lua: 4, 1 KiB) */
#define FLUSH_FRAMES 8      /* complete frames sent (mgba.lua: 1) */
#define RECORD_MS 100       /* pose and shared-world record cadence with a clock
                               (mgba.lua: every 6 frames, 100 ms at 60 frames a second) */

/* ------------------------------------------------------------------------ */
/* Mailbox access: little endian, as emu:read32/write32 see the GBA's memory. */

static uint32_t R32(const struct MpBridge *b, enum MpRegion region, uint32_t offset)
{
    const volatile uint8_t *p = b->ops.region[region] + offset;

    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t R16(const struct MpBridge *b, enum MpRegion region, uint32_t offset)
{
    const volatile uint8_t *p = b->ops.region[region] + offset;

    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static void W32(struct MpBridge *b, enum MpRegion region, uint32_t offset, uint32_t value)
{
    volatile uint8_t *p = b->ops.region[region] + offset;

    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void ReadRange(const struct MpBridge *b, enum MpRegion region, uint32_t offset,
                      uint8_t *out, size_t length)
{
    const volatile uint8_t *p = b->ops.region[region] + offset;
    size_t i;

    for (i = 0; i < length; i++)
        out[i] = p[i];
}

static uint32_t U32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t U16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static void PutU16(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static bool AllZero(const uint8_t *p, size_t length)
{
    size_t i;

    for (i = 0; i < length; i++)
        if (p[i] != 0)
            return false;
    return true;
}

static bool Enabled(uint32_t marker)
{
    return marker != 0;
}

/* ------------------------------------------------------------------------ */

uint32_t MpBridge_Crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    int bit;

    for (i = 0; i < length; i++)
    {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0);
    }
    return crc ^ 0xFFFFFFFFu;
}

/* mgba.lua's start-up asserts on cfg, for the markers this build carries. */
const char *MpBridge_CheckCaps(const struct MpBridgeCaps *c)
{
    if (c->presenceAnimation > 1)
        return "invalid presence animation capability marker";
    if (c->presenceDetail > 1)
        return "invalid presence detail capability marker";
    if (c->battleFfa && (c->battle != 3 || c->battleFfa != 1))
        return "invalid free-for-all battle capability";
    if (c->battleFfa3 && (c->battleFfa != 1 || c->battleFfa3 != 1))
        return "invalid three-player free-for-all battle capability";
    if (c->menuRelease && (c->menu != 2 || c->battleResume != 1 || c->menuRelease != 1))
        return "invalid menu release capability";
    if (c->save > 1)
        return "invalid save mailbox ABI";
    if (c->trade && (!c->save || c->trade != 1))
        return "invalid trade capability marker";
    if (c->emergencySave && (!c->trade || c->emergencySave > 2))
        return "invalid emergency save capability marker";
    if (c->battle && (!c->trade || c->battle > 3))
        return "invalid development battle capability";
    if (c->battleResume && (c->battle != 3 || c->battleResume != 1))
        return "invalid native battle resume capability";
    if (c->saveWindow && (!c->trade || c->saveWindow != 1))
        return "invalid read-only save window";
    if (c->saveImportWindow && (!c->saveWindow || c->saveImportWindow != 1))
        return "invalid save import window";
    if (c->menuOpenPage && (c->menu != 2 || c->menuOpenPage != 1))
        return "invalid menu open page capability";
    if (c->tower && (c->tower != 1 || c->menuOpenPage != 1 || c->world != 1))
        return "invalid Battle Tower capability";
    if (c->menu > 2)
        return "invalid Players menu capability";
    if (c->tradeEffectPrepare && (!c->trade || c->menu != 2 || c->tradeEffectPrepare != 1))
        return "invalid private trade record capability";
    if (c->tradeEffectSave && (!c->tradeEffectPrepare || !c->saveWindow || c->tradeEffectSave != 1))
        return "invalid full trade candidate capability";
    if (c->tradeEffectChoice && (!c->tradeEffectPrepare || c->tradeEffectChoice != 1))
        return "invalid native move presentation capability";
    if (c->battleAdmissionCleanup
     && (!c->battleResume || c->battleExecution != 1 || c->menu != 2
      || c->emergencySave != 2 || c->battleAdmissionCleanup != 1))
        return "invalid native battle admission cleanup capability";
    if (c->reset > 2)
        return "invalid personal encounter capability";
    if (c->resetInput && (c->reset != 2 || c->resetInput != 1))
        return "invalid native reset input capability";
    if (c->world > 1)
        return "invalid shared-world capability";
    if (c->spawns && (!c->world || c->spawns != 1))
        return "invalid shared spawn capability";
    if (c->extras && (!c->world || c->extras != 1))
        return "invalid secret base capability";
    return NULL;
}

static void Log(struct MpBridge *b, bool error, const char *message)
{
    if (b->ops.log != NULL)
        b->ops.log(b->ops.context, error, message);
}

/* routeResetChord */
static void RouteResetChord(struct MpBridge *b, bool enabled)
{
    if (b->ops.routeResetChord != NULL)
        b->ops.routeResetChord(b->ops.context, enabled && Enabled(b->caps.resetInput));
}

/* forgetRuntime */
static void ForgetRuntime(struct MpBridge *b)
{
    b->observedPresence = b->hasPreviousFrame = false;
    b->observedSave = b->hasPreviousSaveFrame = false;
    b->previousSaveState = 0;
    b->observedBattle = b->hasPreviousBattleFrame = false;
    b->previousBattleState = 0;
    b->observedMenu = b->hasPreviousMenuFrame = false;
    b->observedReset = b->hasPreviousResetFrame = false;
}

/* headerReady */
static bool HeaderReady(const struct MpBridge *b)
{
    return R32(b, MP_REGION_PRESENCE, 0) == MP_PRESENCE_MAGIC
        && R16(b, MP_REGION_PRESENCE, 4) == 1 && R16(b, MP_REGION_PRESENCE, 6) == 128;
}

/* frame */
static size_t Frame(uint8_t *out, uint8_t kind, const uint8_t *payload, size_t length,
                    const uint8_t *extra, size_t extraLength)
{
    size_t size = 1 + length + extraLength;

    out[0] = (uint8_t)(size >> 24);
    out[1] = (uint8_t)(size >> 16);
    out[2] = (uint8_t)(size >> 8);
    out[3] = (uint8_t)size;
    out[4] = kind;
    if (length != 0)
        memcpy(out + 5, payload, length);
    if (extraLength != 0)
        memcpy(out + 5 + length, extra, extraLength);
    return 4 + size;
}

/* worldHeaderReady */
static bool WorldHeaderReady(const struct MpBridge *b)
{
    return Enabled(b->caps.world) && R32(b, MP_REGION_WORLD, 0) == MP_WORLD_MAGIC
        && R16(b, MP_REGION_WORLD, 4) == 1 && R16(b, MP_REGION_WORLD, 6) == 256;
}

/* printableNotice */
static bool PrintableNotice(const uint8_t *text, size_t length)
{
    const uint8_t *zero = memchr(text, 0, length);
    size_t last = zero != NULL ? (size_t)(zero - text) : length;
    size_t i;

    if (zero != NULL && !AllZero(zero, length - last))
        return false;
    for (i = 0; i < last; i++)
        if (text[i] < 32 || text[i] > 126)
            return false;
    return true;
}

/* flags (valid 1, present 2, my match 4), completed and cleared masks (five checkpoints), here (0 none, 1-5),
 * its state (0 none, 1-4), three zero bytes; present and my match need a checkpoint */
static bool ValidRivalRecord(const uint8_t *r)
{
    if (AllZero(r, 8))
        return true;
    if (!(r[0] & 1) || (r[0] & ~7u) || (r[1] & ~0x1Fu) || (r[2] & ~0x1Fu) || r[3] > 5 || r[4] > 4 || !AllZero(r + 5, 3))
        return false;
    if ((r[3] == 0) != (r[4] == 0))
        return false;
    return !((r[0] & 6) && r[3] == 0);
}

/* Update 18's outbreak marker at 192 (all zero without one): valid 1, map group, map number, a zero byte, the species
 * (LE16; the host sends 0 and the game does not read it), two zero bytes. region_map.c draws it on the Town Map and
 * checks the map itself. */
static bool ValidOutbreakMarker(const uint8_t *r)
{
    if (AllZero(r, 8))
        return true;
    return r[0] == 1 && r[3] == 0 && U16(r + 4) <= 1700 && r[6] == 0 && r[7] == 0;
}

/* validWorldInbound */
static bool ValidWorldInbound(const struct MpBridge *b, const uint8_t *p, size_t length)
{
    uint32_t seen[3];
    unsigned seenCount = 0;
    unsigned i;
    unsigned k;
    uint32_t walletSeq;
    int32_t walletDelta;
    uint32_t teammate;
    const uint8_t *e;

    if (length != 208 || U32(p) > 3)
        return false;
    for (i = 0; i < 3; i++)
    {
        const uint8_t *o = p + 20 + i * 20;
        uint32_t token = U32(o);

        if (token == 0)
        {
            if (!AllZero(o, 20))
                return false;
            continue;
        }
        for (k = 0; k < seenCount; k++)
            if (seen[k] == token)
                return false;
        if (o[6] > 7 || o[7] > 1 || o[10] > 3 || o[11] != 0 || U16(o + 4) > 1700 || U16(o + 8) > 1700)
            return false;
        seen[seenCount++] = token;
    }
    e = p + 80;
    if (e[6] > 100 || e[7] > 31 || e[20] > 6 || U16(e + 4) > 1700 || !AllZero(e + 21, 11))
        return false;
    if (U32(e) == 0 && !AllZero(e, 32))
        return false;
    /* notice: id at 112, 64 printable characters */
    if (!PrintableNotice(p + 116, 64))
        return false;
    walletSeq = U32(p + 180);
    walletDelta = (int32_t)U32(p + 184);
    teammate = U32(p + 188);
    /* money (+-999,999) or a reward word: bit 30, kind 1-5 in bits 24-27 (src/multiplayer_world.c); with the Battle
     * Tower also kinds 6 (its record) and 7 (a milestone Pokemon) */
    if (walletSeq > 0xFFFF || walletDelta < -999999
     || (walletDelta > 999999 && (((uint32_t)walletDelta & 0x70000000u) != 0x40000000u
                                  || ((((uint32_t)walletDelta) >> 24) & 0xFu) < 1
                                  || ((((uint32_t)walletDelta) >> 24) & 0xFu) > (Enabled(b->caps.tower) ? 7u : 5u)))
     || (walletSeq == 0) != (walletDelta == 0))
        return false;
    if (teammate != 0)
    {
        for (k = 0; k < seenCount && seen[k] != teammate; k++)
            ;
        if (k == seenCount)
            return false;
    }
    /* Update 18's outbreak marker at 192 and Update 31's RivalRecord at 200 (the rules of multiplayer_rival.c
     * ValidRecord), each eight bytes and all zero without one. This check required all sixteen bytes to be zero, so
     * the first rival record and the first outbreak the companion relayed stopped the bridge for good ("invalid
     * shared-world update"): the game lost every other player and its Players menu, while the server still listed it
     * (the owner's report of 2026-10-19). */
    return ValidOutbreakMarker(p + 192) && ValidRivalRecord(p + 200);
}

static uint32_t Odd(uint32_t current)
{
    return (current & ~1u) + 1;
}

/* commitWorld */
static bool CommitWorld(struct MpBridge *b, const uint8_t *p, size_t length)
{
    uint32_t odd;
    uint32_t offset;

    if (!WorldHeaderReady(b) || length != 208)
        return false;
    odd = Odd(R32(b, MP_REGION_WORLD, 44));
    W32(b, MP_REGION_WORLD, 44, odd);
    for (offset = 0; offset <= 204; offset += 4)
        W32(b, MP_REGION_WORLD, 48 + offset, U32(p + offset));
    W32(b, MP_REGION_WORLD, 44, odd + 1);
    return true;
}

/* The shared-spawn mailbox (native extension, no mgba.lua counterpart). */
static bool SpawnHeaderReady(const struct MpBridge *b)
{
    return Enabled(b->caps.spawns) && b->ops.region[MP_REGION_SPAWN] != NULL
        && b->ops.regionSize[MP_REGION_SPAWN] == MP_SPAWN_BYTES
        && R32(b, MP_REGION_SPAWN, 0) == MP_SPAWN_MAGIC
        && R16(b, MP_REGION_SPAWN, 4) == b->caps.spawns && R16(b, MP_REGION_SPAWN, 6) == MP_SPAWN_BYTES;
}

/* Slots: count at most 12, every listed slot with a spawn id, the unused ones
 * zero; the flags word only carries "enabled", and slots need it. */
static bool ValidSpawnInbound(const uint8_t *p, size_t length)
{
    uint32_t count, i;

    if (length != MP_SPAWN_INBOUND || (U32(p) & ~1u) != 0 || p[11] != 0)
        return false;
    count = p[10];
    if (count > 12 || (count != 0 && !(U32(p) & 1u)))
        return false;
    for (i = 0; i < 12; i++)
    {
        const uint8_t *slot = p + 12 + i * 20;
        if (i < count ? U32(slot) == 0 : !AllZero(slot, 20))
            return false;
    }
    return true;
}

static bool CommitSpawn(struct MpBridge *b, const uint8_t *p, size_t length)
{
    uint32_t odd;
    uint32_t offset;

    if (!SpawnHeaderReady(b) || length != MP_SPAWN_INBOUND)
        return false;
    odd = Odd(R32(b, MP_REGION_SPAWN, 128));
    W32(b, MP_REGION_SPAWN, 128, odd);
    for (offset = 0; offset < MP_SPAWN_INBOUND; offset += 4)
        W32(b, MP_REGION_SPAWN, 132 + offset, U32(p + offset));
    W32(b, MP_REGION_SPAWN, 128, odd + 1);
    return true;
}

/* The extras mailbox (native extension, no mgba.lua counterpart). */
static bool ExtrasHeaderReady(const struct MpBridge *b)
{
    return Enabled(b->caps.extras) && b->ops.region[MP_REGION_EXTRAS] != NULL
        && b->ops.regionSize[MP_REGION_EXTRAS] == MP_EXTRAS_BYTES
        && R32(b, MP_REGION_EXTRAS, 0) == MP_EXTRAS_MAGIC
        && R16(b, MP_REGION_EXTRAS, 4) == b->caps.extras && R16(b, MP_REGION_EXTRAS, 6) == MP_EXTRAS_BYTES;
}

/* Flags carry only "enabled"; the source slot byte is followed by three zeros. */
static bool ValidExtrasInbound(const uint8_t *p, size_t length)
{
    if (length != MP_EXTRAS_INBOUND || (U32(p) & ~1u) != 0 || p[13] != 0 || p[14] != 0 || p[15] != 0)
        return false;
    return p[12] < 4;
}

static bool CommitExtras(struct MpBridge *b, const uint8_t *p, size_t length)
{
    uint32_t odd;
    uint32_t offset;

    if (!ExtrasHeaderReady(b) || length != MP_EXTRAS_INBOUND)
        return false;
    odd = Odd(R32(b, MP_REGION_EXTRAS, 180));
    W32(b, MP_REGION_EXTRAS, 180, odd);
    for (offset = 0; offset < MP_EXTRAS_INBOUND; offset += 4)
        W32(b, MP_REGION_EXTRAS, 184 + offset, U32(p + offset));
    W32(b, MP_REGION_EXTRAS, 180, odd + 1);
    return true;
}

/* menuHeaderReady */
static bool MenuHeaderReady(const struct MpBridge *b)
{
    return Enabled(b->caps.menu) && R32(b, MP_REGION_MENU, 0) == MP_MENU_MAGIC
        && R16(b, MP_REGION_MENU, 4) == b->caps.menu && R16(b, MP_REGION_MENU, 6) == 512;
}

/* resetHeaderReady */
static bool ResetHeaderReady(const struct MpBridge *b)
{
    return Enabled(b->caps.reset) && R32(b, MP_REGION_RESET, 0) == MP_RESET_MAGIC
        && R16(b, MP_REGION_RESET, 4) == b->caps.reset && R16(b, MP_REGION_RESET, 6) == 128;
}

/* commitReset */
static bool CommitReset(struct MpBridge *b, const uint8_t *p, size_t length)
{
    uint32_t seq, session, incarnation, encounter, attempt, status;
    uint32_t generation;
    uint32_t odd;
    uint32_t offset;

    if (!ResetHeaderReady(b) || length != 32)
        return false;
    seq = U32(p);
    session = U32(p + 4);
    incarnation = U32(p + 8);
    encounter = U32(p + 12);
    attempt = U32(p + 16);
    status = U32(p + 20);
    if (seq == 0 || session == 0 || incarnation == 0 || encounter < 0x1001 || encounter > 0x1019
     || status > (b->caps.reset == 2 ? 2u : 1u) || !AllZero(p + 24, 8)
     || (status != 0 && attempt != 0) || (status == 0 && attempt == 0))
        return false;
    if (seq != R32(b, MP_REGION_RESET, 16) || session != R32(b, MP_REGION_RESET, 20)
     || incarnation != R32(b, MP_REGION_RESET, 24) || encounter != R32(b, MP_REGION_RESET, 28))
        return true;
    if (status == 0 && attempt <= R32(b, MP_REGION_RESET, 32))
        return false;
    generation = R32(b, MP_REGION_RESET, 48);
    odd = generation + (generation % 2 == 0 ? 1 : 2);
    W32(b, MP_REGION_RESET, 48, odd);
    for (offset = 0; offset <= 28; offset += 4)
        W32(b, MP_REGION_RESET, 52 + offset, U32(p + offset));
    W32(b, MP_REGION_RESET, 48, odd + 1);
    return true;
}

/* menuText */
static bool MenuText(const uint8_t *value, size_t length)
{
    const uint8_t *zero = memchr(value, 0, length);
    size_t i;

    if (zero == NULL || !AllZero(zero, length - (size_t)(zero - value)))
        return false;
    for (i = 0; value + i < zero; i++)
        if (value[i] < 32 || value[i] > 126)
            return false;
    return true;
}

/* validMenuScreen */
static bool ValidMenuScreen(const struct MpBridge *b, const uint8_t *p, size_t length)
{
    uint32_t ack, revision, flags, count, back, admission;
    uint32_t i;

    if (length != 240)
        return false;
    ack = U32(p);
    revision = U32(p + 4);
    flags = p[8];
    count = p[9];
    back = U16(p + 10);
    admission = U16(p + 236);
    /* Battle Tower: Legends (tower=1, src/tower_legends.c): bytes 238-239 a Tower screen's style (0-8) and argument
     * (a floor, 0-100; zero without a style); flags 4 a solo floor's command in the rows area (no rows), flags 5 open
     * Hyper Training (no rows, the rows area zero). */
    if (Enabled(b->caps.tower))
    {
        if (p[238] > 8 || p[239] > 100 || (p[238] == 0 && p[239] != 0))
            return false;
        if (flags == 4 || flags == 5)
        {
            const uint8_t *command = p + 68;

            if (ack == 0 || revision == 0 || count != 0 || admission != 0 || (back != 0 && back != 7)
             || !MenuText(p + 12, 24) || !MenuText(p + 36, 32))
                return false;
            if (flags == 5)
                return AllZero(p + 68, 168);
            return U32(command) == 0x31525754u /* "TWR1" */ && U32(command + 4) != 0 && command[4] != 0
                && command[12] >= 1 && command[12] <= 100 && command[13] == 1
                && AllZero(command + 14, 18) && AllZero(p + 100, 136);
        }
    }
    /* Flags 1 close, 2 reopen a held trade, 3 release a refused Enter battle latch (menuRelease). */
    if (ack == 0 || revision == 0 || flags > (b->caps.menu == 2 ? (Enabled(b->caps.menuRelease) ? 3u : 2u) : 1u) || count > 6
     || (back != 0 && back != 7) || !MenuText(p + 12, 24) || !MenuText(p + 36, 32)
     || !AllZero(p + 68 + count * 28, 168 - count * 28) || (!Enabled(b->caps.tower) && !AllZero(p + 238, 2)))
        return false;
    if (admission != 0 && (!Enabled(b->caps.battleResume) || b->caps.menu != 2 || flags != 0
     || admission > count || U16(p + 68 + (admission - 1) * 28) != admission))
        return false;
    for (i = 0; i < count; i++)
    {
        uint32_t action = U16(p + 68 + i * 28);

        if ((action != 0 && action != i + 1) || !MenuText(p + 70 + i * 28, 26))
            return false;
    }
    return true;
}

/* commitMenu */
static bool CommitMenu(struct MpBridge *b, const uint8_t *p)
{
    uint32_t odd;
    uint32_t offset;

    if (!MenuHeaderReady(b))
        return false;
    /* Stale replies remain harmless even if the local user closed/reopened. */
    if (U32(p) != R32(b, MP_REGION_MENU, 16))
        return true;
    odd = Odd(R32(b, MP_REGION_MENU, 32));
    W32(b, MP_REGION_MENU, 32, odd);
    for (offset = 0; offset <= 236; offset += 4)
        W32(b, MP_REGION_MENU, 36 + offset, U32(p + offset));
    W32(b, MP_REGION_MENU, 32, odd + 1);
    return true;
}

/* saveHeaderReady */
static bool SaveHeaderReady(const struct MpBridge *b)
{
    uint32_t maxState = Enabled(b->caps.emergencySave) ? 10 : (Enabled(b->caps.trade) ? 8 : 7);

    return Enabled(b->caps.save) && R32(b, MP_REGION_SAVE, 0) == MP_SAVE_MAGIC
        && R16(b, MP_REGION_SAVE, 4) == 1 && R16(b, MP_REGION_SAVE, 6) == 512
        && R32(b, MP_REGION_SAVE, 12) <= maxState
        && R32(b, MP_REGION_SAVE, 16) == 0 && R32(b, MP_REGION_SAVE, 20) == 0;
}

/* saveControl */
static bool SaveControl(const struct MpBridge *b, const uint8_t *p, size_t length, bool command,
                        struct MpBridgeSave *out)
{
    static const uint8_t sCommandLengths[31] = {
        0, 0, 0, 0, 0, 16, 128, 0, 0, 0, 0, 60, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 56, 48, 48, 88, 48, 48, 4};
    static const uint8_t sResponseLengths[31] = {
        0, 0, 40, 128, 4, 8, 0, 0, 0, 48, 80, 80, 40, 48, 0, 4, 48, 48, 4, 40, 4, 48, 40, 192, 192, 192, 0, 8, 4, 84, 0};
    const struct MpBridgeCaps *c = &b->caps;
    uint32_t inc, epoch, seq, offset, size, revision, opcode, status, dataLength, crc;
    uint32_t maxOpcode;
    const uint8_t *request;
    const uint8_t *data;

    if (length != 240)
        return false;
    inc = U32(p);
    epoch = U32(p + 4);
    request = p + 8;
    seq = U32(p + 24);
    offset = U32(p + 28);
    size = U32(p + 32);
    revision = U32(p + 36);
    opcode = p[40];
    status = p[41];
    dataLength = U16(p + 42);
    crc = U32(p + 44);
    data = p + 48;
    maxOpcode = Enabled(c->saveImportWindow) ? 30
              : Enabled(c->tradeEffectChoice) ? 29
              : Enabled(c->tradeEffectSave) ? 28
              : Enabled(c->tradeEffectPrepare) ? 26
              : Enabled(c->emergencySave) ? (c->emergencySave == 2 ? 22 : 21)
              : Enabled(c->saveWindow) ? 18
              : Enabled(c->trade) ? 17 : 11;
    if (inc == 0 || epoch == 0 || epoch > 0x7FFFFFFF || seq == 0 || AllZero(request, 16)
     || offset > 131072 || (size != 0 && size != 131072) || revision > 0x7FFFFFFF
     || opcode < 1 || opcode > maxOpcode
     || ((opcode == 27 || opcode == 28) && !Enabled(c->tradeEffectSave))
     || (opcode == 29 && !Enabled(c->tradeEffectChoice))
     || (opcode == 30 && !Enabled(c->saveImportWindow))
     || (opcode >= 19 && opcode <= 22
      && (!Enabled(c->emergencySave) || (opcode == 22 && c->emergencySave != 2)))
     || (opcode == 18 && !Enabled(c->saveWindow))
     || status > (Enabled(c->battleResume) ? 14u : 13u) || dataLength > 192
     || !AllZero(data + dataLength, 192 - dataLength)
     || MpBridge_Crc32(data, dataLength) != crc)
        return false;
    if (command)
    {
        if (status != 0 || dataLength != sCommandLengths[opcode])
            return false;
        if (opcode >= 12 && opcode <= 17 && (size != 131072 || offset != 0))
            return false;
        if ((opcode == 18 || opcode == 30) && (size != 131072 || offset % 4096 != 0 || offset > 126976))
            return false;
        if (opcode == 12 && (revision == 0 || revision >= 0x7FFFFFFF))
            return false;
        if (opcode == 16 && seq != 1)
            return false;
        if (opcode >= 19 && opcode <= 22)
        {
            if (size != 131072 || offset != (opcode == 20 ? 131072u : 0u))
                return false;
            if (opcode != 21 && revision == 0)
                return false;
        }
        if (opcode >= 23 && opcode <= 29)
        {
            if (!Enabled(c->tradeEffectPrepare) || size != 131072 || (opcode != 28 && offset != 0)
             || (opcode == 28 && (!Enabled(c->tradeEffectSave) || offset % 4096 != 0 || offset > 126976))
             || revision == 0 || revision >= 0x7FFFFFFF
             || AllZero(data, 16) || AllZero(data + 16, 32))
                return false;
            if (opcode == 23)
            {
                /* the resolved friendship (0-255) and the Lucky Trade bit (MP_TRADE_RESOLVED_LUCKY, 1 << 8), the
                 * rules of multiplayer_trade_effects.c; 0-255 alone stopped every Lucky Trade's link mid-trade */
                if (U32(data + 80) > 5 || (U32(data + 84) & ~0x1FFu) != 0 || AllZero(data + 48, 32))
                    return false;
            }
            else if (opcode == 24)
            {
                uint32_t move = U16(data + 52);

                if (U32(data + 48) >= 16 || move < 1 || move > 621 || data[54] > 4 || data[55] != 0)
                    return false;
            }
            else if (opcode == 27)
            {
                if (!Enabled(c->tradeEffectSave) || AllZero(data + 48, 32) || data[87] != 0)
                    return false;
            }
        }
        if ((opcode == 3 || opcode == 6)
         && (size != 131072 || offset % 128 != 0 || offset >= 131072))
            return false;
        if (opcode == 5 && (size != 131072 || U32(data + 8) >= 14))
            return false;
    }
    else
    {
        if ((status == 0 && dataLength != sResponseLengths[opcode]) || (status != 0 && dataLength != 0))
            return false;
        if (status == 0 && opcode == 3 && (offset % 128 != 0 || offset > 130944))
            return false;
        if (status == 0 && opcode == 18 && (size != 131072 || offset % 4096 != 0 || offset > 126976))
            return false;
        if (status == 0 && opcode == 30 && (size != 131072 || offset % 4096 != 0 || offset < 4096 || offset > 131072))
            return false;
        if (status == 0 && opcode >= 23 && opcode <= 29 && ((opcode != 28 && offset != 0) || size != 131072
         || (opcode == 28 && (offset % 4096 != 0 || offset > 126976))))
            return false;
    }
    if (out != NULL)
    {
        out->incarnation = inc;
        out->epoch = epoch;
        memcpy(out->request, request, 16);
        out->sequence = seq;
        out->opcode = opcode;
        out->revision = revision;
        out->offset = offset;
        out->size = size;
        out->status = status;
        memcpy(out->payload, p, 240);
    }
    return true;
}

/* commitSave */
static bool CommitSave(struct MpBridge *b, const uint8_t *p)
{
    uint32_t odd;
    uint32_t offset;

    if (!SaveHeaderReady(b))
        return false;
    odd = Odd(R32(b, MP_REGION_SAVE, 24));
    W32(b, MP_REGION_SAVE, 24, odd);
    for (offset = 0; offset <= 236; offset += 4)
        W32(b, MP_REGION_SAVE, 28 + offset, U32(p + offset));
    W32(b, MP_REGION_SAVE, 24, odd + 1);
    return true;
}

/* battleHeaderReady */
static bool BattleHeaderReady(const struct MpBridge *b)
{
    uint32_t maxState = Enabled(b->caps.battleResume) ? 10 : (b->caps.battle >= 2 ? 8 : 3);

    return Enabled(b->caps.battle) && R32(b, MP_REGION_BATTLE, 0) == MP_BATTLE_MAGIC
        && R16(b, MP_REGION_BATTLE, 4) == b->caps.battle && R16(b, MP_REGION_BATTLE, 6) == 512
        && R32(b, MP_REGION_BATTLE, 12) <= maxState
        && R32(b, MP_REGION_BATTLE, 16) == 0 && R32(b, MP_REGION_BATTLE, 20) == 0;
}

/* The lengths tables of battleControl; -1 where the Lua table has no entry. */
static int BattleLength(bool native, bool command, uint32_t opcode, uint32_t size, uint32_t offset)
{
    static const int8_t sNativeCommand[25] = {
        -1, 116, 0, -1, 0, 8, -2, -1, -1, 0, 36, 0, 0, 8, 0, 0, 140, 20, 0, 48, 28, 28, 84, 84, 52};
    static const int8_t sLegacyCommand[10] = {-1, 116, 0, 4, 0, 8, 40, 4, 0, 0};
    static const int8_t sNativeResponse[25] = {
        -1, 64, 64, -1, -2, 0, 0, -1, -1, 64, 64, 64, 0, 0, 64, 88, 88, 88, 88, 64, 28, 28, 84, 84, 52};
    static const int8_t sLegacyResponse[10] = {-1, 64, 64, 64, 40, 0, 0, 0, 0, 64};
    int value;

    if (native)
        value = opcode < 25 ? (command ? sNativeCommand : sNativeResponse)[opcode] : -1;
    else
        value = opcode < 10 ? (command ? sLegacyCommand : sLegacyResponse)[opcode] : -1;
    if (value == -2) /* math.min(128, size - offset) */
        value = size - offset < 128 ? (int)(size - offset) : 128;
    /* native command 6 is the -2 entry; native response 4 likewise */
    return value;
}

/* battleControl */
static bool BattleControl(const struct MpBridge *b, const uint8_t *p, size_t length, bool command,
                          struct MpBridgeBattle *out)
{
    const struct MpBridgeCaps *c = &b->caps;
    uint32_t inc, epoch, seq, block, offset, size, opcode, status, phase, source, kind, reserved;
    uint32_t dataLength, crc, maxOpcode, count;
    const uint8_t *id;
    const uint8_t *data;
    bool native;
    int expected;

    if (length != 240)
        return false;
    inc = U32(p);
    epoch = U32(p + 4);
    id = p + 8;
    seq = U32(p + 24);
    block = U32(p + 28);
    offset = U16(p + 32);
    size = U16(p + 34);
    opcode = p[36];
    status = p[37];
    phase = p[38];
    source = p[39];
    kind = p[40];
    reserved = p[41];
    dataLength = U16(p + 42);
    crc = U32(p + 44);
    data = p + 48;
    maxOpcode = Enabled(c->battleAdmissionCleanup) ? 24 : (Enabled(c->battleResume) ? 21 : 13);
    if (inc == 0 || epoch == 0 || epoch > 0x7FFFFFFF || AllZero(id, 16)
     || seq == 0 || size > 256 || offset > size || opcode < 1 || opcode > maxOpcode
     || status > 10 || source > 3 || reserved != 0
     || dataLength > 192 || !AllZero(data + dataLength, 192 - dataLength)
     || MpBridge_Crc32(data, dataLength) != crc)
        return false;
    /* Native phases: 2 singles, 3 full Multi (four cores), 4 co-op Gym (two
     * cores, the master also running both AI trainers), 5 the free-for-all
     * (four cores, PC builds with its marker). Phase 4 was missing here (and in
     * mgba.lua), so every co-op command stopped the bridge ("invalid battle
     * command") and no co-op battle could start; phase 5 was missing the same
     * way, so the first free-for-all command stopped all four games' links.
     * Update 32: 6 the three-player free-for-all (1v1v1: three cores, the
     * fourth place empty; PC builds with its own marker). */
    native = ((c->battle == 2 || c->battle == 3) && phase == 2)
          || (c->battle == 3 && (phase == 3 || phase == 4))
          || (c->battle == 3 && Enabled(c->battleFfa) && phase == 5)
          || (c->battle == 3 && Enabled(c->battleFfa) && Enabled(c->battleFfa3) && phase == 6);
    count = phase == 3 || phase == 5 ? 4 : phase == 6 ? 3 : 2;
    if (!native && (phase != 1 || opcode > 9 || kind > 1))
        return false;
    if (native && (source >= count || opcode == 3 || opcode == 7 || opcode == 8 || kind == 1 || kind > 8))
        return false;
    if (opcode == 4 || opcode == 5 || opcode == 6)
    {
        if (block == 0)
            return false;
        if (native)
        {
            static const int16_t sSizes[9] = {-1, -1, 28, 32, 200, 200, 200, 16, -1};
            static const uint16_t sControllerSizes[] = {12, 16, 20, 24, 28, 36, 56, 64, 92, 156};
            bool controllerSize = false;
            unsigned i;

            for (i = 0; i < sizeof(sControllerSizes) / sizeof(sControllerSizes[0]); i++)
                if (sControllerSizes[i] == size)
                    controllerSize = true;
            if ((kind == 8 && !controllerSize) || (kind != 8 && sSizes[kind] != (int)size))
                return false;
            if ((opcode == 5 && offset != size)
             || (opcode != 5 && (offset >= size || offset % 128 != 0)))
                return false;
        }
        else
        {
            if (kind != 1 || size != 40)
                return false;
            if ((opcode == 5 && offset != 40) || (opcode != 5 && offset != 0))
                return false;
        }
    }
    else if (block != 0 || offset != 0 || size != 0 || kind != 0)
    {
        return false;
    }
    expected = BattleLength(native, command, opcode, size, offset);
    if (command)
    {
        if (status != 0 || expected < 0 || dataLength != (uint32_t)expected)
            return false;
        if (opcode >= 22)
        {
            uint32_t originalLease = U32(data + 48);

            if (seq != 1 || originalLease == 0 || originalLease > epoch
             || AllZero(data, 16) || AllZero(data + 16, 16) || AllZero(data + 32, 16)
             || (opcode != 24 && AllZero(data + 52, 32)))
                return false;
        }
        if (native && opcode == 1 && (data[115] > 1 || (data[115] == 1 && !Enabled(c->battleResume))))
            return false;
        if (opcode == 5 || opcode == 7)
        {
            uint32_t mask = U32(data + (opcode == 5 ? 4 : 0));

            if (mask == 0 || mask > 15)
                return false;
            if (native && mask != (((1u << count) - 1) ^ (1u << source)))
                return false;
        }
        if (opcode == 6 && !native)
        {
            uint32_t lease = U32(data + 32);

            if (U32(data) != 0x44424D45 || U32(data + 4) != 1 || AllZero(data + 8, 16)
             || U32(data + 24) != source || lease == 0 || lease > 0x7FFFFFFF || U32(data + 36) == 0)
                return false;
        }
    }
    else
    {
        if (status == 0 && (expected < 0 || dataLength != (uint32_t)expected))
            return false;
        if (status != 0 && dataLength != 0)
            return false;
    }
    if (out != NULL)
    {
        /* Bytes 1-37 and 39-41 (1-based) are the immutable correlation fields. */
        out->opcode = opcode;
        memcpy(out->correlation, p, 37);
        memcpy(out->correlation + 37, p + 38, 3);
        memcpy(out->payload, p, 240);
    }
    return true;
}

/* commitBattle */
static bool CommitBattle(struct MpBridge *b, const uint8_t *p)
{
    uint32_t odd;
    uint32_t offset;

    if (!BattleHeaderReady(b))
        return false;
    odd = Odd(R32(b, MP_REGION_BATTLE, 24));
    W32(b, MP_REGION_BATTLE, 24, odd);
    for (offset = 0; offset <= 236; offset += 4)
        W32(b, MP_REGION_BATTLE, 28 + offset, U32(p + offset));
    W32(b, MP_REGION_BATTLE, 24, odd + 1);
    return true;
}

/* commit */
static void Commit(struct MpBridge *b, const uint8_t *p)
{
    uint32_t odd;
    uint32_t offset;

    if (!HeaderReady(b))
        return;
    odd = Odd(R32(b, MP_REGION_PRESENCE, 36));
    W32(b, MP_REGION_PRESENCE, 36, odd);
    for (offset = 0; offset <= 84; offset += 4)
        W32(b, MP_REGION_PRESENCE, 40 + offset, U32(p + offset));
    W32(b, MP_REGION_PRESENCE, 36, odd + 1);
}

/* The capability announcements, flags and pending commands a new connection
 * starts without (the connect branch of onFrame, and stop). */
static void ForgetConnection(struct MpBridge *b)
{
    b->rxLength = 0;
    b->txLength = b->txSent = 0;
    b->hasLatestPose = false;
    b->hasLatestWorld = b->poseSentLast = false;
    b->hasLatestSpawn = b->spawnTurn = false;
    b->spawnCapabilitySent = false;
    b->hasLatestExtras = b->extrasCapabilitySent = b->extrasSentValid = false;
    b->controlCount = b->controlBurst = 0;
    b->save.pending = false;
    b->hasSaveGeneration = false;
    b->capabilitySent = false;
    b->windowCapabilitySent = false;
    b->emergencyCapabilitySent = false;
    b->tradeEffectPrepareCapabilitySent = false;
    b->tradeEffectSaveCapabilitySent = false;
    b->tradeEffectChoiceCapabilitySent = false;
    b->battle.pending = false;
    b->hasBattleGeneration = false;
    b->battleCapabilitySent = false;
    b->battleLooksCapabilitySent = false;
    b->menuCapabilitySent = false;
    b->hasMenuSequence = false;
    b->resetCapabilitySent = false;
    b->hasResetSequence = false;
    b->hasResetReceipt = false;
}

/* stop(reason, fatal) */
static void Stop(struct MpBridge *b, const char *reason, bool fatal)
{
    static const uint8_t sZeros[208];

    RouteResetChord(b, false);
    if (b->connected)
    {
        b->ops.close(b->ops.context);
        b->connected = false;
    }
    ForgetConnection(b);
    Commit(b, sZeros);
    CommitWorld(b, sZeros, 208);
    if (reason != NULL)
    {
        char message[160];
        size_t n;

        b->stopped = fatal;
        b->lastConnect = b->tick;
        strcpy(message, "Multiplayer bridge disconnected: ");
        n = strlen(message);
        strncpy(message + n, reason, sizeof(message) - n - 1);
        message[sizeof(message) - 1] = '\0';
        Log(b, true, message);
    }
}

void MpBridge_Stop(struct MpBridge *b)
{
    Stop(b, NULL, false);
}

/* reliable */
static bool Reliable(struct MpBridge *b, uint8_t kind, const uint8_t *payload, size_t length,
                     const uint8_t *extra, size_t extraLength)
{
    if (b->controlCount >= MP_BRIDGE_CONTROL_LIMIT)
    {
        Stop(b, "save control backpressure", true);
        return false;
    }
    b->controlLength[b->controlCount] = Frame(b->controls[b->controlCount], kind, payload, length,
                                              extra, extraLength);
    b->controlCount++;
    return true;
}

static bool Same(const struct MpBridgeSave *a, const struct MpBridgeSave *b)
{
    return a->incarnation == b->incarnation && a->epoch == b->epoch
        && memcmp(a->request, b->request, 16) == 0 && a->sequence == b->sequence
        && a->opcode == b->opcode && a->revision == b->revision && a->size == b->size;
}

/* pollSave */
static void PollSave(struct MpBridge *b)
{
    struct MpBridgeSave response;
    uint8_t payload[240];
    uint32_t generation;

    if (!b->save.pending || !SaveHeaderReady(b))
        return;
    generation = R32(b, MP_REGION_SAVE, 268);
    if (generation % 2 != 0 || (b->hasSaveGeneration && generation == b->saveGeneration))
        return;
    ReadRange(b, MP_REGION_SAVE, 272, payload, sizeof(payload));
    if (generation != R32(b, MP_REGION_SAVE, 268))
        return;
    if (AllZero(payload, sizeof(payload)))
        return;
    if (!SaveControl(b, payload, sizeof(payload), false, &response))
    {
        Stop(b, "invalid save response", true);
        return;
    }
    b->saveGeneration = generation;
    b->hasSaveGeneration = true;
    /* An old response can remain in ROM RAM across an IPC/server reconnect. */
    if (!Same(&response, &b->save))
        return;
    if (response.status == 0
     && ((response.opcode == 3 && response.offset != b->save.offset)
      || ((response.opcode == 18 || response.opcode == 28) && response.offset != b->save.offset)
      || (response.opcode == 6 && response.offset != b->save.offset + 128)
      || (response.opcode == 30 && response.offset != b->save.offset + 4096)))
    {
        Stop(b, "invalid save response offset", true);
        return;
    }
    if ((response.opcode == 18 || response.opcode == 28) && response.status == 0)
    {
        static uint8_t sWindow[4096];
        uint32_t state = R32(b, MP_REGION_SAVE, 12);

        if ((response.opcode == 18 && state != 2 && state != 6)
         || (response.opcode == 28 && (!Enabled(b->caps.tradeEffectSave) || state != 8)))
        {
            Stop(b, "save window no longer held", true);
            return;
        }
        ReadRange(b, MP_REGION_WINDOW, 0, sWindow, sizeof(sWindow));
        if (b->ops.regionSize[MP_REGION_WINDOW] != sizeof(sWindow)
         || MpBridge_Crc32(sWindow, sizeof(sWindow)) != U32(payload + 48)
         || generation != R32(b, MP_REGION_SAVE, 268) || state != R32(b, MP_REGION_SAVE, 12))
        {
            Stop(b, "save window changed before readback", true);
            return;
        }
        if (Reliable(b, 'F', payload, sizeof(payload), sWindow, sizeof(sWindow)))
            b->save.pending = false;
    }
    else if (Reliable(b, 'R', payload, sizeof(payload), NULL, 0))
    {
        b->save.pending = false;
    }
}

/* pollBattle */
static void PollBattle(struct MpBridge *b)
{
    struct MpBridgeBattle response;
    uint8_t payload[240];
    uint32_t generation;

    if (!b->battle.pending || !BattleHeaderReady(b))
        return;
    generation = R32(b, MP_REGION_BATTLE, 268);
    if (generation % 2 != 0 || (b->hasBattleGeneration && generation == b->battleGeneration))
        return;
    ReadRange(b, MP_REGION_BATTLE, 272, payload, sizeof(payload));
    if (generation != R32(b, MP_REGION_BATTLE, 268) || AllZero(payload, sizeof(payload)))
        return;
    if (!BattleControl(b, payload, sizeof(payload), false, &response))
    {
        Stop(b, "invalid battle response", true);
        return;
    }
    b->battleGeneration = generation;
    b->hasBattleGeneration = true;
    if (memcmp(response.correlation, b->battle.correlation, sizeof(response.correlation)) != 0)
        return;
    if (Reliable(b, 'E', payload, sizeof(payload), NULL, 0))
        b->battle.pending = false;
}

/* pollMenu */
static void PollMenu(struct MpBridge *b)
{
    uint8_t payload[16];
    uint32_t generation, seq, screen, action, kind;

    if (!b->menuCapabilitySent || !MenuHeaderReady(b))
        return;
    if (b->controlCount >= MP_BRIDGE_CONTROL_LIMIT)
        return; /* Initial capability announcements drain first. */
    generation = R32(b, MP_REGION_MENU, 12);
    if (generation % 2 != 0)
        return;
    ReadRange(b, MP_REGION_MENU, 16, payload, sizeof(payload));
    if (generation != R32(b, MP_REGION_MENU, 12) || AllZero(payload, sizeof(payload)))
        return;
    seq = U32(payload);
    screen = U32(payload + 4);
    action = U16(payload + 8);
    kind = payload[10];
    /* An OPEN names no action, or (menuOpenPage, Update 32) its page: 1 the soft reset. */
    if (seq == 0 || action > 7 || kind < 1 || kind > 3 || !AllZero(payload + 11, 5)
     || (kind == 2 && (screen == 0 || action == 0)) || (kind == 3 && action != 0)
     || (kind == 1 && action > (Enabled(b->caps.tower) ? 2u : Enabled(b->caps.menuOpenPage) ? 1u : 0u))
     || (kind == 1 && screen != 0))
    {
        Stop(b, "invalid menu request", true);
        return;
    }
    if (b->hasMenuSequence && seq == b->menuSequence)
        return;
    if (b->hasMenuSequence && seq < b->menuSequence)
    {
        Stop(b, "menu request sequence regressed", true);
        return;
    }
    if (Reliable(b, 'J', payload, sizeof(payload), NULL, 0))
    {
        b->menuSequence = seq;
        b->hasMenuSequence = true;
    }
}

/* pollReset */
static void PollReset(struct MpBridge *b)
{
    uint8_t payload[32];
    uint8_t receipt[32];
    uint32_t generation, seq, session, incarnation, encounter;

    if (!b->resetCapabilitySent || !ResetHeaderReady(b) || b->controlCount >= MP_BRIDGE_CONTROL_LIMIT)
        return;
    generation = R32(b, MP_REGION_RESET, 12);
    if (generation % 2 != 0)
        return;
    ReadRange(b, MP_REGION_RESET, 16, payload, sizeof(payload));
    if (generation != R32(b, MP_REGION_RESET, 12) || AllZero(payload, sizeof(payload)))
        return;
    seq = U32(payload);
    session = U32(payload + 4);
    incarnation = U32(payload + 8);
    encounter = U32(payload + 12);
    if (seq == 0 || session == 0 || incarnation == 0 || encounter < 0x1001 || encounter > 0x1019
     || !AllZero(payload + 20, 12))
    {
        Stop(b, "invalid personal encounter request", true);
        return;
    }
    if (!b->hasResetSequence || seq != b->resetSequence)
    {
        if (b->hasResetSequence && seq < b->resetSequence)
        {
            Stop(b, "personal encounter sequence regressed", true);
            return;
        }
        if (Reliable(b, 'T', payload, sizeof(payload), NULL, 0))
        {
            b->resetSequence = seq;
            b->hasResetSequence = true;
        }
    }
    if (b->caps.reset == 2 && b->controlCount < MP_BRIDGE_CONTROL_LIMIT)
    {
        uint32_t count, state, ready;

        ReadRange(b, MP_REGION_RESET, 84, receipt, sizeof(receipt));
        if (generation != R32(b, MP_REGION_RESET, 12) || AllZero(receipt, sizeof(receipt)))
            return;
        count = U32(receipt + 16);
        state = U32(receipt + 20);
        ready = U32(receipt + 24);
        if (U32(receipt) != seq || U32(receipt + 4) != session || U32(receipt + 8) != incarnation
         || U32(receipt + 12) != encounter || state < 1 || state > 4 || ready > 1
         || U32(receipt + 28) != 0 || (state == 2 && count == 0) || (state != 2 && count != 0)
         || (ready != 0 && state != 3 && state != 4))
        {
            Stop(b, "invalid personal encounter receipt", true);
            return;
        }
        if ((!b->hasResetReceipt || memcmp(receipt, b->resetReceipt, sizeof(receipt)) != 0)
         && Reliable(b, 'Y', receipt, sizeof(receipt), NULL, 0))
        {
            memcpy(b->resetReceipt, receipt, sizeof(receipt));
            b->hasResetReceipt = true;
        }
    }
}

/* validPose; the game's own pose may say why it is off the field in its last byte (presence detail, PC only),
 * the other players' poses never do */
static bool ValidPose(const struct MpBridge *b, const uint8_t *pose, bool local)
{
    return pose[14] >= 1 && pose[14] <= 4 && pose[15] <= 15
        && pose[16] <= 1 && pose[17] <= (Enabled(b->caps.presenceAnimation) ? 5 : 4)
        && pose[18] <= 1
        && (pose[19] == 0 || (local && Enabled(b->caps.presenceDetail) && pose[18] == 0 && pose[19] <= 3));
}

/* regressed: ROM counters wrap normally at uint32; a pause or Flash loop can stall. */
static bool Regressed(uint32_t current, bool hasPrevious, uint32_t previous)
{
    return hasPrevious && (uint32_t)(current - previous) >= 0x80000000u;
}

/* runtimeRestart: a soft reset need not tell the bridge; a fresh handshake
 * gives the companion a new incarnation and fences old commands. */
static void RuntimeRestart(struct MpBridge *b, const char *reason)
{
    Stop(b, reason, false);
    ForgetRuntime(b);
}

/* runtimeReady */
static bool RuntimeReady(struct MpBridge *b)
{
    uint32_t generation;

    if (!HeaderReady(b))
    {
        if (R32(b, MP_REGION_PRESENCE, 0) != 0)
            Stop(b, "mailbox ABI mismatch", true);
        else if (b->observedPresence)
            RuntimeRestart(b, "ROM mailbox reinitialized");
        return false;
    }
    generation = R32(b, MP_REGION_PRESENCE, 12);
    if (generation % 2 == 0)
    {
        uint32_t current = R32(b, MP_REGION_PRESENCE, 8);
        uint8_t pose[20];

        ReadRange(b, MP_REGION_PRESENCE, 16, pose, sizeof(pose));
        if (ValidPose(b, pose, true) && U32(pose + 4) == current
         && generation == R32(b, MP_REGION_PRESENCE, 12) && current == R32(b, MP_REGION_PRESENCE, 8)
         && HeaderReady(b))
        {
            if (Regressed(current, b->hasPreviousFrame, b->previousFrame))
            {
                RuntimeRestart(b, "ROM frame counter restarted");
                return false;
            }
            b->observedPresence = true;
            b->previousFrame = current;
            b->hasPreviousFrame = true;
        }
    }
    if (Enabled(b->caps.save))
    {
        if (!SaveHeaderReady(b))
        {
            if (R32(b, MP_REGION_SAVE, 0) != 0)
            {
                Stop(b, "save mailbox ABI mismatch", true);
                return false;
            }
            else if (b->observedSave)
            {
                RuntimeRestart(b, "ROM save runtime restarted");
                return false;
            }
        }
        else
        {
            uint32_t current = R32(b, MP_REGION_SAVE, 8);
            uint32_t state = R32(b, MP_REGION_SAVE, 12);

            if (current == R32(b, MP_REGION_SAVE, 8) && state == R32(b, MP_REGION_SAVE, 12)
             && SaveHeaderReady(b))
            {
                uint8_t rest[488];

                ReadRange(b, MP_REGION_SAVE, 24, rest, sizeof(rest));
                if (Regressed(current, b->hasPreviousSaveFrame, b->previousSaveFrame)
                 || (b->observedSave && b->previousSaveState != 0 && state == 0 && AllZero(rest, sizeof(rest))))
                {
                    RuntimeRestart(b, "ROM save runtime restarted");
                    return false;
                }
                b->observedSave = true;
                b->previousSaveFrame = current;
                b->hasPreviousSaveFrame = true;
                b->previousSaveState = state;
            }
        }
    }
    if (Enabled(b->caps.battle))
    {
        uint32_t current;
        uint32_t state;
        uint8_t rest[488];

        if (!BattleHeaderReady(b))
        {
            if (R32(b, MP_REGION_BATTLE, 0) != 0)
                Stop(b, "battle mailbox ABI mismatch", true);
            else if (b->observedBattle)
                RuntimeRestart(b, "ROM battle mailbox reinitialized");
            return false;
        }
        current = R32(b, MP_REGION_BATTLE, 8);
        state = R32(b, MP_REGION_BATTLE, 12);
        ReadRange(b, MP_REGION_BATTLE, 24, rest, sizeof(rest));
        if (Regressed(current, b->hasPreviousBattleFrame, b->previousBattleFrame)
         || (b->observedBattle && b->previousBattleState != 0 && state == 0 && AllZero(rest, sizeof(rest))))
        {
            RuntimeRestart(b, "ROM battle runtime restarted");
            return false;
        }
        b->observedBattle = true;
        b->previousBattleFrame = current;
        b->hasPreviousBattleFrame = true;
        b->previousBattleState = state;
    }
    if (Enabled(b->caps.menu))
    {
        uint32_t current;

        if (!MenuHeaderReady(b))
        {
            if (R32(b, MP_REGION_MENU, 0) != 0)
                Stop(b, "menu mailbox ABI mismatch", true);
            else if (b->observedMenu)
                RuntimeRestart(b, "ROM menu mailbox reinitialized");
            return false;
        }
        current = R32(b, MP_REGION_MENU, 8);
        if (Regressed(current, b->hasPreviousMenuFrame, b->previousMenuFrame))
        {
            RuntimeRestart(b, "ROM menu runtime restarted");
            return false;
        }
        b->observedMenu = true;
        b->previousMenuFrame = current;
        b->hasPreviousMenuFrame = true;
    }
    if (Enabled(b->caps.reset))
    {
        uint32_t current;

        if (!ResetHeaderReady(b))
        {
            if (R32(b, MP_REGION_RESET, 0) != 0)
                Stop(b, "personal encounter mailbox ABI mismatch", true);
            else if (b->observedReset)
                RuntimeRestart(b, "ROM personal encounter mailbox reinitialized");
            return false;
        }
        current = R32(b, MP_REGION_RESET, 8);
        if (Regressed(current, b->hasPreviousResetFrame, b->previousResetFrame))
        {
            RuntimeRestart(b, "ROM personal encounter runtime restarted");
            return false;
        }
        b->observedReset = true;
        b->previousResetFrame = current;
        b->hasPreviousResetFrame = true;
    }
    return true;
}

/* validSnapshot */
static bool ValidSnapshot(const struct MpBridge *b, const uint8_t *p, size_t length)
{
    uint32_t flags, epoch, selfSlot, count;
    uint32_t seen[3];
    unsigned i;
    unsigned k;

    if (length != 88)
        return false;
    flags = U32(p);
    epoch = U32(p + 8);
    selfSlot = p[12];
    count = p[13];
    if (flags > 1 || selfSlot > 3 || count > 3 || U16(p + 14) != 0 || (flags == 1 && epoch == 0))
        return false;
    for (i = 0; i < 3; i++)
    {
        const uint8_t *o = p + 16 + i * 24;

        if (i < count)
        {
            uint32_t token = U32(o);

            if (token == 0 || !ValidPose(b, o + 4, false))
                return false;
            for (k = 0; k < i; k++)
                if (seen[k] == token)
                    return false;
            seen[i] = token;
        }
        else if (!AllZero(o, 24))
        {
            return false;
        }
    }
    return flags == 1 || (count == 0 && selfSlot == 0);
}

/* The dispatch of one complete inbound frame (receive's inner loop); false
 * when it stopped the bridge. */
static bool Dispatch(struct MpBridge *b, const uint8_t *payload, size_t size)
{
    const uint8_t *rest = payload + 1;
    size_t restLength = size - 1;

    if (payload[0] == 'S' && ValidSnapshot(b, rest, restLength))
    {
        Commit(b, rest);
    }
    else if (payload[0] == 'C')
    {
        struct MpBridgeSave command;
        bool recovery;

        if (!SaveControl(b, rest, restLength, true, &command) || command.opcode == 30 || !SaveHeaderReady(b))
        {
            Stop(b, "invalid save command", true);
            return false;
        }
        /* Recovery must observe/rebind an uncertain trade command. The ROM
         * fences UUID/incarnation/lease and preserves pending Flash
         * publication; these controls cannot release the hold. */
        recovery = command.opcode == 1 || command.opcode == 16 || command.opcode == 17
                || command.opcode == 21 || command.opcode == 22 || command.opcode == 25;
        if (b->save.pending && !recovery && memcmp(command.payload, b->save.payload, 240) != 0)
        {
            Stop(b, "multiple outstanding save commands", true);
            return false;
        }
        if (CommitSave(b, command.payload))
        {
            b->save = command;
            b->save.pending = true;
        }
    }
    else if (payload[0] == 'i')
    {
        /* WRITE_WINDOW: the sector first, then the command naming its CRC, with the game parked (sdl2.c). The window
         * is rewritten only for this command: another while one is outstanding stops the bridge as for 'C'. */
        struct MpBridgeSave command;
        const uint8_t *sector = rest + 240;
        uint32_t i;

        if (!Enabled(b->caps.saveImportWindow) || size != MP_BRIDGE_IMPORT_FRAME
         || !SaveControl(b, rest, 240, true, &command) || command.opcode != 30 || !SaveHeaderReady(b)
         || b->ops.region[MP_REGION_IMPORT] == NULL || b->ops.regionSize[MP_REGION_IMPORT] != 4096
         || MpBridge_Crc32(sector, 4096) != U32(rest + 48))
        {
            Stop(b, "invalid save import window", true);
            return false;
        }
        if (b->save.pending && memcmp(command.payload, b->save.payload, 240) != 0)
        {
            Stop(b, "multiple outstanding save commands", true);
            return false;
        }
        for (i = 0; i < 4096; i++)
            b->ops.region[MP_REGION_IMPORT][i] = sector[i];
        if (CommitSave(b, command.payload))
        {
            b->save = command;
            b->save.pending = true;
        }
    }
    else if (payload[0] == 'D')
    {
        struct MpBridgeBattle command;

        if (!BattleControl(b, rest, restLength, true, &command) || !BattleHeaderReady(b))
        {
            Stop(b, "invalid battle command", true);
            return false;
        }
        if (b->battle.pending && command.opcode != 2 && memcmp(command.payload, b->battle.payload, 240) != 0)
        {
            Stop(b, "multiple outstanding battle commands", true);
            return false;
        }
        if (CommitBattle(b, command.payload))
        {
            b->battle = command;
            b->battle.pending = true;
        }
    }
    else if (payload[0] == 'U')
    {
        if (!b->resetCapabilitySent || !CommitReset(b, rest, restLength))
        {
            Stop(b, "invalid personal encounter acknowledgment", true);
            return false;
        }
    }
    else if (payload[0] == 'N')
    {
        if (!Enabled(b->caps.world) || !ValidWorldInbound(b, rest, restLength)
         || !CommitWorld(b, rest, restLength))
        {
            Stop(b, "invalid shared-world update", true);
            return false;
        }
    }
    else if (payload[0] == 'M')
    {
        if (!b->menuCapabilitySent || !ValidMenuScreen(b, rest, restLength) || !CommitMenu(b, rest))
        {
            Stop(b, "invalid menu screen", true);
            return false;
        }
    }
    else if (payload[0] == 'n')
    {
        /* The extension (mp_bridge.h): the host's shared wild Pokemon, only
         * after the 'w' announcement. */
        if (!b->spawnCapabilitySent || !ValidSpawnInbound(rest, restLength)
         || !CommitSpawn(b, rest, restLength))
        {
            Stop(b, "invalid shared spawn update", true);
            return false;
        }
    }
    else if (payload[0] == 'y')
    {
        /* The third extension (mp_bridge.h): a secret base from the host, only
         * after the 'e' announcement. */
        if (!b->extrasCapabilitySent || !ValidExtrasInbound(rest, restLength)
         || !CommitExtras(b, rest, restLength))
        {
            Stop(b, "invalid secret base update", true);
            return false;
        }
    }
    else if (payload[0] == 'B')
    {
        /* The extension (mp_bridge.h): a bound battle's generation and a look
         * per binding slot, only after the 'A' announcement. */
        if (!b->battleLooksCapabilitySent || restLength != 16 + MP_BRIDGE_BATTLE_LOOKS * 8)
        {
            Stop(b, "invalid battle looks", true);
            return false;
        }
        b->ops.battleLooks(b->ops.context, rest, rest + 16);
    }
    else
    {
        Stop(b, "invalid bridge packet", true);
        return false;
    }
    return true;
}

/* receive: bounded to RECEIVE_ROUNDS reads per frame; partial headers and bodies persist. Frames are at most 256
 * bytes, but for an import window 'i' (exactly MP_BRIDGE_IMPORT_FRAME) once the game has the capability. */
static void Receive(struct MpBridge *b)
{
    int round;
    size_t rxLimit = Enabled(b->caps.saveImportWindow) ? 4 + MP_BRIDGE_IMPORT_FRAME : RX_LIMIT;

    for (round = 0; round < RECEIVE_ROUNDS; round++)
    {
        int got = b->ops.receive(b->ops.context, b->rx + b->rxLength, 256);

        if (got == MP_IO_AGAIN)
            return;
        if (got == 0)
        {
            Stop(b, "disconnected", false);
            return;
        }
        if (got < 0 || got > 256)
        {
            Stop(b, "receive failure", false);
            return;
        }
        b->rxLength += (size_t)got;
        while (b->rxLength >= 4)
        {
            uint32_t size = ((uint32_t)b->rx[0] << 24) | ((uint32_t)b->rx[1] << 16)
                          | ((uint32_t)b->rx[2] << 8) | b->rx[3];
            static uint8_t payload[MP_BRIDGE_IMPORT_FRAME];

            if (size < 1 || (size > MAX_INBOUND_FRAME
             && !(Enabled(b->caps.saveImportWindow) && size == MP_BRIDGE_IMPORT_FRAME)))
            {
                Stop(b, "invalid IPC frame length", true);
                return;
            }
            if (b->rxLength < size + 4)
                break;
            memcpy(payload, b->rx + 4, size);
            memmove(b->rx, b->rx + 4 + size, b->rxLength - 4 - size);
            b->rxLength -= 4 + size;
            if (!Dispatch(b, payload, size))
                return;
        }
        if (b->rxLength > rxLimit)
        {
            Stop(b, "IPC receive buffer exceeded", true);
            return;
        }
    }
}

/* Whether a reliable frame of `kind` still waits in the control queue. */
static bool ControlQueued(const struct MpBridge *b, uint8_t kind)
{
    unsigned i;

    for (i = 0; i < b->controlCount; i++)
        if (b->controls[i][4] == kind)
            return true;
    return false;
}

/* flush: one frame (or the rest of one); true when a frame was completed and
 * the socket may take another. */
static bool FlushOne(struct MpBridge *b)
{
    int sent;

    if (b->txLength == 0)
    {
        if (b->controlCount > 0 && (!b->hasLatestPose || b->controlBurst < 2))
        {
            memcpy(b->tx, b->controls[0], b->controlLength[0]);
            b->txLength = b->controlLength[0];
            b->controlCount--;
            memmove(b->controls[0], b->controls[1], b->controlCount * sizeof(b->controls[0]));
            memmove(b->controlLength, b->controlLength + 1, b->controlCount * sizeof(b->controlLength[0]));
            b->controlBurst++;
        }
        else if (b->hasLatestSpawn && !ControlQueued(b, 'w')
              && (b->spawnTurn || !(b->hasLatestPose || b->hasLatestWorld)))
        {
            /* The spawn record goes right after a pose or world record, so it
             * is never starved by them (claims and results ride on it), but
             * never before its 'w' announcement has left: records may pass
             * queued controls, and the companion refuses a 'g' before 'w'. */
            memcpy(b->tx, b->latestSpawn, sizeof(b->latestSpawn));
            b->txLength = sizeof(b->latestSpawn);
            b->hasLatestSpawn = false;
            b->spawnTurn = false;
            b->controlBurst = 0;
        }
        else if (b->hasLatestExtras && !ControlQueued(b, 'e') && !b->hasLatestPose && !b->hasLatestWorld
              && !b->hasLatestSpawn)
        {
            /* A base changes rarely: it goes out only when nothing else waits, and never
             * before its 'e' announcement has left. */
            memcpy(b->tx, b->latestExtras, sizeof(b->latestExtras));
            b->txLength = sizeof(b->latestExtras);
            b->hasLatestExtras = false;
            b->controlBurst = 0;
        }
        else if (b->hasLatestPose && !(b->hasLatestWorld && b->poseSentLast))
        {
            memcpy(b->tx, b->latestPose, sizeof(b->latestPose));
            b->txLength = sizeof(b->latestPose);
            b->hasLatestPose = false;
            b->controlBurst = 0;
            b->poseSentLast = true;
            b->spawnTurn = true;
        }
        else if (b->hasLatestWorld)
        {
            /* Alternate with poses so a congested link never starves the
             * shared-world record (actions such as wallet receipts ride on it). */
            memcpy(b->tx, b->latestWorld, sizeof(b->latestWorld));
            b->txLength = sizeof(b->latestWorld);
            b->hasLatestWorld = false;
            b->controlBurst = 0;
            b->poseSentLast = false;
            b->spawnTurn = true;
        }
        b->txSent = 0;
    }
    if (b->txLength == 0)
        return false;
    sent = b->ops.send(b->ops.context, b->tx + b->txSent, b->txLength - b->txSent);
    if (sent == MP_IO_AGAIN)
        return false;
    if (sent == MP_IO_ERROR)
    {
        Stop(b, "send failure", false);
        return false;
    }
    if (sent < 0 || (size_t)sent > b->txLength - b->txSent)
    {
        Stop(b, "invalid send result", true);
        return false;
    }
    b->txSent += (size_t)sent;
    if (b->txSent != b->txLength)
        return false; /* the socket is full: the rest next frame */
    b->txLength = b->txSent = 0;
    return true;
}

/* mgba.lua's flush sends one frame per emulated frame; see FLUSH_FRAMES. */
static void Flush(struct MpBridge *b)
{
    int frames;

    for (frames = 0; frames < FLUSH_FRAMES && b->connected; frames++)
        if (!FlushOne(b))
            return;
}

/* Whether the pose (or the shared-world record) is due for publication: every
 * RECORD_MS of real time when the platform has a clock, else every 6 frames
 * at `phase`, as mgba.lua does. */
static bool RecordDue(struct MpBridge *b, bool *published, uint32_t *at, int phase)
{
    uint32_t now;

    if (b->ops.milliseconds == NULL)
        return b->tick % 6 == phase;
    now = b->ops.milliseconds(b->ops.context);
    if (*published && now - *at < RECORD_MS)
        return false;
    *published = true;
    *at = now;
    return true;
}

static void Capability(uint8_t out[8], uint32_t a, uint32_t b2, uint32_t c, uint32_t d)
{
    PutU16(out, a);
    PutU16(out + 2, b2);
    PutU16(out + 4, c);
    PutU16(out + 6, d);
}

/* onFrame */
void MpBridge_Step(struct MpBridge *b)
{
    const struct MpBridgeCaps *c = &b->caps;
    uint8_t cap[8];

    b->tick++;
    if (!b->configured || b->stopped)
        return;
    if (!RuntimeReady(b))
    {
        RouteResetChord(b, false);
        return;
    }
    if (!b->connected && b->tick - b->lastConnect >= RECONNECT_FRAMES)
    {
        b->lastConnect = b->tick;
        if (!b->ops.connect(b->ops.context, b->port))
            return;
        b->connected = true;
        ForgetConnection(b);
        b->txLength = Frame(b->tx, 'H', b->token, sizeof(b->token), NULL, 0);
        b->txSent = 0;
        Log(b, false, "Multiplayer bridge connected to local companion");
    }
    if (!b->connected)
        return;
    if (SaveHeaderReady(b) && !b->capabilitySent)
    {
        Capability(cap, 1, 512, 240, 192);
        if (!Reliable(b, 'V', cap, 8, NULL, 0))
            return;
        b->capabilitySent = true;
    }
    if (BattleHeaderReady(b) && !b->battleCapabilitySent)
    {
        Capability(cap, c->battle, 512, 240, 192);
        if (!Reliable(b, 'W', cap, 8, NULL, 0))
            return;
        b->battleCapabilitySent = true;
    }
    if (Enabled(c->saveWindow) && SaveHeaderReady(b) && b->capabilitySent && !b->windowCapabilitySent)
    {
        PutU16(cap, 1);
        PutU16(cap + 2, 4096);
        if (!Reliable(b, 'X', cap, 4, NULL, 0))
            return;
        b->windowCapabilitySent = true;
    }
    if (MenuHeaderReady(b) && !b->menuCapabilitySent)
    {
        Capability(cap, c->menu, 512, 240, 16);
        if (!Reliable(b, 'I', cap, 8, NULL, 0))
            return;
        b->menuCapabilitySent = true;
    }
    /* The existing four reliable slots remain bounded. Advertise the fifth
     * optional capability only when earlier announcements have drained. */
    if (Enabled(c->emergencySave) && SaveHeaderReady(b) && b->capabilitySent
     && !b->emergencyCapabilitySent && b->controlCount < MP_BRIDGE_CONTROL_LIMIT)
    {
        Capability(cap, c->emergencySave, 19, c->emergencySave == 2 ? 22 : 21, 10);
        if (!Reliable(b, 'Z', cap, 8, NULL, 0))
            return;
        b->emergencyCapabilitySent = true;
    }
    if (Enabled(c->tradeEffectPrepare) && SaveHeaderReady(b) && b->capabilitySent
     && !b->tradeEffectPrepareCapabilitySent && b->controlCount < MP_BRIDGE_CONTROL_LIMIT)
    {
        PutU16(cap, 1);
        PutU16(cap + 2, 192);
        if (!Reliable(b, 'K', cap, 4, NULL, 0))
            return;
        b->tradeEffectPrepareCapabilitySent = true;
    }
    if (Enabled(c->tradeEffectChoice) && SaveHeaderReady(b) && b->tradeEffectPrepareCapabilitySent
     && !b->tradeEffectChoiceCapabilitySent && b->controlCount < MP_BRIDGE_CONTROL_LIMIT)
    {
        PutU16(cap, 1);
        PutU16(cap + 2, 84);
        if (!Reliable(b, 'O', cap, 4, NULL, 0))
            return;
        b->tradeEffectChoiceCapabilitySent = true;
    }
    if (Enabled(c->tradeEffectSave) && SaveHeaderReady(b) && b->tradeEffectPrepareCapabilitySent
     && b->windowCapabilitySent && !b->tradeEffectSaveCapabilitySent
     && b->controlCount < MP_BRIDGE_CONTROL_LIMIT)
    {
        PutU16(cap, 1);
        PutU16(cap + 2, 4096);
        if (!Reliable(b, 'L', cap, 4, NULL, 0))
            return;
        b->tradeEffectSaveCapabilitySent = true;
    }
    /* The extension (mp_bridge.h): the battle looks the native game draws the
     * other players' characters from. Optional, after the battle capability. */
    if (b->ops.battleLooks != NULL && b->battleCapabilitySent && !b->battleLooksCapabilitySent
     && b->controlCount < MP_BRIDGE_CONTROL_LIMIT)
    {
        PutU16(cap, 1);
        PutU16(cap + 2, MP_BRIDGE_BATTLE_LOOKS);
        if (!Reliable(b, 'A', cap, 4, NULL, 0))
            return;
        b->battleLooksCapabilitySent = true;
    }
    /* The second extension: shared wild Pokemon, once the world mailbox is up. */
    if (WorldHeaderReady(b) && SpawnHeaderReady(b) && !b->spawnCapabilitySent
     && b->controlCount < MP_BRIDGE_CONTROL_LIMIT)
    {
        Capability(cap, c->spawns, 12, MP_SPAWN_BYTES, 20);
        if (!Reliable(b, 'w', cap, 8, NULL, 0))
            return;
        b->spawnCapabilitySent = true;
    }
    /* The third extension: multiplayer Secret Bases, once the world mailbox is up. */
    if (WorldHeaderReady(b) && ExtrasHeaderReady(b) && !b->extrasCapabilitySent
     && b->controlCount < MP_BRIDGE_CONTROL_LIMIT)
    {
        PutU16(cap, 1);
        PutU16(cap + 2, MP_EXTRAS_BYTES);
        PutU16(cap + 4, 160);
        PutU16(cap + 6, 0);
        if (!Reliable(b, 'e', cap, 8, NULL, 0))
            return;
        b->extrasCapabilitySent = true;
    }
    Receive(b);
    if (!b->connected)
        return;
    PollSave(b);
    if (!b->connected)
        return;
    PollBattle(b);
    if (!b->connected)
        return;
    PollMenu(b);
    if (!b->connected)
        return;
    if (ResetHeaderReady(b) && !b->resetCapabilitySent && b->controlCount < MP_BRIDGE_CONTROL_LIMIT)
    {
        Capability(cap, c->reset, 128, 32, 32);
        if (!Reliable(b, 'Q', cap, 8, NULL, 0))
            return;
        b->resetCapabilitySent = true;
    }
    PollReset(b);
    if (!b->connected)
        return;
    RouteResetChord(b, b->resetCapabilitySent && ResetHeaderReady(b));
    if (WorldHeaderReady(b) && RecordDue(b, &b->worldPublished, &b->worldAt, 3))
    {
        uint32_t generation = R32(b, MP_REGION_WORLD, 8);

        if (generation % 2 == 0)
        {
            uint8_t record[32];

            ReadRange(b, MP_REGION_WORLD, 12, record, sizeof(record));
            if (generation == R32(b, MP_REGION_WORLD, 8))
            {
                Frame(b->latestWorld, 'G', record, sizeof(record), NULL, 0);
                b->hasLatestWorld = true;
            }
        }
    }
    if (b->spawnCapabilitySent && SpawnHeaderReady(b) && RecordDue(b, &b->spawnPublished, &b->spawnAt, 1))
    {
        uint32_t generation = R32(b, MP_REGION_SPAWN, 8);

        if (generation % 2 == 0)
        {
            uint8_t record[MP_SPAWN_LOCAL];

            ReadRange(b, MP_REGION_SPAWN, 12, record, sizeof(record));
            if (generation == R32(b, MP_REGION_SPAWN, 8))
            {
                Frame(b->latestSpawn, 'g', record, sizeof(record), NULL, 0);
                b->hasLatestSpawn = true;
            }
        }
    }
    if (b->extrasCapabilitySent && ExtrasHeaderReady(b) && RecordDue(b, &b->extrasPublished, &b->extrasAt, 2))
    {
        uint32_t generation = R32(b, MP_REGION_EXTRAS, 8);

        if (generation % 2 == 0)
        {
            uint8_t record[MP_EXTRAS_LOCAL];

            ReadRange(b, MP_REGION_EXTRAS, 12, record, sizeof(record));
            if (generation == R32(b, MP_REGION_EXTRAS, 8))
            {
                uint32_t seq = U32(record);

                /* Only a base the host has not seen on this connection: the record's number
                 * is the game's, and a new connection forgets what was sent. */
                if (seq != 0 && (!b->extrasSentValid || seq != b->extrasSentSeq) && !b->hasLatestExtras)
                {
                    Frame(b->latestExtras, 'x', record, sizeof(record), NULL, 0);
                    b->hasLatestExtras = true;
                    b->extrasSentValid = true;
                    b->extrasSentSeq = seq;
                }
            }
        }
    }
    if (RecordDue(b, &b->posePublished, &b->poseAt, 0))
    {
        uint32_t generation = R32(b, MP_REGION_PRESENCE, 12);

        if (generation % 2 == 0)
        {
            uint8_t pose[20];

            ReadRange(b, MP_REGION_PRESENCE, 16, pose, sizeof(pose));
            if (generation == R32(b, MP_REGION_PRESENCE, 12) && ValidPose(b, pose, true))
            {
                Frame(b->latestPose, 'P', pose, sizeof(pose), NULL, 0);
                b->hasLatestPose = true;
            }
        }
    }
    Flush(b);
}

void MpBridge_Init(struct MpBridge *b, const struct MpBridgeOps *ops,
                   const struct MpBridgeCaps *caps, uint16_t port, const uint8_t token[32])
{
    memset(b, 0, sizeof(*b));
    b->ops = *ops;
    b->caps = *caps;
    b->port = port;
    memcpy(b->token, token, sizeof(b->token));
    b->tick = 0;
    b->lastConnect = -RECONNECT_FRAMES;
    b->configured = true;
    RouteResetChord(b, false);
}

/* ------------------------------------------------------------------------ */
/* The reset-chord guard: native/windows-runtime/sdl-multiplayer.h of the app
 * repository. Once a reboot chord is detected, releasing one button must not
 * expose held A/B or Start as a new ordinary action; require release of all
 * four before rearming. A verified native input capability may handle the
 * complete gesture itself; unsupported builds never see it. */

uint32_t MpBridge_FilterInput(struct MpBridgeInput *input, uint32_t keys)
{
    const uint32_t chord = 15u; /* GBA A | B | Select | Start */

    input->keys = keys;
    if ((keys & chord) == chord && !input->chordHeld)
    {
        input->chordHeld = true;
        input->chordRouted = input->nativeChordEnabled;
    }
    /* The native frame may never have observed a very brief complete chord.
     * Do not expose its partial release as ordinary A/B/Select/Start, or let
     * re-pressing one still-held gesture become a second native request. */
    if (input->chordHeld && (keys & chord) != chord)
        input->chordRouted = false;
    if (!(keys & chord))
        input->chordHeld = input->chordRouted = false;
    return input->chordHeld && !input->chordRouted ? keys & ~chord : keys;
}

void MpBridge_EnableNativeChord(struct MpBridgeInput *input, bool enabled)
{
    input->nativeChordEnabled = enabled;
    /* Revocation consumes the remainder. Enabling never revives a gesture
     * that began without capability/connection or was revoked midway. */
    if (!enabled)
        input->chordRouted = false;
}

#ifndef MP_BRIDGE_UNIT_TEST
/* ------------------------------------------------------------------------ */
/* Platform glue: the game's mailboxes and markers, loopback TCP, SDL logging. */
#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#ifdef _WIN32
/* Sockets only: keep windows.h's GDI and min/max macros away from the game's
 * own TRANSPARENT, min and max (include/gba/defines.h, include/global.h). */
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET MpSocket;
#define MP_INVALID_SOCKET INVALID_SOCKET
#else
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int MpSocket;
#define MP_INVALID_SOCKET (-1)
#endif

#include "global.h"
#include "multiplayer.h"
#include "multiplayer_battle.h"
#include "constants/battle.h"
#include "multiplayer_local_reset.h"
#include "multiplayer_menu.h"
#include "multiplayer_presence.h"
#include "multiplayer_reset_admission.h"
#include "multiplayer_save.h"
#include "multiplayer_spawns.h"
#include "multiplayer_extras.h"
#include "multiplayer_world.h"
#include "save.h"
#include "character_customization.h"
#include "tower_legends.h"

extern const u32 gMultiplayerPresenceAnimationAbiVersion; /* multiplayer.c */
extern const u32 gMultiplayerOwnershipAbi;                /* multiplayer_ownership.c */

static struct MpBridge sBridge;
static struct MpBridgeInput sInput;
static bool sEnabled;
static MpSocket sSocket = MP_INVALID_SOCKET;
#ifdef _WIN32
static bool sWinsockReady;
#endif

static void CloseSocket(void)
{
    if (sSocket == MP_INVALID_SOCKET)
        return;
#ifdef _WIN32
    closesocket(sSocket);
#else
    close(sSocket);
#endif
    sSocket = MP_INVALID_SOCKET;
}

static bool GlueConnect(void *context, uint16_t port)
{
    struct sockaddr_in address;
    int one = 1;

    (void)context;
#ifdef _WIN32
    if (!sWinsockReady)
    {
        WSADATA data;

        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            return false;
        sWinsockReady = true;
    }
#endif
    CloseSocket();
    sSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sSocket == MP_INVALID_SOCKET)
        return false;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    /* Loopback: the connect completes or is refused at once. */
    if (connect(sSocket, (struct sockaddr *)&address, sizeof(address)) != 0)
    {
        CloseSocket();
        return false;
    }
    setsockopt(sSocket, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
#ifdef _WIN32
    {
        u_long nonBlocking = 1;

        ioctlsocket(sSocket, FIONBIO, &nonBlocking);
    }
#else
    fcntl(sSocket, F_SETFL, fcntl(sSocket, F_GETFL, 0) | O_NONBLOCK);
#endif
    return true;
}

static bool WouldBlock(void)
{
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

static int GlueSend(void *context, const uint8_t *data, size_t length)
{
    int sent;

    (void)context;
#ifdef _WIN32
    sent = send(sSocket, (const char *)data, (int)length, 0);
#else
    sent = (int)send(sSocket, data, length, MSG_NOSIGNAL);
#endif
    if (sent < 0)
        return WouldBlock() ? MP_IO_AGAIN : MP_IO_ERROR;
    return sent;
}

static int GlueReceive(void *context, uint8_t *data, size_t max)
{
    int got;

    (void)context;
#ifdef _WIN32
    got = recv(sSocket, (char *)data, (int)max, 0);
#else
    got = (int)recv(sSocket, data, max, 0);
#endif
    if (got < 0)
        return WouldBlock() ? MP_IO_AGAIN : MP_IO_ERROR;
    return got;
}

static void GlueClose(void *context)
{
    (void)context;
    CloseSocket();
}

static uint32_t GlueMilliseconds(void *context)
{
    (void)context;
    return SDL_GetTicks();
}

static void GlueLog(void *context, bool error, const char *message)
{
    (void)context;
    if (error)
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
    else
        SDL_Log("%s", message);
}

/* The battle looks extension (mp_bridge.h): to the character selection. */
static void GlueBattleLooks(void *context, const uint8_t *generation, const uint8_t *looks)
{
    (void)context;
    CharCustom_SetBattleLooks(generation, looks);
}

static void GlueRouteResetChord(void *context, bool enabled)
{
    (void)context;
    MpBridge_EnableNativeChord(&sInput, enabled);
}

static int HexValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

static char sRole; /* 'h' host, 'j' join, 0: not said (an older launcher) */
static uint32_t sLinkFrames; /* game frames since the bridge was configured */

/* --link-role host|join (player_runtime.py): an argument rather than a bridge-file line, so that a launcher newer than
 * the game never stops it from starting (a game ignores arguments it does not know but refuses unknown bridge lines). */
void MpBridge_SetRole(const char *role)
{
    sRole = strcmp(role, "host") == 0 ? 'h' : strcmp(role, "join") == 0 ? 'j' : 0;
}

char MpBridge_Role(void)
{
    return MpBridge_IsEnabled() ? sRole : 0;
}

bool MpBridge_Configure(const char *path)
{
    struct MpBridgeOps ops;
    struct MpBridgeCaps caps;
    char line[160];
    FILE *file = fopen(path, "r");
    long port = -1;
    uint8_t token[32];
    bool haveToken = false;
    bool optIn[5] = {false, false, false, false, false};
    static const char *const sOptIns[5] = {
        "trade_effect_prepare=1", "trade_effect_save=1", "trade_effect_choice=1", "menu_open_page=1", "tower=1"};
    const char *problem;

    if (file == NULL)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Multiplayer bridge: cannot read %s", path);
        return false;
    }
    while (fgets(line, sizeof(line), file) != NULL)
    {
        size_t n = strcspn(line, "\r\n");

        line[n] = '\0';
        if (strncmp(line, "port=", 5) == 0 && port < 0)
        {
            char *end;

            port = strtol(line + 5, &end, 10);
            if (*end != '\0' || end == line + 5 || port < 1 || port > 65535)
                port = 0;
        }
        else if (strncmp(line, "token=", 6) == 0 && !haveToken && strlen(line + 6) == 64)
        {
            int i;

            haveToken = true;
            for (i = 0; i < 32; i++)
            {
                int high = HexValue(line[6 + i * 2]);
                int low = HexValue(line[7 + i * 2]);

                if (high < 0 || low < 0)
                    haveToken = false;
                token[i] = (uint8_t)((high << 4) | (low & 15));
            }
            if (!haveToken)
                break;
        }
        else if (strcmp(line, sOptIns[0]) == 0 && !optIn[0])
            optIn[0] = true;
        else if (strcmp(line, sOptIns[1]) == 0 && !optIn[1])
            optIn[1] = true;
        else if (strcmp(line, sOptIns[2]) == 0 && !optIn[2])
            optIn[2] = true;
        else if (strcmp(line, sOptIns[3]) == 0 && !optIn[3])
            optIn[3] = true;
        else if (strcmp(line, sOptIns[4]) == 0 && !optIn[4])
            optIn[4] = true;
        else if (line[0] != '\0')
        {
            port = 0;
            break;
        }
    }
    fclose(file);
    if (port < 1 || !haveToken)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Multiplayer bridge: %s must hold port=1..65535 and token=64 lowercase hex digits", path);
        return false;
    }

    /* What scripts/multiplayer/generate_manifest.py and bridge_config.py would
     * give mgba.lua for this build: the save mailbox always exists; trade needs
     * the ownership save profile as well as its own marker. */
    memset(&caps, 0, sizeof(caps));
    caps.presenceAnimation = *(const volatile u32 *)&gMultiplayerPresenceAnimationAbiVersion;
    caps.presenceDetail = *(const volatile u32 *)&gMultiplayerPresenceDetailAbiVersion;
    caps.save = 1;
    caps.trade = *(const volatile u32 *)&gMultiplayerOwnershipAbi == 1
               ? *(const volatile u32 *)&gMultiplayerTradeAbiVersion : 0;
    caps.emergencySave = *(const volatile u32 *)&gMultiplayerEmergencySaveAbiVersion;
    caps.battle = *(const volatile u32 *)&gMultiplayerBattleAbiVersion;
    caps.battleResume = *(const volatile u32 *)&gMultiplayerBattleResumeAbiVersion;
    caps.battleAdmissionCleanup = *(const volatile u32 *)&gMultiplayerBattleAdmissionCleanupAbiVersion;
    caps.battleExecution = caps.battleAdmissionCleanup ? 1 : 0;
#if MP_FFA_ENABLED
    caps.battleFfa = *(const volatile u32 *)&gMultiplayerBattleFfaAbiVersion;
    caps.battleFfa3 = *(const volatile u32 *)&gMultiplayerBattleFfa3AbiVersion;
#endif
    caps.menuRelease = *(const volatile u32 *)&gMultiplayerMenuReleaseAbiVersion;
    caps.saveImportWindow = *(const volatile u32 *)&gMultiplayerSaveImportWindowAbiVersion;
    /* Update 32: the launcher opts in when its manifest has menu_open_page_abi_version 1 (an older launcher would
     * refuse the page an OPEN names). */
    caps.menuOpenPage = optIn[3] ? *(const volatile u32 *)&gMultiplayerMenuOpenPageAbiVersion : 0;
    /* Battle Tower: Legends: the launcher opts in (tower=1) when its manifest has tower_abi_version 1. */
    caps.tower = optIn[4] && caps.menuOpenPage == 1 ? *(const volatile u32 *)&gMultiplayerTowerAbiVersion : 0;
    caps.saveWindow = *(const volatile u32 *)&gMultiplayerSaveWindowAbiVersion;
    caps.menu = *(const volatile u32 *)&gMultiplayerMenuAbiVersion;
    /* The trade effect transports are explicit opt-ins of the manifest, never
     * inferred from their markers (generate_manifest.py): the launcher writes
     * the matching lines when its manifest has them. */
    caps.tradeEffectPrepare = optIn[0] ? *(const volatile u32 *)&gMultiplayerTradeEffectPrepareAbiVersion : 0;
    caps.tradeEffectSave = optIn[1] ? *(const volatile u32 *)&gMultiplayerTradeEffectSaveAbiVersion : 0;
    caps.tradeEffectChoice = optIn[2] ? *(const volatile u32 *)&gMultiplayerTradeEffectChoiceAbiVersion : 0;
    caps.reset = *(const volatile u32 *)&gMultiplayerResetAbiVersion;
    caps.resetInput = *(const volatile u32 *)&gMultiplayerResetInputAbiVersion;
    caps.world = *(const volatile u32 *)&gMultiplayerWorldAbiVersion;
    caps.spawns = caps.world ? *(const volatile u32 *)&gMultiplayerSpawnAbiVersion : 0;
    caps.extras = caps.world ? *(const volatile u32 *)&gMultiplayerExtrasAbiVersion : 0;
    problem = MpBridge_CheckCaps(&caps);
    if (problem != NULL)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Multiplayer bridge: %s", problem);
        return false;
    }

    memset(&ops, 0, sizeof(ops));
    ops.region[MP_REGION_PRESENCE] = (volatile uint8_t *)&gMultiplayerMailbox;
    ops.regionSize[MP_REGION_PRESENCE] = sizeof(gMultiplayerMailbox);
    ops.region[MP_REGION_SAVE] = (volatile uint8_t *)&gMultiplayerSaveMailbox;
    ops.regionSize[MP_REGION_SAVE] = sizeof(gMultiplayerSaveMailbox);
    ops.region[MP_REGION_BATTLE] = (volatile uint8_t *)&gMultiplayerBattleMailbox;
    ops.regionSize[MP_REGION_BATTLE] = sizeof(gMultiplayerBattleMailbox);
    ops.region[MP_REGION_MENU] = (volatile uint8_t *)&gMultiplayerMenuMailbox;
    ops.regionSize[MP_REGION_MENU] = sizeof(gMultiplayerMenuMailbox);
    ops.region[MP_REGION_RESET] = (volatile uint8_t *)&gMultiplayerResetMailbox;
    ops.regionSize[MP_REGION_RESET] = sizeof(gMultiplayerResetMailbox);
    ops.region[MP_REGION_WORLD] = (volatile uint8_t *)&gMultiplayerWorldMailbox;
    ops.regionSize[MP_REGION_WORLD] = sizeof(gMultiplayerWorldMailbox);
    ops.region[MP_REGION_WINDOW] = (volatile uint8_t *)&gSaveDataBuffer;
    ops.regionSize[MP_REGION_WINDOW] = sizeof(gSaveDataBuffer);
    ops.region[MP_REGION_SPAWN] = (volatile uint8_t *)&gMultiplayerSpawnMailbox;
    ops.regionSize[MP_REGION_SPAWN] = sizeof(gMultiplayerSpawnMailbox);
    ops.region[MP_REGION_EXTRAS] = (volatile uint8_t *)&gMultiplayerExtrasMailbox;
    ops.regionSize[MP_REGION_EXTRAS] = sizeof(gMultiplayerExtrasMailbox);
    ops.region[MP_REGION_IMPORT] = (volatile uint8_t *)gMultiplayerSaveImportWindow;
    ops.regionSize[MP_REGION_IMPORT] = sizeof(gMultiplayerSaveImportWindow);
    if (ops.regionSize[MP_REGION_PRESENCE] != 128 || ops.regionSize[MP_REGION_SAVE] != 512
     || ops.regionSize[MP_REGION_BATTLE] != 512 || ops.regionSize[MP_REGION_MENU] != 512
     || ops.regionSize[MP_REGION_RESET] != 128 || ops.regionSize[MP_REGION_WORLD] != 256
     || ops.regionSize[MP_REGION_WINDOW] != 4096 || ops.regionSize[MP_REGION_SPAWN] != 384
     || ops.regionSize[MP_REGION_EXTRAS] != 384 || ops.regionSize[MP_REGION_IMPORT] != 4096)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Multiplayer bridge: mailbox sizes differ from the ABI");
        return false;
    }
    ops.connect = GlueConnect;
    ops.send = GlueSend;
    ops.receive = GlueReceive;
    ops.close = GlueClose;
    ops.log = GlueLog;
    ops.routeResetChord = GlueRouteResetChord;
    ops.milliseconds = GlueMilliseconds;
    ops.battleLooks = GlueBattleLooks;
    memset(&sInput, 0, sizeof(sInput));
    MpBridge_Init(&sBridge, &ops, &caps, (uint16_t)port, token);
    memset(token, 0, sizeof(token));
    sEnabled = true;
    SDL_Log("Multiplayer bridge: companion at 127.0.0.1:%ld; save %u trade %u emergency %u battle %u/%u/%u "
            "window %u menu %u effects %u/%u/%u reset %u/%u world %u spawns %u running %u detail %u ffa %u/%u release %u "
            "import %u page %u tower %u",
            port, (unsigned)caps.save, (unsigned)caps.trade, (unsigned)caps.emergencySave,
            (unsigned)caps.battle, (unsigned)caps.battleResume, (unsigned)caps.battleAdmissionCleanup,
            (unsigned)caps.saveWindow, (unsigned)caps.menu, (unsigned)caps.tradeEffectPrepare,
            (unsigned)caps.tradeEffectSave, (unsigned)caps.tradeEffectChoice, (unsigned)caps.reset,
            (unsigned)caps.resetInput, (unsigned)caps.world, (unsigned)caps.spawns,
            (unsigned)caps.presenceAnimation, (unsigned)caps.presenceDetail, (unsigned)caps.battleFfa,
            (unsigned)caps.battleFfa3, (unsigned)caps.menuRelease, (unsigned)caps.saveImportWindow, (unsigned)caps.menuOpenPage,
            (unsigned)caps.tower);
    return true;
}

bool MpBridge_IsEnabled(void)
{
    return sEnabled;
}

bool MpBridge_MenuOpenPageEnabled(void)
{
    return sEnabled && sBridge.caps.menuOpenPage == 1;
}

bool MpBridge_TowerEnabled(void)
{
    return sEnabled && sBridge.caps.tower == 1;
}

/* The link status the game shows (the pause menu's LINK): see enum MpLinkState. A connection is shown only once the
 * companion reports the world session (Multiplayer_IsConnected: its heartbeat with the CONNECTED flag); "not connected"
 * after MP_LINK_PATIENCE_FRAMES without one, "lost" once one was had and has gone. */
#define MP_LINK_PATIENCE_FRAMES (60 * 20)
static bool sSetup, sLinkSeen;

void MpBridge_Frame(void)
{
    if (sEnabled)
    {
        MpBridge_Step(&sBridge);
        if (sLinkFrames < 0xFFFFFFFFu)
            sLinkFrames++;
        if (Multiplayer_IsConnected())
            sLinkSeen = true;
    }
}

void MpBridge_SetSetupSession(void)
{
    sSetup = true;
}

enum MpLinkState MpBridge_LinkState(void)
{
    if (!sEnabled)
        return sSetup ? MP_LINK_SETUP : MP_LINK_OFFLINE;
    if (Multiplayer_IsConnected())
        return MP_LINK_CONNECTED;
    if (sLinkSeen)
        return MP_LINK_LOST;
    return sLinkFrames >= MP_LINK_PATIENCE_FRAMES ? MP_LINK_NOT_CONNECTED : MP_LINK_CONNECTING;
}

bool MpBridge_SaveCommandPending(void)
{
    return sEnabled && sBridge.save.pending;
}

uint16_t MpBridge_FilterKeys(uint16_t keys)
{
    if (!sEnabled)
        return keys;
    return (uint16_t)MpBridge_FilterInput(&sInput, keys);
}

void MpBridge_Shutdown(void)
{
    if (!sEnabled)
        return;
    MpBridge_Stop(&sBridge);
    sEnabled = false;
#ifdef _WIN32
    if (sWinsockReady)
        WSACleanup();
    sWinsockReady = false;
#endif
}
#endif // MP_BRIDGE_UNIT_TEST
