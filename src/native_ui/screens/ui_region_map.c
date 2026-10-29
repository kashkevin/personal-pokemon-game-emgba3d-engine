#ifdef ENABLE_NATIVE_UI
/*
 * The Fly map (region_map.c: FLY from the party menu) and the wall map (field_region_map.c: the region map on a
 * Pokemon Center's wall) in the design's own pieces instead of the game's 240x160 screens: the region's map art (the
 * Pokédex AREA page's) at twice the game's size in a bezel, the places you can fly to as pins (bright once visited, as
 * the game's icons are), the Battle Frontier's red frame, you and the other trainers where the game puts them (the
 * multiplayer markers), the place under the cursor lit cell by cell, and its name in a side card with whether you can
 * fly there. The game keeps the map: its cursor and its moves, its names, the flight. A click on a cell puts the
 * cursor there (the map's own input takes it, as SELECT's jump to a trainer does); a click on the cursor's own place
 * flies there on the Fly map (A); a right click goes back (B). The owner's request of 2026-10-26: every screen in the
 * current interface. Project-owned.
 * The PokéNav's HOENN MAP too (step 13 of the owner's plan): zoomed in (A) the map at four times the game's size
 * scrolling under a still cursor, as the game's does; the side card shows a visited town's own map (the game's) with a
 * key to what is in it, or a route's landmarks; off the map (the event islands) the map dims as the game's does.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "decompress.h"
#include "landmark.h"
#include "main.h"
#include "region_map.h"
#include "string_util.h"
#include "constants/characters.h"
#include "constants/region_map_sections.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

#define CELL 16                         /* a map cell on the canvas: the game's 8 pixels twice */
#define COLS (UI_REGION_MAP_W / 8)
#define ROWS (UI_REGION_MAP_H / 8)
#define MAP_X 14
#define MAP_Y 34
#define MAP_W (COLS * CELL)
#define MAP_H (ROWS * CELL)
#define SIDE_X 508
#define SIDE_Y 28
#define SIDE_W 124
#define SIDE_H 316
#define MARKERS_MAX 8

enum
{
    HIT_CELL = UI_HIT_SCREEN,  /* index: y * 32 + x */
    HIT_ZOOMED,                /* the zoomed map: a click zooms out (A) */
};

static const u8 sText_None[] = _("-");
static const u8 sText_CanFly[] = _("You can fly here.");
static const u8 sText_NotVisited[] = _("Not visited yet.");
static const u8 sText_Route[] = _("A route.");
static const u8 sText_Hoenn[] = _("Hoenn");
static const u8 sText_Fly[] = _("Fly");
static const u8 sText_Cancel[] = _("Cancel");
static const u8 sText_Close[] = _("Close");
static const u8 sText_Trainers[] = _("Trainers");
static const u8 sText_FlySpot[] = _("Fly spot");
static const u8 sText_NotYet[] = _("Not visited");
static const u8 sText_You[] = _("You");
static const u8 sText_Others[] = _("Other trainers");
static const u8 sText_ClickHint[] = _("Click a place to move there; click it again to fly.");
static const u8 sText_ClickHintTown[] = _("Click a place to look at it.");
static const u8 sText_ClickHintNav[] = _("Click a place to move there; click it again to zoom in.");
static const u8 sText_ClickHintZoomed[] = _("Click the map to zoom out.");
static const u8 sText_ZoomIn[] = _("Zoom in");
static const u8 sText_ZoomOut[] = _("Zoom out");
static const u8 sText_OffMap[] = _("You are off the map.");
static const u8 sText_Visited[] = _("Visited.");

static const u8 sText_KeyCenter[] = _("POKéMON CENTER");
static const u8 sText_KeyMart[] = _("POKéMON MART");
static const u8 sText_KeyGym[] = _("POKéMON GYM");
static const u8 sText_KeyTent[] = _("BATTLE TENT");
static const u8 sText_KeyContest[] = _("POKéMON CONTEST");

/* the town map's key (the game's city_zoom_text.png): its colours in the town map tiles' palette */
static const struct
{
    uint32_t hex;
    const u8 *label;
} sTownKey[] = {
    {0xFF4141, sText_KeyCenter},
    {0x1010FF, sText_KeyMart},
    {0xCD8352, sText_KeyGym},
    {0xFF8300, sText_KeyTent},
    {0x839CF6, sText_KeyContest},
};

