#ifdef ENABLE_NATIVE_UI
/*
 * The PC storage's wallpapers as original pixel art (the approved design's
 * preview/js/screens/pc.js: forest, beach, city, desert, savanna, crag,
 * volcano, snow, ported line for line), plus simple pieces in the same manner
 * for the game's other wallpapers (cave, seafloor, river, sky, polka-dot,
 * Pokémon Center, machine, simple) and Walda's (her two colours). None of the
 * game's own wallpaper art is used. Project-owned.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "native_ui/ui_draw.h"
#include "native_ui/ui_engine.h"
#include "native_ui/ui_kit.h"
#include "native_ui/ui_storage.h"

#define WW UI_STORAGE_WALL_W
#define WH UI_STORAGE_WALL_H

static struct UiRamp R(UiRGBA base)
{
    return UiRampDefault(base);
}

static int FloorDiv(int a, int b)
{
    int q = a / b;

    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

static void Put(struct UiBitmap *bm, int x, int y, UiRGBA c)
{
    uint8_t *p = &bm->d[(y * bm->w + x) * 4];

    p[0] = c & 0xFF;
    p[1] = (c >> 8) & 0xFF;
    p[2] = (c >> 16) & 0xFF;
    p[3] = 255;
}

static UiRGBA FromBgr555(u16 c)
{
    int r = c & 0x1F, g = (c >> 5) & 0x1F, b = (c >> 10) & 0x1F;

    return UI_RGB((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2));
}

/* ---------------------------------------------------------------- the design's eight */

static void Forest(struct UiBitmap *bm)
{
    struct UiRamp g = R(UI_P_TYPES_GRASS), gr = R(UI_P_TYPES_GROUND);
    UiRGBA f0 = UiMix(g.hi, UI_P_PAPER, 0.55), f1 = UiMix(g.hi, UI_P_PAPER, 0.3), f2 = UiMix(g.base, UI_P_PAPER, 0.32),
           f3 = UiMix(gr.lo, UI_P_PAPER, 0.35);

    for (int y = 0; y < WH; y++)
        for (int x = 0; x < WW; x++)
        {
            int row = y / 40, tx = (x + (row % 2 ? 16 : 0)) % 32, ty = y % 40;
            UiRGBA c = (x * 3 + y * 5) % 23 == 0 ? f1 : f0;

            if (ty >= 31 && ty <= 36 && tx >= 14 && tx <= 17)
            {
                c = f3;
            }
            else if (ty >= 4 && ty <= 31)
            {
                int tier = (ty - 4) / 9, k = (ty - 4) % 9, hw = 2 + k + tier * 2;

                if (fabs(tx - 15.5) < hw)
                    c = tx >= 16 ? f2 : f1;
            }
            Put(bm, x, y, c);
        }
}

static void Beach(struct UiBitmap *bm)
{
    struct UiRamp w = R(UI_P_TYPES_WATER), s = R(UI_P_TYPES_GROUND);
    UiRGBA sky = UiMix(w.hi, UI_P_PAPER, 0.55), sea = UiMix(w.base, UI_P_PAPER, 0.42), sea2 = UiMix(w.base, UI_P_PAPER, 0.3),
           foam = UiMix(w.hi, UI_P_PAPER, 0.8);
    UiRGBA sand = UiMix(s.hi, UI_P_PAPER, 0.45), sand2 = UiMix(s.base, UI_P_PAPER, 0.35), sun = UiMix(UI_P_TYPES_ELECTRIC, UI_P_PAPER, 0.45);

    for (int y = 0; y < WH; y++)
        for (int x = 0; x < WW; x++)
        {
            int seg = x / 16, m = x % 16;
            int shore = 150 + (seg % 2 ? 3 : 0) + (seg % 2 == 0 ? m / 4 : 3 - m / 4);
            UiRGBA c;

            if (y < 70)
            {
                if ((x - 200) * (x - 200) + (y - 28) * (y - 28) <= 225)
                    c = sun;
                else
                    c = y >= 66 ? sea : sky;
            }
            else if (y < shore)
            {
                int band = (y - 70) % 14, wv = (x + (y / 14) * 5) % 12;

                if ((band == 0 && wv < 6) || (band == 1 && (wv == 6 || wv == 7)))
                    c = foam;
                else
                    c = ((y - 70) / 14) % 2 == 0 ? sea : sea2;
            }
            else if (y < shore + 3)
            {
                c = foam;
            }
            else
            {
                c = ((x * 7 + y * 3) % 17 == 0 || ((x + 1) * 7 + y * 3) % 17 == 0) ? sand2 : sand;
            }
            Put(bm, x, y, c);
        }
}

