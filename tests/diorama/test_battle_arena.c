/* The fallback battle scene's plan (src/diorama/scene/battle_arena.c; KNOWN_ISSUES V10). */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "diorama/battle_arena.h"
#include "diorama/battle_scene.h"
#include "diorama/battle_stage.h"

#define DEG (3.14159265358979323846f / 180.0f)

/* A synthetic room: 48x48 tiles, nothing known unless painted. */
struct Room
{
    struct DioramaArenaCell cells[48][48];
    bool known[48][48];
};

static struct Room sRoom;

static bool CellAt(void *context, int x, int y, struct DioramaArenaCell *cell)
{
    struct Room *room = context;

    if (x < 0 || y < 0 || x >= 48 || y >= 48 || !room->known[y][x])
        return false;
    *cell = room->cells[y][x];
    return true;
}

static void Paint(int x, int y, uint16_t slot, bool open, bool blocked, uint8_t kind, float lift)
{
    sRoom.known[y][x] = true;
    sRoom.cells[y][x].slot = slot;
    sRoom.cells[y][x].open = open;
    sRoom.cells[y][x].blocked = blocked;
    sRoom.cells[y][x].kind = kind;
    sRoom.cells[y][x].lift = lift;
}

/* A room from x0..x1, y0..y1 (inclusive) of floor: a checkerboard of slots 10 and 11 (or one slot, 10), its north
 * wall two rows above y0 (upper 20 + x % 3, lower 30 + x % 3), the black (void) round it. */
static void Room(int x0, int y0, int x1, int y1, bool checker)
{
    memset(&sRoom, 0, sizeof(sRoom));
    for (int y = 0; y < 48; ++y)
        for (int x = 0; x < 48; ++x)
            Paint(x, y, 1, false, true, DIORAMA_GROUND_VOID, 0.0f);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            Paint(x, y, (uint16_t)(checker ? 10 + ((x + y) & 1) : 10), true, false, DIORAMA_GROUND_PLAIN, 0.0f);
    for (int x = x0; x <= x1; ++x)
    {
        Paint(x, y0 - 2, (uint16_t)(20 + x % 3), false, true, DIORAMA_GROUND_PLAIN, 0.0f);
        Paint(x, y0 - 1, (uint16_t)(30 + x % 3), false, true, DIORAMA_GROUND_PLAIN, 0.0f);
    }
}

static void Request(struct DioramaArenaRequest *r, float tx, float ty)
{
    memset(r, 0, sizeof(*r));
    r->targetX = tx;
    r->targetY = ty;
    r->ground = 0.0f;
    r->floorKind = DIORAMA_GROUND_PLAIN;
    r->north = 4.5f;
    r->south = 8.5f;
    r->halfWidth = 6.0f;
    r->reach = 20;
    r->cellAt = CellAt;
    r->context = &sRoom;
}

/* The arena spans the request's box: the back wall north of the target by `north`, the floor past the eye. */
static void TestBox(void)
{
    struct DioramaArenaRequest r;
    struct DioramaArena a;

    Room(4, 6, 40, 40, false);
    Request(&r, 20.5f, 24.5f);
    assert(DioramaArena_Plan(&r, &a) && a.active);
    assert(a.x0 == 15 && a.columns == 12);         /* 14.5..26.5 → tiles 15..26 */
    assert(a.y0 == 20);                            /* 24.5 - 4.5 = 20 */
    assert(a.y0 + a.rows >= 33);                   /* past 24.5 + 8.5 */
    assert(a.ground == 0.0f);
    printf("box: x %d+%d, y %d+%d\n", a.x0, a.columns, a.y0, a.rows);
}

/* A plain floor is laid with its one tile; a checkerboard keeps its phase with the room's own. */
static void TestFloorPattern(void)
{
    struct DioramaArenaRequest r;
    struct DioramaArena a;

    Room(4, 6, 40, 40, false);
    Request(&r, 20.5f, 24.5f);
    assert(DioramaArena_Plan(&r, &a));
    assert(a.periodX == 1 && a.periodY == 1);
    for (int row = 0; row < a.rows; ++row)
        for (int column = 0; column < a.columns; ++column)
            assert(a.floor[row][column] == 10);
    Room(4, 6, 40, 40, true);
    assert(DioramaArena_Plan(&r, &a));
    assert(a.periodX * a.periodY == 2 || (a.periodX == 2 && a.periodY == 2));
    for (int row = 0; row < a.rows; ++row)
        for (int column = 0; column < a.columns; ++column)
            assert(a.floor[row][column] == 10 + (((a.x0 + column) + (a.y0 + row)) & 1));
    printf("floor: period %dx%d from %d tiles\n", a.periodX, a.periodY, a.floorSamples);
}

