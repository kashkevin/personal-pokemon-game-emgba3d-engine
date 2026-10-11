#ifdef ENABLE_NATIVE_UI
/*
 * The native UI's pixel-art kit (see native_ui/ui_kit.h): a line-by-line port
 * of the approved design's preview renderer (artifacts/ui-art-direction/
 * preview/js/core.js: masks, patterns, bitmaps, paint), so shapes come out
 * pixel for pixel as the design shows them. Project-owned.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "native_ui/ui_kit.h"

/* ---------------------------------------------------------------- numbers, colours */

int UiPyRound(double x)
{
    double f = floor(x);
    double d = x - f;

    if (fabs(d - 0.5) < 1e-9)
        return ((long long)f % 2 == 0) ? (int)f : (int)f + 1;
    return (int)floor(x + 0.5);
}

UiRGBA UiMix(UiRGBA c1, UiRGBA c2, double t)
{
    return UI_RGBA(UiPyRound(UI_R(c1) * (1 - t) + UI_R(c2) * t),
                   UiPyRound(UI_G(c1) * (1 - t) + UI_G(c2) * t),
                   UiPyRound(UI_B(c1) * (1 - t) + UI_B(c2) * t), 255);
}

UiRGBA UiWithAlpha(UiRGBA c, int a)
{
    return (c & 0x00FFFFFFu) | ((uint32_t)(a & 0xFF) << 24);
}

struct UiRamp UiRampOf(UiRGBA base, double hiT, double loT, double inkT)
{
    return UiRampOf2(base, hiT, loT, inkT, UI_NONE, UI_NONE);
}

struct UiRamp UiRampOf2(UiRGBA base, double hiT, double loT, double inkT, UiRGBA warm, UiRGBA cool)
{
    struct UiRamp r;

    if (warm == UI_NONE)
        warm = UI_RGB(255, 244, 200);
    if (cool == UI_NONE)
        cool = UI_RGB(20, 24, 60);
    if (UI_A(base) == 0)
        base = UiWithAlpha(base, 255);
    r.hi = UiMix(base, warm, hiT);
    r.base = base;
    r.lo = UiMix(base, cool, loT);
    r.ink = UiMix(base, cool, inkT);
    return r;
}

uint32_t UiHash(const char *s)
{
    uint32_t h = 2166136261u;

    for (; *s != '\0'; s++)
        h = (h ^ (uint8_t)*s) * 16777619u;
    return h;
}

/* ---------------------------------------------------------------- the mask cache */

#define MASK_BUDGET_BYTES (48u << 20)

struct MaskSlot
{
    struct UiMask *mask;
};

static struct MaskSlot *sMaskSlots;
static uint32_t sMaskCapacity;
static uint32_t sMaskCount;
static size_t sMaskBytes;

static void FreeMask(struct UiMask *m)
{
    free((void *)m->key);
    free(m->d);
    free(m);
}

static void MaskCacheClear(void)
{
    for (uint32_t i = 0; i < sMaskCapacity; i++)
        if (sMaskSlots[i].mask != NULL)
            FreeMask(sMaskSlots[i].mask);
    free(sMaskSlots);
    sMaskSlots = NULL;
    sMaskCapacity = sMaskCount = 0;
    sMaskBytes = 0;
}

static struct UiMask *MaskFind(const char *key, uint32_t hash)
{
    if (sMaskCapacity == 0)
        return NULL;
    for (uint32_t i = hash & (sMaskCapacity - 1);; i = (i + 1) & (sMaskCapacity - 1))
    {
        struct UiMask *m = sMaskSlots[i].mask;

        if (m == NULL)
            return NULL;
        if (m->hash == hash && strcmp(m->key, key) == 0)
            return m;
    }
}

static void MaskInsertSlot(struct UiMask *m)
{
    for (uint32_t i = m->hash & (sMaskCapacity - 1);; i = (i + 1) & (sMaskCapacity - 1))
        if (sMaskSlots[i].mask == NULL)
        {
            sMaskSlots[i].mask = m;
            return;
        }
}