static void City(struct UiBitmap *bm)
{
    static const int sHeights[] = {96, 140, 120, 160, 84, 128, 150, 104, 136, 90, 156, 118};
    struct UiRamp st = R(UI_P_TYPES_STEEL);
    UiRGBA csky = UiMix(st.hi, UI_P_PAPER, 0.5), b1 = UiMix(st.base, UI_P_PAPER, 0.35), b2 = UiMix(st.lo, UI_P_PAPER, 0.38),
           win = UiMix(UI_P_TYPES_ELECTRIC, UI_P_PAPER, 0.45), dwin = UiMix(st.lo, UI_P_PAPER, 0.2);

    for (int y = 0; y < WH; y++)
        for (int x = 0; x < WW; x++)
        {
            int col = x / 22, top = WH - sHeights[col % 12];
            UiRGBA c;

            if (y >= top)
            {
                int lx = x % 22, ly = y - top;

                if (lx >= 2 && lx <= 19 && ly >= 6 && (lx % 5 == 3 || lx % 5 == 4) && (ly % 9 >= 4 && ly % 9 <= 6))
                    c = (col * 7 + (ly / 9) * 3 + lx / 5) % 4 != 0 ? win : dwin;
                else if (lx == 0 || lx == 21)
                    c = dwin;
                else
                    c = col % 2 == 0 ? b1 : b2;
            }
            else
            {
                c = (y < 60 && (x * 13 + y * 7) % 97 == 0) ? UI_P_PAPER : csky;
            }
            Put(bm, x, y, c);
        }
}

static void Desert(struct UiBitmap *bm)
{
    struct UiRamp d = R(UI_P_TYPES_GROUND);
    UiRGBA dsky = UiMix(UI_P_TYPES_FIRE, UI_P_PAPER, 0.84), dsun = UiMix(UI_P_TYPES_ELECTRIC, UI_P_PAPER, 0.42),
           dsunr = UiMix(UI_P_TYPES_ELECTRIC, UI_P_PAPER, 0.65);
    UiRGBA d1 = UiMix(d.hi, UI_P_PAPER, 0.3), d2 = UiMix(d.base, UI_P_PAPER, 0.38), d3 = UiMix(d.lo, UI_P_PAPER, 0.45);
    const struct { int x0, top, w, h; UiRGBA c; } dunes[4] = {
        {-60, 128, 230, 40, d2}, {120, 140, 220, 44, d2}, {-30, 176, 190, 44, d1}, {130, 196, 200, 40, d1}};

    for (int y = 0; y < WH; y++)
        for (int x = 0; x < WW; x++)
        {
            int r = (x - 188) * (x - 188) + (y - 40) * (y - 40);
            bool found = false;
            int bestTop = 0, bestMid = 0;
            UiRGBA bestC = 0, c;

            if (r <= 196)
            {
                Put(bm, x, y, dsun);
                continue;
            }
            if (r <= 289 && (x + y) % 2 == 0)
            {
                Put(bm, x, y, dsunr);
                continue;
            }
            for (int k = 0; k < 4; k++)
            {
                double t = (double)(x - dunes[k].x0) / dunes[k].w;

                if (t >= 0 && t <= 1)
                {
                    int top = dunes[k].top - (int)floor(dunes[k].h * sin(t * M_PI));

                    if (y >= top)
                    {
                        found = true;
                        bestTop = top;
                        bestC = dunes[k].c;
                        bestMid = dunes[k].x0 + dunes[k].w / 2;
                    }
                }
            }
            if (found)
            {
                int dd = y - bestTop;

                if (dd > 0 && (dd + (x > bestMid ? x / 3 : -(x / 3))) % 11 == 0)
                    c = d3;
                else if (dd < 2)
                    c = UiMix(bestC, UI_P_PAPER, 0.4);
                else
                    c = bestC;
            }
            else
            {
                c = (y > 90 && (x * 7 + y * 3) % 53 == 0) ? UiMix(dsky, d1, 0.5) : dsky;
            }
            Put(bm, x, y, c);
        }
}