/* The room's own floor stays where the arena stands on it - a rug where it lies; under the furniture it clears away,
 * over a lower floor, on a tile in a table's shadow and on a statue's head drawn over a walkable tile, the room's
 * plain pattern. */
static void TestFloorKeepsTheRoom(void)
{
    struct DioramaArenaRequest r;
    struct DioramaArena a;

    Room(4, 6, 40, 40, true);
    for (int y = 22; y <= 26; ++y)
        for (int x = 18; x <= 23; ++x)
            Paint(x, y, 77, true, false, DIORAMA_GROUND_PLAIN, 0.0f);       /* a rug */
    for (int x = 10; x <= 30; x += 4)
    {
        Paint(x, 28, 88, false, true, DIORAMA_GROUND_PLAIN, 0.0f);          /* tables */
        Paint(x, 29, 66, true, false, DIORAMA_GROUND_PLAIN, 0.0f);          /* their shadows */
    }
    for (int x = 4; x <= 40; ++x)
        Paint(x, 31, 99, true, false, DIORAMA_GROUND_PLAIN, -1.0f);         /* a lower floor */
    Paint(16, 21, 55, true, false, DIORAMA_GROUND_PLAIN, 0.0f);             /* a statue's head, on a walkable tile */
    Paint(16, 22, 56, false, true, DIORAMA_GROUND_PLAIN, 0.0f);             /* its base */
    Request(&r, 20.5f, 24.5f);
    assert(DioramaArena_Plan(&r, &a));
    assert(a.periodX == 2 && a.periodY == 2);
    for (int row = 0; row < a.rows; ++row)
        for (int column = 0; column < a.columns; ++column)
        {
            int x = a.x0 + column, y = a.y0 + row;
            bool rug = x >= 18 && x <= 23 && y >= 22 && y <= 26;

            assert(a.floor[row][column] == (rug ? 77 : 10 + ((x + y) & 1)));
        }
    printf("floor keeps the room: the rug where it lies, the pattern under the tables, their shadows, the lower floor, "
           "a statue's head\n");
}

/* The back wall is the room's north wall, column for column; the side walls carry it on round the corners. */
static void TestWalls(void)
{
    struct DioramaArenaRequest r;
    struct DioramaArena a;

    Room(4, 6, 40, 40, false);
    Request(&r, 20.5f, 24.5f);
    assert(DioramaArena_Plan(&r, &a) && a.walls);
    for (int column = 0; column < a.columns; ++column)
    {
        int x = a.x0 + column;

        assert(a.back[column][0] == 20 + x % 3 && a.back[column][1] == 30 + x % 3);
    }
    for (int row = 0; row < a.rows; ++row)
    {
        int west = a.x0 - 1 - row, east = a.x0 + a.columns + row;

        assert(a.west[row][0] == 20 + west % 3 && a.west[row][1] == 30 + west % 3);
        assert(a.east[row][0] == 20 + east % 3 && a.east[row][1] == 30 + east % 3);
    }
    printf("walls: %d columns of the north wall read\n", a.wallColumns);
}

/* Where the north wall has a doorway, or the arena reaches past the room's sides, the wall's art in its phase. */
static void TestWallGaps(void)
{
    struct DioramaArenaRequest r;
    struct DioramaArena a;

    Room(12, 6, 28, 40, false);
    Paint(20, 5, 5, true, false, DIORAMA_GROUND_PLAIN, 0.0f); /* a doorway in the wall's lower row */
    Paint(20, 4, 1, false, true, DIORAMA_GROUND_VOID, 0.0f);  /* the black over it */
    Request(&r, 20.5f, 24.5f);
    r.halfWidth = 11.0f;                                       /* past the room's sides, 12..28 */
    assert(DioramaArena_Plan(&r, &a) && a.walls);
    for (int column = 0; column < a.columns; ++column)
    {
        int x = a.x0 + column;

        assert(a.back[column][0] == 20 + x % 3 && a.back[column][1] == 30 + x % 3);
    }
    printf("wall gaps: %d of %d columns read\n", a.wallColumns, a.columns);
}

/* A maze: walls in rows, each throwing its shadow on the tile south of it, as regular as a tiling - the floor's
 * plain tile alone, under the walls and their shadows too. */
static void TestMazeFloor(void)
{
    struct DioramaArenaRequest r;
    struct DioramaArena a;

    Room(4, 6, 40, 40, false);
    for (int y = 8; y <= 40; ++y)
        for (int x = 4; x <= 40; ++x)
            if (x % 4 != 0)
            {
                if (y % 3 == 2)
                    Paint(x, y, 40, false, true, DIORAMA_GROUND_PLAIN, 0.0f); /* a wall */
                else if (y % 3 == 0)
                    Paint(x, y, 12, true, false, DIORAMA_GROUND_PLAIN, 0.0f); /* its shadow */
            }
    Request(&r, 20.5f, 24.5f);
    assert(DioramaArena_Plan(&r, &a));
    assert(a.periodX == 1 && a.periodY == 1);
    for (int row = 0; row < a.rows; ++row)
        for (int column = 0; column < a.columns; ++column)
            assert(a.floor[row][column] == 10);
    printf("maze floor: the plain tile alone\n");
}

