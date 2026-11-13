/*
 * The fallback battle scene of a room (include/diorama/battle_arena.h; KNOWN_ISSUES V10, the owner's decision
 * #57): the arena's floor and walls, read off the room round the stage.
 *
 * The floor: where the arena stands on the room's own open floor at the battle's level, that floor as it is (a gym's
 * switches, a rug where they are) - a tile the floor has at least ARENA_FLOOR_COMMON times; one drawn only once or
 * twice is something's own (a statue's head drawn over the walkable tile above its base). Where something stood that
 * the arena clears away, and on a tile in the shadow of what stood north of it, the room's plain floor: read off its
 * open floor within reach of the stage (the shadowed tiles left out), one tile where it is most of them, else the
 * pattern they repeat in - the smallest period of up to two tiles each way in which nearly all of them agree, in step
 * with the room's own - so a checkerboard stays a checkerboard and a maze's walls leave no shadows behind.
 *
 * The walls: the room's north wall, as the GBA draws it - the two rows above the northmost floor of each column at
 * the battle's level - read for each column the arena's back wall spans, so the battle stands before the room's
 * own wall, its windows and its emblems where they are drawn; the side walls carry it on round the corners (the
 * columns past each end of the back wall). A column with no wall above its floor (a doorway, the room's edge) takes
 * the wall's commonest art in its phase.
 *
 * The scenery: the room's commonest object (a model a few cells across standing on its floor - Mt. Pyre's graves, a
 * gym's pillars or statues) set along both side walls, from the back wall to the battle's middle, a cell apart: the
 * room's own things round the battle, none in front of it nor within a tile and a half of a battler's feet.
 */
#include <math.h>
#include <string.h>

#include "diorama/battle_arena.h"
#include "diorama/battle_scene.h"

#define ARENA_MAX_REACH 24
#define ARENA_MAX_PERIOD 2            /* a plain floor's pattern repeats in at most this many tiles each way */
#define ARENA_WALL_PERIOD 4           /* a wall's (its windows, its emblems) in at most this many columns */
#define ARENA_LEVEL 0.05f            /* tiles: a floor tile at the battle's level */
#define ARENA_AGREEMENT 0.85f        /* of the floor tiles that agree with their phase's commonest */
#define ARENA_DOMINANT 0.6f          /* of the floor tiles one tile is: laid alone */
#define ARENA_FLOOR_COMMON 4         /* times a tile is on the room's open floor to be its floor's */
#define ARENA_PHASE_SAMPLES 2        /* each phase read off at least this many tiles */
#define ARENA_TALLY_SLOTS 16         /* distinct pictures counted in one phase */
#define ARENA_MAX_SAMPLES ((2 * ARENA_MAX_REACH + 1) * (2 * ARENA_MAX_REACH + 1))
#define ARENA_WALL_SPAN (DIORAMA_ARENA_MAX_COLUMNS + 2 * DIORAMA_ARENA_MAX_ROWS + 2)
#define ARENA_PIECE_MAX_CELLS 3   /* an object set round the battle: at most this many cells each way, */
#define ARENA_PIECE_MIN_TOP 0.4f  /* standing at least this high (not a mat), */
#define ARENA_PIECE_MAX_TOP 2.5f  /* not much over the walls */
#define ARENA_PIECE_CLEAR 1.5f    /* tiles kept between a copy and a battler's feet */

struct Tally
{
    uint32_t key[ARENA_TALLY_SLOTS];
    uint16_t count[ARENA_TALLY_SLOTS];
    uint8_t used;
    uint16_t total;
};

struct Sample
{
    int16_t x, y;
    uint16_t slot;
};

static struct Sample sSamples[ARENA_MAX_SAMPLES];

static void Count(struct Tally *t, uint32_t key)
{
    ++t->total;
    for (unsigned i = 0; i < t->used; ++i)
        if (t->key[i] == key)
        {
            ++t->count[i];
            return;
        }
    if (t->used < ARENA_TALLY_SLOTS)
    {
        t->key[t->used] = key;
        t->count[t->used] = 1;
        ++t->used;
    }
}