static void MaskInsert(struct UiMask *m)
{
    if ((sMaskCount + 1) * 2 > sMaskCapacity)
    {
        struct MaskSlot *old = sMaskSlots;
        uint32_t oldCapacity = sMaskCapacity;

        sMaskCapacity = sMaskCapacity == 0 ? 1024 : sMaskCapacity * 2;
        sMaskSlots = calloc(sMaskCapacity, sizeof(*sMaskSlots));
        for (uint32_t i = 0; i < oldCapacity; i++)
            if (old[i].mask != NULL)
                MaskInsertSlot(old[i].mask);
        free(old);
    }
    MaskInsertSlot(m);
    sMaskCount++;
    sMaskBytes += (size_t)m->w * m->h + strlen(m->key) + sizeof(*m);
}

void UiKit_EndFrame(void)
{
    if (sMaskBytes > MASK_BUDGET_BYTES)
        MaskCacheClear();
}

void UiKit_Shutdown(void)
{
    MaskCacheClear();
}

/* A growing key buffer. */
struct KeyBuf
{
    char *s;
    size_t len;
    size_t cap;
};

static void KeyAdd(struct KeyBuf *k, const char *fmt, ...)
{
    va_list ap;
    int n;

    for (;;)
    {
        va_start(ap, fmt);
        n = vsnprintf(k->s != NULL ? k->s + k->len : NULL, k->s != NULL ? k->cap - k->len : 0, fmt, ap);
        va_end(ap);
        if (n < 0)
            return;
        if (k->s != NULL && k->len + (size_t)n < k->cap)
        {
            k->len += (size_t)n;
            return;
        }
        k->cap = (k->cap == 0 ? 64 : k->cap * 2) + (size_t)n;
        k->s = realloc(k->s, k->cap);
        if (k->len == 0)
            k->s[0] = '\0';
    }
}

/* Looks the key up; on a miss makes a cleared w x h mask under that key and
 * reports it as new (the caller fills it). Takes ownership of key->s. */
static struct UiMask *MaskGet(struct KeyBuf *key, int w, int h, bool *isNew)
{
    uint32_t hash = UiHash(key->s);
    struct UiMask *m = MaskFind(key->s, hash);

    if (m != NULL)
    {
        free(key->s);
        *isNew = false;
        return m;
    }
    if (w < 0)
        w = 0;
    if (h < 0)
        h = 0;
    m = calloc(1, sizeof(*m));
    m->w = (uint16_t)w;
    m->h = (uint16_t)h;
    m->hash = hash;
    m->key = key->s;
    m->d = calloc((size_t)(w > 0 ? w : 1) * (h > 0 ? h : 1), 1);
    MaskInsert(m);
    *isNew = true;
    return m;
}

static inline uint8_t MaskAt(const struct UiMask *m, int x, int y)
{
    return (x >= 0 && y >= 0 && x < m->w && y < m->h) ? m->d[y * m->w + x] : 0;
}

/* ---------------------------------------------------------------- corners */

static const int8_t sCornerTable[9][8] = {
    {0},
    {1},
    {2, 1},
    {3, 1, 1},
    {4, 2, 1, 1},
    {5, 3, 2, 1, 1},
    {6, 4, 3, 2, 1, 1},
    {7, 5, 3, 2, 2, 1, 1},
    {8, 5, 4, 3, 2, 1, 1, 1},
};

struct UiCorner UiCornerR(int r)
{
    struct UiCorner c = {0};

    if (r <= 0)
        return c;
    if (r <= 8)
    {
        c.n = (int8_t)r;
        memcpy(c.p, sCornerTable[r], (size_t)r);
        return c;
    }
    if (r > 16)
        r = 16;
    c.n = (int8_t)r;
    for (int row = 0; row < r; row++)
    {
        double dy = r - row - 0.5;

        c.p[row] = (int8_t)UiPyRound(r - sqrt(fmax(0.0, (double)r * r - dy * dy)));
    }
    return c;
}

struct UiCorner UiCornerP(int n, const int8_t *profile)
{
    struct UiCorner c = {0};

    if (n > 16)
        n = 16;
    c.n = (int8_t)n;
    memcpy(c.p, profile, (size_t)n);
    return c;
}

