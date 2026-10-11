#ifndef GUARD_DIORAMA_BATTLE_SCENE_H
#define GUARD_DIORAMA_BATTLE_SCENE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * The 3D battle: the Diorama world drawn as a battle's scenery, the battle's
 * own picture over it. A port of ZallaxDev/pokeemerald-3Ds-dualscreen's 3D
 * battle (3ds_port/src/voxel/voxel_battle.c, RenderBattleWorld in
 * 3ds_port/src/3ds_video.c, the battle camera in ctr_voxel.c; MIT, see
 * src/diorama/README.md) to the Diorama renderer, single screen.
 *
 * The battle engine, its controllers, its sprites, healthboxes, text box,
 * menus and move animations are unchanged: they draw the same GBA picture as
 * ever. Only the layer the GBA draws its scenery on (BG3; while the intro
 * slides the scenery in, also BG1 and BG2) is replaced, pixel for pixel, by
 * the world seen from a battle camera: the GBA renderer leaves those layers
 * see-through (with the GBA's own blending against them carried in the
 * alpha channel) and the world is drawn underneath. Nothing here writes game
 * memory or decides anything about the battle.
 *
 * Built only with DIORAMA=1 (ENABLE_DIORAMA); the GBA build and the 2D
 * native build are unchanged.
 */

/* ── Game side (game units; battle_bg.c, battle_anim.c, battle_intro.c) ── */

/* Whether BG3 holds the battle environment's scenery - and not a move's
 * background, which is loaded into the same BG3 (LoadMoveBg) and has to be
 * shown as it is. */
uint8_t DioramaBattle_SceneryShown(void);
void DioramaBattle_SetSceneryShown(uint8_t shown);
/* A move's background loaded into BG3 (LoadMoveBg): how many bytes of BG3's
 * tilemap it filled (most fill the rows above the text box or the screen's,
 * not the whole map), and a count of such loads. */
void DioramaBattle_NoteMoveBg(uint32_t tilemapBytes);
uint32_t DioramaBattle_MoveBgMapSize(void);
uint32_t DioramaBattle_MoveBgLoads(void);
/* Whether the intro is sliding the scenery in (BG1/BG2 hold its entry
 * picture meanwhile; the 3D battle has the camera glide in instead). */
uint8_t DioramaBattle_IntroSliding(void);
/* Whether a battle transition is playing on the field (battle_transition.c):
 * the 3D view composes it over the 3D field (gl_battle.c). */
uint8_t DioramaBattle_TransitionActive(void);

/*
 * The map's sprites in OAM (sprite.c, battle_capture.c): those placed on the
 * map (coordOffsetEnabled) and the weather's, which the 3D view draws itself;
 * not the ones placed on the screen, such as a battle transition's. Marked
 * as the OAM buffer is built, latched as it is loaded into OAM.
 */
struct Sprite;
void DioramaSprites_BeginOam(void);
void DioramaSprites_MarkOam(const struct Sprite *sprite, uint8_t first, uint8_t end);
void DioramaSprites_LatchOam(void);
extern uint32_t gDioramaMapOam[4]; /* bit n: OAM entry n, as OAM holds it now */

/* ── Captured once per frame at the frame boundary (battle_capture.c) ── */

#define DIORAMA_BATTLE_MAX_SHADOWS 4

/* What the ground under a battler is, as the battle sees it. */
enum DioramaBattleGround
{
    DIORAMA_GROUND_PLAIN,
    DIORAMA_GROUND_GRASS,
    DIORAMA_GROUND_SAND,
    DIORAMA_GROUND_WATER,
    DIORAMA_GROUND_VOID, /* nothing drawn there: black indoors, past the map */
};

/* A soft contact shadow under a battler, in GBA picture coordinates. */
struct DioramaBattleShadow
{
    float x, y, rx, ry;
};

/* A battler's sprite and its healthbox as the game draws them this frame, in GBA picture
 * coordinates (battle_environment.h turns them into the world's terms). */
#define DIORAMA_BATTLE_MAX_BATTLERS 4
struct DioramaBattleBattlerSprite
{
    bool present;            /* in the battle and its sprite drawn */
    bool opponent;
    uint8_t battler;         /* B_BATTLER index */
    uint8_t position;        /* B_POSITION_* */
    float x0, y0, x1, y1;    /* the sprite's 64 x 64 box, offsets and animation included */
    float footX, footY;      /* where it stands: the point its contact shadow is centred on */
    bool healthbox;          /* its healthbox drawn */
    float hx0, hy0, hx1, hy1;
};

struct DioramaBattleFrame
{
    bool inBattle;
    bool link;          /* a link battle: its intro shows its VS frame on BG1/BG2 */
    bool wild;
    bool doubles;
    bool sceneryShown;  /* DioramaBattle_SceneryShown() */
    bool introSliding;  /* DioramaBattle_IntroSliding() */
    bool bg3Enabled;    /* DISPCNT BG3 on */
    bool forcedBlank;   /* DISPCNT forced blank */
    bool surfing;
    bool transition;    /* DioramaBattle_TransitionActive(), outside battles */
    /* During a transition, the mosaic on the field's layers (the Blur
     * transition's), REG_MOSAIC's BG sizes: 0 none, n blocks of n + 1. */
    uint8_t fieldMosaicH, fieldMosaicV;
    uint8_t playerFacing; /* DIR_* */
    int16_t playerMapX, playerMapY; /* map coordinates, without MAP_OFFSET */
    /* The same in the Diorama world's cell coordinates: the game's map grid,
     * MAP_OFFSET included (the snapshot's cells and camera, the terrain). */
    int16_t playerGridX, playerGridY;
    uint8_t mapGroup, mapNum;
    uint16_t mapLayoutId;
    uint32_t battleTypeFlags;
    /* BG3's scroll from rest, GBA pixels (a move shaking the scenery). */
    float sceneryShiftX, sceneryShiftY;
    /* The scenery's palettes (BG 2-4) faded towards a colour, whatever does
     * it: a palette fade, or a move blending the background on its own. */
    float fadeAmount;
    float fadeRgb[3];
    uint8_t shadowCount;
    struct DioramaBattleShadow shadows[DIORAMA_BATTLE_MAX_SHADOWS];
    struct DioramaBattleBattlerSprite battlers[DIORAMA_BATTLE_MAX_BATTLERS];
    uint8_t battleEnvironment; /* gBattleEnvironment (BATTLE_ENVIRONMENT_*) */
    uint8_t mapType;           /* gMapHeader.mapType */
};