/* The commonest key (the smaller on a tie, so the plan never depends on the order the room was read in). */
static bool Mode(const struct Tally *t, uint32_t *key, unsigned *count)
{
    unsigned best = 0;

    if (t->used == 0)
        return false;
    for (unsigned i = 1; i < t->used; ++i)
        if (t->count[i] > t->count[best] || (t->count[i] == t->count[best] && t->key[i] < t->key[best]))
            best = i;
    *key = t->key[best];
    if (count != NULL)
        *count = t->count[best];
    return true;
}

static int Phase(int v, int period)
{
    int p = v % period;

    return p < 0 ? p + period : p;
}

static bool FloorAt(const struct DioramaArenaRequest *r, int x, int y, struct DioramaArenaCell *cell)
{
    return r->cellAt(r->context, x, y, cell) && cell->open && !cell->blocked
        && cell->kind != DIORAMA_GROUND_VOID && fabsf(cell->lift - r->ground) <= ARENA_LEVEL;
}

/* A floor tile of the battle's own ground, its art the floor's alone: open floor at the battle's level, nothing
 * standing north of it to throw a shadow on it (a wall's foot drawn on the tile). */
static bool PlainFloorAt(const struct DioramaArenaRequest *r, int x, int y, struct DioramaArenaCell *cell)
{
    struct DioramaArenaCell north;

    return FloorAt(r, x, y, cell) && cell->kind == r->floorKind && cell->slot != DIORAMA_ARENA_NO_SLOT
        && (!r->cellAt(r->context, x, y - 1, &north) || north.open || north.kind == DIORAMA_GROUND_VOID);
}

/* The floor's plain tiles round the stage. */
static int ReadFloor(const struct DioramaArenaRequest *r, int cx, int cy, int reach)
{
    int n = 0;

    for (int y = cy - reach; y <= cy + reach; ++y)
        for (int x = cx - reach; x <= cx + reach; ++x)
        {
            struct DioramaArenaCell cell;

            if (n < ARENA_MAX_SAMPLES && PlainFloorAt(r, x, y, &cell))
            {
                sSamples[n].x = (int16_t)x;
                sSamples[n].y = (int16_t)y;
                sSamples[n].slot = cell.slot;
                ++n;
            }
        }
    return n;
}

/* How many of the floor's plain tiles are drawn with this picture. */
static int SampleCount(int n, uint16_t slot)
{
    int count = 0;

    for (int i = 0; i < n; ++i)
        count += sSamples[i].slot == slot;
    return count;
}

/* The smallest period the floor repeats in (see the top of the file); its phases' commonest tiles in *phases. */
static void FloorPattern(int n, int *periodX, int *periodY, struct Tally phases[ARENA_MAX_PERIOD][ARENA_MAX_PERIOD])
{
    float bestAgreement = -1.0f;
    int bestX = 1, bestY = 1;
    bool bestMet = false;

    /* one tile most of the floor: that tile alone */
    {
        static struct Tally one;
        uint32_t key;
        unsigned count;

        memset(&one, 0, sizeof(one));
        for (int i = 0; i < n; ++i)
            Count(&one, sSamples[i].slot);
        if (Mode(&one, &key, &count) && (float)count >= ARENA_DOMINANT * (float)n)
        {
            bestMet = true;
            bestAgreement = (float)count / (float)n;
        }
    }
    for (int area = 1; area <= ARENA_MAX_PERIOD * ARENA_MAX_PERIOD && !bestMet; ++area)
        for (int px = 1; px <= ARENA_MAX_PERIOD; ++px)
        {
            int py = area / px;
            static struct Tally t[ARENA_MAX_PERIOD][ARENA_MAX_PERIOD];
            unsigned agree = 0;
            bool enough = true;
            float agreement;

            if (area % px != 0 || py > ARENA_MAX_PERIOD)
                continue;
            memset(t, 0, sizeof(t));
            for (int i = 0; i < n; ++i)
                Count(&t[Phase(sSamples[i].y, py)][Phase(sSamples[i].x, px)], sSamples[i].slot);
            for (int j = 0; j < py; ++j)
                for (int i = 0; i < px; ++i)
                {
                    uint32_t key;
                    unsigned count;

                    if (t[j][i].total < ARENA_PHASE_SAMPLES || !Mode(&t[j][i], &key, &count))
                    {
                        enough = false;
                        continue;
                    }
                    agree += count;
                }
            if (!enough)
                continue;
            agreement = (float)agree / (float)n;
            /* the first (smallest) period that agrees enough; else the one that agrees best */
            if (agreement >= ARENA_AGREEMENT && !bestMet)
            {
                bestMet = true;
                bestAgreement = agreement;
                bestX = px;
                bestY = py;
            }
            else if (!bestMet && agreement > bestAgreement + 0.02f)
            {
                bestAgreement = agreement;
                bestX = px;
                bestY = py;
            }
        }
    *periodX = bestX;
    *periodY = bestY;
    memset(phases, 0, sizeof(struct Tally) * ARENA_MAX_PERIOD * ARENA_MAX_PERIOD);
    for (int i = 0; i < n; ++i)
        Count(&phases[Phase(sSamples[i].y, bestY)][Phase(sSamples[i].x, bestX)], sSamples[i].slot);
}