struct Savanna
{
    UiRGBA crown, crown2, tree;
};

/* one acacia: crown ellipses, the trunk and its fork (0: not the tree) */
static UiRGBA Acacia(const struct Savanna *s, int x, int y, int cx, int base, double sc)
{
    static const int sParts[3][4] = {{-22, 0, 26, 6}, {14, -3, 24, 6}, {-2, -7, 20, 5}};
    int top = base - (int)floor(46 * sc), fork;

    for (int i = 0; i < 3; i++)
    {
        int oy = top + (int)floor(sParts[i][1] * sc);
        double ex = (x - (cx + floor(sParts[i][0] * sc))) / (sParts[i][2] * sc), ey = (y - oy) / (sParts[i][3] * sc);

        if (ex * ex + ey * ey <= 1)
            return y <= oy ? s->crown : s->crown2;
    }
    fork = top + (int)floor(20 * sc);
    if (y >= top + (int)floor(5 * sc) && y <= base)
    {
        if (abs(x - cx) <= 1 && y > fork)
            return s->tree;
        if (y <= fork)
        {
            int dd = fork - y;

            if (fabs(x - (cx - dd * 0.9)) <= 1 || fabs(x - (cx + dd * 0.7)) <= 1)
                return s->tree;
        }
    }
    return 0;
}

static void SavannaWall(struct UiBitmap *bm)
{
    struct UiRamp gs = R(UI_P_TYPES_GRASS), bu = R(UI_P_TYPES_BUG), g2 = R(UI_P_TYPES_GROUND);
    UiRGBA ssky = UiMix(UI_P_TYPES_ELECTRIC, UI_P_PAPER, 0.74), ssun = UiMix(UI_P_TYPES_ELECTRIC, UI_P_TYPES_PSYCHIC, 0.35),
           grass = UiMix(bu.hi, UI_P_PAPER, 0.35), grass2 = UiMix(bu.base, UI_P_PAPER, 0.35);
    UiRGBA hills = UiMix(bu.hi, UI_P_PAPER, 0.55);
    struct Savanna s = {UiMix(gs.lo, UI_P_PAPER, 0.3), UiMix(gs.base, UI_P_PAPER, 0.35), UiMix(g2.lo, UI_P_PAPER, 0.15)};

    for (int y = 0; y < WH; y++)
        for (int x = 0; x < WW; x++)
        {
            UiRGBA c;

            if ((x - 196) * (x - 196) + (y - 118) * (y - 118) <= 676 && y < 132)
            {
                c = ssun;
            }
            else if (y >= 150)
            {
                if ((x % 6 == 0 && y < 158) || ((x + 3) % 8 == 0 && y < 155))
                    c = grass2;
                else
                    c = (x * 3 + y) % 7 ? grass : grass2;
            }
            else if (y >= 146)
            {
                c = x % 6 == 0 ? grass2 : grass;
            }
            else
            {
                c = Acacia(&s, x, y, 64, 150, 1);
                if (c == 0)
                    c = Acacia(&s, x, y, 150, 150, 0.55);
                if (c == 0)
                {
                    int hill = 132 + (int)floor(6 * sin(x / 19.0) + 4 * sin(x / 7.0));

                    c = y >= hill ? hills : ssky;
                }
            }
            Put(bm, x, y, c);
        }
}

