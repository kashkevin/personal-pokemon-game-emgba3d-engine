#ifndef GUARD_DIORAMA_GL_GRADE_H
#define GUARD_DIORAMA_GL_GRADE_H

/*
 * The 3DS project's colour grade (pokeemerald-3Ds-dualscreen
 * 3ds_port/src/voxel/voxel_grade.c, MIT; see src/diorama/README.md): in the 3D
 * view every colour of the scene is pushed away from its luma by 1.30. "The
 * GBA art is pastel, made for a dim, unlit screen; lit and shaded in 3D it
 * read flat and washed out." Contrast is left alone. The 3DS grades its
 * textures; here the shaders grade each texel before it is lit.
 *
 * PKMN_DIORAMA_GRADE=0 turns it off (saturation 1).
 */
float DioramaGLGrade_Saturation(void);

/* GLSL: `uniform float saturation;` and `vec3 Grade(vec3)`, for a fragment shader. */
#define DIORAMA_GLSL_GRADE \
    "uniform float saturation;\n" \
    "vec3 Grade(vec3 c) {\n" \
    "  float luma = dot(c, vec3(0.299, 0.587, 0.114));\n" \
    "  return clamp(vec3(luma) + (c - vec3(luma)) * saturation, 0.0, 1.0);\n" \
    "}\n"

#endif // GUARD_DIORAMA_GL_GRADE_H
