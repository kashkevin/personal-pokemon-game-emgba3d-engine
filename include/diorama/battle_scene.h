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
/* Whether the intro is sliding the scenery in (BG1/BG2 hold its entry
 * picture meanwhile; the 3D battle has the camera glide in instead). */
uint8_t DioramaBattle_IntroSliding(void);

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
};

/* Reads the game: only at the frame boundary, while the game thread is
 * parked (sdl2.c VDraw). */
void DioramaBattle_Capture(struct DioramaBattleFrame *frame);
/* Behaviour of a metatile, as the battle sees it (enum DioramaBattleGround,
 * never VOID). */
uint8_t DioramaBattle_GroundKind(uint8_t behavior);

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
 * to the sides of a window wider than 3:2. */
struct DioramaSceneEdge
{
    uint16_t color;
    uint8_t cover;
};
extern struct DioramaSceneEdge gDioramaSceneEdges[160][2];

#endif // GUARD_DIORAMA_BATTLE_SCENE_H