/* Reads the game: only at the frame boundary, while the game thread is
 * parked (sdl2.c VDraw). */
void DioramaBattle_Capture(struct DioramaBattleFrame *frame);
/* Behaviour of a metatile, as the battle sees it (enum DioramaBattleGround,
 * never VOID). */
uint8_t DioramaBattle_GroundKind(uint8_t behavior);
/* The field's tile animations (tileset_anims.c: water, flowers...) as they
 * would run `frames` frames after the field stopped, their tiles copied into
 * `tiles` (BG VRAM's first `size` bytes, as the scene snapshot holds them)
 * instead of VRAM. Only at the frame boundary, like DioramaBattle_Capture. */
void DioramaBattle_AnimateFieldTiles(uint8_t *tiles, uint32_t size, uint32_t frames);

/* ── The GBA renderer (gba_easy_draw.c) ── */

/* BGs (bit n = BGn) that the world stands in for in the frame being drawn;
 * 0 draws the GBA picture exactly as ever. Set before DrawFrame. */
extern uint8_t gDioramaSceneLayers;
/* Per pixel of that frame, how much of the world shows through it, 0..16
 * (16: only the world; 0: opaque; between: the GBA's own blend with the
 * layer the world stands in for). The pixel's colour is the rest of the GBA
 * blend, so final = colour + world * cover / 16 (premultiplied alpha). */
extern uint8_t gDioramaSceneCover[160 * 240];
/* Per pixel row of that frame, the backgrounds' colour (RGB555,
 * premultiplied) and cover at the picture's left and right edges - window
 * state and blending included, sprites left out - to carry the picture out
 * to the sides of a window wider than 3:2; and which layer that colour is
 * (0-3 BGn, 4 the backdrop). Recorded when gDioramaSceneLayers or
 * gDioramaSceneWideWanted is set. */
struct DioramaSceneEdge
{
    uint16_t color;
    uint8_t cover;
    uint8_t layer;
};
extern struct DioramaSceneEdge gDioramaSceneEdges[160][2];
/* OAM entries left out of the frame being drawn (bit n: entry n): the map's
 * sprites, while a battle transition is composed over the 3D field. Set
 * before DrawFrame, with gDioramaSceneLayers. */
extern uint32_t gDioramaSceneSkipObj[4];
/* Per pixel row of that frame, BG1's scroll less line 0's (x, y): how far a
 * transition moves the field's layers on that line (they move together). */
extern int16_t gDioramaSceneLineShift[160][2];
/* With gDioramaSceneArtWanted set before DrawFrame: per pixel of that frame,
 * what BG3 - the battle's scenery - would show (RGB555 | 0x8000; where it is
 * see-through, the backdrop colour) in its unfaded colours, its blending left
 * out. The 3D battle shows it where its world has nothing drawn, and fades it
 * with the world. */
extern uint16_t gDioramaSceneArt[160 * 240];
extern bool gDioramaSceneArtWanted;
/* With gDioramaSceneWideWanted set before DrawFrame (and the margins, in GBA
 * pixels: left of column 0, right of column 239, above row 0): how the screen
 * would go on there if it were that much larger - BG3 (a move's own
 * background) over the backdrop: the part of its tilemap the move filled
 * (DioramaBattle_MoveBgMapSize) repeated where it repeats without a seam,
 * mirrored at the picture's edges where it would show one; with BG3's scroll
 * on each row, the frame's palette and BG3's and the backdrop's brightness and
 * blending as the GBA applies them, the window state of the nearest column of
 * the picture. The other layers (the text box, the battlers, a move's effects)
 * stay inside the picture. RGB555 rows of left + 240 + right pixels, top + 160
 * rows; row 0 is GBA row -top, column 0 GBA column -left (inside the picture:
 * unused). */
#define DIORAMA_WIDE_MAX_SIDE 256
#define DIORAMA_WIDE_MAX_TOP 96
#define DIORAMA_WIDE_MAX_W (DIORAMA_WIDE_MAX_SIDE + 240 + DIORAMA_WIDE_MAX_SIDE)
#define DIORAMA_WIDE_MAX_H (DIORAMA_WIDE_MAX_TOP + 160)
extern bool gDioramaSceneWideWanted;
extern uint16_t gDioramaSceneWideLeft;
extern uint16_t gDioramaSceneWideRight;
extern uint16_t gDioramaSceneWideTop;
extern uint16_t gDioramaSceneWide[DIORAMA_WIDE_MAX_H * DIORAMA_WIDE_MAX_W];

#endif // GUARD_DIORAMA_BATTLE_SCENE_H