struct UiCorner UiCornerChamfer(int n)
{
    struct UiCorner c = {0};

    if (n > 16)
        n = 16;
    c.n = (int8_t)n;
    for (int i = 0; i < n; i++)
        c.p[i] = (int8_t)(n - i);
    return c;
}

static void CornerKey(struct KeyBuf *k, const struct UiCorner *c)
{
    KeyAdd(k, "[");
    for (int i = 0; i < c->n; i++)
        KeyAdd(k, i == 0 ? "%d" : ",%d", c->p[i]);
    KeyAdd(k, "]");
}

/* ---------------------------------------------------------------- shapes */

const struct UiMask *UiM_Rect(int w, int h)
{
    struct KeyBuf k = {0};
    bool isNew;
    struct UiMask *m;

    KeyAdd(&k, "R%dx%d", w, h);
    m = MaskGet(&k, w, h, &isNew);
    if (isNew)
        memset(m->d, 1, (size_t)m->w * m->h);
    return m;
}

const struct UiMask *UiM_Rounded(int w, int h, struct UiCorner tl, struct UiCorner tr,
                                 struct UiCorner bl, struct UiCorner br)
{
    struct KeyBuf k = {0};
    bool isNew;
    struct UiMask *m;

    KeyAdd(&k, "r%dx%d:", w, h);
    CornerKey(&k, &tl);
    CornerKey(&k, &tr);
    CornerKey(&k, &bl);
    CornerKey(&k, &br);
    m = MaskGet(&k, w, h, &isNew);
    if (isNew)
    {
        const struct UiCorner *corners[4] = {&tl, &tr, &bl, &br};

        memset(m->d, 1, (size_t)m->w * m->h);
        for (int c = 0; c < 4; c++)
        {
            bool top = c < 2;
            bool left = (c & 1) == 0;

            for (int i = 0; i < corners[c]->n; i++)
            {
                int inset = corners[c]->p[i];
                int y = top ? i : h - 1 - i;

                if (inset <= 0 || y < 0 || y >= h)
                    continue;
                for (int x = 0; x < inset && x < w; x++)
                    m->d[y * w + (left ? x : w - 1 - x)] = 0;
            }
        }
    }
    return m;
}

const struct UiMask *UiM_RoundedR(int w, int h, int r)
{
    struct UiCorner c = UiCornerR(r);

    return UiM_Rounded(w, h, c, c, c, c);
}

const struct UiMask *UiM_Rounded4(int w, int h, int tl, int tr, int bl, int br)
{
    return UiM_Rounded(w, h, UiCornerR(tl), UiCornerR(tr), UiCornerR(bl), UiCornerR(br));
}

const struct UiMask *UiM_Capsule(int w, int h, bool left, bool right)
{
    struct UiCorner prof = {0};
    struct UiCorner none = {0};
    double r = h / 2.0;
    int rows = (h + 1) / 2;

    if (rows > 16)
        rows = 16;
    prof.n = (int8_t)rows;
    for (int row = 0; row < rows; row++)
    {
        double dy = r - row - 0.5;

        prof.p[row] = (int8_t)UiPyRound(r - sqrt(fmax(0.0, r * r - dy * dy)));
    }
    return UiM_Rounded(w, h, left ? prof : none, right ? prof : none, left ? prof : none, right ? prof : none);
}

const struct UiMask *UiM_Ellipse(int w, int h)
{
    struct KeyBuf k = {0};
    bool isNew;
    struct UiMask *m;

    KeyAdd(&k, "e%dx%d", w, h);
    m = MaskGet(&k, w, h, &isNew);
    if (isNew)
    {
        double rx = w / 2.0;
        double ry = h / 2.0;

        for (int y = 0; y < h; y++)
        {
            double dy = (y + 0.5 - ry) / ry;
            double half;
            int x0;
            int x1;

            if (fabs(dy) > 1)
                continue;
            half = rx * sqrt(fmax(0.0, 1 - dy * dy));
            x0 = UiPyRound(rx - half);
            x1 = UiPyRound(rx + half);
            for (int x = x0 < 0 ? 0 : x0; x < (x1 < w ? x1 : w); x++)
                m->d[y * w + x] = 1;
        }
    }
    return m;
}

