#ifdef ENABLE_NATIVE_UI
/*
 * 6 · Pokédex: the entry's AREA, CRY and SIZE pages and the search form, drawn
 * in the approved design's red device (preview/js/screens/pokedex.js) over the
 * game's own pages, which keep every rule: where a Pokémon lives
 * (pokedex_area_screen.c works the areas out from the wild encounter tables,
 * roamers and Feebas, and blinks its markers), the cry and its meter
 * (pokedex_cry_screen.c samples the sound mixer), the size comparison
 * (gSpeciesInfo's scales and offsets, as the game's affine sprites use them)
 * and the search (pokedex.c: its rows, options, rules and results). The pages
 * take the left screen, below the entry's header and above its buttons; the
 * search form takes both screens (the form left, an open option list or the
 * game's text right). Project-owned.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "decompress.h"
#include "main.h"
#include "palette.h"
#include "pokedex.h"
#include "pokedex_common.h"
#include "pokemon.h"
#include "region_map.h"
#include "string_util.h"
#include "constants/characters.h"
#include "constants/region_map_sections.h"
#include "native_ui/ui_dex.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_gfx.h"
#include "native_ui/ui_mon.h"
#include "native_ui/ui_screen.h"
#include "native_ui/ui_text.h"
#include "native_ui/ui_widgets.h"

/* ---------------------------------------------------------------- strings (project-owned) */

static const u8 sText_AreaHelp[] = _("Where it lives in the wild");
static const u8 sText_AreaHelpUnknown[] = _("It is not found in the wild here");
static const u8 sText_CryHelp[] = _("Play its cry again");
static const u8 sText_SizeHelp[] = _("Its size beside yours");
static const u8 sText_Habitat[] = _("Found in ");
static const u8 sText_AndMore[] = _(" and more");
static const u8 sText_Comma[] = _(", ");
static const u8 sText_Any[] = _("Any");
static const u8 sText_Numerical[] = _("Numerical");
static const u8 sText_AtoZ[] = _("A to Z");
static const u8 sText_Heaviest[] = _("Heaviest");
static const u8 sText_Lightest[] = _("Lightest");
static const u8 sText_Tallest[] = _("Tallest");
static const u8 sText_Smallest[] = _("Smallest");
static const u8 *const sOrderNames[] = {sText_Numerical, sText_AtoZ, sText_Heaviest, sText_Lightest, sText_Tallest, sText_Smallest};
static const u8 sText_HoennDex[] = _("Hoenn Pokédex");
static const u8 sText_NationalDex[] = _("National Pokédex");
static const u8 sText_Found1[] = _(" Pokémon found");
static const u8 sText_SearchTitle[] = _("Choose what to look for");
static const u8 sText_Empty[] = _("");
static const u8 sText_ShiftTitle[] = _("Change how the list is shown");

/* ---------------------------------------------------------------- captured state */

static struct NativeDexArea sArea;
static bool sAreaUp;
static struct NativeDexCry sCry;
static bool sCryUp;
static struct NativeDexSearch sSearch;
static bool sSearchUp;
static u8 sPlayerName[PLAYER_NAME_LENGTH + 1];
static u16 sTrainerPic;
static u16 sMonScale, sTrainerScale;
static s16 sMonOffsetSigned, sTrainerOffsetSigned;

/* the cry's waveform as the game drew it: two samples a frame, newest last */
#define WAVE_LEN 268
static s8 sWave[WAVE_LEN];
static int sWaveCount;

/* the area page's place names (the map sections that glow or carry a marker) */
#define AREA_NAMES 12
static u8 sAreaNames[AREA_NAMES][32];
static int sAreaNameCount;
static bool sAreaNamesMore;
static u16 sAreaNamesSpecies = 0xFFFF;
static u32 sAreaNamesStamp;

/* the region map as the AREA page shows it: 30 x 19 tiles (240 x 152) */
#define MAP_COLS 30
#define MAP_ROWS 19
#define MAP_W (MAP_COLS * 8)
#define MAP_H (MAP_ROWS * 8)
static u8 *sMapRgba;
static int sMapType = -1;
static int sMapSerial;
static u16 sMapSections[MAP_ROWS][MAP_COLS];
/* the player's icon (16x16) */
static u8 sIconRgba[16 * 16 * 4];
static bool sIconFemale;
static bool sIconReady;

static void TitleCase(u8 *dst, int size, const u8 *src, bool dropStop);

/* ---------------------------------------------------------------- capture */

static void BuildMap(u8 type)
{
    const struct RegionMapInfo *info = &gRegionMapInfos[type];
    u32 gfxSize, tmSize;
    u8 *tiles, *tm;
    int colours;

    if (sMapRgba == NULL)
        sMapRgba = malloc(MAP_W * MAP_H * 4);
    if (sMapRgba == NULL || info->dexMapGfx == NULL || info->dexMapTilemap == NULL || info->dexMapPalette == NULL)
        return;
    gfxSize = GetDecompressedDataSize(info->dexMapGfx);
    tmSize = GetDecompressedDataSize(info->dexMapTilemap);
    tiles = calloc(gfxSize + 64, 1);
    tm = calloc(tmSize + 64, 1);
    if (tiles == NULL || tm == NULL)
    {
        free(tiles);
        free(tm);
        return;
    }
    DecompressDataWithHeaderWram(info->dexMapGfx, tiles);
    DecompressDataWithHeaderWram(info->dexMapTilemap, tm);
    colours = info->dexMapPaletteSize / 2;
    memset(sMapRgba, 0, MAP_W * MAP_H * 4);
    for (int r = 0; r < MAP_ROWS; r++)
        for (int c = 0; c < MAP_COLS; c++)
        {
            u32 at = (u32)(r * 32 + c) * 2;
            u16 entry = at + 1 < tmSize ? (u16)(tm[at] | (tm[at + 1] << 8)) : 0;
            u32 tile = entry & 0x3FF;
            bool hflip = (entry >> 10) & 1, vflip = (entry >> 11) & 1;

            for (int py = 0; py < 8; py++)
                for (int px = 0; px < 8; px++)
                {
                    int sx = hflip ? 7 - px : px, sy = vflip ? 7 - py : py;
                    u32 off = tile * 64 + (u32)(sy * 8 + sx);
                    u8 p = off < gfxSize ? tiles[off] : 0;
                    u8 *d = &sMapRgba[((size_t)(r * 8 + py) * MAP_W + (c * 8 + px)) * 4];
                    u16 col;

                    /* 8bpp: the map's colours are BG palettes 7 onwards (index 112); 0 is clear */
                    if (p < 112 || p - 112 >= colours)
                        continue;
                    col = info->dexMapPalette[p - 112];
                    d[0] = ((col & 0x1F) << 3) | ((col & 0x1F) >> 2);
                    d[1] = (((col >> 5) & 0x1F) << 3) | (((col >> 5) & 0x1F) >> 2);
                    d[2] = (((col >> 10) & 0x1F) << 3) | (((col >> 10) & 0x1F) >> 2);
                    d[3] = 255;
                }
        }
    free(tiles);
    free(tm);
    for (int r = 0; r < MAP_ROWS; r++)
        for (int c = 0; c < MAP_COLS; c++)
            sMapSections[r][c] = GetRegionMapSecIdAt(c, r);
    sMapType = type;
    sMapSerial++;
}