static void Crag(struct UiBitmap *bm)
{
    struct UiRamp rk = R(UI_P_TYPES_ROCK), sl = R(UI_P_TYPES_STEEL);
    UiRGBA ksky = UiMix(sl.hi, UI_P_PAPER, 0.62), face = UiMix(rk.base, UI_P_PAPER, 0.5), face2 = UiMix(rk.base, UI_P_PAPER, 0.62),
           crack = UiMix(rk.lo, UI_P_PAPER, 0.35), khl = UiMix(rk.hi, UI_P_PAPER, 0.35), kshade = UiMix(rk.base, UI_P_PAPER, 0.38);

    for (int y = 0; y < WH; y++)
        for (int x = 0; x < WW; x++)
        {
            int a = 26 + (int)floor(abs(x - 40) * 1.3), b = 12 + (int)floor(abs(x - 130) * 1.1), c3 = 34 + (int)floor(abs(x - 214) * 1.4);
            int ridge = a < b ? a : b;
            UiRGBA c;

            ridge = ridge < c3 ? ridge : c3;
            ridge += (x / 5) % 3 == 0 ? 2 : 0;
            if (y < ridge)
            {
                c = (x * 13 + y * 7) % 71 == 0 ? UI_P_PAPER : ksky;
            }
            else if (y < ridge + 2)
            {
                c = khl;
            }
            else
            {
                int wy = y + (int)floor(5 * sin(x / 17.0) + 3 * sin(x / 41.0));
                int layer = FloorDiv(wy, 20), ly = ((wy % 20) + 20) % 20;

                if (ly == 0)
                {
                    c = crack;
                }
                else if (ly == 1)
                {
                    c = khl;
                }
                else
                {
                    int seeds[2] = {((layer * 53 + 7) % 61 + 61) % 61, ((layer * 29 + 31) % 61 + 61) % 61};

                    c = layer % 2 == 0 ? face : face2;
                    for (int i = 0; i < 2; i++)
                    {
                        int ck = (x + seeds[i] + ly / 3) % 61;

                        if (ck == 0)
                        {
                            c = crack;
                            break;
                        }
                        if (ck == 1)
                        {
                            c = kshade;
                            break;
                        }
                    }
                }
            }
            Put(bm, x, y, c);
        }
}

static void Volcano(struct UiBitmap *bm)
{
    static const int sPuffs[4][3] = {{124, 40, 13}, {110, 26, 11}, {136, 18, 10}, {122, 8, 8}};
    struct UiRamp fi = R(UI_P_TYPES_FIRE), dk = R(UI_P_TYPES_DARK);
    UiRGBA vsky = UiMix(fi.hi, UI_P_PAPER, 0.38), vsky2 = UiMix(fi.base, UI_P_PAPER, 0.5), mtn = UiMix(dk.base, UI_P_PAPER, 0.18),
           mtn2 = UiMix(dk.lo, UI_P_PAPER, 0.12);
    UiRGBA lava = UiMix(fi.base, UI_P_TYPES_ELECTRIC, 0.3), glow = UiMix(UI_P_TYPES_ELECTRIC, UI_P_PAPER, 0.25),
           smoke = UiMix(dk.hi, UI_P_PAPER, 0.45), smoke2 = UiMix(smoke, UI_P_PAPER, 0.3);
    const int cx = 124;
    const double riv[3][2] = {{cx - 6, -0.35}, {cx + 5, 0.3}, {cx - 2, 0.05}};

    for (int y = 0; y < WH; y++)
        for (int x = 0; x < WW; x++)
        {
            UiRGBA c = 0;
            int top;

            for (int i = 0; i < 4 && c == 0; i++)
            {
                int dx = x - sPuffs[i][0], dy = y - sPuffs[i][1];

                if (dx * dx + dy * dy <= sPuffs[i][2] * sPuffs[i][2])
                    c = (x + y) % 3 ? smoke : smoke2;
            }
            if (c != 0)
            {
                Put(bm, x, y, c);
                continue;
            }
            top = 64 + (int)floor(abs(x - cx) * 0.95);
            if (y >= top)
            {
                if (abs(x - cx) < 16 && y < top + 3)
                {
                    c = lava;
                }
                else
                {
                    for (int k = 0; k < 3 && c == 0; k++)
                    {
                        int lx = (int)riv[k][0] + (int)floor((y - 64) * riv[k][1]) + (int)floor(2 * sin(y / 6.0));

                        if (abs(x - lx) <= 1 && y < 200)
                            c = (y / 4) % 3 ? lava : glow;
                    }
                    if (c == 0)
                        c = x < cx ? mtn : mtn2;
                }
            }
            else if (y > 150 && (x * 11 + y * 5) % 37 == 0)
            {
                c = glow;
            }
            else
            {
                c = y < 132 ? vsky : (y < 134 ? UiMix(vsky, vsky2, 0.5) : vsky2);
            }
            Put(bm, x, y, c);
        }
}