/* the players' colours (the PLAYERS screen's and the Trainer Card's): player ids 1-4 */
static const char *const sSlotHeads[4] = {"head_green", "head_blue", "head_red", "head_yellow"};
static const uint32_t sSlotHex[4] = {0x58B048, 0x4C88E0, 0xE85848, 0xE0B028};

struct Marker
{
    u8 kind;    /* 0 a trainer, 1 the World Event, 2 the outbreak (region_map.c's MP_MARKER_*) */
    u8 slot;
    u16 x, y;
};

static struct NativeRegionMapState sR;
static u8 sArt[UI_REGION_MAP_W * UI_REGION_MAP_H * 4];
static int sArtType = -1;
static u16 sSections[ROWS][COLS];
static u8 sIcon[16 * 16 * 4];
static int sIconFemale = -1;
static u8 sName[32], sSubName[32];
static struct Marker sMarkers[MARKERS_MAX];
static int sMarkerCount;
static int sViewX = MAP_X, sViewY = MAP_Y, sCell = CELL;   /* the map as drawn: 2x, or 4x zoomed in */
static u8 sTown[80 * 80 * 4];             /* the cursor's town's map (the HOENN MAP's), 10x10 tiles */
static int sTownKeyOf = -1;               /* its mapsec * 8 + position (-1: none) */
static u8 sTownKeys;                      /* which of sTownKey it shows */
static u8 *sTownTiles;                    /* the town map tiles, decompressed on first use */
static u8 sLandmarks[4][24];
static int sLandmarkCount;

/* ---------------------------------------------------------------- capture */

/* a visited town's map as the HOENN MAP shows it (the game's tiles and tilemap), and the key's colours in it */
static void LoadTown(void)
{
    const u32 *tiles = NULL, *tilemap;
    const u16 *palette = NULL;
    int key = sR.mapSec * 8 + sR.posWithinMapSec;
    u16 entries[100];

    if (sR.mapSecType != MAPSECTYPE_CITY_CANFLY
     || (tilemap = PokenavRegionMap_NativeUiTownMap(sR.mapSec, sR.posWithinMapSec, &tiles, &palette)) == NULL)
    {
        sTownKeyOf = -1;
        return;
    }
    if (key == sTownKeyOf)
        return;
    if (sTownTiles == NULL)
    {
        u32 size = GetDecompressedDataSize(tiles);

        if (size < 32 || (sTownTiles = calloc(size + 64, 1)) == NULL)
            return;
        DecompressDataWithHeaderWram(tiles, sTownTiles);
    }
    if (GetDecompressedDataSize(tilemap) < sizeof(entries))
        return;
    DecompressDataWithHeaderWram(tilemap, entries);
    memset(sTown, 0, sizeof(sTown));
    for (int i = 0; i < 100; i++)
    {
        u8 tile[8 * 8 * 4];
        int t = entries[i] & 0x3FF, hflip = (entries[i] >> 10) & 1, vflip = (entries[i] >> 11) & 1;

        if (t >= 64)
            continue;   /* the town map tiles are 64 */
        UiGfx_Decode4bpp(tile, 8, sTownTiles + t * 32, palette, 8, 8);
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
            {
                const u8 *src = &tile[((vflip ? 7 - y : y) * 8 + (hflip ? 7 - x : x)) * 4];

                memcpy(&sTown[(((i / 10) * 8 + y) * 80 + (i % 10) * 8 + x) * 4], src, 4);
            }
    }
    /* the key: the colours the town's map shows */
    sTownKeys = 0;
    for (size_t k = 0; k < ARRAY_COUNT(sTownKey); k++)
        for (int i = 0; i < 80 * 80 && !(sTownKeys & (1 << k)); i++)
        {
            const u8 *px = &sTown[i * 4];

            if (px[3] != 0 && abs(px[0] - (int)(sTownKey[k].hex >> 16)) < 10
             && abs(px[1] - (int)((sTownKey[k].hex >> 8) & 0xFF)) < 10 && abs(px[2] - (int)(sTownKey[k].hex & 0xFF)) < 10)
                sTownKeys |= 1 << k;
        }
    sTownKeyOf = key;
}