const struct UiMask *UiM_Circle(int d)
{
    return UiM_Ellipse(d, d);
}

static int CompareDouble(const void *a, const void *b)
{
    double x = *(const double *)a;
    double y = *(const double *)b;

    return (x > y) - (x < y);
}

const struct UiMask *UiM_Poly(int w, int h, const int *pts, int n)
{
    struct KeyBuf k = {0};
    bool isNew;
    struct UiMask *m;

    KeyAdd(&k, "p%dx%d:", w, h);
    for (int i = 0; i < n; i++)
        KeyAdd(&k, i == 0 ? "%d,%d" : ";%d,%d", pts[i * 2], pts[i * 2 + 1]);
    m = MaskGet(&k, w, h, &isNew);
    if (isNew)
    {
        double xs[64];

        for (int y = 0; y < h; y++)
        {
            double cy = y + 0.5;
            int count = 0;

            for (int i = 0; i < n && count < 64; i++)
            {
                double ax = pts[i * 2], ay = pts[i * 2 + 1];
                double bx = pts[((i + 1) % n) * 2], by = pts[((i + 1) % n) * 2 + 1];

                if ((ay <= cy && by > cy) || (by <= cy && ay > cy))
                    xs[count++] = ax + (cy - ay) * (bx - ax) / (by - ay);
            }
            qsort(xs, (size_t)count, sizeof(double), CompareDouble);
            for (int j = 0; j + 1 < count; j += 2)
            {
                int x0 = (int)ceil(xs[j] - 0.5);
                int x1 = (int)floor(xs[j + 1] - 0.5);

                if (x0 < 0)
                    x0 = 0;
                if (x1 > w - 1)
                    x1 = w - 1;
                for (int x = x0; x <= x1; x++)
                    m->d[y * w + x] = 1;
            }
        }
        for (int j = 0; j < n; j++)
        {
            int px = pts[j * 2], py = pts[j * 2 + 1];
            int qx = pts[((j + 1) % n) * 2], qy = pts[((j + 1) % n) * 2 + 1];
            int steps = abs(qx - px) > abs(qy - py) ? abs(qx - px) : abs(qy - py);

            if (steps < 1)
                steps = 1;
            for (int s = 0; s <= steps; s++)
            {
                /* JS Math.round: halves go up */
                int ex = (int)floor(px + (double)(qx - px) * s / steps + 0.5);
                int ey = (int)floor(py + (double)(qy - py) * s / steps + 0.5);

                if (ex >= 0 && ey >= 0 && ex < w && ey < h)
                    m->d[ey * w + ex] = 1;
            }
        }
    }
    return m;
}

const struct UiMask *UiM_Rows(const char *const *rows, int count, const char *on)
{
    struct KeyBuf k = {0};
    bool isNew;
    struct UiMask *m;
    int w = 0;

    if (on == NULL)
        on = "#";
    KeyAdd(&k, "s%s:", on);
    for (int i = 0; i < count; i++)
    {
        int len = (int)strlen(rows[i]);

        if (len > w)
            w = len;
        KeyAdd(&k, i == 0 ? "%s" : "/%s", rows[i]);
    }
    m = MaskGet(&k, w, count, &isNew);
    if (isNew)
        for (int y = 0; y < count; y++)
            for (int x = 0; rows[y][x] != '\0'; x++)
                if (strchr(on, rows[y][x]) != NULL)
                    m->d[y * w + x] = 1;
    return m;
}

/* ---------------------------------------------------------------- morphology and set operations */

const struct UiMask *UiM_Erode4(const struct UiMask *a)
{
    struct KeyBuf k = {0};
    bool isNew;
    struct UiMask *m;

    KeyAdd(&k, "E(%s)", a->key);
    m = MaskGet(&k, a->w, a->h, &isNew);
    if (isNew)
    {
        int w = a->w, h = a->h;

        for (int y = 1; y < h - 1; y++)
            for (int x = 1; x < w - 1; x++)
            {
                int i = y * w + x;

                if (a->d[i] && a->d[i - 1] && a->d[i + 1] && a->d[i - w] && a->d[i + w])
                    m->d[i] = 1;
            }
    }
    return m;
}