static void Snow(struct UiBitmap *bm)
{
    static const int sBig[][2] = {{2, 0}, {2, 1}, {0, 2}, {1, 2}, {2, 2}, {3, 2}, {4, 2}, {2, 3}, {2, 4}, {0, 0}, {4, 0}, {0, 4}, {4, 4}};
    struct UiRamp ic = R(UI_P_TYPES_ICE);
    UiRGBA nbg = UiMix(ic.hi, UI_P_PAPER, 0.5), drift = UiMix(ic.hi, UI_P_PAPER, 0.85), nsh = UiMix(ic.base, UI_P_PAPER, 0.5),
           fl2 = UiMix(ic.hi, UI_P_PAPER, 0.75);

    for (int y = 0; y < WH; y++)
        for (int x = 0; x < WW; x++)
        {
            int hill = 172 + (int)floor(9 * sin(x / 22.0) + 3 * sin(x / 7.0));
            UiRGBA c;

            if (y >= hill)
            {
                c = y < hill + 2 ? nsh : ((x * 5 + y * 3) % 29 == 0 ? nsh : drift);
            }
            else
            {
                int row = y / 26, tx = (x + (row % 2 ? 13 : 0)) % 26, ty = y % 26;

                c = nbg;
                for (size_t i = 0; i < sizeof(sBig) / sizeof(sBig[0]); i++)
                    if (tx - 10 == sBig[i][0] && ty - 10 == sBig[i][1])
                        c = UI_P_PAPER;
                if (c == nbg && tx >= 22 && tx <= 23 && ty >= 22 && ty <= 23)
                    c = fl2;
            }
            Put(bm, x, y, c);
        }
}

/* ---------------------------------------------------------------- the game's others, in the same manner */

static void Cave(struct UiBitmap *bm)
{
    struct UiRamp gr = R(UI_P_TYPES_GROUND), rk = R(UI_P_TYPES_ROCK);
    UiRGBA wall = UiMix(gr.lo, UI_P_INK, 0.25), wall2 = UiMix(gr.lo, UI_P_INK, 0.4), stone = UiMix(rk.base, UI_P_PAPER, 0.15),
           floor0 = UiMix(gr.base, UI_P_INK, 0.2), glint = UiMix(UI_P_TYPES_ICE, UI_P_PAPER, 0.5);

    for (int y = 0; y < WH; y++)
        for (int x = 0; x < WW; x++)
        {
            int col = x / 26, tx = x % 26, len = 18 + (col * 37) % 30;
            UiRGBA c = ((x / 12 + y / 9) % 2) ? wall : wall2;

            /* stalactites from the ceiling, a rubble floor, a few glints */
            if (y < len && abs(tx - 13) < (len - y) / 5 + 1)
                c = stone;
            if (y >= 172 + (int)floor(4 * sin(x / 13.0)))
                c = (x * 7 + y * 3) % 13 == 0 ? stone : floor0;
            if ((x * 31 + y * 17) % 211 == 0)
                c = glint;
            Put(bm, x, y, c);
        }
}

static void Seafloor(struct UiBitmap *bm)
{
    struct UiRamp w = R(UI_P_TYPES_WATER), s = R(UI_P_TYPES_GROUND);
    UiRGBA top = UiMix(w.base, UI_P_PAPER, 0.35), deep = UiMix(w.lo, UI_P_PAPER, 0.15), bubble = UiMix(w.hi, UI_P_PAPER, 0.8),
           sand = UiMix(s.base, UI_P_PAPER, 0.35), weed = UiMix(UI_P_TYPES_GRASS, UI_P_INK, 0.2);

    for (int y = 0; y < WH; y++)
        for (int x = 0; x < WW; x++)
        {
            UiRGBA c = UiMix(top, deep, (double)y / WH);
            int floorY = 180 + (int)floor(5 * sin(x / 17.0));
            int bx = x % 40, by = (y + (x / 40) * 23) % 52;

            if ((bx - 20) * (bx - 20) + (by - 26) * (by - 26) <= 9 && (bx - 20) * (bx - 20) + (by - 26) * (by - 26) >= 4)
                c = bubble;
            if (x % 36 == 8 && y > floorY - 30 + (int)floor(3 * sin(y / 4.0)) && y < floorY)
                c = weed;
            if (y >= floorY)
                c = (x * 5 + y * 3) % 19 == 0 ? UiMix(sand, UI_P_INK, 0.15) : sand;
            Put(bm, x, y, c);
        }
}