static void CaptureAreaNames(void)
{
    u16 seen[NATIVE_DEX_AREA_MAX + NATIVE_DEX_AREA_MARKERS];
    int seenCount = 0;

    sAreaNameCount = 0;
    sAreaNamesMore = false;
    for (int k = 0; k < sArea.overworldCount + sArea.specialCount; k++)
    {
        u16 sec = k < sArea.overworldCount ? sArea.overworldSections[k] : sArea.specialSections[k - sArea.overworldCount];
        bool dup = false;

        if (sec >= MAPSEC_NONE)
            continue;
        for (int i = 0; i < seenCount && !dup; i++)
            dup = seen[i] == sec;
        if (dup)
            continue;
        seen[seenCount++] = sec;
        if (sAreaNameCount == AREA_NAMES)
        {
            sAreaNamesMore = true;
            continue;
        }
        {
            u8 name[32];

            GetMapName(name, sec, 0);
            TitleCase(sAreaNames[sAreaNameCount], sizeof(sAreaNames[0]), name, false);
        }
        /* a name repeated under another section (Route 119's two halves) */
        for (int i = 0; i < sAreaNameCount && !dup; i++)
            dup = StringCompare(sAreaNames[i], sAreaNames[sAreaNameCount]) == 0;
        if (!dup)
            sAreaNameCount++;
    }
}

static void CaptureArea(void)
{
    bool was = sAreaUp && sArea.found;

    sAreaUp = PokedexArea_NativeUiState(&sArea);
    if (!sAreaUp)
        return;
    if (sMapType != sArea.regionMapType)
        BuildMap(sArea.regionMapType);
    if (!sIconReady || sIconFemale != (bool)sArea.female)
    {
        const u16 *pal = NULL;
        const u8 *gfx = RegionMap_NativeUiPlayerIcon(sArea.female, &pal);

        memset(sIconRgba, 0, sizeof(sIconRgba));
        if (gfx != NULL && pal != NULL)
            UiGfx_Decode4bpp(sIconRgba, 16, gfx, pal, 16, 16);
        sIconFemale = sArea.female;
        sIconReady = true;
    }
    if (sArea.found && (!was || sAreaNamesSpecies != sArea.species))
    {
        CaptureAreaNames();
        sAreaNamesSpecies = sArea.species;
        sAreaNamesStamp++;
    }
}

static void CaptureCry(void)
{
    bool was = sCryUp;

    sCryUp = PokedexCry_NativeUiState(&sCry);
    if (!sCryUp)
        return;
    if (!was)
    {
        memset(sWave, 0, sizeof(sWave));
        sWaveCount = 0;
    }
    /* two samples a frame, as the game draws them */
    memmove(sWave, sWave + 2, WAVE_LEN - 2);
    sWave[WAVE_LEN - 2] = sCry.playing ? sCry.amplitude[0] : 0;
    sWave[WAVE_LEN - 1] = sCry.playing ? sCry.amplitude[1] : 0;
    if (sWaveCount < WAVE_LEN)
        sWaveCount += 2;
}

void UiDexPages_Capture(const struct NativePokedex *p, const struct UiDexEntry *e)
{
    bool areaPage = p->phase == NATIVE_DEX_SUBPAGE && p->subPage == AREA_SCREEN;
    bool cryPage = p->phase == NATIVE_DEX_SUBPAGE && p->subPage == CRY_SCREEN;
    bool sizePage = (p->phase == NATIVE_DEX_SUBPAGE || p->phase == NATIVE_DEX_LEAVING) && p->subPage == SIZE_SCREEN;

    if (areaPage)
        CaptureArea();
    else
        sAreaUp = false;
    if (cryPage)
        CaptureCry();
    else
        sCryUp = false;
    if (sizePage && e->species != SPECIES_NONE && e->species < NUM_SPECIES)
    {
        StringCopyN(sPlayerName, gSaveBlock2Ptr->playerName, PLAYER_NAME_LENGTH);
        sPlayerName[PLAYER_NAME_LENGTH] = EOS;
        /* the player's picture as the engine's page shows it (a chosen character's own) */
        sTrainerPic = MultiplayerAppearance_NativeUiFrontPic(MultiplayerAppearance_NativeUiLocalCharacter(),
                                                             gSaveBlock2Ptr->playerGender != MALE);
        UiGfx_PrepareTrainerPic(sTrainerPic);
        sMonScale = gSpeciesInfo[e->species].pokemonScale;
        sMonOffsetSigned = (s16)gSpeciesInfo[e->species].pokemonOffset;
        sTrainerScale = gSpeciesInfo[e->species].trainerScale;
        sTrainerOffsetSigned = (s16)gSpeciesInfo[e->species].trainerOffset;
    }
    /* the form once the game has set its rows up (its loading's second step) */
    sSearchUp = p->phase == NATIVE_DEX_PAGE && Pokedex_NativeUiSearch(&sSearch) && sSearch.values[SEARCH_ORDER] != NULL;
    UiDexPages_SearchStep();
}

/* ---------------------------------------------------------------- small pieces */

struct RgbaCtx
{
    const u8 *rgba;
    int w;
    int h;
};

static void BuildRgba(struct UiBitmap *bm, const void *ctx)
{
    const struct RgbaCtx *c = ctx;

    memcpy(bm->d, c->rgba, (size_t)c->w * c->h * 4);
}

static void BlitRgba(const char *key, const u8 *rgba, int w, int h, int x, int y)
{
    struct RgbaCtx c = {rgba, w, h};

    UiG_Blit(UiG_Cached(key, w, h, BuildRgba, &c), x, y);
}