const struct UiMask *UiM_Dilate4(const struct UiMask *a)
{
    struct KeyBuf k = {0};
    bool isNew;
    struct UiMask *m;

    KeyAdd(&k, "D4(%s)", a->key);
    m = MaskGet(&k, a->w, a->h, &isNew);
    if (isNew)
    {
        int w = a->w, h = a->h;

        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
            {
                int i = y * w + x;

                if (a->d[i] || (x > 0 && a->d[i - 1]) || (x < w - 1 && a->d[i + 1])
                 || (y > 0 && a->d[i - w]) || (y < h - 1 && a->d[i + w]))
                    m->d[i] = 1;
            }
    }
    return m;
}

const struct UiMask *UiM_Dilate8(const struct UiMask *a)
{
    struct KeyBuf k = {0};
    bool isNew;
    struct UiMask *m;

    KeyAdd(&k, "D8(%s)", a->key);
    m = MaskGet(&k, a->w, a->h, &isNew);
    if (isNew)
    {
        int w = a->w, h = a->h;

        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
            {
                uint8_t on = 0;

                for (int dy = -1; dy <= 1 && !on; dy++)
                    for (int dx = -1; dx <= 1; dx++)
                        if (MaskAt(a, x + dx, y + dy))
                        {
                            on = 1;
                            break;
                        }
                m->d[y * w + x] = on;
            }
    }
    return m;
}

const struct UiMask *UiM_Shift(const struct UiMask *a, int dx, int dy)
{
    struct KeyBuf k = {0};
    bool isNew;
    struct UiMask *m;

    KeyAdd(&k, "S%d,%d(%s)", dx, dy, a->key);
    m = MaskGet(&k, a->w, a->h, &isNew);
    if (isNew)
        for (int y = 0; y < a->h; y++)
            for (int x = 0; x < a->w; x++)
                m->d[y * a->w + x] = MaskAt(a, x - dx, y - dy);
    return m;
}

const struct UiMask *UiM_Pad(const struct UiMask *a, int t, int b, int l, int r)
{
    struct KeyBuf k = {0};
    bool isNew;
    struct UiMask *m;
    int w = a->w + l + r;
    int h = a->h + t + b;

    KeyAdd(&k, "P%d,%d,%d,%d(%s)", t, b, l, r, a->key);
    m = MaskGet(&k, w, h, &isNew);
    if (isNew)
        for (int y = 0; y < a->h; y++)
            for (int x = 0; x < a->w; x++)
                m->d[(y + t) * w + x + l] = a->d[y * a->w + x];
    return m;
}

const struct UiMask *UiM_Place(const struct UiMask *a, const struct UiMask *b, int x, int y)
{
    struct KeyBuf k = {0};
    bool isNew;
    struct UiMask *m;

    KeyAdd(&k, "L%d,%d(%dx%d:%s)", x, y, a->w, a->h, b->key);
    m = MaskGet(&k, a->w, a->h, &isNew);
    if (isNew)
        for (int yy = 0; yy < b->h; yy++)
            for (int xx = 0; xx < b->w; xx++)
            {
                int X = xx + x, Y = yy + y;

                if (X >= 0 && Y >= 0 && X < a->w && Y < a->h && b->d[yy * b->w + xx])
                    m->d[Y * a->w + X] = 1;
            }
    return m;
}

static const struct UiMask *Binop(char op, const struct UiMask *a, const struct UiMask *b)
{
    struct KeyBuf k = {0};
    bool isNew;
    struct UiMask *m;

    KeyAdd(&k, "(%s)%c(%s)", a->key, op, b->key);
    m = MaskGet(&k, a->w, a->h, &isNew);
    if (isNew)
        for (int y = 0; y < a->h; y++)
            for (int x = 0; x < a->w; x++)
            {
                uint8_t p = a->d[y * a->w + x];
                uint8_t q = (x < b->w && y < b->h) ? b->d[y * b->w + x] : 0;

                m->d[y * a->w + x] = op == '&' ? (p & q) : op == '|' ? (p | q) : (p & (q ^ 1));
            }
    return m;
}

