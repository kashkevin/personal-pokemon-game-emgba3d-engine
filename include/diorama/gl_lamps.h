#ifndef GUARD_DIORAMA_GL_LAMPS_H
#define GUARD_DIORAMA_GL_LAMPS_H

#include <stdbool.h>
#include <stdint.h>

#include "diorama/gl_loader.h"

/*
 * Lamp light (graphics overhaul, phase 4, project-owned): in the evening and
 * at night the street lamps (props.h DIORAMA_PROP_LIGHT) light the ground,
 * walls and characters round them with a warm pool. A few point lights - the
 * DIORAMA_MAX_LAMPS nearest the camera - with a smooth falloff and no shadows
 * of their own, added to what the sun and the night tint give (cheap: one
 * loop over a handful of uniforms, nothing when no lamp is lit). In the
 * terrain, building, tree, prop and sprite shaders.
 */
#define DIORAMA_MAX_LAMPS 16

struct DioramaLampUniforms
{
    GLint count;
    GLint places;
    GLint colour;
};

/* The lamps of the scene: positions (x, y, z of the light) and how far each
 * reaches, in tiles; how lit they are (0 by day, 1 at night). Kept until the
 * next call. */
void DioramaGLLamps_Set(const float (*places)[4], unsigned count, float strength);
/* The scene's centre this frame: the lamps nearest it are the ones lit. */
void DioramaGLLamps_SetFocus(float x, float z);
/* Lamps light only the field pass (set on for it, off after it): a 3D
 * battle's frame or a debug view has none. */
void DioramaGLLamps_SetActive(bool active);
float DioramaGLLamps_Strength(void);

/* How dark it is for lights to show: 1 at night, 0.55 in the evening, 0 by
 * day and wherever the sky does not reach (indoors, caves, under the sea).
 * mapType: gMapHeader.mapType; timeOfDay: gTimeOfDay. */
float DioramaGLLamps_NightFor(uint8_t mapType, uint8_t timeOfDay);
/* This frame's night (the compositor, from the scene): the buildings' windows
 * glow with it. */
void DioramaGLLamps_SetNight(float night);
/* The night for the pass being drawn: 0 outside the field pass. */
float DioramaGLLamps_Night(void);
bool DioramaGLLamps_Locate(GLuint program, struct DioramaLampUniforms *uniforms);
/* This frame's lamps into a program in use (none in a shadow or debug pass:
 * call DioramaGLLamps_ApplyNone). */
void DioramaGLLamps_Apply(const struct DioramaLampUniforms *uniforms);
void DioramaGLLamps_ApplyNone(const struct DioramaLampUniforms *uniforms);

/* GLSL for a fragment shader: the uniforms and Lamps(world position), the
 * light to add, as a colour to multiply the texel by. */
#define DIORAMA_GLSL_LAMPS \
    "uniform int lampCount;\n" \
    "uniform vec4 lampPlaces[16];\n" /* x, y, z, reach */ \
    "uniform vec3 lampColour;\n" \
    "vec3 Lamps(vec3 p) {\n" \
    "  float sum = 0.0;\n" \
    "  for (int i = 0; i < lampCount; i++) {\n" \
    "    vec3 d = p - lampPlaces[i].xyz;\n" \
    "    float f = clamp(1.0 - dot(d, d) / (lampPlaces[i].w * lampPlaces[i].w), 0.0, 1.0);\n" \
    "    sum += f * f;\n" \
    "  }\n" \
    "  return lampColour * min(sum, 1.5);\n" \
    "}\n"

#endif