/* A gym's switches on a regular lattice, a quarter of the floor: where the arena fills - under a cleared table on
 * the lattice - the plain tile that is most of the floor, not the lattice's switch; the switches stay where they lie. */
static void TestDominantFloor(void)
{
    struct DioramaArenaRequest r;
    struct DioramaArena a;

    Room(4, 6, 40, 40, false);
    for (int y = 6; y <= 40; ++y)
        for (int x = 4; x <= 40; ++x)
            if (x % 2 == 0 && y % 2 == 0)
                Paint(x, y, 13, true, false, DIORAMA_GROUND_PLAIN, 0.0f);  /* a switch */
    Paint(20, 22, 88, false, true, DIORAMA_GROUND_PLAIN, 0.0f);            /* a table on the lattice */
    Request(&r, 20.5f, 24.5f);
    assert(DioramaArena_Plan(&r, &a));
    assert(a.periodX == 1 && a.periodY == 1);
    for (int row = 0; row < a.rows; ++row)
        for (int column = 0; column < a.columns; ++column)
        {
            int x = a.x0 + column, y = a.y0 + row;
            /* the table's own tile, and the one in its shadow south of it, are filled */
            bool filled = x == 20 && (y == 22 || y == 23);

            assert(a.floor[row][column] == (!filled && x % 2 == 0 && y % 2 == 0 ? 13 : 10));
        }
    printf("dominant floor: the plain tile where the arena fills, the switches where they lie\n");
}

/* A cave's two levels: the floor and the wall of the battle's own level. */
static void TestLevels(void)
{
    struct DioramaArenaRequest r;
    struct DioramaArena a;

    Room(4, 6, 40, 40, false);
    for (int y = 6; y <= 15; ++y)
        for (int x = 4; x <= 40; ++x)
            Paint(x, y, 50, true, false, DIORAMA_GROUND_PLAIN, 1.0f); /* a raised level, its own floor */
    for (int x = 4; x <= 40; ++x)
        Paint(x, 16, (uint16_t)(60 + x % 2), false, true, DIORAMA_GROUND_PLAIN, 0.0f); /* its face */
    Request(&r, 20.5f, 26.5f);
    assert(DioramaArena_Plan(&r, &a));
    for (int row = 0; row < a.rows; ++row)
        for (int column = 0; column < a.columns; ++column)
            assert(a.floor[row][column] == 10);
    for (int column = 0; column < a.columns; ++column)
        assert(a.back[column][1] == 60 + (a.x0 + column) % 2);
    printf("levels: the lower level's floor and the raised one's face\n");
}

/* No floor of the battle's ground in reach: no arena. */
static void TestNoFloor(void)
{
    struct DioramaArenaRequest r;
    struct DioramaArena a;

    Room(4, 6, 40, 40, false);
    Request(&r, 20.5f, 24.5f);
    r.floorKind = DIORAMA_GROUND_SAND;
    assert(!DioramaArena_Plan(&r, &a) && !a.active);
    printf("no floor: no arena\n");
}

/* The room's commonest object, copies along both side walls from the back wall to the battle's middle, clear of the
 * battlers; a lone statue, a long counter and a mat left out. */
static void TestPieces(void)
{
    static const struct DioramaArenaObject objects[] = {
        {1, 100, 1, 1, 1.0f}, {2, 100, 1, 1, 1.0f}, {3, 200, 1, 2, 1.5f}, {4, 100, 1, 1, 1.0f},
        {5, 300, 5, 1, 1.0f}, {6, 300, 5, 1, 1.0f}, {7, 300, 5, 1, 1.0f}, {8, 300, 5, 1, 1.0f},
        {9, 400, 2, 2, 0.1f}, {10, 400, 2, 2, 0.1f}, {11, 400, 2, 2, 0.1f}, {12, 400, 2, 2, 0.1f},
    };
    struct DioramaArenaRequest r;
    struct DioramaArena a;
    unsigned west = 0, east = 0;

    Room(4, 6, 40, 40, false);
    Request(&r, 20.5f, 24.5f);
    r.objects = objects;
    r.objectCount = sizeof(objects) / sizeof(objects[0]);
    r.battlerCount = 2;
    r.battlerX[0] = -1.6f; r.battlerY[0] = 1.2f;
    r.battlerX[1] = 1.6f;  r.battlerY[1] = -1.2f;
    assert(DioramaArena_Plan(&r, &a) && a.pieceCount >= 2);
    for (unsigned i = 0; i < a.pieceCount; ++i)
    {
        const struct DioramaArenaPiece *p = &a.pieces[i];

        assert(p->id == 1 && p->width == 1 && p->height == 1 && p->top == 1.0f);
        assert(p->x == a.x0 || p->x == a.x0 + a.columns - 1);
        west += p->x == a.x0;
        east += p->x != a.x0;
        assert(p->y >= a.y0 && (float)(p->y + p->height) <= r.targetY);
        for (unsigned b = 0; b < 2; ++b)
        {
            float fx = r.targetX + r.battlerX[b], fy = r.targetY + r.battlerY[b];

            assert(!(fx > p->x - 1.5f && fx < p->x + p->width + 1.5f
                     && fy > p->y - 1.5f && fy < p->y + p->height + 1.5f));
        }
        for (unsigned j = 0; j < i; ++j)
            assert(!(a.pieces[j].x == p->x && a.pieces[j].y == p->y));
    }
    assert(west > 0 && east > 0);
    r.objectCount = 0;
    assert(DioramaArena_Plan(&r, &a) && a.pieceCount == 0);
    printf("pieces: %u copies of the commonest object, %u west and %u east\n", west + east, west, east);
}

