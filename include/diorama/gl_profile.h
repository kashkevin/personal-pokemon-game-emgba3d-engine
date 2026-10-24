#ifndef GUARD_DIORAMA_GL_PROFILE_H
#define GUARD_DIORAMA_GL_PROFILE_H

/*
 * Renderer profiling and GPU memory accounting (performance work 2026-10-11,
 * project-owned).
 *
 * Always on, and cheap: the draw calls, vertices and instances the renderer
 * submits, the bytes it uploads, and the GPU memory it allocates (textures,
 * buffers, renderbuffers), counted through the function-like macros at the end
 * of this header. gl_loader.h includes it, so every renderer file (and any later
 * one, such as the 3D battle's) is counted while its call sites stay plain GL.
 *
 * On request (PKMN_DIORAMA_PROFILE=1, the benchmark): the CPU time of each part
 * of a frame and, with timer queries (core in OpenGL 3.3), its GPU time. GPU
 * timestamps are read back a few frames later, never waited for, so profiling
 * does not stall the pipeline it measures.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <SDL2/SDL_opengl.h>

enum DioramaProfileSection
{
    DIORAMA_PROF_FRAME,       /* DioramaGL_Present up to the swap */
    DIORAMA_PROF_TICK,        /* the main thread's part of a game frame (2D picture, interrupts) */
    DIORAMA_PROF_SYNC,        /* a new snapshot: atlas, then each renderer's Sync */
    DIORAMA_PROF_SYNC_ATLAS,  /* ... its parts (inside SYNC) */
    DIORAMA_PROF_SYNC_TERRAIN,
    DIORAMA_PROF_SYNC_OBJECTS,
    DIORAMA_PROF_SYNC_BUILDINGS,
    DIORAMA_PROF_SYNC_TREES,
    DIORAMA_PROF_SYNC_PROPS,
    DIORAMA_PROF_SHADOW,      /* the sun's depth map, when it is redrawn */
    DIORAMA_PROF_TERRAIN,
    DIORAMA_PROF_TREES,
    DIORAMA_PROF_PROPS,
    DIORAMA_PROF_BUILDINGS,
    DIORAMA_PROF_OBJECTS,
    DIORAMA_PROF_AIR,         /* dust, rays, bloom */
    DIORAMA_PROF_FIELD_LIGHT, /* a dark cave's light */
    DIORAMA_PROF_RESOLVE,     /* the offscreen scene into the window */
    DIORAMA_PROF_2D,          /* weather picture, 2D layers, UI, overlay */
    DIORAMA_PROF_SWAP,        /* SwapWindow (CPU: the driver's present; VSync waits here) */
    DIORAMA_PROF_COUNT
};

/* After the GL context and DioramaGL_LoadFunctions: optional entry points
 * (timer queries, glGetStringi) and the driver's memory extensions. */
void DioramaProfile_Init(void);
/* The section timers (counting is always on). PKMN_DIORAMA_PROFILE=1 starts
 * with them on; the benchmark turns them on. */
void DioramaProfile_SetTiming(bool on);
bool DioramaProfile_Timing(void);
/* Whether PKMN_DIORAMA_PROFILE=1 asked for the periodic log. */
bool DioramaProfile_LogRequested(void);
/* Sections do not nest except inside FRAME and the SYNC parts inside SYNC.
 * Unmatched calls are ignored. */
void DioramaProfile_Begin(enum DioramaProfileSection section);
void DioramaProfile_End(enum DioramaProfileSection section);
/* After the swap: closes this frame's counts and timers. */
void DioramaProfile_FrameDone(void);
/* A section's CPU time in the last frame done (0 when it did not run or
 * timing is off): PKMN_PERF_FRAME_LOG's columns. */
double DioramaProfile_LastFrameMs(enum DioramaProfileSection section);
const char *DioramaProfile_SectionName(enum DioramaProfileSection section);

#define DIORAMA_PROFILE_UPLOAD_SITES 5

struct DioramaProfileStats
{
    unsigned frames;
    double seconds;
    double cpuMs[DIORAMA_PROF_COUNT];   /* average per frame (TICK and SYNC: per frame too) */
    double gpuMs[DIORAMA_PROF_COUNT];   /* average per frame measured; < 0 when unknown */
    double cpuMsMax[DIORAMA_PROF_COUNT];
    unsigned gpuFrames;                 /* frames whose GPU times were read back */
    unsigned syncs;                     /* frames that synced a new snapshot */
    unsigned ticks;                     /* game frames run by the main thread */
    double drawsPerFrame;
    double verticesPerFrame;
    double instancesPerFrame;
    double textureUploadKBPerFrame;
    double bufferUploadKBPerFrame;
    /* The call sites that uploaded the most, largest first ("file.c:line"). */
    unsigned uploadSiteCount;
    struct
    {
        const char *site;
        double kbPerFrame;
        double callsPerFrame;
    } uploadSites[DIORAMA_PROFILE_UPLOAD_SITES];
};
/* What was counted since the last call (or since timing was turned on). */
void DioramaProfile_TakeStats(struct DioramaProfileStats *stats);
/* One line for the log: frame rate, CPU and GPU per section, draws, uploads. */
void DioramaProfile_FormatStats(const struct DioramaProfileStats *stats, char *buffer, size_t size);

