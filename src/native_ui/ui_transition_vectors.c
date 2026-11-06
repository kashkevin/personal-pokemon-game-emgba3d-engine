#ifdef ENABLE_NATIVE_UI
/*
 * MS-108: the Battle Frontier's transition art as vector pieces (native_ui/ui_transition_art.h). The logo
 * (frontier_logo.png on its map: three discs, each with a grey outline, a white rim and a blue face with a white ring,
 * the white egg over their middle with its blue band and head), the circles' transitions' centre (frontier_logo_center)
 * and the three circles (frontier_logo_circles.png's frames) are drawn, every one of their edges, with circles - so the
 * curves are true curves at any size, where the smoothed pixels kept a wobble along the art's long steps.
 *
 * The circles are the art's own: each edge's circle was fitted to the art's pixels (their centres and radii, the lines'
 * widths) so that the shapes, sampled at the art's pixel middles, give back the art's pixels - of the coloured pixels
 * 44 of 3,216 differ in each circle's frame, 44 of 6,044 in the centre, 300 of 19,358 in the logo, every one within a
 * pixel of an edge: the art's own anti-aliasing (light-blue pixels on some steps of an edge) and ties. Where the art
 * draws a light-blue or a grey line along an edge, the line keeps the width that gives the art back best (0.5 to 1 GBA
 * pixel); where its light-blue only marks some steps (the egg's edge, the band's lower edge), the window's own
 * anti-aliasing takes its place. Mirror images share their numbers (x = 240 - x). tools/native_ui/frontier_vectors.py
 * checks these tables against the art.
 *
 * At run time a piece is used only for the art it was measured from: UiArt_MatchPiece compares it with the game's own
 * pixels (BG0's map and tiles, a sprite's tiles, as decoded from VRAM) and the smoothed pixels draw anything else.
 * Project-owned.
 */
#include <math.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "native_ui/ui_transition_art.h"

#define NONE {0, 0, 0, 0}

/* the circles' frames (sprite pixels, 64 x 64): a white disc, its blue face with a light-blue line round it (0.6
 * pixels), a white ring 3.8 pixels wide - the top circle's 4 pixels above the middle, the left one's 3 left and 2 down,
 * the right one's 3 right and 2 down */
static const struct UiArtShape sCircleTop[] = {
    {5, 0, {{32, 32, 31.97f, 0}, NONE, NONE}},                      /* the white disc */
    {7, 6, {{32, 32, 28.47f, 0.6f}, {32, 28, -13.84f, 0}, NONE}},   /* the blue face and its line, the ring white */
    {7, 0, {{32, 28, 10.03f, 0}, NONE, NONE}},                      /* the blue inside the ring */
};

static const struct UiArtShape sCircleLeft[] = {
    {5, 0, {{32, 32, 31.97f, 0}, NONE, NONE}},
    {7, 6, {{32, 32, 28.47f, 0.6f}, {29, 34, -13.84f, 0}, NONE}},
    {7, 0, {{29, 34, 10.03f, 0}, NONE, NONE}},
};

static const struct UiArtShape sCircleRight[] = {
    {5, 0, {{32, 32, 31.97f, 0}, NONE, NONE}},
    {7, 6, {{32, 32, 28.47f, 0.6f}, {35, 34, -13.84f, 0}, NONE}},
    {7, 0, {{35, 34, 10.03f, 0}, NONE, NONE}},
};

/* the centre (its map's pixels; the game shows it 5 lines lower): a white disc; inside its rim the blue band, with
 * light-blue lines along its top and round the head's ring, cut off below by a white ring round a large circle; inside
 * that the white lobes (a colour of their own); the blue head */
static const struct UiArtShape sCentre[] = {
    {5, 0, {{120, 75, 43.79f, 0}, NONE, NONE}},                                                   /* the white disc */
    {7, 6, {{120, 74.95f, 38.45f, 0.95f}, {120, 99.84f, -48.94f, 0}, {120, 60.9f, -19.24f, 0.73f}}}, /* the blue band */
    {8, 0, {{120, 74.95f, 38.97f, 0}, {120, 101.69f, 46.87f, 0}, {120, 60.9f, -18.94f, 0}}},      /* the white lobes */
    {7, 0, {{120, 60.49f, 14.77f, 0}, NONE, NONE}},                                               /* the blue head */
};