static void River(struct UiBitmap *bm)
{
    struct UiRamp w = R(UI_P_TYPES_WATER), g = R(UI_P_TYPES_GRASS);
    UiRGBA bank = UiMix(g.hi, UI_P_PAPER, 0.4), bank2 = UiMix(g.base, UI_P_PAPER, 0.4), water = UiMix(w.base, UI_P_PAPER, 0.42),
           ripple = UiMix(w.hi, UI_P_PAPER, 0.8);

    for (int y = 0; y < WH; y++)
        for (int x = 0; x < WW; x++)
        {
            int edge1 = 66 + (int)floor(6 * sin(x / 23.0)), edge2 = 146 + (int)floor(6 * sin(x / 19.0 + 1));
            UiRGBA c;

            if (y < edge1 || y > edge2)
                c = (x * 3 + y * 5) % 17 == 0 ? bank2 : bank;
            else if (((x + y / 2) % 24 < 6) && (y - edge1) % 12 == 5)
                c = ripple;
            else
                c = water;
            Put(bm, x, y, c);
        }
}

static void Sky(struct UiBitmap *bm)
{
    static const int sClouds[5][3] = {{40, 40, 22}, {150, 30, 28}, {210, 90, 20}, {80, 120, 26}, {180, 170, 24}};
    UiRGBA top = UiMix(UI_P_TYPES_FLYING, UI_P_PAPER, 0.45), low = UiMix(UI_P_TYPES_WATER, UI_P_PAPER, 0.7),
           cloud = UI_P_PAPER, shade = UiMix(UI_P_TYPES_FLYING, UI_P_PAPER, 0.8);

    for (int y = 0; y < WH; y++)
        for (int x = 0; x < WW; x++)
        {
            UiRGBA c = UiMix(top, low, (double)y / WH);

            for (int i = 0; i < 5; i++)
            {
                double dx = (x - sClouds[i][0]) / (double)sClouds[i][2], dy = (y - sClouds[i][1]) / (sClouds[i][2] * 0.45);

                if (dx * dx + dy * dy <= 1)
                    c = dy > 0.45 ? shade : cloud;
            }
            Put(bm, x, y, c);
        }
}

static void Polkadot(struct UiBitmap *bm)
{
    UiRGBA bg = UiMix(UI_P_TYPES_FAIRY, UI_P_PAPER, 0.55), dot = UI_P_PAPER, dot2 = UiMix(UI_P_TYPES_FAIRY, UI_P_PAPER, 0.25);

    for (int y = 0; y < WH; y++)
        for (int x = 0; x < WW; x++)
        {
            int row = y / 20, tx = (x + (row % 2 ? 10 : 0)) % 20, ty = y % 20;
            int d = (tx - 10) * (tx - 10) + (ty - 10) * (ty - 10);

            Put(bm, x, y, d <= 16 ? (row % 3 == 1 ? dot2 : dot) : bg);
        }
}

static void Pokecenter(struct UiBitmap *bm)
{
    UiRGBA red = UiMix(UI_P_TYPES_FIRE, UI_P_TYPES_FIGHTING, 0.5), red2 = UiMix(red, UI_P_PAPER, 0.3), white = UI_P_PAPER,
           cream = UiMix(UI_P_PAPER, UI_P_CREAM_LO, 0.4);

    for (int y = 0; y < WH; y++)
        for (int x = 0; x < WW; x++)
        {
            UiRGBA c = (y / 8) % 2 ? cream : white;
            int tx = x % 32, ty = y % 32;

            /* small crosses on the light half, a red band below */
            if (y < 150 && ((abs(tx - 16) <= 1 && abs(ty - 16) <= 5) || (abs(ty - 16) <= 1 && abs(tx - 16) <= 5)))
                c = red2;
            if (y >= 150)
                c = y < 154 ? red2 : red;
            Put(bm, x, y, c);
        }
}