static void CaptureNav(void)
{
    LoadTown();
    sLandmarkCount = 0;
    if (sR.mapSecType == MAPSECTYPE_ROUTE || sR.mapSecType == MAPSECTYPE_BATTLE_FRONTIER)
    {
        const u8 *name;

        while (sLandmarkCount < (int)ARRAY_COUNT(sLandmarks)
            && (name = GetLandmarkName(sR.mapSec, sR.posWithinMapSec, sLandmarkCount)) != NULL)
        {
            UiText_TitleCase(sLandmarks[sLandmarkCount], sizeof(sLandmarks[0]), name, false);
            sLandmarkCount++;
        }
    }
}

static bool Detect(void)
{
    struct NativeRegionMapState s;

    return RegionMap_NativeUiState(&s);
}

static void Capture(void)
{
    RegionMap_NativeUiState(&sR);
    if (sArtType != sR.regionMapType)
    {
        if (UiGfx_RegionMapArt(sR.regionMapType, sArt))
            sArtType = sR.regionMapType;
        for (int r = 0; r < ROWS; r++)
            for (int c = 0; c < COLS; c++)
                sSections[r][c] = GetRegionMapSecIdAt(c, r);
    }
    if (sIconFemale != sR.female)
    {
        const u16 *pal = NULL;
        const u8 *gfx = RegionMap_NativeUiPlayerIcon(sR.female, &pal);

        memset(sIcon, 0, sizeof(sIcon));
        if (gfx != NULL && pal != NULL)
            UiGfx_Decode4bpp(sIcon, 16, gfx, pal, 16, 16);
        sIconFemale = sR.female;
    }
    UiText_TitleCase(sName, sizeof(sName), sR.mapSec < MAPSEC_NONE ? sR.name : NULL, false);
    UiText_TitleCase(sSubName, sizeof(sSubName), sR.subName, false);
    sMarkerCount = 0;
    for (u32 i = 0; i < RegionMap_PortableMarkerCount() && sMarkerCount < MARKERS_MAX; i++)
    {
        struct Marker *m = &sMarkers[sMarkerCount];

        if (RegionMap_PortableMarkerAt(i, &m->kind, &m->slot, &m->x, &m->y))
            sMarkerCount++;
    }
    if (sR.kind == NATIVE_REGION_MAP_POKENAV)
        CaptureNav();
}

/* ---------------------------------------------------------------- pieces */

struct RgbaCtx
{
    const u8 *rgba;
    int w, h;
};

static void BuildRgba(struct UiBitmap *bm, const void *ctx)
{
    const struct RgbaCtx *c = ctx;

    memcpy(bm->d, c->rgba, (size_t)c->w * c->h * 4);
}

static void BlitRgba2x(const char *key, const u8 *rgba, int w, int h, int x, int y)
{
    struct RgbaCtx c = {rgba, w, h};

    UiG_BlitPart(UiG_Cached(key, w, h, BuildRgba, &c), 0, 0, w, h, x, y, 2, false, 1.0);
}

static int CellX(int x)
{
    return sViewX + x * sCell;
}

static int CellY(int y)
{
    return sViewY + y * sCell;
}

static void Background(int t)
{
    UiRGBA base = UI_HEX(0x1F3F63), line = UiMix(base, UI_HEX(0xFFFFFF), 0.07);
    struct UiPattern grid = UiPat_Dots(16, 2, 0, 0, false);
    int bx, by, bw, bh;

    UiG_Bleed(&bx, &by, &bw, &bh);
    UiG_Rect(bx, by, bw, bh, base);
    UiG_FillPattern(&grid, bx, by, bw, bh, line, t / 8, t / 8);
}

/* the place under the cursor, cell by cell: a light veil with a white rim */
static void PlaceGlow(int t)
{
    UiRGBA edge = UI_HEX(0xFFFFFF);
    double pulse = 0.28 + 0.10 * sin(t * 0.12);

    if (sR.mapSec >= MAPSEC_NONE)
        return;
    UiG_Save();
    UiG_Alpha(pulse);
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (sSections[r][c] == sR.mapSec)
                UiG_Rect(CellX(c), CellY(r), sCell, sCell, edge);
    UiG_Restore();
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
        {
            int x = CellX(c), y = CellY(r);

            if (sSections[r][c] != sR.mapSec)
                continue;
            if (r == 0 || sSections[r - 1][c] != sR.mapSec)
                UiG_Rect(x, y, sCell, 2, edge);
            if (r == ROWS - 1 || sSections[r + 1][c] != sR.mapSec)
                UiG_Rect(x, y + sCell - 2, sCell, 2, edge);
            if (c == 0 || sSections[r][c - 1] != sR.mapSec)
                UiG_Rect(x, y, 2, sCell, edge);
            if (c == COLS - 1 || sSections[r][c + 1] != sR.mapSec)
                UiG_Rect(x + sCell - 2, y, 2, sCell, edge);
        }
}

