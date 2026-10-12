#ifndef GUARD_DIORAMA_GL_BATTLE_SPRITES_H
#define GUARD_DIORAMA_GL_BATTLE_SPRITES_H

/*
 * The animated battle sprites around the 3D battle's picture (platform/battle_sprites.h): the
 * window shows the world above and beside the 240x160 picture (gl_battle.c BattleLayout), and a
 * Pokemon taller or wider than the GBA's 64 pixels keeps its head and wings there instead of
 * losing them at the picture's edge. Inside the picture the sprites are part of the GBA picture
 * itself (gba_easy_draw.c).
 *
 * Called by DioramaGLBattle_Present after the picture, with its layout (window pixels: the
 * picture's top-left and its pixels per GBA pixel). Main thread, GL context current.
 */
void DioramaGLBattleSprites_Draw(int outW, int outH, float gameX, float gameY, float scale);

#endif // GUARD_DIORAMA_GL_BATTLE_SPRITES_H
