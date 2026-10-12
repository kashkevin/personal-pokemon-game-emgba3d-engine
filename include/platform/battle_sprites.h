#ifndef GUARD_PLATFORM_BATTLE_SPRITES_H
#define GUARD_PLATFORM_BATTLE_SPRITES_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Animated battle sprites (native builds with ENABLE_BATTLE_SPRITES; the GBA
 * build has none of it). The Pokemon in a battle are drawn from the sprite
 * library's pack (battle-sprites.ebsp beside the program; format and loader
 * in platform/battle_sprite_library.h): Black/White-style animations in place
 * of the GBA pictures, wherever the pack holds the species' facing.
 *
 * The battle engine, its controllers, sprites and move animations are not
 * changed and draw as ever. The swap happens where the GBA picture is drawn
 * (gba_easy_draw.c): an OAM entry showing a battler's picture - its own sprite
 * or a move's copy of it - is drawn from the pack instead of from its tiles,
 * with the entry's position, priority, affine transform, flip, mosaic,
 * window and blending, and with its palette's fade or tint carried over to
 * the library sprite's colours; and so is the copy a move draws on BG1 or BG2
 * (the "monbg" commands). Everything else - healthboxes, the text box, move
 * effects, the 3D world - layers over and under it exactly as it did over and
 * under the GBA picture.
 *
 * Kept as the game's GBA sprite: a species or facing the pack does not hold
 * (permission-gated or missing art: Gen 6, Megas, Primals...), a shiny
 * without a library shiny, the Substitute doll, a battler whose palette is
 * changed in a way that is not a fade (then for as long as it is), and
 * anything outside the battle screen.
 *
 * Purely visual: nothing here writes game memory, uses the random number
 * generator or changes timing, so battle mechanics and multiplayer
 * synchronisation are untouched. The game-side hooks only record what the
 * game itself loaded (which species, personality and palette a battler's
 * picture shows); the frame is drawn from them at the frame boundary, while
 * the game thread is parked.
 *
 *   PKMN_BATTLE_SPRITES=0          GBA sprites everywhere (A/B comparison)
 *   PKMN_BATTLE_SPRITES_PACK=path  another pack
 *   PKMN_BATTLE_SPRITES_LOG=1      one line per battler look: pack or GBA, and why
 *   PKMN_BATTLE_SPRITES_TRACE=path per frame, what was drawn (JSON lines; tests)
 *   PKMN_BATTLE_SPRITES_DUMP=stem  with _DUMP_FROM/_DUMP_EVERY/_DUMP_COUNT (battle
 *                                  frames): the 240x160 picture as stem-NNNN.png (tests)
 */

/* ── Game thread: what a battler's picture shows (battle_gfx_sfx_util.c, battle_anim.c,
 *    battle_main.c). `pic` is the decompressed 64x64 picture (frame 0 first), `palette`
 *    its 16 colours before any tint. ── */
void BattleSprites_NoteMon(uint32_t battler, uint32_t species, uint32_t personality, bool shiny,
                           const uint16_t *palette, const void *pic);
void BattleSprites_NoteSubstitute(uint32_t battler);
/* A move copied the battler's picture onto BG1 or BG2 (MoveBattlerSpriteToBG): the copy
 * is the 8x8 tiles from tilesOffset at the layer's map origin, in BG palette paletteId. */
void BattleSprites_NoteMonBg(uint32_t battler, uint32_t bgId, uint32_t tilesOffset, uint32_t paletteId);
void BattleSprites_NoteMonBgCleared(uint32_t bgId);
/* A fainting foe sinks into the ground: nothing of it is drawn from screen row lineY down
 * (the GBA erases its picture's rows there as it moves down). */
void BattleSprites_NoteSink(uint32_t battler, int32_t lineY);

/* ── Main thread, the game thread parked ── */
/* sprite.c LoadOam: the state that goes with the OAM, palette and VRAM just loaded. */
void BattleSprites_LatchOam(void);
/* gba_easy_draw.c DrawFrame: decides this frame's swaps; EndFrame sees the picture. */
void BattleSprites_BeginFrame(void);
void BattleSprites_EndFrame(const uint16_t *pixels);

/* ── The renderer (gba_easy_draw.c) ── */
struct BattleSpriteView
{
    const uint8_t *pixels;   /* the frame shown: palette indices, row-major, index 0 see-through */
    uint32_t stride;         /* bytes per row of pixels */
    int16_t width, height;
    int16_t left, top;       /* the frame's top-left in the coordinates of the GBA picture's
                              * 64x64 box (the OAM entry's texture space) */
    int16_t clipBottom;      /* screen row from which nothing is drawn, or INT16_MAX */
    const uint16_t *colors;  /* RGB555 per palette index, faded as the battler's palette is */
};

/* Per OAM entry, the library sprite drawn in its place this frame (NULL: drawn as ever). */
extern const struct BattleSpriteView *gBattleSpriteObjViews[128];
/* Per BG, the battler copy on it (BG1, BG2; NULL: none), its tiles and palette. */
struct BattleSpriteBgCopy
{
    const struct BattleSpriteView *view;
    uint16_t firstTile;
    uint8_t paletteId;
};
extern struct BattleSpriteBgCopy gBattleSpriteBgCopies[4];

/*
 * What of the library sprites lies above the picture's top edge, for the 3D battle to draw where
 * its window shows the world above the 240x160 picture (gl_battle_sprites.c): a Pokemon taller
 * than the GBA's 64 pixels keeps its head there. Each is drawn only in the rows its battler
 * covers at rest (from restTop to the picture's top), within the picture's columns: a sprite
 * moving off the screen (a slide-in, Fly) leaves it as the GBA picture shows it leave.
 */
#define BATTLE_SPRITE_OVERFLOW_MAX 8
struct BattleSpriteOverflow
{
    const struct BattleSpriteView *view;
    bool affine, flipX, flipY;
    int16_t x, y;            /* non-affine: the 64x64 box's top-left on the screen; affine: its centre */
    int16_t pa, pb, pc, pd;  /* the texture matrix (8.8), affine only */
    int16_t restTop;         /* the topmost screen row it may be drawn on (< 0: above the picture) */
    uint8_t alpha;           /* 0..16: a semi-transparent entry's EVA */
    int8_t brightness;       /* the frame's brightness change of sprites: +n/16 whiter, -n/16 darker */
};
extern struct BattleSpriteOverflow gBattleSpriteOverflow[BATTLE_SPRITE_OVERFLOW_MAX];
extern unsigned gBattleSpriteOverflowCount; /* back to front */
extern uint32_t gBattleSpriteOverflowSerial; /* new with every picture: their frames may have changed */

#endif // GUARD_PLATFORM_BATTLE_SPRITES_H
