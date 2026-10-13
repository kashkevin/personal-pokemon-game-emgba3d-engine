#ifndef GUARD_DIORAMA_GL_LOADER_H
#define GUARD_DIORAMA_GL_LOADER_H

#include <stdbool.h>
#include <SDL2/SDL_opengl.h>

extern PFNGLACTIVETEXTUREPROC dglActiveTexture;
extern PFNGLATTACHSHADERPROC dglAttachShader;
extern PFNGLBINDBUFFERPROC dglBindBuffer;
extern PFNGLBINDFRAMEBUFFERPROC dglBindFramebuffer;
extern PFNGLBLITFRAMEBUFFERPROC dglBlitFramebuffer;
extern PFNGLCHECKFRAMEBUFFERSTATUSPROC dglCheckFramebufferStatus;
extern PFNGLDELETEFRAMEBUFFERSPROC dglDeleteFramebuffers;
extern PFNGLFRAMEBUFFERTEXTURE2DPROC dglFramebufferTexture2D;
extern PFNGLGENFRAMEBUFFERSPROC dglGenFramebuffers;
extern PFNGLUNIFORMMATRIX4FVPROC dglUniformMatrix4fv;
extern PFNGLBINDVERTEXARRAYPROC dglBindVertexArray;
extern PFNGLBUFFERDATAPROC dglBufferData;
extern PFNGLCOMPILESHADERPROC dglCompileShader;
extern PFNGLCREATEPROGRAMPROC dglCreateProgram;
extern PFNGLCREATESHADERPROC dglCreateShader;
extern PFNGLDELETEBUFFERSPROC dglDeleteBuffers;
extern PFNGLDELETEPROGRAMPROC dglDeleteProgram;
extern PFNGLDELETESHADERPROC dglDeleteShader;
extern PFNGLDELETEVERTEXARRAYSPROC dglDeleteVertexArrays;
extern PFNGLDRAWARRAYSINSTANCEDPROC dglDrawArraysInstanced;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC dglEnableVertexAttribArray;
extern PFNGLGENBUFFERSPROC dglGenBuffers;
extern PFNGLGENVERTEXARRAYSPROC dglGenVertexArrays;
extern PFNGLGETPROGRAMINFOLOGPROC dglGetProgramInfoLog;
extern PFNGLGETPROGRAMIVPROC dglGetProgramiv;
extern PFNGLGETSHADERINFOLOGPROC dglGetShaderInfoLog;
extern PFNGLGETSHADERIVPROC dglGetShaderiv;
extern PFNGLGETUNIFORMLOCATIONPROC dglGetUniformLocation;
extern PFNGLLINKPROGRAMPROC dglLinkProgram;
extern PFNGLSHADERSOURCEPROC dglShaderSource;
extern PFNGLUNIFORM1FPROC dglUniform1f;
extern PFNGLUNIFORM1IPROC dglUniform1i;
extern PFNGLUNIFORM2FPROC dglUniform2f;
extern PFNGLUNIFORM3FPROC dglUniform3f;
extern PFNGLUNIFORM4FPROC dglUniform4f;
extern PFNGLUSEPROGRAMPROC dglUseProgram;
extern PFNGLVERTEXATTRIBPOINTERPROC dglVertexAttribPointer;
extern PFNGLVERTEXATTRIBDIVISORPROC dglVertexAttribDivisor;
/* Renderbuffers: the 3D scene's own render resolution and multisampling
 * (gl_target.h). */
extern PFNGLGENRENDERBUFFERSPROC dglGenRenderbuffers;
extern PFNGLBINDRENDERBUFFERPROC dglBindRenderbuffer;
extern PFNGLRENDERBUFFERSTORAGEPROC dglRenderbufferStorage;
extern PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC dglRenderbufferStorageMultisample;
extern PFNGLFRAMEBUFFERRENDERBUFFERPROC dglFramebufferRenderbuffer;
extern PFNGLDELETERENDERBUFFERSPROC dglDeleteRenderbuffers;
/* Arrays of uniforms (the lamps, gl_lamps.h). */
extern PFNGLUNIFORM4FVPROC dglUniform4fv;

bool DioramaGL_LoadFunctions(void);

/* Every renderer file counts its draws, uploads and GPU allocations through
 * gl_profile.h's wrappers (included last, so they wrap the names declared above). */
#include "diorama/gl_profile.h"

#endif
