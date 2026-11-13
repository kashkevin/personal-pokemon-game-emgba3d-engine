#ifndef GUARD_DIORAMA_BATTLE_ARENA_H
#define GUARD_DIORAMA_BATTLE_ARENA_H

#include <stdbool.h>
#include <stdint.h>

/*
 * The fallback battle scene of a room (KNOWN_ISSUES V10, the owner's decision #57): where the battle stage search
 * finds no clear camera position in a room or a cave - every stage near the player has a wall pressed close
 * behind a battler, something standing over a battler's picture, or the black past the room in view - the
 * battle stands in an arena of the room's own: the room's floor under it, its pattern kept, and the room's own
 * north wall - its art, the two rows the GBA draws it in - standing behind the battle and at its sides, where the
 * battle camera reads them as clear. The room's light and air are the field's (the arena is drawn by the
 * terrain's own shader, gl_terrain_renderer.h); what stands inside the arena is cleared away for the battle
 * (gl_light.h DioramaGLLight_SetClearing), and past its walls the room stays as it is. Where the search finds a
 * clear stage, the battle stands in the room as it is (gl_battle.c ChooseStage).
 *
 * No GL, no game headers: the room comes through a callback, so the plan is unit-tested on its own
 * (tests/diorama/test_battle_arena.c). Coordinates are the stage search's (battle_stage.h): the map's tiles,
 * tile (x, y) spanning x..x+1 and y..y+1, y growing south.
 */

#define DIORAMA_ARENA_NO_SLOT 0xFFFF
#define DIORAMA_ARENA_MAX_COLUMNS 28
#define DIORAMA_ARENA_MAX_ROWS 24
#define DIORAMA_ARENA_WALL_ROWS 2   /* a room's wall is drawn in two rows of metatiles */
#define DIORAMA_ARENA_MAX_PIECES 12 /* copies of the room's objects set round the battle */

/* One map cell as the arena reads it. */
struct DioramaArenaCell
{
    uint16_t slot;  /* its picture: an atlas slot (metatile_atlas.h DioramaAtlas_CellSlot) */
    uint8_t kind;   /* enum DioramaBattleGround (battle_scene.h) */
    bool blocked;   /* collision */
    bool open;      /* nothing standing on it, no step, no slope: the room's plain floor */
    float lift;     /* its ground's height, tiles */
};

/* Fills *cell for tile (x, y); false where nothing is known (the black past a room, past the scene). */
typedef bool (*DioramaArenaCellFn)(void *context, int x, int y, struct DioramaArenaCell *cell);

/* One of the room's own objects - a model standing on its floor (a grave, a statue, a pillar, a plant) - as the
 * caller found it. */
struct DioramaArenaObject
{
    uint32_t id;           /* the caller's (a placement) */
    uint32_t model;        /* the same for every copy of one model */
    uint8_t width, height; /* cells */
    float top;             /* its height, tiles */
};

/* A copy of one of them set in the arena, its north-west tile at (x, y). */
struct DioramaArenaPiece
{
    uint32_t id;
    int16_t x, y;
    uint8_t width, height;
    float top;
};

struct DioramaArenaRequest
{
    float targetX, targetY; /* the stage's target (battle_stage.h DioramaStageResult) */
    float ground;           /* the stage's ground */
    uint8_t floorKind;      /* the ground the battle stands on (enum DioramaBattleGround) */
    float north;            /* the back wall's distance north of the target, tiles */
    float south;            /* how far south of the target the floor reaches (past the eye), tiles */
    float halfWidth;        /* the floor's half width, tiles: the side walls stand there */
    int reach;              /* how far round the target the room is read for its floor and its wall, tiles */
    /* The room's objects to set round the battle, and where the battlers stand (tiles from the target): no copy
     * stands in front of the battle, nor within a tile and a half of a battler's feet. */
    const struct DioramaArenaObject *objects;
    unsigned objectCount;
    unsigned battlerCount;
    float battlerX[4], battlerY[4];
    DioramaArenaCellFn cellAt;
    void *context;
};

struct DioramaArena
{
    bool active;
    /* The floor: tiles x0..x0+columns-1, y0..y0+rows-1; the back wall stands on its north edge (y = y0), the side
     * walls on its west (x = x0) and east (x = x0 + columns) edges, from the back wall to its south edge. */
    int x0, y0;
    int columns, rows;
    float ground;
    uint16_t floor[DIORAMA_ARENA_MAX_ROWS][DIORAMA_ARENA_MAX_COLUMNS];
    /* Each wall's art, its upper row first: the back wall's for each column west to east, each side wall's for
     * each row north to south. DIORAMA_ARENA_NO_SLOT in [0][0] of a wall: the room has no wall to show (it
     * stands bare). */
    uint16_t back[DIORAMA_ARENA_MAX_COLUMNS][DIORAMA_ARENA_WALL_ROWS];
    uint16_t west[DIORAMA_ARENA_MAX_ROWS][DIORAMA_ARENA_WALL_ROWS];
    uint16_t east[DIORAMA_ARENA_MAX_ROWS][DIORAMA_ARENA_WALL_ROWS];
    bool walls;            /* the room gave its wall's art */
    /* The room's own objects set round the battle: along its side walls, from the back wall to the battle's
     * middle, the room's commonest object (KNOWN_ISSUES V10: the scenery arranged round the battle). */
    struct DioramaArenaPiece pieces[DIORAMA_ARENA_MAX_PIECES];
    unsigned pieceCount;
    /* What the plan found, for the log and the tests: the floor's pattern repeats every periodX x periodY tiles,
     * read off this many of the room's floor tiles; this many columns of its north wall were read. */
    int periodX, periodY;
    int floorSamples;
    int wallColumns;
};

/* Plans the arena; false (arena->active false) when the room gives no floor to lay. */
bool DioramaArena_Plan(const struct DioramaArenaRequest *request, struct DioramaArena *arena);

#endif // GUARD_DIORAMA_BATTLE_ARENA_H