const struct UiMask *UiM_And(const struct UiMask *a, const struct UiMask *b) { return Binop('&', a, b); }
const struct UiMask *UiM_Or(const struct UiMask *a, const struct UiMask *b) { return Binop('|', a, b); }
const struct UiMask *UiM_Sub(const struct UiMask *a, const struct UiMask *b) { return Binop('-', a, b); }

const struct UiMask *UiM_Not(const struct UiMask *a)
{
    struct KeyBuf k = {0};
    bool isNew;
    struct UiMask *m;

    KeyAdd(&k, "~(%s)", a->key);
    m = MaskGet(&k, a->w, a->h, &isNew);
    if (isNew)
        for (int i = 0; i < a->w * a->h; i++)
            m->d[i] = a->d[i] ^ 1;
    return m;
}

const struct UiMask *UiM_Window(const struct UiMask *a, int x0, int y0, int x1, int y1)
{
    struct KeyBuf k = {0};
    bool isNew;
    struct UiMask *m;

    KeyAdd(&k, "W%d,%d,%d,%d(%s)", x0, y0, x1, y1, a->key);
    m = MaskGet(&k, a->w, a->h, &isNew);
    if (isNew)
        for (int y = y0 < 0 ? 0 : y0; y < (y1 < a->h ? y1 : a->h); y++)
            for (int x = x0 < 0 ? 0 : x0; x < (x1 < a->w ? x1 : a->w); x++)
                m->d[y * a->w + x] = a->d[y * a->w + x];
    return m;
}

const struct UiMask *UiM_Outline(const struct UiMask *a)
{
    return UiM_Sub(a, UiM_Erode4(a));
}

const struct UiMask *UiM_Carve(const struct UiMask *a, const int *cuts, int count, const char *tag)
{
    struct KeyBuf k = {0};
    bool isNew;
    struct UiMask *m;

    KeyAdd(&k, "C%s(%s)", tag, a->key);
    m = MaskGet(&k, a->w, a->h, &isNew);
    if (isNew)
    {
        memcpy(m->d, a->d, (size_t)a->w * a->h);
        for (int c = 0; c < count; c++)
        {
            int y = cuts[c * 3] < 0 ? a->h + cuts[c * 3] : cuts[c * 3];

            for (int i = 0; i < cuts[c * 3 + 2]; i++)
            {
                int x = cuts[c * 3 + 1] ? a->w - 1 - i : i;

                if (y >= 0 && y < a->h && x >= 0 && x < a->w)
                    m->d[y * a->w + x] = 0;
            }
        }
    }
    return m;
}

/* ---------------------------------------------------------------- patterns */

static int Mod(int a, int n)
{
    return ((a % n) + n) % n;
}

struct UiPattern UiPat_Diag(int period, int width, int phase, int dir)
{
    struct UiPattern p = {UI_PAT_DIAG, (int16_t)period, (int16_t)width, (int16_t)phase, (int16_t)dir, 0, NULL};
    return p;
}

struct UiPattern UiPat_HStripe(int period, int width, int phase)
{
    struct UiPattern p = {UI_PAT_HSTRIPE, (int16_t)period, (int16_t)width, (int16_t)phase, 0, 0, NULL};
    return p;
}

struct UiPattern UiPat_VStripe(int period, int width, int phase)
{
    struct UiPattern p = {UI_PAT_VSTRIPE, (int16_t)period, (int16_t)width, (int16_t)phase, 0, 0, NULL};
    return p;
}

struct UiPattern UiPat_Dots(int step, int size, int ox, int oy, bool stagger)
{
    struct UiPattern p = {UI_PAT_DOTS, (int16_t)step, (int16_t)size, (int16_t)ox, (int16_t)oy, (int16_t)stagger, NULL};
    return p;
}

struct UiPattern UiPat_Checker(int size, int phase)
{
    struct UiPattern p = {UI_PAT_CHECKER, (int16_t)size, (int16_t)phase, 0, 0, 0, NULL};
    return p;
}