/* The two rows above the northmost floor of column x at the battle's level, upper row first; false where the
 * column has no wall there. */
static bool WallAbove(const struct DioramaArenaRequest *r, int x, int top, int bottom, uint16_t rows[2])
{
    for (int y = top; y <= bottom; ++y)
    {
        struct DioramaArenaCell floor, lower, upper;

        if (!FloorAt(r, x, y, &floor))
            continue;
        if (!r->cellAt(r->context, x, y - 1, &lower) || lower.open || lower.kind == DIORAMA_GROUND_VOID
         || lower.slot == DIORAMA_ARENA_NO_SLOT)
            return false;
        rows[1] = lower.slot;
        rows[0] = r->cellAt(r->context, x, y - 2, &upper) && !upper.open && upper.kind != DIORAMA_GROUND_VOID
                && upper.slot != DIORAMA_ARENA_NO_SLOT ? upper.slot : lower.slot;
        return true;
    }
    return false;
}

static uint32_t WallKey(const uint16_t rows[2])
{
    return ((uint32_t)rows[0] << 16) | rows[1];
}

static bool Usable(const struct DioramaArenaObject *o)
{
    return o->width >= 1 && o->height >= 1 && o->width <= ARENA_PIECE_MAX_CELLS && o->height <= ARENA_PIECE_MAX_CELLS
        && o->top >= ARENA_PIECE_MIN_TOP && o->top <= ARENA_PIECE_MAX_TOP;
}

/* The room's commonest usable model (the smaller model on a tie): the index of its first object, -1 for none. */
static int CommonestObject(const struct DioramaArenaRequest *r)
{
    int best = -1;
    unsigned bestCount = 0;

    for (unsigned i = 0; i < r->objectCount; ++i)
    {
        unsigned count = 0;
        bool first = true;

        if (!Usable(&r->objects[i]))
            continue;
        for (unsigned j = 0; j < r->objectCount && first; ++j)
            if (r->objects[j].model == r->objects[i].model && Usable(&r->objects[j]))
            {
                if (j < i)
                    first = false;
                else
                    ++count;
            }
        if (first && (count > bestCount
                      || (count == bestCount && best >= 0 && r->objects[i].model < r->objects[best].model)))
        {
            best = (int)i;
            bestCount = count;
        }
    }
    return best;
}

static bool NearBattler(const struct DioramaArenaRequest *r, float x0, float y0, float x1, float y1)
{
    for (unsigned b = 0; b < r->battlerCount && b < 4; ++b)
    {
        float fx = r->targetX + r->battlerX[b], fy = r->targetY + r->battlerY[b];

        if (fx > x0 - ARENA_PIECE_CLEAR && fx < x1 + ARENA_PIECE_CLEAR
         && fy > y0 - ARENA_PIECE_CLEAR && fy < y1 + ARENA_PIECE_CLEAR)
            return true;
    }
    return false;
}