/* The arena as the stage search reads it: its floor plain, its walls and the copies tall. */
static bool ArenaStageCell(void *context, int x, int y, struct DioramaStageCell *cell)
{
    const struct DioramaArena *a = context;
    bool inside = x >= a->x0 && x < a->x0 + a->columns && y >= a->y0 && y < a->y0 + a->rows;
    bool wall = (y == a->y0 - 1 && x >= a->x0 - 1 && x <= a->x0 + a->columns)
             || ((x == a->x0 - 1 || x == a->x0 + a->columns) && y >= a->y0 && y < a->y0 + a->rows);

    if (!inside && !wall)
        return false;
    memset(cell, 0, sizeof(*cell));
    cell->kind = DIORAMA_GROUND_PLAIN;
    cell->lift = a->ground;
    if (wall)
    {
        cell->blocked = true;
        cell->top = (float)DIORAMA_ARENA_WALL_ROWS;
        return true;
    }
    for (unsigned i = 0; i < a->pieceCount; ++i)
    {
        const struct DioramaArenaPiece *p = &a->pieces[i];

        if (x >= p->x && x < p->x + p->width && y >= p->y && y < p->y + p->height)
        {
            cell->blocked = true;
            cell->top = p->top;
        }
    }
    return true;
}

/* What the battle camera sees of the arena, the battle as the stage search stands it there: nothing over a battler,
 * no wall pressed behind one, none of the black past the room (the request's own wide window). */
static void TestArenaIsClear(void)
{
    static const struct DioramaArenaObject objects[] = {{1, 100, 1, 1, 1.0f}, {2, 100, 1, 1, 1.0f}};
    struct DioramaArenaRequest r;
    struct DioramaStageRequest s;
    struct DioramaStageView seen;
    struct DioramaArena a;

    Room(4, 6, 40, 40, false);
    Request(&r, 20.5f, 24.5f);
    r.objects = objects;
    r.objectCount = 2;
    r.battlerCount = 2;
    r.battlerX[0] = -1.6f; r.battlerY[0] = 1.2f;
    r.battlerX[1] = 1.6f;  r.battlerY[1] = -1.2f;
    assert(DioramaArena_Plan(&r, &a) && a.pieceCount > 0);
    memset(&s, 0, sizeof(s));
    s.view.pitch = 30.0f * DEG;
    s.view.distance = 8.0829f;
    s.view.focal = 271.8f;
    s.view.targetRow = 60.57f;
    s.visibleLeft = -40.0f;
    s.visibleRight = 280.0f;
    s.visibleTop = -20.0f;
    s.visibleBottom = 112.0f;
    s.playerX = 20;
    s.playerY = 25;
    s.facing = 2;
    s.wild = true;
    s.indoor = true;
    s.cellAt = ArenaStageCell;
    s.context = &a;
    DioramaStage_Measure(&s, r.targetX, r.targetY, &seen);
    printf("arena seen: black %.1f%%, a battler covered %.1f%%, pressed %.2f\n", 100.0f * seen.voidFraction,
           100.0f * seen.overlap, seen.cramped);
    assert(seen.overlap < 0.001f && seen.cramped < 0.001f && seen.voidFraction < 0.02f);
}

int main(void)
{
    TestBox();
    TestFloorPattern();
    TestFloorKeepsTheRoom();
    TestWalls();
    TestWallGaps();
    TestMazeFloor();
    TestDominantFloor();
    TestLevels();
    TestNoFloor();
    TestPieces();
    TestArenaIsClear();
    printf("test_battle_arena: all passed\n");
    return 0;
}