struct UiPattern UiPat_Below(int y0)
{
    struct UiPattern p = {UI_PAT_BELOW, (int16_t)y0, 0, 0, 0, 0, NULL};
    return p;
}

bool UiPat_At(const struct UiPattern *p, int x, int y)
{
    bool on;

    switch (p->kind)
    {
    case UI_PAT_DIAG:
        on = Mod(x + p->d * y + p->c, p->a) < p->b;
        break;
    case UI_PAT_HSTRIPE:
        on = Mod(y + p->c, p->a) < p->b;
        break;
    case UI_PAT_VSTRIPE:
        on = Mod(x + p->c, p->a) < p->b;
        break;
    case UI_PAT_DOTS:
    {
        int step = p->a;
        int row = (int)floor((double)(y + p->d) / step);
        int sx = Mod(x + p->c + Mod(row, 2) * (p->e ? step / 2 : 0), step);

        on = sx < p->b && Mod(y + p->d, step) < p->b;
        break;
    }
    case UI_PAT_CHECKER:
        on = Mod((int)floor((double)(x + p->b) / p->a) + (int)floor((double)(y + p->b) / p->a), 2) == 0;
        break;
    case UI_PAT_BELOW:
        on = y > p->a;
        break;
    default:
        on = false;
        break;
    }
    if (on && p->and_ != NULL)
        on = UiPat_At(p->and_, x, y);
    return on;
}

void UiPat_Key(const struct UiPattern *p, char *buf, int size)
{
    int n = snprintf(buf, (size_t)size, "pk%d:%d,%d,%d,%d,%d", p->kind, p->a, p->b, p->c, p->d, p->e);

    if (p->and_ != NULL && n > 0 && n < size)
    {
        buf[n++] = '&';
        if (n < size)
            UiPat_Key(p->and_, buf + n, size - n);
    }
}

void UiPat_TileSize(const struct UiPattern *p, int *w, int *h)
{
    switch (p->kind)
    {
    case UI_PAT_DIAG: *w = *h = p->a; break;
    case UI_PAT_HSTRIPE: *w = 1; *h = p->a; break;
    case UI_PAT_VSTRIPE: *w = p->a; *h = 1; break;
    case UI_PAT_DOTS:
    case UI_PAT_CHECKER: *w = *h = p->a * 2; break;
    default: *w = *h = 16; break;
    }
}

const struct UiMask *UiM_Pattern(const struct UiMask *a, const struct UiPattern *p)
{
    struct KeyBuf k = {0};
    bool isNew;
    struct UiMask *m;
    char pk[128];

    UiPat_Key(p, pk, sizeof(pk));
    KeyAdd(&k, "Q%s(%s)", pk, a->key);
    m = MaskGet(&k, a->w, a->h, &isNew);
    if (isNew)
        for (int y = 0; y < a->h; y++)
            for (int x = 0; x < a->w; x++)
                m->d[y * a->w + x] = a->d[y * a->w + x] && UiPat_At(p, x, y);
    return m;
}

/* ---------------------------------------------------------------- bitmaps */

void UiBitmap_Init(struct UiBitmap *bm, int w, int h)
{
    bm->w = w;
    bm->h = h;
    bm->d = calloc((size_t)(w > 0 ? w : 1) * (h > 0 ? h : 1), 4);
}

void UiBitmap_Free(struct UiBitmap *bm)
{
    free(bm->d);
    bm->d = NULL;
}