/* a fly spot: a red pin once visited (the game's bright icon), a small dark one before */
static void FlySpot(const struct NativeRegionFlySpot *s, int t)
{
    int x = CellX(s->x), y = CellY(s->y), w = s->w * sCell, h = s->h * sCell;
    bool here = s->mapSec == sR.mapSec;

    if (s->outline)
    {
        /* the Battle Frontier's red frame */
        UiRGBA red = here && (t / 8) % 2 ? UI_HEX(0xFF9080) : UI_P_CURSOR;

        UiG_Rect(x - 2, y - 2, w + 4, 3, red);
        UiG_Rect(x - 2, y + h - 1, w + 4, 3, red);
        UiG_Rect(x - 2, y - 2, 3, h + 4, red);
        UiG_Rect(x + w - 1, y - 2, 3, h + 4, red);
        return;
    }
    {
        UiRGBA fill = s->visited ? (here && (t / 8) % 2 ? UI_HEX(0xFF9080) : UI_P_CMD_FIGHT)
                                 : UiMix(UI_P_INK, UI_P_PAPER, here && (t / 8) % 2 ? 0.6 : 0.4);
        UiRGBA hi = s->visited ? UI_HEX(0xFFC8B8) : UiMix(UI_P_INK, UI_P_PAPER, 0.7);
        UiRGBA lo = s->visited ? UI_P_HP_RED_2 : UiMix(UI_P_INK, UI_P_PAPER, 0.2);

        UiG_Paint(UiM_Rounded4(w - 4, h - 4, 3, 3, 3, 3), x + 2, y + 2, fill,
                  &(struct UiPaint){UI_P_INK, hi, lo, 0, 0, NULL, UI_NONE});
        if (s->visited)
            UiG_Rect(x + w / 2 - 2, y + h / 2 - 2, 4, 4, UI_P_PAPER);
    }
}