/* The scenery (see the top of the file). */
static void ArrangePieces(const struct DioramaArenaRequest *r, struct DioramaArena *a)
{
    int pick = r->objects != NULL ? CommonestObject(r) : -1;
    const struct DioramaArenaObject *o;
    int w, h, westX, eastX;

    if (pick < 0)
        return;
    o = &r->objects[pick];
    w = o->width;
    h = o->height;
    westX = a->x0;
    eastX = a->x0 + a->columns - w;
    if (eastX - westX < 2 * w + 2)
        return; /* too narrow for a copy each side clear of the battle */
    for (int y = a->y0; (float)(y + h) <= r->targetY && a->pieceCount + 2 <= DIORAMA_ARENA_MAX_PIECES; y += h + 1)
        for (int side = 0; side < 2; ++side)
        {
            int x = side == 0 ? westX : eastX;
            struct DioramaArenaPiece *p;

            if (NearBattler(r, (float)x, (float)y, (float)(x + w), (float)(y + h)))
                continue;
            p = &a->pieces[a->pieceCount++];
            p->id = o->id;
            p->x = (int16_t)x;
            p->y = (int16_t)y;
            p->width = (uint8_t)w;
            p->height = (uint8_t)h;
            p->top = o->top;
        }
}

bool DioramaArena_Plan(const struct DioramaArenaRequest *r, struct DioramaArena *arena)
{
    static struct Tally phases[ARENA_MAX_PERIOD][ARENA_MAX_PERIOD];
    struct Tally all;
    struct Tally wallPhase[ARENA_WALL_PERIOD];
    static uint16_t wall[ARENA_WALL_SPAN][2];
    static bool wallFound[ARENA_WALL_SPAN];
    int reach = r->reach < 1 ? 1 : r->reach > ARENA_MAX_REACH ? ARENA_MAX_REACH : r->reach;
    int cx = (int)floorf(r->targetX), cy = (int)floorf(r->targetY);
    int x1, y1, n, wallX0, span, wallPeriod = 1;
    uint32_t key, overall;

    memset(arena, 0, sizeof(*arena));
    if (r->cellAt == NULL)
        return false;
    /* The floor's tiles: whole tiles from the side walls' lines and the back wall's to past the eye. */
    arena->x0 = (int)floorf(r->targetX - r->halfWidth + 0.5f);
    x1 = (int)floorf(r->targetX + r->halfWidth + 0.5f);
    arena->y0 = (int)floorf(r->targetY - r->north + 0.5f);
    y1 = (int)ceilf(r->targetY + r->south);
    arena->columns = x1 - arena->x0;
    arena->rows = y1 - arena->y0;
    if (arena->columns < 1 || arena->rows < 1)
        return false;
    if (arena->columns > DIORAMA_ARENA_MAX_COLUMNS)
    {
        arena->x0 += (arena->columns - DIORAMA_ARENA_MAX_COLUMNS) / 2;
        arena->columns = DIORAMA_ARENA_MAX_COLUMNS;
    }
    if (arena->rows > DIORAMA_ARENA_MAX_ROWS)
        arena->rows = DIORAMA_ARENA_MAX_ROWS; /* the south end, under the eye, is what goes */
    arena->ground = r->ground;

    /* The floor. */
    n = ReadFloor(r, cx, cy, reach);
    arena->floorSamples = n;
    if (n == 0)
        return false;
    FloorPattern(n, &arena->periodX, &arena->periodY, phases);
    memset(&all, 0, sizeof(all));
    for (int i = 0; i < n; ++i)
        Count(&all, sSamples[i].slot);
    Mode(&all, &overall, NULL);
    for (int row = 0; row < arena->rows; ++row)
        for (int column = 0; column < arena->columns; ++column)
        {
            const struct Tally *t = &phases[Phase(arena->y0 + row, arena->periodY)][Phase(arena->x0 + column,
                                                                                          arena->periodX)];
            struct DioramaArenaCell real;

            if (PlainFloorAt(r, arena->x0 + column, arena->y0 + row, &real)
             && SampleCount(n, real.slot) >= ARENA_FLOOR_COMMON)
                arena->floor[row][column] = real.slot; /* the room's own floor here */
            else
                arena->floor[row][column] = (uint16_t)(Mode(t, &key, NULL) ? key : overall);
        }

    /* The north wall's art for every column the walls take it from: the back wall's own, and as many past each
     * end as the side walls have rows. */
    wallX0 = arena->x0 - arena->rows - 1;
    span = arena->columns + 2 * arena->rows + 2;
    memset(wallPhase, 0, sizeof(wallPhase));
    for (int i = 0; i < span && i < ARENA_WALL_SPAN; ++i)
    {
        wallFound[i] = WallAbove(r, wallX0 + i, cy - reach, cy + reach, wall[i]);
        arena->wallColumns += wallFound[i];
    }
    if (arena->wallColumns > 0)
    {
        /* its period, as the floor's (along the wall only) */
        float best = -1.0f;

        for (int p = 1; p <= ARENA_WALL_PERIOD; ++p)
        {
            struct Tally t[ARENA_WALL_PERIOD];
            unsigned agree = 0, total = 0;

            memset(t, 0, sizeof(t));
            for (int i = 0; i < span && i < ARENA_WALL_SPAN; ++i)
                if (wallFound[i])
                    Count(&t[Phase(wallX0 + i, p)], WallKey(wall[i]));
            for (int q = 0; q < p; ++q)
            {
                unsigned count;

                if (Mode(&t[q], &key, &count))
                    agree += count;
                total += t[q].total;
            }
            if (total != 0 && (float)agree / (float)total > best + 0.02f)
            {
                best = (float)agree / (float)total;
                wallPeriod = p;
                if (best >= ARENA_AGREEMENT)
                    break;
            }
        }
        for (int i = 0; i < span && i < ARENA_WALL_SPAN; ++i)
            if (wallFound[i])
                Count(&wallPhase[Phase(wallX0 + i, wallPeriod)], WallKey(wall[i]));
        for (int i = 0; i < span && i < ARENA_WALL_SPAN; ++i)
            if (!wallFound[i])
            {
                const struct Tally *t = &wallPhase[Phase(wallX0 + i, wallPeriod)];
                uint32_t k;

                if (!Mode(t, &k, NULL))
                {
                    /* this phase never seen: the commonest of all */
                    struct Tally any;

                    memset(&any, 0, sizeof(any));
                    for (int q = 0; q < wallPeriod; ++q)
                        for (unsigned s = 0; s < wallPhase[q].used; ++s)
                            for (unsigned c = 0; c < wallPhase[q].count[s]; ++c)
                                Count(&any, wallPhase[q].key[s]);
                    Mode(&any, &k, NULL);
                }
                wall[i][0] = (uint16_t)(k >> 16);
                wall[i][1] = (uint16_t)(k & 0xFFFF);
            }
        arena->walls = true;
        for (int column = 0; column < arena->columns; ++column)
            memcpy(arena->back[column], wall[column + arena->rows + 1], sizeof(arena->back[column]));
        for (int row = 0; row < arena->rows; ++row)
        {
            /* round the corners: the west wall takes the columns west of the back wall's, going south */
            memcpy(arena->west[row], wall[arena->rows - row], sizeof(arena->west[row]));
            memcpy(arena->east[row], wall[arena->rows + 1 + arena->columns + row], sizeof(arena->east[row]));
        }
    }
    else
    {
        arena->back[0][0] = arena->west[0][0] = arena->east[0][0] = DIORAMA_ARENA_NO_SLOT;
    }
    ArrangePieces(r, arena);
    arena->active = true;
    return true;
}