struct DioramaGpuMemory
{
    uint64_t textureBytes;
    uint64_t bufferBytes;
    uint64_t renderbufferBytes;
    uint64_t peakBytes;                 /* the most the three summed to */
    unsigned textures;
    unsigned buffers;
    unsigned renderbuffers;
    /* What the driver says, in MB, -1 when it cannot: NVIDIA (GL_NVX_gpu_memory_info)
     * dedicated, total available and current available; AMD (GL_ATI_meminfo)
     * free texture memory as current available. */
    int driverDedicatedMB;
    int driverTotalMB;
    int driverAvailableMB;
};
void DioramaProfile_GpuMemory(struct DioramaGpuMemory *memory);
void DioramaProfile_FormatGpuMemory(const struct DioramaGpuMemory *memory, char *buffer, size_t size);

/* The counted calls (gl_profile.c). */
void DioramaProfile_DrawArrays(GLenum mode, GLint first, GLsizei count);
void DioramaProfile_DrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instances);
void DioramaProfile_DrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);
/* The uploads also take their call site (DIORAMA_PROFILE_SITE), for the log's
 * list of what uploads the most. */
void DioramaProfile_TexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width,
                               GLsizei height, GLint border, GLenum format, GLenum type,
                               const void *pixels, const char *site);
void DioramaProfile_TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei width,
                                  GLsizei height, GLenum format, GLenum type, const void *pixels,
                                  const char *site);
void DioramaProfile_BindTexture(GLenum target, GLuint texture);
void DioramaProfile_ActiveTexture(GLenum unit);
void DioramaProfile_DeleteTextures(GLsizei count, const GLuint *textures);
void DioramaProfile_BindBuffer(GLenum target, GLuint buffer);
void DioramaProfile_BufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage,
                               const char *site);
void DioramaProfile_DeleteBuffers(GLsizei count, const GLuint *buffers);
void DioramaProfile_BindRenderbuffer(GLenum target, GLuint renderbuffer);
void DioramaProfile_RenderbufferStorage(GLenum target, GLenum internalFormat, GLsizei width,
                                        GLsizei height);
void DioramaProfile_RenderbufferStorageMultisample(GLenum target, GLsizei samples,
                                                   GLenum internalFormat, GLsizei width,
                                                   GLsizei height);
void DioramaProfile_DeleteRenderbuffers(GLsizei count, const GLuint *renderbuffers);

#define DIORAMA_PROFILE_STRING2(x) #x
#define DIORAMA_PROFILE_STRING(x) DIORAMA_PROFILE_STRING2(x)
#define DIORAMA_PROFILE_SITE (__FILE__ ":" DIORAMA_PROFILE_STRING(__LINE__))

#ifndef DIORAMA_PROFILE_NO_WRAP
#define glDrawArrays(mode, first, count) DioramaProfile_DrawArrays(mode, first, count)
#define dglDrawArraysInstanced(mode, first, count, instances) \
    DioramaProfile_DrawArraysInstanced(mode, first, count, instances)
#define glDrawElements(mode, count, type, indices) DioramaProfile_DrawElements(mode, count, type, indices)
#define glTexImage2D(target, level, internalFormat, width, height, border, format, type, pixels) \
    DioramaProfile_TexImage2D(target, level, internalFormat, width, height, border, format, type, pixels, \
                              DIORAMA_PROFILE_SITE)
#define glTexSubImage2D(target, level, x, y, width, height, format, type, pixels) \
    DioramaProfile_TexSubImage2D(target, level, x, y, width, height, format, type, pixels, \
                                 DIORAMA_PROFILE_SITE)
#define glBindTexture(target, texture) DioramaProfile_BindTexture(target, texture)
#define dglActiveTexture(unit) DioramaProfile_ActiveTexture(unit)
#define glDeleteTextures(count, textures) DioramaProfile_DeleteTextures(count, textures)
#define dglBindBuffer(target, buffer) DioramaProfile_BindBuffer(target, buffer)
#define dglBufferData(target, size, data, usage) \
    DioramaProfile_BufferData(target, size, data, usage, DIORAMA_PROFILE_SITE)
#define dglDeleteBuffers(count, buffers) DioramaProfile_DeleteBuffers(count, buffers)
#define dglBindRenderbuffer(target, renderbuffer) DioramaProfile_BindRenderbuffer(target, renderbuffer)
#define dglRenderbufferStorage(target, internalFormat, width, height) \
    DioramaProfile_RenderbufferStorage(target, internalFormat, width, height)
#define dglRenderbufferStorageMultisample(target, samples, internalFormat, width, height) \
    DioramaProfile_RenderbufferStorageMultisample(target, samples, internalFormat, width, height)
#define dglDeleteRenderbuffers(count, renderbuffers) DioramaProfile_DeleteRenderbuffers(count, renderbuffers)
#endif

#endif
