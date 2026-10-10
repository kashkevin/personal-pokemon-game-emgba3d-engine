#ifndef GUARD_DIORAMA_GL_QUALITY_H
#define GUARD_DIORAMA_GL_QUALITY_H

#include <stdbool.h>

/*
 * The player's graphics quality (the PC settings menu, pokeemerald.cfg; set by
 * src/platform/sdl2.c on the main thread): the sun shadow map's side (0 for no
 * shadows), the weather light with the sun dapples and the colour grade, and
 * the light in the air (dust, sun rays, bloom, mist banks). The defaults are
 * the look before these settings (2048, on, on). The PKMN_DIORAMA_SHADOWS,
 * _LIGHT, _GRADE and _AIR switches still turn a feature off for one run.
 */
void DioramaGLQuality_Set(int shadowMapSize, bool lighting, bool effects);
int DioramaGLQuality_ShadowMapSize(void);
bool DioramaGLQuality_Lighting(void);
bool DioramaGLQuality_Effects(void);

#endif // GUARD_DIORAMA_GL_QUALITY_H