/* the logo (its map's pixels): the grey outline and the white of the three discs, then each blue region cut out of the
 * white by the rings, the egg and the head's ring, with light-blue lines (0.5 to 1 pixel) where the art has them */
static const struct UiArtShape sLogo[] = {
    {4, 0, {{120, 51.34f, 50.97f, 0}, NONE, NONE}},                     /* the grey outline: the top disc's */
    {4, 0, {{87.18f, 107.56f, 51.16f, 0}, NONE, NONE}},                 /* the left disc's */
    {4, 0, {{152.82f, 107.56f, 51.16f, 0}, NONE, NONE}},                /* the right disc's */
    {1, 0, {{120, 51.17f, 49.99f, 0}, NONE, NONE}},                     /* the white: the top disc's */
    {1, 0, {{87.22f, 107.54f, 50.29f, 0}, NONE, NONE}},                 /* the left disc's */
    {1, 0, {{152.78f, 107.54f, 50.29f, 0}, NONE, NONE}},                /* the right disc's */
    /* the top disc's blue, round its ring and the egg; the blue inside its ring */
    {3, 2, {{120, 50.95f, 44.36f, 0.98f}, {120, 45.07f, -22.32f, 0.7f}, {120, 90.9f, -50.82f, 0}}},
    {3, 2, {{120, 46, 16.45f, 0.5f}, {120, 90.9f, -50.82f, 0}, NONE}},
    /* the egg's band, under the head's ring; the head */
    {3, 2, {{120, 90.9f, 44.35f, 0.85f}, {120, 118.37f, -55.88f, 0}, {120, 73.4f, -21.9f, 0.6f}}},
    {3, 2, {{120, 74, 16.35f, 0.9f}, NONE, NONE}},
    /* the left disc's blue and inside its ring; the right disc's, mirrored */
    {3, 2, {{85.97f, 108.03f, 44.38f, 0.92f}, {81.1f, 111.05f, -22.22f, 0.9f}, {120, 90.9f, -50.82f, 0}}},
    {3, 2, {{81.25f, 110.03f, 16.6f, 0.5f}, {120, 90.9f, -50.82f, 0}, NONE}},
    {3, 2, {{154.03f, 108.03f, 44.38f, 0.92f}, {158.9f, 111.05f, -22.22f, 0.9f}, {120, 90.9f, -50.82f, 0}}},
    {3, 2, {{158.75f, 110.03f, 16.6f, 0.5f}, {120, 90.9f, -50.82f, 0}, NONE}},
};

const struct UiArtPiece gUiArtPieces[UI_ART_PIECE_COUNT] = {
    [UI_ART_PIECE_FRONTIER_LOGO] = {"the Frontier logo", sLogo, (int)(sizeof(sLogo) / sizeof(sLogo[0]))},
    [UI_ART_PIECE_FRONTIER_CENTRE] = {"the Frontier logo's centre", sCentre,
                                      (int)(sizeof(sCentre) / sizeof(sCentre[0]))},
    [UI_ART_PIECE_FRONTIER_CIRCLE_TOP] = {"the Frontier's top circle", sCircleTop,
                                          (int)(sizeof(sCircleTop) / sizeof(sCircleTop[0]))},
    [UI_ART_PIECE_FRONTIER_CIRCLE_LEFT] = {"the Frontier's left circle", sCircleLeft,
                                           (int)(sizeof(sCircleLeft) / sizeof(sCircleLeft[0]))},
    [UI_ART_PIECE_FRONTIER_CIRCLE_RIGHT] = {"the Frontier's right circle", sCircleRight,
                                            (int)(sizeof(sCircleRight) / sizeof(sCircleRight[0]))},
};