static void Markers(int t)
{
    for (int i = 0; i < sMarkerCount; i++)
    {
        const struct Marker *m = &sMarkers[i];
        int cx = CellX(m->x) + sCell / 2, cy = CellY(m->y) + sCell / 2;

        if (m->kind == 0 && m->slot < 4)
        {
            /* fanned out like the game's when trainers share a cell */
            int rank = 0;

            for (int j = 0; j < i; j++)
                rank += sMarkers[j].kind == 0 && sMarkers[j].x == m->x && sMarkers[j].y == m->y;
            if (sR.playerShown && sR.playerX == m->x && sR.playerY == m->y)
                rank++;
            cx += (rank & 1 ? -10 : 10) * (rank != 0);
            cy += rank >= 2 ? 8 : -4 * (rank != 0);
            UiG_Paint(UiM_Rounded4(20, 20, 10, 10, 10, 10), cx - 10, cy - 10, UI_HEX(sSlotHex[m->slot]),
                      &(struct UiPaint){UI_P_INK, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
            UiGfx_Icon(sSlotHeads[m->slot], cx - 8, cy - 8, 16);
        }
        else if (m->kind == 1)
        {
            /* the World Event: a flashing red frame round its cell */
            if ((t / 10) % 2 == 0)
            {
                UiG_Rect(cx - 12, cy - 12, 24, 3, UI_P_CURSOR);
                UiG_Rect(cx - 12, cy + 9, 24, 3, UI_P_CURSOR);
                UiG_Rect(cx - 12, cy - 12, 3, 24, UI_P_CURSOR);
                UiG_Rect(cx + 9, cy - 12, 3, 24, UI_P_CURSOR);
            }
        }
        else if (m->kind == 2)
        {
            /* the outbreak: a pink dot, flashing */
            if ((t / 10) % 2 == 0)
                UiG_Paint(UiM_Rounded4(10, 10, 5, 5, 5, 5), cx - 5, cy - 5, UI_HEX(0xF080C0),
                          &(struct UiPaint){UI_P_INK, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
        }
    }
}

/* the cursor: a red reticle a little larger than a cell, breathing, gliding from cell to cell with the game's */
static void Cursor(int t)
{
    bool zoomed = sR.kind == NATIVE_REGION_MAP_POKENAV && sR.zoomed;
    int cx = zoomed ? MAP_X + MAP_W / 2 : MAP_X + sR.cursorPixX * 2, cy = zoomed ? MAP_Y + MAP_H / 2 : MAP_Y + sR.cursorPixY * 2;
    int half = (zoomed ? 30 : 15) + (int)floor(1.0 + 1.0 * sin(t * 0.15)), arm = zoomed ? 12 : 8;

    for (int i = 0; i < 2; i++)
    {
        UiRGBA c = i == 0 ? UI_P_INK : UI_P_CURSOR;
        int o = i == 0 ? 1 : 0, w = i == 0 ? 5 : 3, a = arm + 2 * o, h0 = half + o;

        /* four corners, ink under red */
        UiG_Rect(cx - h0, cy - h0, a, w, c);
        UiG_Rect(cx - h0, cy - h0, w, a, c);
        UiG_Rect(cx + h0 - a, cy - h0, a, w, c);
        UiG_Rect(cx + h0 - w, cy - h0, w, a, c);
        UiG_Rect(cx - h0, cy + h0 - w, a, w, c);
        UiG_Rect(cx - h0, cy + h0 - a, w, a, c);
        UiG_Rect(cx + h0 - a, cy + h0 - w, a, w, c);
        UiG_Rect(cx + h0 - w, cy + h0 - a, w, a, c);
    }
}

static void MapPanel(int t)
{
    const struct UiHit *hover = NativeUi_MouseHover();
    bool zoomed = sR.kind == NATIVE_REGION_MAP_POKENAV && sR.zoomed;

    /* the view: the whole map at twice the game's size, or zoomed in at four times round the cursor's cell (the
       art's pixel scrollX + 0x34, scrollY + 0x44, as the game's zoomed map scrolls) */
    sCell = zoomed ? 2 * CELL : CELL;
    sViewX = zoomed ? MAP_X + MAP_W / 2 - (sR.scrollX + 0x34) * 4 - sCell / 2 : MAP_X;
    sViewY = zoomed ? MAP_Y + MAP_H / 2 - (sR.scrollY + 0x44) * 4 - sCell / 2 : MAP_Y;
    UiG_Paint(UiM_Rounded4(MAP_W + 12, MAP_H + 12, 4, 4, 4, 4), MAP_X - 6, MAP_Y - 6, UI_HEX(0x16263A),
              &(struct UiPaint){UI_P_INK, UI_HEX(0x3C5A7C), UI_NONE, 0, 0, NULL, UI_NONE});
    UiG_Rect(MAP_X, MAP_Y, MAP_W, MAP_H, UI_HEX(0x3870B8));
    UiG_Save();
    UiG_Clip(MAP_X, MAP_Y, MAP_W, MAP_H);
    if (sArtType >= 0)
    {
        /* the art's sea, its stripes (its corner's two rows) carried on past its edges: zoomed in, the view runs
           past them */
        int scale = zoomed ? 4 : 2;

        for (int r = (MAP_Y - sViewY) / scale - 2; sViewY + r * scale < MAP_Y + MAP_H; r++)
        {
            const u8 *px = &sArt[(r & 1) * UI_REGION_MAP_W * 4];

            UiG_Rect(MAP_X, sViewY + r * scale, MAP_W, scale, UI_RGBA(px[0], px[1], px[2], 255));
        }
    }
    if (sArtType >= 0)
    {
        char key[32];
        struct RgbaCtx c = {sArt, UI_REGION_MAP_W, UI_REGION_MAP_H};

        snprintf(key, sizeof(key), "regionmap:%d", sArtType);
        UiG_BlitPart(UiG_Cached(key, UI_REGION_MAP_W, UI_REGION_MAP_H, BuildRgba, &c), 0, 0, UI_REGION_MAP_W,
                     UI_REGION_MAP_H, sViewX, sViewY, zoomed ? 4 : 2, false, 1.0);
    }
    if (sR.offMap)
    {
        /* off the map (the event islands): the game dims its map and shows no cursor */
        UiG_Rect(MAP_X, MAP_Y, MAP_W, MAP_H, UiWithAlpha(UI_HEX(0x000000), 96));
        UiG_Restore();
        return;
    }
    PlaceGlow(t);
    for (int i = 0; i < sR.flyCount; i++)
        FlySpot(&sR.fly[i], t);
    /* the cell under the mouse */
    if (hover != NULL && hover->id == HIT_CELL && sR.input && !zoomed)
    {
        int x = CellX(hover->index % 32), y = CellY(hover->index / 32);

        UiG_Rect(x, y, CELL, 1, UI_P_PAPER);
        UiG_Rect(x, y + CELL - 1, CELL, 1, UI_P_PAPER);
        UiG_Rect(x, y, 1, CELL, UI_P_PAPER);
        UiG_Rect(x + CELL - 1, y, 1, CELL, UI_P_PAPER);
    }
    Markers(t);
    /* you, where the game puts you */
    if (sR.playerShown)
        BlitRgba2x(sIconFemale ? "regionicon:f" : "regionicon:m", sIcon, 16, 16, CellX(sR.playerX) + sCell / 2 - 16,
                   CellY(sR.playerY) + sCell / 2 - 16);
    Cursor(t);
    UiG_Restore();
    if (zoomed)
    {
        UiG_Hit(HIT_ZOOMED, 0, MAP_X, MAP_Y, MAP_W, MAP_H);
        return;
    }
    /* the cells the cursor can stand on */
    for (int y = 2; y <= 16; y++)
        for (int x = 1; x <= 28; x++)
            UiG_Hit(HIT_CELL, y * 32 + x, CellX(x), CellY(y), CELL, CELL);
}

static int LegendRow(int x, int y, int kind, const u8 *label)
{
    if (kind == 0)
        UiG_Paint(UiM_Rounded4(12, 12, 3, 3, 3, 3), x, y + 1, UI_P_CMD_FIGHT,
                  &(struct UiPaint){UI_P_INK, UI_HEX(0xFFC8B8), UI_P_HP_RED_2, 0, 0, NULL, UI_NONE});
    else if (kind == 1)
        UiG_Paint(UiM_Rounded4(12, 12, 3, 3, 3, 3), x, y + 1, UiMix(UI_P_INK, UI_P_PAPER, 0.4),
                  &(struct UiPaint){UI_P_INK, UiMix(UI_P_INK, UI_P_PAPER, 0.7), UI_NONE, 0, 0, NULL, UI_NONE});
    else if (kind == 2)
    {
        struct RgbaCtx c = {sIcon, 16, 16};

        UiG_Blit(UiG_Cached(sIconFemale ? "regionicon:f" : "regionicon:m", 16, 16, BuildRgba, &c), x - 2, y - 2);
    }
    else
        UiGfx_Icon("head_blue", x - 2, y - 1, 16);
    UiText_Draw(UI_FONT_SMALL, x + 20, y + 1, label, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    return y + 18;
}

/* the HOENN MAP's word on the cursor's place: a visited town's own map and what is in it, a route's landmarks */
static void NavInfo(int y)
{
    if (sR.offMap)
    {
        UiText_Draw(UI_FONT_SMALL, SIDE_X + 10, y + 2, sText_OffMap, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        return;
    }
    switch (sR.mapSecType)
    {
    case MAPSECTYPE_CITY_CANFLY:
        UiText_Draw(UI_FONT_SMALL, SIDE_X + 10, y + 2, sText_Visited, UI_P_HP_GREEN_2, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        y += 16;
        if (sTownKeyOf >= 0)
        {
            struct RgbaCtx c = {sTown, 80, 80};
            char key[24];

            UiG_Paint(UiM_Rounded4(86, 86, 3, 3, 3, 3), SIDE_X + 19, y, UI_HEX(0x16263A),
                      &(struct UiPaint){UI_P_INK, UI_HEX(0x3C5A7C), UI_NONE, 0, 0, NULL, UI_NONE});
            snprintf(key, sizeof(key), "navtown:%d", sTownKeyOf);
            UiG_Blit(UiG_Cached(key, 80, 80, BuildRgba, &c), SIDE_X + 22, y + 3);
            y += 92;
            for (size_t k = 0; k < ARRAY_COUNT(sTownKey); k++)
            {
                if (!(sTownKeys & (1 << k)))
                    continue;
                UiG_Paint(UiM_Rounded4(7, 7, 1, 1, 1, 1), SIDE_X + 10, y + 2, UI_HEX(sTownKey[k].hex),
                          &(struct UiPaint){UI_P_INK, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
                UiText_Draw(UI_FONT_SMALL, SIDE_X + 21, y, sTownKey[k].label, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
                y += 12;
            }
        }
        break;
    case MAPSECTYPE_CITY_CANTFLY:
        UiText_Draw(UI_FONT_SMALL, SIDE_X + 10, y + 2, sText_NotVisited, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        break;
    case MAPSECTYPE_ROUTE:
    case MAPSECTYPE_BATTLE_FRONTIER:
        if (sLandmarkCount == 0)
            UiText_Draw(UI_FONT_SMALL, SIDE_X + 10, y + 2, sText_Route, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        for (int i = 0; i < sLandmarkCount; i++)
        {
            UiG_Paint(UiM_Rounded4(5, 5, 2, 2, 2, 2), SIDE_X + 11, y + 5 + i * 13, UI_P_CURSOR,
                      &(struct UiPaint){UI_P_INK, UI_NONE, UI_NONE, 0, 0, NULL, UI_NONE});
            UiText_Draw(UI_FONT_SMALL, SIDE_X + 21, y + 2 + i * 13, sLandmarks[i], UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT,
                        0);
        }
        break;
    }
}

static void SidePanel(void)
{
    struct UiRamp r = UiRampOf(UI_P_CREAM, 0.38, 0.28, 0.62);
    const u8 *starts[3];
    int lengths[3], n, y = SIDE_Y + 10;
    const u8 *status = NULL;
    UiRGBA statusColour = UI_P_TEXT_MUTED;

    UiG_Paint(UiM_Rounded4(SIDE_W, SIDE_H, 4, 4, 4, 4), SIDE_X, SIDE_Y, UI_P_CREAM,
              &(struct UiPaint){UI_P_INK, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, UI_NONE});
    UiCaps_Draw(SIDE_X + 10, y, "PLACE", UI_P_TEXT_W, r.ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
    y += 14;
    /* the place's name, two lines at most */
    n = sName[0] != EOS ? UiText_Wrap(sName, SIDE_W - 20, UI_FONT_NORMAL, starts, lengths, 2) : 0;
    for (int i = 0; i < n; i++)
    {
        u8 line[40];

        UiText_CopyN(line, sizeof(line), starts[i], lengths[i]);
        UiText_Draw(UI_FONT_NORMAL, SIDE_X + 10, y, line, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        y += 15;
    }
    if (n == 0)
    {
        UiText_Draw(UI_FONT_NORMAL, SIDE_X + 10, y, sText_None, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        y += 15;
    }
    if (sSubName[0] != EOS)
    {
        UiText_Draw(UI_FONT_SMALL, SIDE_X + 10, y, sSubName, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        y += 13;
    }
    /* what it is to you */
    if (sR.kind == NATIVE_REGION_MAP_POKENAV)
    {
        NavInfo(y);
        status = NULL;
    }
    else if (sR.kind == NATIVE_REGION_MAP_FLY)
    {
        if (sR.canFly)
            status = sText_CanFly, statusColour = UI_P_HP_GREEN_2;
        else if (sR.mapSecType == MAPSECTYPE_CITY_CANTFLY)
            status = sText_NotVisited;
        else if (sR.mapSecType == MAPSECTYPE_ROUTE)
            status = sText_Route;
    }
    else
        status = sText_Hoenn;
    if (status != NULL)
    {
        UiText_Draw(UI_FONT_SMALL, SIDE_X + 10, y + 2, status, statusColour, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        y += 15;
    }
    /* the key */
    /* from the bottom up: how to use it, then the key */
    n = UiText_Wrap(sR.kind == NATIVE_REGION_MAP_FLY ? sText_ClickHint
                    : sR.kind != NATIVE_REGION_MAP_POKENAV ? sText_ClickHintTown
                    : sR.zoomed ? sText_ClickHintZoomed : sText_ClickHintNav, SIDE_W - 20, UI_FONT_SMALL, starts, lengths, 3);
    y = SIDE_Y + SIDE_H - 8 - n * 11;
    for (int i = 0; i < n; i++)
    {
        u8 line[64];

        UiText_CopyN(line, sizeof(line), starts[i], lengths[i]);
        UiText_Draw(UI_FONT_SMALL, SIDE_X + 10, y + i * 11, line, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    }
    y -= 8 + 18 * (2 + (sR.kind == NATIVE_REGION_MAP_FLY ? 2 : 0));
    UiG_Rect(SIDE_X + 8, y - 8, SIDE_W - 16, 1, UI_P_CREAM_LO);
    if (sR.kind == NATIVE_REGION_MAP_FLY)
    {
        y = LegendRow(SIDE_X + 12, y, 0, sText_FlySpot);
        y = LegendRow(SIDE_X + 12, y, 1, sText_NotYet);
    }
    y = LegendRow(SIDE_X + 12, y, 2, sText_You);
    LegendRow(SIDE_X + 12, y, 3, sText_Others);
}

static void Hints(void)
{
    struct UiHintItem hints[4];
    int n = 0;

    if (sR.kind == NATIVE_REGION_MAP_FLY)
    {
        if (sR.canFly)
            hints[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sText_Fly};
        hints[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Cancel};
    }
    else if (sR.kind == NATIVE_REGION_MAP_POKENAV)
    {
        if (!sR.offMap)
            hints[n++] = (struct UiHintItem){UI_ACT_CONFIRM, sR.zoomed ? sText_ZoomOut : sText_ZoomIn};
        if (sR.canFly)
            hints[n++] = (struct UiHintItem){UI_ACT_R, sText_Fly};
        hints[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Close};
    }
    else
    {
        if (sR.canFly)
            hints[n++] = (struct UiHintItem){UI_ACT_R, sText_Fly};
        hints[n++] = (struct UiHintItem){UI_ACT_CANCEL, sText_Close};
    }
    if (sMarkerCount > 0 && sR.kind == NATIVE_REGION_MAP_FLY)
        hints[n++] = (struct UiHintItem){UI_ACT_SELECT, sText_Trainers};
    UiW_Hints(16, 347, hints, n, true, 10);
}

static void Draw(int t)
{
    struct UiRamp tab = UiRampOf(UI_HEX(0x3C78C8), 0.38, 0.28, 0.62);

    Background(t);
    UiW_TitleTab(0, 8, sR.kind == NATIVE_REGION_MAP_FLY ? "FLY" : sR.kind == NATIVE_REGION_MAP_POKENAV ? "HOENN MAP" : "TOWN MAP",
                 &tab, 0, "map");
    MapPanel(t);
    SidePanel();
    Hints();
    UiG_Veil(NativeUi_ScreenFade());
}

static bool Ready(void)
{
    return sR.input;
}

static bool Click(const struct UiHit *hit, int button)
{
    if (NativeUi_PressesPending())
        return true;
    if (button == 3)
    {
        if (sR.input)
            NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button == 1 && hit->id == HIT_ZOOMED)
    {
        /* the HOENN MAP zoomed in: back out (A) */
        if (sR.input)
            NativeUi_QueuePress(A_BUTTON);
        return true;
    }
    if (button != 1 || hit->id != HIT_CELL)
        return false;
    if (sR.input && !sR.offMap)
    {
        u16 x = hit->index % 32, y = hit->index / 32;

        if (x != sR.cursorX || y != sR.cursorY)
            RegionMap_NativeUiMoveCursor(x, y);
        else if ((sR.kind == NATIVE_REGION_MAP_FLY && sR.canFly) || sR.kind == NATIVE_REGION_MAP_POKENAV)
            NativeUi_QueuePress(A_BUTTON);   /* fly there; the HOENN MAP zooms in */
    }
    return true;
}

static enum UiPicture Picture(void)
{
    return UI_PICTURE_HIDE_ALL;
}

const struct UiScreen gUiScreen_RegionMap = {
    .name = "region_map",
    .detect = Detect,
    .capture = Capture,
    .draw = Draw,
    .click = Click,
    .ready = Ready,
    .picture = Picture,
};
#endif // ENABLE_NATIVE_UI