static void Machine(struct UiBitmap *bm)
{
    struct UiRamp st = R(UI_P_TYPES_STEEL);
    UiRGBA plate = UiMix(st.base, UI_P_PAPER, 0.3), line = UiMix(st.lo, UI_P_PAPER, 0.15), hi = UiMix(st.hi, UI_P_PAPER, 0.6),
           bolt = UiMix(st.lo, UI_P_INK, 0.2), led = UiMix(UI_P_TYPES_GRASS, UI_P_PAPER, 0.2);

    for (int y = 0; y < WH; y++)
        for (int x = 0; x < WW; x++)
        {
            int tx = x % 62, ty = y % 52;
            UiRGBA c = plate;

            if (tx == 0 || ty == 0)
                c = line;
            else if (tx == 1 || ty == 1)
                c = hi;
            if ((tx == 5 || tx == 56) && (ty == 5 || ty == 46))
                c = bolt;
            if (tx >= 26 && tx <= 28 && ty == 26 && (x / 62 + y / 52) % 3 == 0)
                c = led;
            Put(bm, x, y, c);
        }
}

static void Plain(struct UiBitmap *bm)
{
    UiRGBA bg = UiMix(UI_P_CREAM, UI_P_PAPER, 0.5), dot = UiMix(UI_P_CREAM_LO, UI_P_PAPER, 0.4);

    for (int y = 0; y < WH; y++)
        for (int x = 0; x < WW; x++)
            Put(bm, x, y, (x % 8 == 4 && y % 8 == 4) ? dot : bg);
}

static void Friends(struct UiBitmap *bm, UiRGBA bg, UiRGBA fg)
{
    for (int y = 0; y < WH; y++)
        for (int x = 0; x < WW; x++)
            Put(bm, x, y, ((x + y) / 6) % 4 == 0 ? fg : bg);
}

/* ---------------------------------------------------------------- the pieces */

struct WallCtx
{
    int kind;
    UiRGBA bg;
    UiRGBA fg;
};

static void BuildWall(struct UiBitmap *bm, const void *ctx)
{
    const struct WallCtx *c = ctx;

    switch (c->kind)
    {
    case NATIVE_WALLPAPER_FOREST: Forest(bm); break;
    case NATIVE_WALLPAPER_CITY: City(bm); break;
    case NATIVE_WALLPAPER_DESERT: Desert(bm); break;
    case NATIVE_WALLPAPER_SAVANNA: SavannaWall(bm); break;
    case NATIVE_WALLPAPER_CRAG: Crag(bm); break;
    case NATIVE_WALLPAPER_VOLCANO: Volcano(bm); break;
    case NATIVE_WALLPAPER_SNOW: Snow(bm); break;
    case NATIVE_WALLPAPER_CAVE: Cave(bm); break;
    case NATIVE_WALLPAPER_BEACH: Beach(bm); break;
    case NATIVE_WALLPAPER_SEAFLOOR: Seafloor(bm); break;
    case NATIVE_WALLPAPER_RIVER: River(bm); break;
    case NATIVE_WALLPAPER_SKY: Sky(bm); break;
    case NATIVE_WALLPAPER_POLKADOT: Polkadot(bm); break;
    case NATIVE_WALLPAPER_POKECENTER: Pokecenter(bm); break;
    case NATIVE_WALLPAPER_MACHINE: Machine(bm); break;
    case NATIVE_WALLPAPER_FRIENDS: Friends(bm, c->bg, c->fg); break;
    default: Plain(bm); break;
    }
}

const struct UiPiece *UiStorage_Wall(int kind, const u16 walda[2])
{
    struct WallCtx c = {kind, UI_NONE, UI_NONE};
    char key[48];

    if (kind == NATIVE_WALLPAPER_FRIENDS)
    {
        c.bg = FromBgr555(walda[0]);
        c.fg = FromBgr555(walda[1]);
        snprintf(key, sizeof(key), "pcwall:%d:%04x%04x", kind, walda[0], walda[1]);
    }
    else
    {
        snprintf(key, sizeof(key), "pcwall:%d", kind);
    }
    return UiG_Cached(key, WW, WH, BuildWall, &c);
}