void UiArt_PieceBox(const struct UiArtPiece *piece, float box[4])
{
    box[0] = box[1] = 1e9f;
    box[2] = box[3] = -1e9f;
    for (int i = 0; i < piece->count; i++)
        for (int j = 0; j < UI_ART_SHAPE_CIRCLES; j++)
        {
            const struct UiArtCircle *c = &piece->shapes[i].circles[j];
            float reach = c->r + c->line / 2 + 1; /* a pixel's margin */

            if (c->r <= 0)
                continue;
            box[0] = fminf(box[0], c->x - reach);
            box[1] = fminf(box[1], c->y - reach);
            box[2] = fmaxf(box[2], c->x + reach);
            box[3] = fmaxf(box[3], c->y + reach);
        }
}

/* the colour the piece paints at (x, y) (0: none): its last shape there - its colour inside its edges (an edge with a
 * line moved in by half the line's width), its line colour out to the lines' outer sides */
static uint8_t ColourAt(const struct UiArtPiece *piece, float x, float y)
{
    uint8_t colour = 0;

    for (int i = 0; i < piece->count; i++)
    {
        const struct UiArtShape *shape = &piece->shapes[i];
        bool core = true, halo = true;

        for (int j = 0; j < UI_ART_SHAPE_CIRCLES && halo; j++)
        {
            const struct UiArtCircle *c = &shape->circles[j];
            float dx = x - c->x, dy = y - c->y, d = sqrtf(dx * dx + dy * dy);
            float half = shape->line != 0 ? c->line / 2 : 0;

            if (c->r > 0)
            {
                core = core && d < c->r - half;
                halo = halo && d < c->r + half;
            }
            else if (c->r < 0)
            {
                core = core && d >= -c->r + half;
                halo = halo && d >= -c->r - half;
            }
        }
        if (core)
            colour = shape->colour;
        else if (halo)
            colour = shape->line;
    }
    return colour;
}

/* how many of the art's pixels the piece gets wrong (at its pixels' middles), against how many either colours; a
 * background's art all in one palette (else the art is not the piece's: every coloured pixel counts as wrong) */
static void Compare(const struct UiArtPiece *piece, const uint8_t *slots, int w, int h, bool background, int *differ,
                    int *coloured, int *palette)
{
    float box[4];
    int pal = -1;

    UiArt_PieceBox(piece, box);
    *differ = *coloured = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            uint8_t s = slots[y * w + x], art = s & 15, want = 0;
            float px = x + 0.5f, py = y + 0.5f;

            if (px >= box[0] && px < box[2] && py >= box[1] && py < box[3])
                want = ColourAt(piece, px, py);
            if (art == 0 && want == 0)
                continue;
            (*coloured)++;
            if (art != 0 && background)
            {
                if (pal < 0)
                    pal = s >> 4;
                else if (pal != s >> 4)
                    art = 0xFF;
            }
            if (art != want)
                (*differ)++;
        }
    *palette = pal < 0 ? 0 : pal;
}

const struct UiArtPiece *UiArt_MatchPiece(const uint8_t *slots, int w, int h, bool background,
                                          const enum UiArtPieceId *candidates, int count, int *palette, int *differ)
{
    const struct UiArtPiece *best = NULL;
    int bestDiffer = 0, bestPalette = 0;

    for (int i = 0; i < count; i++)
    {
        const struct UiArtPiece *piece;
        int d, coloured, pal;

        if (candidates[i] < 0 || candidates[i] >= UI_ART_PIECE_COUNT)
            continue;
        piece = &gUiArtPieces[candidates[i]];
        Compare(piece, slots, w, h, background, &d, &coloured, &pal);
        if (coloured > 0 && d * 50 <= coloured && (best == NULL || d < bestDiffer))
        {
            best = piece;
            bestDiffer = d;
            bestPalette = pal;
        }
    }
    if (palette != NULL)
        *palette = bestPalette;
    if (differ != NULL)
        *differ = bestDiffer;
    return best;
}
void UiArt_LogPiece(const struct UiArtPiece *piece, int differ)
{
    SDL_Log("Native UI: %s drawn as true circles (%d of the art's pixels differ, at its edges)", piece->name, differ);
}
#endif // ENABLE_NATIVE_UI
