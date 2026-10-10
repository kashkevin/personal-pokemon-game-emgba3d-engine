#ifndef GUARD_DIORAMA_GL_LIGHT_H
#define GUARD_DIORAMA_GL_LIGHT_H

#include <stdbool.h>

#include "diorama/gl_loader.h"
#include "diorama/scene_snapshot.h"

/*
 * The 3DS project's outdoor light (pokeemerald-3Ds-dualscreen
 * 3ds_port/src/voxel/ctr_voxel.c: LightFor, FogLight, SetGrade; MIT, see
 * src/diorama/README.md): the scene is warm where the sun reaches it and cool
 * in shadow ("a shadow reads as cool light rather than as a dark hole"), and
 * the distance hazes towards a pale sky, both as the weather has it: a warmer
 * sun in sunshine, grey-blue light and more haze in rain, a pale veil in fog,
 * dust in ash and sand, cold and dim in a cave's fog. Indoors (a building or
 * a secret base) none of it. Not ported (yet): the sun dapples, rays, bloom
 * and motes.
 *
 * Here, as the 3DS vertex shader (voxel.v.pica) has it: a point's brightness
 * (its face's baked shade x the sun's shadow map) picks the tint, fully the
 * shade's at 0.62 or below, fully the sun's at 1, so a cast shadow and the
 * dark side of a wall both lean cool; the haze mixes in by view depth. In the
 * terrain, building and sprite shaders. The haze colour follows the palette's
 * night tint and fades (the terrain's palette line). PKMN_DIORAMA_LIGHT=0
 * turns it all off.
 */
struct DioramaLightUniforms
{
    GLint sunTint;
    GLint shadeTint;
    GLint haze;
    GLint hazeColor;
};

/* Once a frame, before drawing: the light of the snapshot's weather and map
 * (neutral indoors, or when not `enabled`: the terrain debug view). */
void DioramaGLLight_Update(const struct DioramaSceneSnapshot *snapshot, bool enabled);
bool DioramaGLLight_Locate(GLuint program, struct DioramaLightUniforms *uniforms);
/* This frame's light for a draw seen at `cameraPitch` (the haze starts at the
 * player's distance); neutral (no tint, no haze) for debug and shadow passes. */
void DioramaGLLight_Apply(const struct DioramaLightUniforms *uniforms, float cameraPitch);
void DioramaGLLight_ApplyNeutral(const struct DioramaLightUniforms *uniforms);

/* GLSL for a fragment shader: the uniforms, Light(brightness), the tinted
 * brightness to multiply a texel by, and Haze(colour, view depth). */
#define DIORAMA_GLSL_LIGHT \
    "uniform vec3 sunTint;\n" \
    "uniform vec3 shadeTint;\n" \
    "uniform vec3 haze;\n" /* start, per unit of depth, most */ \
    "uniform vec3 hazeColor;\n" \
    "vec3 Light(float brightness) {\n" \
    "  return brightness * mix(shadeTint, sunTint, clamp((brightness - 0.62) / 0.38, 0.0, 1.0));\n" \
    "}\n" \
    "vec3 Haze(vec3 c, float viewDepth) {\n" \
    "  return mix(c, hazeColor, clamp((viewDepth - haze.x) * haze.y, 0.0, haze.z));\n" \
    "}\n"

#endif // GUARD_DIORAMA_GL_LIGHT_H
