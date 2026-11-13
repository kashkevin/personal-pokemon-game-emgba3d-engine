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
 * a secret base) none of it. And the sun dapples: soft patches of light and
 * shade drifting slowly the way the shadows fall, from a small tileable noise
 * texture laid over the world along the sun (MakeDapple, DappleUniforms).
 * The light in the air - the sun rays, the sunlit dust and the bloom - is
 * gl_air.h's, from the values here.
 *
 * Here, as the 3DS vertex shader (voxel.v.pica) has it: a point's brightness
 * (its face's baked shade x the sun's shadow map) picks the tint, fully the
 * shade's at 0.62 or below, fully the sun's at 1, so a cast shadow and the
 * dark side of a wall both lean cool; the haze mixes in by view depth. In the
 * terrain, building and sprite shaders. The haze colour follows the palette's
 * night tint and fades (the terrain's palette line). The dapples are
 * anchored to the world: a map crossing moves the map's origin, not the
 * pattern. At night (the game's clock; the 3DS has none) they are half as
 * strong, as the sun's shadows are. PKMN_DIORAMA_LIGHT=0 turns it all off.
 */
#define DIORAMA_LIGHT_TEXTURE_UNIT 4 /* the dapple pattern */
struct DioramaLightUniforms
{
    GLint sunTint;
    GLint shadeTint;
    GLint haze;
    GLint hazeColor;
    GLint dappleMap;
    GLint dapple;
    GLint dappleAnchor;
    GLint edgeFog;
    GLint edgeFogColor;
    GLint clearInner;
    GLint clearOuter;
    GLint clearFloor;
};

/* Once a frame, before drawing: the light of the snapshot's weather and map
 * (neutral indoors, or when not `enabled`: the terrain debug view). */
void DioramaGLLight_Update(const struct DioramaSceneSnapshot *snapshot, bool enabled);
bool DioramaGLLight_Locate(GLuint program, struct DioramaLightUniforms *uniforms);
/* This frame's light for a draw seen at `cameraPitch` (the haze starts at the
 * player's distance); neutral (no tint, no haze) for debug and shadow passes. */
void DioramaGLLight_Apply(const struct DioramaLightUniforms *uniforms, float cameraPitch);
void DioramaGLLight_ApplyNeutral(const struct DioramaLightUniforms *uniforms);
/* The fog at the scene's edge (graphics overhaul, phase 1): from `start` to
 * `end` tiles out from the scene's centre (the larger of the east and north
 * distances) everything fades into `colour`, the colour the picture is
 * cleared to past the cells, so no view ends at a board's edge. Set for each
 * field frame and turned off after it (a 3D battle's frame has none). */
void DioramaGLLight_SetEdgeFog(bool on, float centreX, float centreZ, float start, float end,
                               const float colour[3]);
/* A 3D battle's arena in a cramped room (battle_arena.h, KNOWN_ISSUES V10): everything inside `inner` and what
 * stands higher than `level` inside `outer` (each x0, z0, x1, z1 in the scene's x east, z north) is cleared away
 * from the terrain, the trees, the detail objects and the buildings (their shaders' Cleared). Set for the battle's
 * world and turned off after it, as the edge fog; Apply writes it into a program, ApplyNeutral writes none. */
void DioramaGLLight_SetClearing(bool on, const float inner[4], const float outer[4], float level);
/* The clearing now set (none: false), for a shader of its own (the battle's curtain). */
bool DioramaGLLight_GetClearing(float inner[4], float outer[4], float *level);
/* Writes the clearing into a program's uniforms, or none (`on` false): a draw that must stand in it - the arena
 * itself - turns it off for itself and back on after. */
void DioramaGLLight_ApplyClearing(const struct DioramaLightUniforms *uniforms, bool on);
/* This frame's light in the air, for gl_air.h. */
struct DioramaAirLight
{
    float rays;   /* the sun rays' strength at their brightest, 0 for none */
    float bloom;  /* the glow around the brightest parts */
    float motes;  /* the sunlit dust, at its brightest */
    float fadeScale[3], fadeOffset[3]; /* the palette's fade and night tint, per channel */
    float anchorX, anchorZ;            /* the dapples' world anchor (x east, z south) */
    GLuint pattern;                    /* the dapple texture, 0 when there is none */
    float fog;                         /* the game's fog, 0-1 (its sprites' blend) */
    bool cave;                         /* ... and it is a cave's */
};
void DioramaGLLight_GetAir(struct DioramaAirLight *air);
/* This frame's distance haze for a draw seen at `cameraPitch`: where it starts
 * (view depth), how much per unit of depth after, the most; and its colour. */
void DioramaGLLight_GetHaze(float cameraPitch, float haze[3], float colour[3]);
/* The 3DS port's DappleNoise: value noise repeating every `period` cells. */
float DioramaGLLight_Noise(float u, float v, int period, uint32_t seed);
/* Frees the dapple pattern (made again on the next Update). */
void DioramaGLLight_Shutdown(void);

/* GLSL for a fragment shader: Cleared(world position), whether a point is cleared away for a battle's arena
 * (DioramaGLLight_SetClearing; a box with x0 above x1 holds nothing). */
#define DIORAMA_GLSL_CLEARING \
    "uniform vec4 clearInner;\n" \
    "uniform vec4 clearOuter;\n" \
    "uniform float clearFloor;\n" \
    "bool Cleared(vec3 p) {\n" \
    "  return (p.x > clearInner.x && p.x < clearInner.z && p.z > clearInner.y && p.z < clearInner.w)\n" \
    "      || (p.y > clearFloor && p.x > clearOuter.x && p.x < clearOuter.z\n" \
    "          && p.z > clearOuter.y && p.z < clearOuter.w);\n" \
    "}\n"

/* GLSL for a fragment shader: the uniforms, Light(brightness), the tinted
 * brightness to multiply a texel by, Dapple(world position), the dapples'
 * factor there, Haze(colour, view depth) and Cleared(world position). */
#define DIORAMA_GLSL_LIGHT \
    DIORAMA_GLSL_CLEARING \
    "uniform vec3 sunTint;\n" \
    "uniform vec3 shadeTint;\n" \
    "uniform vec3 haze;\n" /* start, per unit of depth, most */ \
    "uniform vec3 hazeColor;\n" \
    "vec3 Light(float brightness) {\n" \
    "  return brightness * mix(shadeTint, sunTint, clamp((brightness - 0.62) / 0.38, 0.0, 1.0));\n" \
    "}\n" \
    "uniform sampler2D dappleMap;\n" \
    "uniform vec4 dapple;\n" /* low, high, drift along the sun (tiles), on */ \
    "uniform vec2 dappleAnchor;\n" /* the map's origin in the world (x east, z south) */ \
    "float Dapple(vec3 p) {\n" \
    "  if (dapple.w < 0.5) return 1.0;\n" \
    /* Slid along the sun by its height (a roof takes the patch the light \
     * passed through on its way to the ground), then turned to the sun. */ \
    "  vec2 g = vec2(p.x + 0.85 * p.y, 0.55 * p.y - p.z) + dappleAnchor;\n" \
    "  vec2 sun = vec2(0.839570, 0.543251);\n" \
    "  vec2 uv = vec2((dot(sun, g) - dapple.z) / 24.0, (sun.x * g.y - sun.y * g.x) / 16.8);\n" \
    "  return mix(dapple.x, dapple.y, texture(dappleMap, uv).r);\n" \
    "}\n" \
    "vec3 Haze(vec3 c, float viewDepth) {\n" \
    "  return mix(c, hazeColor, clamp((viewDepth - haze.x) * haze.y, 0.0, haze.z));\n" \
    "}\n" \
    "uniform vec4 edgeFog;\n" /* the scene's centre x, z; start, end (tiles) */ \
    "uniform vec3 edgeFogColor;\n" \
    "vec3 Haze(vec3 c, float viewDepth, vec3 p) {\n" \
    "  vec2 d = abs(p.xz - edgeFog.xy);\n" \
    "  float edge = smoothstep(edgeFog.z, edgeFog.w, max(d.x, d.y));\n" \
    "  return mix(Haze(c, viewDepth), edgeFogColor, edge);\n" \
    "}\n"

#endif // GUARD_DIORAMA_GL_LIGHT_H