struct UiRamp UiStorage_WallRamp(int kind, const u16 walda[2])
{
    UiRGBA t;

    switch (kind)
    {
    case NATIVE_WALLPAPER_FOREST: t = UI_P_TYPES_GRASS; break;
    case NATIVE_WALLPAPER_BEACH: t = UI_P_TYPES_WATER; break;
    case NATIVE_WALLPAPER_CITY: t = UI_P_TYPES_STEEL; break;
    case NATIVE_WALLPAPER_DESERT: t = UI_P_TYPES_GROUND; break;
    case NATIVE_WALLPAPER_SAVANNA: t = UI_P_TYPES_BUG; break;
    case NATIVE_WALLPAPER_CRAG: t = UI_P_TYPES_ROCK; break;
    case NATIVE_WALLPAPER_VOLCANO: t = UiMix(UI_P_TYPES_FIRE, UI_P_TYPES_DARK, 0.55); break;
    case NATIVE_WALLPAPER_SNOW: t = UI_P_TYPES_ICE; break;
    case NATIVE_WALLPAPER_CAVE: t = UiMix(UI_P_TYPES_GROUND, UI_P_TYPES_DARK, 0.45); break;
    case NATIVE_WALLPAPER_SEAFLOOR: t = UiMix(UI_P_TYPES_WATER, UI_P_TYPES_DARK, 0.35); break;
    case NATIVE_WALLPAPER_RIVER: t = UiMix(UI_P_TYPES_WATER, UI_P_TYPES_GRASS, 0.3); break;
    case NATIVE_WALLPAPER_SKY: t = UI_P_TYPES_FLYING; break;
    case NATIVE_WALLPAPER_POLKADOT: t = UI_P_TYPES_FAIRY; break;
    case NATIVE_WALLPAPER_POKECENTER: t = UiMix(UI_P_TYPES_FIRE, UI_P_TYPES_FIGHTING, 0.5); break;
    case NATIVE_WALLPAPER_MACHINE: t = UiMix(UI_P_TYPES_STEEL, UI_P_INK, 0.2); break;
    case NATIVE_WALLPAPER_FRIENDS: t = FromBgr555(walda[0]); break;
    default: t = UI_P_TYPES_NORMAL; break;
    }
    return UiRampOf(UiMix(t, UI_P_INK, 0.12), 0.4, 0.26, 0.62);
}

static const u8 sWall_Forest[] = _("Forest");
static const u8 sWall_City[] = _("City");
static const u8 sWall_Desert[] = _("Desert");
static const u8 sWall_Savanna[] = _("Savanna");
static const u8 sWall_Crag[] = _("Crag");
static const u8 sWall_Volcano[] = _("Volcano");
static const u8 sWall_Snow[] = _("Snow");
static const u8 sWall_Cave[] = _("Cave");
static const u8 sWall_Beach[] = _("Beach");
static const u8 sWall_Seafloor[] = _("Seafloor");
static const u8 sWall_River[] = _("River");
static const u8 sWall_Sky[] = _("Sky");
static const u8 sWall_Polkadot[] = _("Polka-dot");
static const u8 sWall_Pokecenter[] = _("Pokécenter");
static const u8 sWall_Machine[] = _("Machine");
static const u8 sWall_Simple[] = _("Simple");
static const u8 sWall_Friends[] = _("Friends");

const u8 *UiStorage_WallName(int kind)
{
    static const u8 *const sNames[NATIVE_WALLPAPER_COUNT] = {
        sWall_Forest, sWall_City, sWall_Desert, sWall_Savanna, sWall_Crag, sWall_Volcano, sWall_Snow, sWall_Cave,
        sWall_Beach, sWall_Seafloor, sWall_River, sWall_Sky, sWall_Polkadot, sWall_Pokecenter, sWall_Machine,
        sWall_Simple, sWall_Friends,
    };

    return kind >= 0 && kind < NATIVE_WALLPAPER_COUNT ? sNames[kind] : sWall_Simple;
}
#endif // ENABLE_NATIVE_UI