/* a line of pixels (Bresenham) */
static void Line(int x0, int y0, int x1, int y1, UiRGBA c)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;

    for (int guard = 0; guard < 1024; guard++)
    {
        UiG_Put(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        if (2 * err >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (2 * err <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

/* an engine string in the design's case: each word's first letter (and one after a
 * full stop: "S.S.") as it is, the rest lower case; dropStop drops a final full stop */
static void TitleCase(u8 *dst, int size, const u8 *src, bool dropStop)
{
    int i = 0;
    bool wordStart = true;

    for (; src != NULL && src[i] != EOS && i < size - 1; i++)
    {
        u8 ch = src[i];
        /* "OF" inside a name stays small: Cave of Origin */
        bool of = wordStart && i > 0 && ch == CHAR_O && src[i + 1] == CHAR_F && (src[i + 2] == CHAR_SPACE || src[i + 2] == EOS);

        if ((!wordStart || of) && ch >= CHAR_A && ch <= CHAR_Z)
            ch = ch - CHAR_A + CHAR_a;
        wordStart = ch == CHAR_SPACE || ch == CHAR_PERIOD;
        dst[i] = ch;
    }
    dst[i] = EOS;
    /* "DON'T SPECIFY." and the like */
    if (dropStop && i > 0 && dst[i - 1] == CHAR_PERIOD)
        dst[i - 1] = EOS;
}

/* ---------------------------------------------------------------- AREA */

static bool SectionGlows(u16 sec)
{
    for (int k = 0; k < sArea.overworldCount; k++)
        if (sArea.overworldSections[k] == sec)
            return true;
    return false;
}

static void DrawMarker(int cx, int cy, const struct UiRamp *dv)
{
    static const char *const sRing[] = {
        "....####....", "..##wwww##..", ".#wwoooww#..", ".#woo..oow#.", "#wo......ow#", "#wo......ow#",
        "#wo......ow#", "#wo......ow#", ".#woo..oow#.", ".#wwoooww#..", "..##wwww##..", "....####....",
    };
    struct UiPixPal pal[3] = {{'#', dv->ink}, {'w', UI_HEX(0xF8F8F8)}, {'o', UI_P_CURSOR}};

    UiW_Pix("dexring", sRing, 12, pal, 3, cx - 6, cy - 6, false);
}

static void DrawArea(const struct DexPal *c, const struct UiDexEntry *e, int x, int y, int w, int t)
{
    const struct UiRamp *dv = &c->dev;
    int pw = MAP_W + 8, ph = MAP_H + 8, px = x + (w - pw) / 2, py = y;
    int mx = px + 4, my = py + 4;
    char key[48];

    (void)e;
    /* the map in a dark bezel */
    UiG_Paint(UiM_Rounded4(pw, ph, 2, 2, 2, 2), px, py, c->bezel, &(struct UiPaint){dv->ink, c->bezelHi, UI_NONE, 0, 0, NULL, 0});
    if (sMapRgba == NULL || sMapType < 0)
    {
        UiG_Rect(mx, my, MAP_W, MAP_H, UiMix(UI_P_TYPES_WATER, UI_P_PAPER, 0.5));
        return;
    }
    snprintf(key, sizeof(key), "dexmap:%d:%d", sMapType, sMapSerial);
    BlitRgba(key, sMapRgba, MAP_W, MAP_H, mx, my);
    if (!sAreaUp || !sArea.found)
        return;
    /* where it lives: its sections' cells, pulsing with the game's glow, a white rim inside
     * and the device's ink outside */
    {
        double pulse = 0.45 + 0.45 * (sArea.glowEva > 16 ? 1.0 : sArea.glowEva / 16.0);
        UiRGBA fill = UiMix(UI_P_CURSOR, UI_HEX(0xFFFFFF), 0.12), edge = UI_HEX(0xF8F8F8), outer = dv->ink;

        UiG_Save();
        UiG_Clip(mx, my, MAP_W, MAP_H);
        UiG_Alpha(pulse);
        for (int r = 0; r < MAP_ROWS; r++)
            for (int col = 0; col < MAP_COLS; col++)
                if (sMapSections[r][col] < MAPSEC_NONE && SectionGlows(sMapSections[r][col]))
                    UiG_Rect(mx + col * 8, my + r * 8, 8, 8, fill);
        UiG_Restore();
        for (int r = 0; r < MAP_ROWS; r++)
            for (int col = 0; col < MAP_COLS; col++)
            {
                u16 sec = sMapSections[r][col];
                int cx = mx + col * 8, cy = my + r * 8;

                if (sec >= MAPSEC_NONE || !SectionGlows(sec))
                    continue;
                if (r == 0 || !(sMapSections[r - 1][col] < MAPSEC_NONE && SectionGlows(sMapSections[r - 1][col])))
                {
                    UiG_HLine(cx, cy, 8, edge);
                    UiG_HLine(cx, cy - 1, 8, outer);
                }
                if (r == MAP_ROWS - 1 || !(sMapSections[r + 1][col] < MAPSEC_NONE && SectionGlows(sMapSections[r + 1][col])))
                {
                    UiG_HLine(cx, cy + 7, 8, edge);
                    UiG_HLine(cx, cy + 8, 8, outer);
                }
                if (col == 0 || !(sMapSections[r][col - 1] < MAPSEC_NONE && SectionGlows(sMapSections[r][col - 1])))
                {
                    UiG_VLine(cx, cy, 8, edge);
                    UiG_VLine(cx - 1, cy, 8, outer);
                }
                if (col == MAP_COLS - 1 || !(sMapSections[r][col + 1] < MAPSEC_NONE && SectionGlows(sMapSections[r][col + 1])))
                {
                    UiG_VLine(cx + 7, cy, 8, edge);
                    UiG_VLine(cx + 8, cy, 8, outer);
                }
            }
    }
    /* caves and special places: rings that blink with the game's markers */
    if (sArea.markersShown)
        for (int k = 0; k < sArea.specialCount; k++)
            DrawMarker(mx + sArea.markerX[k], my + sArea.markerY[k] - sArea.mapScrollY, dv);
    /* the player's icon, where the game puts it */
    if (sArea.playerShown && sIconReady)
        BlitRgba(sIconFemale ? "dexicon:f" : "dexicon:m", sIconRgba, 16, 16, mx + sArea.playerX - 8,
                 my + sArea.playerY - sArea.mapScrollY - 8);
    (void)t;
}

static void DrawAreaFooter(const struct DexPal *c, int x, int y, int w)
{
    const struct UiRamp *dv = &c->dev;

    if (!sAreaUp || !sArea.found)
        return;
    if (sArea.overworldCount == 0 && sArea.specialCount == 0)
    {
        /* the game's AREA UNKNOWN */
        int pw = UiCaps_Width("AREA UNKNOWN") + 24;

        UiG_Paint(UiM_Rounded4(pw, 17, 2, 2, 2, 2), x + (w - pw) / 2, y, UiMix(dv->ink, UI_P_INK, 0.4),
                  &(struct UiPaint){dv->ink, dv->lo, UI_NONE, 0, 0, NULL, 0});
        UiCaps_Draw(x + w / 2, y + 6, "AREA UNKNOWN", UI_P_TEXT_W, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
        return;
    }
    {
        u8 line[256];
        const u8 *starts[2];
        int lengths[2], n;

        line[0] = EOS;
        UiText_Append(line, sizeof(line), sText_Habitat);
        for (int i = 0; i < sAreaNameCount; i++)
        {
            if (i > 0)
                UiText_Append(line, sizeof(line), sText_Comma);
            UiText_Append(line, sizeof(line), sAreaNames[i]);
        }
        if (sAreaNamesMore)
            UiText_Append(line, sizeof(line), sText_AndMore);
        n = UiText_Wrap(line, w - 8, UI_FONT_SMALL, starts, lengths, 2);
        for (int i = 0; i < n && i < 2; i++)
        {
            u8 b[128];

            UiText_CopyN(b, sizeof(b), starts[i], lengths[i]);
            UiText_Draw(UI_FONT_SMALL, x + 4, y + i * 11, b, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        }
    }
}

/* ---------------------------------------------------------------- CRY */

static void DrawMeter(const struct DexPal *c, int x, int y, int w, int h)
{
    const struct UiRamp *dv = &c->dev;
    int cx = x + w / 2, cy = y + h - 10, radius = h - 22;
    double rot = sCryUp ? sCry.needle : 32;
    double a;

    UiG_Paint(UiM_Rounded4(w, h, 3, 3, 3, 3), x, y, UI_P_PAPER, &(struct UiPaint){dv->ink, UI_HEX(0xFFFFFF), UI_P_PAPER_LO, 0, 0, NULL, 0});
    /* the scale: ticks from quiet (upper left) to loud (upper right), its loud end red */
    for (int k = 0; k <= 8; k++)
    {
        double ang = (135.0 - k * 90.0 / 8.0) * 3.14159265 / 180.0;
        int x0 = cx + (int)(cos(ang) * (radius - (k % 2 ? 4 : 7))), y0 = cy - (int)(sin(ang) * (radius - (k % 2 ? 4 : 7)));
        int x1 = cx + (int)(cos(ang) * radius), y1 = cy - (int)(sin(ang) * radius);

        Line(x0, y0, x1, y1, k >= 7 ? UI_P_CURSOR : UI_P_INK);
    }
    for (int k = 0; k <= 60; k++)
    {
        double ang = (135.0 - k * 1.5) * 3.14159265 / 180.0;

        UiG_Put(cx + (int)(cos(ang) * (radius + 2)), cy - (int)(sin(ang) * (radius + 2)), k >= 50 ? UI_P_CURSOR : UI_P_INK);
    }
    UiCaps_Draw(cx, cy - radius / 2 - 2, "VU", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
    /* the game's needle: rotation 32 at rest, -32 at the loudest (256 a turn) */
    a = (90.0 + rot * 360.0 / 256.0) * 3.14159265 / 180.0;
    Line(cx, cy, cx + (int)(cos(a) * (radius - 2)), cy - (int)(sin(a) * (radius - 2)), UI_P_CURSOR);
    Line(cx + 1, cy, cx + 1 + (int)(cos(a) * (radius - 3)), cy - (int)(sin(a) * (radius - 3)), UiMix(UI_P_CURSOR, dv->ink, 0.3));
    UiG_Paint(UiM_Circle(7), cx - 3, cy - 3, dv->base, &(struct UiPaint){dv->ink, dv->hi, UI_NONE, 0, 0, NULL, 0});
}

static void DrawWave(const struct DexPal *c, int x, int y, int w, int h)
{
    const struct UiRamp *dv = &c->dev;
    UiRGBA screen = UiMix(UI_P_INK, dv->ink, 0.25), grid = UiMix(screen, UI_P_TYPES_WATER, 0.18);
    UiRGBA wave = UiMix(UI_P_TYPES_WATER, UI_HEX(0xFFFFFF), 0.35), mid = UiMix(screen, UI_P_TYPES_WATER, 0.55);
    int cy = y + h / 2, n = w - 4;
    int prev = cy;

    UiG_Paint(UiM_Rounded4(w, h, 2, 2, 2, 2), x, y, screen, &(struct UiPaint){dv->ink, UI_NONE, UiMix(screen, UI_HEX(0x000000), 0.3), 0, 0, NULL, 0});
    for (int gx = x + 6; gx < x + w - 2; gx += 12)
        UiG_VLine(gx, y + 2, h - 4, grid);
    for (int gy = y + 4; gy < y + h - 2; gy += 8)
        UiG_HLine(x + 2, gy, w - 4, grid);
    UiG_HLine(x + 2, cy, w - 4, mid);
    /* the samples as the game draws them: (v + 127) / 254 of the height, a line from the last */
    for (int i = 0; i < n; i++)
    {
        int s = WAVE_LEN - n + i, v = s >= 0 ? sWave[s] : 0;
        int yy = y + 2 + (int)((long)(v + 127) * (h - 5) / 254);

        if (yy == cy && prev == cy)
            continue;
        Line(x + 2 + i, prev, x + 2 + i, yy, wave);
        prev = yy;
    }
}

static void DrawCry(const struct DexPal *c, const struct UiDexEntry *e, int x, int y, int w, int t)
{
    const struct UiRamp *dv = &c->dev;
    UiRGBA body, ink, grid, shadow;
    int sx = x, sy = y, sw = 128, sh = 112, gy = sy + sh - 14, ix0 = sx + sw + 8;
    bool playing = sCryUp && sCry.playing;

    (void)t;
    UiDex_StageColours(e, &body, &ink, &grid, &shadow);
    UiDex_GridPanel(sx, sy, sw, sh, body, ink, grid);
    UiG_Mask(UiM_Ellipse(76, 12), sx + (sw >> 1) - 38, gy - 5, shadow);
    UiG_Save();
    UiG_Clip(sx + 1, sy + 1, sw - 2, sh - 2);
    if (e->owned)
    {
        if (!UiGfx_MonPic(e->species, e->personality, false, false, 0, sx + (sw >> 1), gy + 1, NULL))
            UiW_Missing(sx + (sw >> 1) - 24, gy - 48, 48, 48);
    }
    else
    {
        struct UiFilter sil = {0};

        sil.silhouette = UiMix(UI_P_INK, dv->ink, 0.35);
        if (!UiGfx_MonPic(e->species, e->personality, false, false, 0, sx + (sw >> 1), gy + 1, &sil))
            UiW_Missing(sx + (sw >> 1) - 24, gy - 48, 48, 48);
    }
    UiG_Restore();
    UiG_Hit(DEX_HIT_PLAY, 0, sx, sy, sw, sh);
    /* its name, the play button (lit while the cry plays), the game's meter */
    UiCaps_Draw(ix0, sy + 4, "CRY", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    UiG_Text(ix0 + UiCaps_Width("CRY") + 6, sy - 1, e->name);
    {
        int bx = ix0, by = sy + 16, bw = x + w - ix0;
        struct UiRamp r = UiRampOf(playing ? UI_P_TYPES_GRASS : dv->base, 0.42, 0.25, 0.62);
        static const char *const sPlay[] = {"#....", "##...", "#w#..", "#ww#.", "#www#", "#ww#.", "#w#..", "##...", "#...."};
        struct UiPixPal pal[2] = {{'#', r.ink}, {'w', UI_HEX(0xF8F8F8)}};

        DrawMeter(c, bx, by + 24, bw, 72);
        UiG_Paint(UiM_Rounded4(bw, 20, 3, 3, 3, 3), bx, by, r.base, &(struct UiPaint){r.ink, r.hi, r.lo, 0, 2, NULL, 0});
        UiW_Hint(bx + 8, by + 4, UI_ACT_CONFIRM, sText_Empty, false);
        UiW_Pix("dexplay", sPlay, 9, pal, 2, bx + 26, by + 5, false);
        UiCaps_Draw(bx + bw - 8, by + 7, playing ? "PLAYING" : "PLAY", UI_P_TEXT_W, r.ink, UI_CAPS_OUTLINE, UI_ALIGN_RIGHT, UI_NONE);
        UiG_Hit(DEX_HIT_PLAY, 0, bx, by, bw, 20);
    }
    DrawWave(c, x, sy + sh + 6, w, 66);
}

/* ---------------------------------------------------------------- SIZE */

static void DrawSize(const struct DexPal *c, const struct UiDexEntry *e, int x, int y, int w, int t)
{
    const struct UiRamp *dv = &c->dev;
    UiRGBA body, ink, grid, shadow;
    int sh = 150, gy = y + sh - 14, cx = x + w / 2;
    struct UiFilter sil = {0};
    u8 b[48];

    (void)t;
    UiDex_StageColours(e, &body, &ink, &grid, &shadow);
    UiDex_GridPanel(x, y, w, sh, body, ink, grid);
    UiG_HLine(x + 8, gy + 1, w - 16, shadow);
    sil.silhouette = UiMix(UI_P_INK, dv->ink, 0.35);
    /* the game's comparison: a 144 x 64 box, the Pokémon at x 88 and you at x 152,
     * each 64 x 64 picture scaled about its centre (56) and moved down by its offset,
     * the ground at y 88; here every pixel 2 x 2, x 120 at the stage's middle */
    UiG_Save();
    UiG_Clip(x + 1, y + 1, w - 2, sh - 2);
    UiGfx_MonPicAffine(e->species, e->personality, false, 0, cx + (88 - 120) * 2, gy + (56 + sMonOffsetSigned - 88) * 2,
                       sMonScale, 2, &sil);
    UiGfx_TrainerPicAffine(sTrainerPic, cx + (152 - 120) * 2, gy + (56 + sTrainerOffsetSigned - 88) * 2, sTrainerScale, 2,
                           &sil);
    UiG_Restore();
    /* who is who */
    {
        int ty = y + sh + 6, lw = w / 2 - 4;

        UiG_Paint(UiM_Rounded4(lw, 32, 2, 2, 2, 2), x, ty, UI_P_PAPER, &(struct UiPaint){UiMix(UI_P_PAPER_LO, dv->lo, 0.3), UI_HEX(0xFFFFFF), UI_P_PAPER_LO, 0, 0, NULL, 0});
        UiG_Text(x + 6, ty + 1, e->name);
        UiDex_HeightText(b, sizeof(b), e->height, UNITS == UNITS_IMPERIAL);
        UiCaps_Draw(x + 6, ty + 21, "HT", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        UiText_Draw(UI_FONT_SMALL, x + 20, ty + 17, b, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        UiDex_WeightText(b, sizeof(b), e->weight, UNITS == UNITS_IMPERIAL);
        UiCaps_Draw(x + 70, ty + 21, "WT", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
        UiText_Draw(UI_FONT_SMALL, x + 84, ty + 17, b, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
        UiG_Paint(UiM_Rounded4(lw, 32, 2, 2, 2, 2), x + w - lw, ty, UI_P_PAPER, &(struct UiPaint){UiMix(UI_P_PAPER_LO, dv->lo, 0.3), UI_HEX(0xFFFFFF), UI_P_PAPER_LO, 0, 0, NULL, 0});
        UiG_Text(x + w - lw + 6, ty + 1, sPlayerName);
        UiCaps_Draw(x + w - lw + 6, ty + 21, "YOU", UI_P_TEXT_MUTED, UI_NONE, UI_CAPS_PLAIN, UI_ALIGN_LEFT, UI_NONE);
    }
}

/* ---------------------------------------------------------------- pages */

void UiDexPages_DrawPage(const struct DexPal *c, u8 page, const struct UiDexEntry *e, int x, int y, int w, int t)
{
    if (e->species == SPECIES_NONE)
        return;
    switch (page)
    {
    case AREA_SCREEN:
        DrawArea(c, e, x, y, w, t);
        DrawAreaFooter(c, x, y + MAP_H + 12, w);
        break;
    case CRY_SCREEN:
        DrawCry(c, e, x, y, w, t);
        break;
    case SIZE_SCREEN:
        DrawSize(c, e, x, y, w, t);
        break;
    default:
        break;
    }
}

const u8 *UiDexPages_PageHelp(u8 page, const struct UiDexEntry *e)
{
    (void)e;
    switch (page)
    {
    case AREA_SCREEN:
        return sAreaUp && sArea.found && sArea.overworldCount == 0 && sArea.specialCount == 0 ? sText_AreaHelpUnknown : sText_AreaHelp;
    case CRY_SCREEN:
        return sText_CryHelp;
    case SIZE_SCREEN:
        return sText_SizeHelp;
    default:
        return NULL;
    }
}

/* ---------------------------------------------------------------- the search form */

/* mouse goals: a top-bar tab, a row, an option; stepped with the game's own presses */
static int sGoalTab = -1;
static int sGoalRow = -1;
static int sGoalOption = -1;
static bool sGoalThenA;
static int sGoalFrames;

bool UiDexPages_SearchUp(void)
{
    return sSearchUp;
}

const struct NativeDexSearch *UiDexPages_Search(void)
{
    return &sSearch;
}

void UiDexPages_SearchCancelGoals(void)
{
    sGoalTab = sGoalRow = sGoalOption = -1;
}

/* the rows each top-bar tab reaches (the game's movement maps: SHIFT only orders and modes) */
static bool RowAvailable(int row)
{
    if (row == SEARCH_MODE && !sSearch.national)
        return false;
    if (sSearch.topBar == SEARCH_TOPBAR_SHIFT)
        return row == SEARCH_ORDER || row == SEARCH_MODE || row == SEARCH_OK;
    return sSearch.topBar == SEARCH_TOPBAR_SEARCH;
}

/* the game's vertical order of rows (the two TYPE boxes share a line) */
static int RowLine(int row)
{
    static const u8 sLine[SEARCH_COUNT] = {0, 1, 2, 2, 3, 4, 5};

    return row >= 0 && row < SEARCH_COUNT ? sLine[row] : 0;
}

void UiDexPages_SearchStep(void)
{
    if (sGoalTab < 0 && sGoalRow < 0 && sGoalOption < 0)
        return;
    if (!sSearchUp || ++sGoalFrames > 900)
    {
        UiDexPages_SearchCancelGoals();
        return;
    }
    if (!sSearch.ready || NativeUi_PressesPending())
        return;
    if (sGoalTab >= 0)
    {
        /* back to the top bar, then along it, then A */
        if (sSearch.state == NATIVE_DEX_SEARCH_OPTIONS || sSearch.state == NATIVE_DEX_SEARCH_MENU)
        {
            NativeUi_QueuePress(B_BUTTON);
            return;
        }
        if (sSearch.state != NATIVE_DEX_SEARCH_TOPBAR)
            return;
        if (sSearch.topBar != sGoalTab)
        {
            NativeUi_QueuePress(sGoalTab > sSearch.topBar ? DPAD_RIGHT : DPAD_LEFT);
            return;
        }
        sGoalTab = -1;
        if (sGoalThenA || sGoalRow >= 0)
            NativeUi_QueuePress(A_BUTTON);
        return;
    }
    if (sGoalRow >= 0)
    {
        if (sSearch.state == NATIVE_DEX_SEARCH_OPTIONS)
        {
            NativeUi_QueuePress(B_BUTTON);
            return;
        }
        if (sSearch.state != NATIVE_DEX_SEARCH_MENU)
            return;
        if (sSearch.menuItem == sGoalRow)
        {
            sGoalRow = -1;
            NativeUi_QueuePress(A_BUTTON);
            return;
        }
        if (RowLine(sSearch.menuItem) == RowLine(sGoalRow))
            NativeUi_QueuePress(sGoalRow > sSearch.menuItem ? DPAD_RIGHT : DPAD_LEFT);
        else
            NativeUi_QueuePress(RowLine(sGoalRow) > RowLine(sSearch.menuItem) ? DPAD_DOWN : DPAD_UP);
        return;
    }
    if (sGoalOption >= 0)
    {
        int at = sSearch.optionCursor + sSearch.optionScroll;

        if (sSearch.state != NATIVE_DEX_SEARCH_OPTIONS || sGoalOption >= sSearch.optionCount)
        {
            sGoalOption = -1;
            return;
        }
        if (at == sGoalOption)
        {
            sGoalOption = -1;
            if (sGoalThenA)
                NativeUi_QueuePress(A_BUTTON);
            return;
        }
        NativeUi_QueuePress(sGoalOption > at ? DPAD_DOWN : DPAD_UP);
    }
}

static void SetGoal(int tab, int row, int option, bool thenA)
{
    sGoalTab = tab;
    sGoalRow = row;
    sGoalOption = option;
    sGoalThenA = thenA;
    sGoalFrames = 0;
}

bool UiDexPages_SearchClick(const struct UiHit *hit, int button)
{
    if (!sSearchUp)
        return false;
    if (button == 3)
    {
        UiDexPages_SearchCancelGoals();
        NativeUi_QueuePress(B_BUTTON);
        return true;
    }
    if (button != 1 || hit == NULL)
        return false;
    if (NativeUi_PressesPending() || sGoalTab >= 0 || sGoalRow >= 0 || sGoalOption >= 0)
        return true;
    if (sSearch.state == NATIVE_DEX_SEARCH_DONE)
    {
        NativeUi_QueuePress(A_BUTTON);
        return true;
    }
    switch (hit->id)
    {
    case DEX_HIT_SEARCH_TAB:
        SetGoal(hit->index, -1, -1, true);
        return true;
    case DEX_HIT_SEARCH_ROW:
    {
        int row = hit->index;

        if (sSearch.state == NATIVE_DEX_SEARCH_TOPBAR)
        {
            /* the tab that reaches the row (SEARCH for the search's own rows), then the row */
            int tab = sSearch.topBar;

            if (tab == SEARCH_TOPBAR_CANCEL || (tab == SEARCH_TOPBAR_SHIFT && row < SEARCH_ORDER))
                tab = SEARCH_TOPBAR_SEARCH;
            SetGoal(tab, row, -1, true);
            return true;
        }
        if (!RowAvailable(row))
            return true;
        SetGoal(-1, row, -1, true);
        return true;
    }
    case DEX_HIT_SEARCH_OPTION:
        if (sSearch.state == NATIVE_DEX_SEARCH_OPTIONS)
            SetGoal(-1, -1, hit->index, true);
        return true;
    case DEX_HIT_SEARCH_ARROW:
        if (sSearch.state == NATIVE_DEX_SEARCH_OPTIONS)
            NativeUi_QueuePress(hit->index == 0 ? DPAD_UP : DPAD_DOWN);
        return true;
    default:
        return true;
    }
}

bool UiDexPages_SearchWheel(int dy)
{
    if (!sSearchUp)
        return false;
    if (NativeUi_PressesPending())
        return true;
    if (sSearch.state == NATIVE_DEX_SEARCH_OPTIONS || sSearch.state == NATIVE_DEX_SEARCH_MENU)
        NativeUi_QueuePress(dy > 0 ? DPAD_UP : DPAD_DOWN);
    else if (sSearch.state == NATIVE_DEX_SEARCH_TOPBAR)
        NativeUi_QueuePress(dy > 0 ? DPAD_LEFT : DPAD_RIGHT);
    return true;
}

/* an option as the design shows it: a type badge, the design's words for orders and
 * modes, "Any" for the game's DON'T SPECIFY., else the game's words in title case */
static int DrawOption(int row, int index, const u8 *title, int x, int y, int maxW)
{
    u8 b[48];

    if ((row == SEARCH_TYPE_LEFT || row == SEARCH_TYPE_RIGHT) && index > 0)
    {
        static const u8 sTypeIds[] = {TYPE_NONE, TYPE_NORMAL, TYPE_FIGHTING, TYPE_FLYING, TYPE_POISON, TYPE_GROUND,
                                      TYPE_ROCK, TYPE_BUG, TYPE_GHOST, TYPE_STEEL, TYPE_FIRE, TYPE_WATER, TYPE_GRASS,
                                      TYPE_ELECTRIC, TYPE_PSYCHIC, TYPE_ICE, TYPE_DRAGON, TYPE_DARK, TYPE_FAIRY};
        int type = index < (int)sizeof(sTypeIds) ? UiMon_Type(sTypeIds[index]) : -1;

        if (type >= 0)
            return UiW_TypeBadge(x, y + 3, type, 0);
    }
    if (row == SEARCH_ORDER && index >= 0 && index < 6)
        return UiG_Text(x, y, sOrderNames[index]);
    if (row == SEARCH_MODE)
        return UiG_Text(x, y, index == DEX_MODE_NATIONAL ? sText_NationalDex : sText_HoennDex);
    if ((row == SEARCH_NAME || row == SEARCH_COLOR || row == SEARCH_TYPE_LEFT || row == SEARCH_TYPE_RIGHT) && index == 0)
        return UiG_TextEx(x, y, sText_Any, UI_FONT_NORMAL, UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT);
    if (row == SEARCH_NAME)
        return UiG_Text(x, y, title);
    TitleCase(b, sizeof(b), title, true);
    return UiText_Draw(UiText_Width(UI_FONT_NORMAL, b, 0) > maxW ? UI_FONT_NARROW : UI_FONT_NORMAL, x, y, b, UI_P_TEXT,
                       UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
}

static void DrawForm(const struct DexPal *c, int t)
{
    const struct UiRamp *dv = &c->dev;
    int x = DEX_PANE_X, y = DEX_PANE_Y, w = DEX_PANE_W, h = DEX_PANE_H;
    static const char *const sTabs[3] = {"SEARCH", "SHIFT", "CANCEL"};
    static const char *const sLabels[SEARCH_COUNT] = {"NAME", "COLOR", "TYPE", "TYPE", "ORDER", "MODE", "OK"};
    bool onBar = sSearch.state == NATIVE_DEX_SEARCH_TOPBAR;
    bool inMenu = sSearch.state == NATIVE_DEX_SEARCH_MENU || sSearch.state == NATIVE_DEX_SEARCH_OPTIONS;
    int tx = x + 3;

    UiG_Paint(UiM_Rounded4(w, h, 2, 2, 2, 2), x, y, UI_P_CREAM, &(struct UiPaint){UI_NONE, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, 0});
    /* the top bar as the list's tabs */
    for (int i = 0; i < 3; i++)
    {
        bool on = i == sSearch.topBar;
        int tw = UiCaps_Width(sTabs[i]) + 22, th = on ? 17 : 15, ty = on ? y + 2 : y + 4;
        const struct UiMask *m = UiM_Rounded4(tw, th, 2, 2, 0, 0);

        if (on && onBar)
            UiW_SelectFrame(m, tx, ty, UI_NONE, UI_NONE, 0);
        UiG_Paint(m, tx, ty, on ? dv->base : UiMix(UI_P_CREAM_LO, dv->hi, 0.25),
                  &(struct UiPaint){on ? dv->ink : UiMix(UI_P_CREAM_LO, dv->lo, 0.5), on ? dv->hi : UI_P_CREAM_HI, UI_NONE, 0, 0, NULL, 0});
        UiCaps_Draw(tx + (tw >> 1), ty + 5, sTabs[i], on ? UI_P_TEXT_W : UiMix(dv->lo, UI_P_TEXT_MUTED, 0.4), on ? dv->ink : UI_NONE,
                    on ? UI_CAPS_OUTLINE : UI_CAPS_PLAIN, UI_ALIGN_CENTER, UI_NONE);
        UiG_Hit(DEX_HIT_SEARCH_TAB, i, tx, y + 2, tw, 17);
        tx += tw + 3;
    }
    UiG_Rect(x + 2, y + 19, w - 4, 2, dv->base);
    UiG_HLine(x + 2, y + 21, w - 4, dv->lo);
    UiText_Draw(UI_FONT_SMALL, x + 8, y + 25, sSearch.topBar == SEARCH_TOPBAR_SHIFT ? sText_ShiftTitle : sText_SearchTitle,
                UI_P_TEXT_MUTED, UI_P_TEXT_SH, UI_ALIGN_LEFT, 0);
    /* the rows on a paper sheet: a label chip and the chosen option */
    {
        int ly = y + 42, rowH = 30, lbx = x + 6, lbw = w - 12;

        UiG_Paint(UiM_Rounded4(lbw, 5 * rowH + 4, 1, 1, 1, 1), lbx, ly - 2, UI_P_PAPER,
                  &(struct UiPaint){UiMix(UI_P_CREAM_LO, dv->lo, 0.45), UI_NONE, UI_P_PAPER_LO, 0, 0, NULL, 0});
        for (int line = 0; line < 5; line++)
        {
            int ry = ly + line * rowH;
            int rows[2] = {line < 2 ? line : line == 2 ? SEARCH_TYPE_LEFT : line + 1, line == 2 ? SEARCH_TYPE_RIGHT : -1};
            bool avail = RowAvailable(rows[0]);
            bool selLine = inMenu && (sSearch.menuItem == rows[0] || (rows[1] >= 0 && sSearch.menuItem == rows[1]));
            struct UiRamp chip = UiRampOf(avail ? UiMix(UI_P_TYPES_GROUND, dv->base, 0.25) : UI_HEX(0xA8A0A0), 0.42, 0.25, 0.62);

            if (rows[0] == SEARCH_MODE && !sSearch.national)
                continue;
            if (selLine)
                UiG_Paint(UiM_Rounded(lbw - 4, rowH - 2, UiCornerR(2), UiCornerChamfer(6), UiCornerR(2), UiCornerR(2)), lbx + 2, ry,
                          c->rowSel, &(struct UiPaint){dv->lo, c->rowSelHi, c->rowSelLo, 0, 0, NULL, 0});
            else if (line % 2 == 1)
                UiG_Rect(lbx + 1, ry, lbw - 2, rowH - 1, c->stripe);
            UiG_Paint(UiM_Rounded4(52, 18, 3, 3, 3, 3), lbx + 10, ry + 5, chip.base, &(struct UiPaint){chip.ink, chip.hi, chip.lo, 0, 2, NULL, 0});
            UiCaps_Draw(lbx + 36, ry + 11, sLabels[rows[0]], UI_P_TEXT_W, chip.ink, UI_CAPS_OUTLINE, UI_ALIGN_CENTER, UI_NONE);
            for (int k = 0; k < 2; k++)
            {
                int row = rows[k], vx = rows[1] >= 0 ? lbx + 70 + k * 100 : lbx + 70, vw = rows[1] >= 0 ? 96 : lbw - 80;
                bool sel = inMenu && sSearch.menuItem == row;

                if (row < 0)
                    continue;
                UiG_Paint(UiM_Rounded4(vw, 20, 2, 2, 2, 2), vx, ry + 4, sel ? UI_HEX(0xFFFFFF) : UiMix(UI_P_PAPER, UI_P_PAPER_LO, 0.3),
                          &(struct UiPaint){avail ? UiMix(UI_P_PAPER_LO, dv->lo, 0.5) : UI_P_PAPER_LO, UI_NONE, UI_NONE, 0, 0, NULL, 0});
                if (sSearch.values[row] != NULL)
                {
                    UiG_Save();
                    UiG_Clip(vx + 2, ry + 4, vw - 4, 20);
                    if (!avail)
                        UiG_Alpha(0.45);
                    DrawOption(row, sSearch.chosen[row], sSearch.values[row], vx + 5, ry + 5, vw - 10);
                    UiG_Restore();
                }
                if (sel)
                    UiW_Cursor(vx - 7, ry + 9, UiW_Nudge(t), UI_NONE);
                UiG_Hit(DEX_HIT_SEARCH_ROW, row, rows[1] >= 0 ? vx - 4 : lbx, ry, rows[1] >= 0 ? vw + 8 : lbw, rowH);
            }
        }
        /* OK: search, or change the list (SHIFT) */
        {
            int oy = ly + 5 * rowH + 8, ow = 96, ox = x + (w - ow) / 2;
            bool sel = inMenu && sSearch.menuItem == SEARCH_OK;
            struct UiRamp r = UiRampOf(RowAvailable(SEARCH_OK) ? dv->base : UI_HEX(0xA8A0A0), 0.42, 0.25, 0.62);
            const struct UiMask *m = UiM_Rounded4(ow, 24, 3, 3, 3, 3);

            if (sel)
                UiW_SelectFrame(m, ox, oy, UI_NONE, UI_NONE, 0);
            UiG_Paint(m, ox, oy, r.base, &(struct UiPaint){r.ink, r.hi, r.lo, 0, 2, NULL, 0});
            UiCaps_Draw(ox + ow / 2, oy + 8, sSearch.topBar == SEARCH_TOPBAR_SHIFT ? "OK" : "SEARCH", UI_P_TEXT_W, r.ink,
                        UI_CAPS_OUTLINE, UI_ALIGN_CENTER, UI_NONE);
            UiG_Hit(DEX_HIT_SEARCH_ROW, SEARCH_OK, ox, oy, ow, 24);
        }
    }
}

static void DrawSide(const struct DexPal *c, int t)
{
    const struct UiRamp *dv = &c->dev;
    int x = DEX_LIST_X, y = DEX_PANE_Y, w = DEX_LIST_W, h = DEX_PANE_H;

    UiG_Paint(UiM_Rounded4(w, h, 2, 2, 2, 2), x, y, UI_P_CREAM, &(struct UiPaint){UI_NONE, UI_P_CREAM_HI, UI_P_CREAM_LO, 0, 0, NULL, 0});
    if (sSearch.state == NATIVE_DEX_SEARCH_OPTIONS && sSearch.menuItem < SEARCH_OK)
    {
        /* the open row's options as the game scrolls them: six at a time */
        static const char *const sLabels[SEARCH_OK] = {"NAME", "COLOR", "TYPE", "TYPE", "ORDER", "MODE"};
        int ly = y + 26, rowH = 32, lbx = x + 6, lbw = w - 26;
        int first = sSearch.optionScroll, cur = sSearch.optionCursor + sSearch.optionScroll;

        UiG_Paint(UiM_Rounded4(w - 4, 19, 2, 2, 0, 0), x + 2, y + 2, dv->base, &(struct UiPaint){UI_NONE, dv->hi, dv->lo, 0, 0, NULL, 0});
        UiCaps_Draw(x + 10, y + 8, sLabels[sSearch.menuItem], UI_P_TEXT_W, dv->ink, UI_CAPS_OUTLINE, UI_ALIGN_LEFT, UI_NONE);
        UiG_Paint(UiM_Rounded4(lbw, MAX_SEARCH_PARAM_ON_SCREEN * rowH + 4, 1, 1, 1, 1), lbx, ly - 2, UI_P_PAPER,
                  &(struct UiPaint){UiMix(UI_P_CREAM_LO, dv->lo, 0.45), UI_NONE, UI_P_PAPER_LO, 0, 0, NULL, 0});
        for (int r = 0; r < MAX_SEARCH_PARAM_ON_SCREEN && first + r < sSearch.optionCount; r++)
        {
            int idx = first + r, ry = ly + r * rowH;
            bool sel = idx == cur;

            if (sel)
                UiG_Paint(UiM_Rounded(lbw - 4, rowH - 2, UiCornerR(2), UiCornerChamfer(6), UiCornerR(2), UiCornerR(2)), lbx + 2, ry,
                          c->rowSel, &(struct UiPaint){dv->lo, c->rowSelHi, c->rowSelLo, 0, 0, NULL, 0});
            else if (r % 2 == 1)
                UiG_Rect(lbx + 1, ry, lbw - 2, rowH - 1, c->stripe);
            DrawOption(sSearch.menuItem, idx, sSearch.options[idx], lbx + 18, ry + 7, lbw - 24);
            if (idx == sSearch.chosen[sSearch.menuItem])
                UiDex_CaughtMark(lbx + lbw - 16, ry + 11);
            if (sel)
                UiW_Cursor(lbx + 4, ry + 11, UiW_Nudge(t), UI_NONE);
            UiG_Hit(DEX_HIT_SEARCH_OPTION, idx, lbx, ry, lbw, rowH);
        }
        /* the scroll arrows, where there is more */
        if (first > 0)
        {
            UiW_DownArrow(x + w - 16, ly + 2, dv->base, true);
            UiG_Hit(DEX_HIT_SEARCH_ARROW, 0, x + w - 20, ly - 2, 16, 16);
        }
        if (first + MAX_SEARCH_PARAM_ON_SCREEN < sSearch.optionCount)
        {
            UiW_DownArrow(x + w - 16, ly + MAX_SEARCH_PARAM_ON_SCREEN * rowH - 12, dv->base, false);
            UiG_Hit(DEX_HIT_SEARCH_ARROW, 1, x + w - 20, ly + MAX_SEARCH_PARAM_ON_SCREEN * rowH - 16, 16, 16);
        }
    }
    else
    {
        /* the lens and what the search does */
        int cx = x + w / 2, cy = y + 92;

        UiG_Paint(UiM_Circle(96), cx - 48, cy - 48, UI_P_PAPER, &(struct UiPaint){dv->ink, UI_NONE, UI_P_PAPER_LO, 0, 0, NULL, 0});
        UiG_Paint(UiM_Circle(80), cx - 40, cy - 40, c->lens.base, &(struct UiPaint){c->lens.ink, c->lens.hi, c->lens.lo, 3, 0, NULL, 0});
        UiG_Rect(cx - 22, cy - 26, 10, 6, UI_HEX(0xF8F8F8));
        UiG_Rect(cx - 24, cy - 20, 4, 4, c->lens.hi);
        if (sSearch.state == NATIVE_DEX_SEARCH_SEARCHING)
        {
            /* a sweep while the game searches */
            double a = (t % 32) * 2 * 3.14159265 / 32;

            Line(cx, cy, cx + (int)(cos(a) * 34), cy - (int)(sin(a) * 34), UI_HEX(0xF8F8F8));
        }
        if (sSearch.state == NATIVE_DEX_SEARCH_DONE && sSearch.results > 0)
        {
            u8 b[48];

            b[0] = EOS;
            UiText_AppendNumber(b, sizeof(b), sSearch.results, 1, true);
            UiText_Append(b, sizeof(b), sText_Found1);
            UiText_Draw(UI_FONT_NORMAL, cx, cy + 56, b, UI_P_TEXT, UI_P_TEXT_SH, UI_ALIGN_CENTER, 0);
        }
    }
    /* the game's text box */
    if (sSearch.message != NULL && sSearch.message[0] != EOS)
    {
        int inner[4];
        struct UiRamp band = UiW_FieldBand();

        UiW_DialogBox(x + 4, y + h - 62, w - 8, 58, &band, UI_P_PAPER, inner);
        UiW_MessagePage(sSearch.message, 1000, inner[0], inner[1], inner[2], 2);
    }
}

void UiDexPages_DrawSearch(const struct DexPal *c, int t)
{
    if (sSearch.state == NATIVE_DEX_SEARCH_DONE)
    {
        /* "Search completed.": a click anywhere goes on (A) */
        int bx, by, bw, bh;

        UiG_Bleed(&bx, &by, &bw, &bh);
        UiG_Hit(DEX_HIT_BACKDROP, 0, bx, by, bw, bh);
    }
    DrawForm(c, t);
    DrawSide(c, t);
}
#endif // ENABLE_NATIVE_UI