void UiBitmap_FillMask(struct UiBitmap *bm, const struct UiMask *m, int x, int y, UiRGBA c)
{
    int a = UI_A(c);

    if (m == NULL || a <= 0)
        return;
    for (int yy = 0; yy < m->h; yy++)
    {
        int Y = yy + y;

        if (Y < 0 || Y >= bm->h)
            continue;
        for (int xx = 0; xx < m->w; xx++)
        {
            int X = xx + x;
            uint8_t *p;

            if (!m->d[yy * m->w + xx] || X < 0 || X >= bm->w)
                continue;
            p = &bm->d[(Y * bm->w + X) * 4];
            if (a >= 255)
            {
                p[0] = (uint8_t)UI_R(c);
                p[1] = (uint8_t)UI_G(c);
                p[2] = (uint8_t)UI_B(c);
                p[3] = 255;
            }
            else
            {
                double t = a / 255.0;

                p[0] = (uint8_t)UiPyRound(p[0] * (1 - t) + UI_R(c) * t);
                p[1] = (uint8_t)UiPyRound(p[1] * (1 - t) + UI_G(c) * t);
                p[2] = (uint8_t)UiPyRound(p[2] * (1 - t) + UI_B(c) * t);
                if (p[3] < a)
                    p[3] = (uint8_t)a;
            }
        }
    }
}

void UiBitmap_Rect(struct UiBitmap *bm, int x, int y, int w, int h, UiRGBA c)
{
    if (w > 0 && h > 0)
        UiBitmap_FillMask(bm, UiM_Rect(w, h), x, y, c);
}

void UiBitmap_Put(struct UiBitmap *bm, int x, int y, UiRGBA c)
{
    if (x < 0 || y < 0 || x >= bm->w || y >= bm->h)
        return;
    UiBitmap_FillMask(bm, UiM_Rect(1, 1), x, y, c);
}

void UiBitmap_Paint(struct UiBitmap *bm, const struct UiMask *m, int x, int y, UiRGBA body,
                    const struct UiPaint *o)
{
    static const struct UiPaint sNone = {0};
    const struct UiMask *inner;
    const struct UiMask *cur;
    const struct UiMask *band;

    if (o == NULL)
        o = &sNone;
    inner = o->ink != UI_NONE ? UiM_Erode4(m) : m;
    UiBitmap_FillMask(bm, inner, x, y, body);
    if (o->pattern != NULL)
        UiBitmap_FillMask(bm, UiM_Pattern(inner, o->pattern), x, y, o->patternColor);
    if (o->hi != UI_NONE)
    {
        band = NULL;
        cur = inner;
        for (int k = 0; k < (o->hiW > 0 ? o->hiW : 1); k++)
        {
            const struct UiMask *et = UiM_Sub(cur, UiM_Shift(cur, 0, 1));
            const struct UiMask *el = UiM_Sub(cur, UiM_Shift(cur, 1, 0));
            const struct UiMask *e = UiM_Or(et, el);

            band = band != NULL ? UiM_Or(band, e) : e;
            cur = UiM_Sub(cur, e);
        }
        UiBitmap_FillMask(bm, band, x, y, o->hi);
    }
    if (o->lo != UI_NONE)
    {
        band = NULL;
        cur = inner;
        for (int k = 0; k < (o->loW > 0 ? o->loW : 1); k++)
        {
            const struct UiMask *et = UiM_Sub(cur, UiM_Shift(cur, 0, -1));
            const struct UiMask *el = UiM_Sub(cur, UiM_Shift(cur, -1, 0));
            const struct UiMask *e = UiM_Or(et, el);

            band = band != NULL ? UiM_Or(band, e) : e;
            cur = UiM_Sub(cur, e);
        }
        UiBitmap_FillMask(bm, band, x, y, o->lo);
    }
    if (o->ink != UI_NONE)
        UiBitmap_FillMask(bm, UiM_Sub(m, inner), x, y, o->ink);
}

void UiPaintKey(char *buf, int size, const struct UiMask *m, UiRGBA body, const struct UiPaint *o)
{
    static const struct UiPaint sNone = {0};
    char pk[128] = "";

    if (o == NULL)
        o = &sNone;
    if (o->pattern != NULL)
        UiPat_Key(o->pattern, pk, sizeof(pk));
    snprintf(buf, (size_t)size, "%s|%08x|%08x|%08x|%08x|%d%d|%s%08x", m->key, body, o->ink, o->hi, o->lo,
             o->hiW > 0 ? o->hiW : 1, o->loW > 0 ? o->loW : 1, pk, o->pattern != NULL ? o->patternColor : 0);
}
#endif // ENABLE_NATIVE_UI
