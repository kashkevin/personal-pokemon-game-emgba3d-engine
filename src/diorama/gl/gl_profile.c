#ifdef ENABLE_DIORAMA
/*
 * Renderer profiling and GPU memory accounting (see diorama/gl_profile.h).
 * Project-owned (performance work, 2026-10-11).
 */
#define DIORAMA_PROFILE_NO_WRAP
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "diorama/gl_loader.h"
#include "diorama/gl_profile.h"

#ifndef GL_TIMESTAMP
#define GL_TIMESTAMP 0x8E28
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif
#ifndef GL_NUM_EXTENSIONS
#define GL_NUM_EXTENSIONS 0x821D
#endif
#define GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX 0x9047
#define GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX 0x9048
#define GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX 0x9049
#define TEXTURE_FREE_MEMORY_ATI 0x87FC

typedef void (APIENTRY *QueryCounterProc)(GLuint id, GLenum target);
typedef void (APIENTRY *GenQueriesProc)(GLsizei n, GLuint *ids);
typedef void (APIENTRY *DeleteQueriesProc)(GLsizei n, const GLuint *ids);
typedef void (APIENTRY *GetQueryObjectivProc)(GLuint id, GLenum name, GLint *value);
typedef void (APIENTRY *GetQueryObjectui64vProc)(GLuint id, GLenum name, GLuint64 *value);
typedef const GLubyte *(APIENTRY *GetStringiProc)(GLenum name, GLuint index);

static QueryCounterProc sQueryCounter;
static GenQueriesProc sGenQueries;
static DeleteQueriesProc sDeleteQueries;
static GetQueryObjectivProc sGetQueryObjectiv;
static GetQueryObjectui64vProc sGetQueryObjectui64v;
static bool sHasTimer;
static bool sHasNvx;
static bool sHasAti;

/* GPU timestamps: two per section per frame, read back FRAMES_IN_FLIGHT frames later. */
#define FRAMES_IN_FLIGHT 4
static GLuint sQueries[FRAMES_IN_FLIGHT][DIORAMA_PROF_COUNT][2];
static bool sIssued[FRAMES_IN_FLIGHT][DIORAMA_PROF_COUNT][2];
static bool sQueriesMade;
static unsigned sSlot;

static bool sTiming;
static bool sLogRequested;
static Uint64 sCpuBegin[DIORAMA_PROF_COUNT];
static bool sOpen[DIORAMA_PROF_COUNT];
static double sCpuFrame[DIORAMA_PROF_COUNT];   /* this frame */
static bool sTouchedFrame[DIORAMA_PROF_COUNT];

/* The window since the last TakeStats. */
static unsigned sFrames;
static Uint64 sWindowStart;
static double sCpuSum[DIORAMA_PROF_COUNT];
static double sCpuMax[DIORAMA_PROF_COUNT];
static double sGpuSum[DIORAMA_PROF_COUNT];
static unsigned sGpuCount[DIORAMA_PROF_COUNT];
static unsigned sGpuFrames;
static unsigned sSyncs;
static unsigned sTicks;
static uint64_t sDraws;
static uint64_t sVertices;
static uint64_t sInstances;
static uint64_t sTextureUploadBytes;
static uint64_t sBufferUploadBytes;
/* Uploads by call site (the string's address: one per site). */
#define UPLOAD_SITES 64
static struct UploadSite
{
    const char *site;
    uint64_t bytes;
    uint32_t calls;
} sUploadSites[UPLOAD_SITES];
static unsigned sUploadSiteCount;

/* GPU memory: bytes per GL name, and what is bound where. */
struct NameTable
{
    uint64_t *bytes;
    unsigned capacity;
    unsigned live;
    uint64_t total;
};
static struct NameTable sTextures;
static struct NameTable sBuffers;
static struct NameTable sRenderbuffers;
static uint64_t sPeakBytes;
#define TEXTURE_UNITS 32
static GLuint sBoundTexture2D[TEXTURE_UNITS];
static unsigned sActiveUnit;
static GLuint sBoundArrayBuffer;
static GLuint sBoundOtherBuffer[8];
static GLenum sBoundOtherTarget[8];
static GLuint sBoundRenderbuffer;

static const char *const sSectionNames[DIORAMA_PROF_COUNT] = {
    [DIORAMA_PROF_FRAME] = "frame",
    [DIORAMA_PROF_TICK] = "tick",
    [DIORAMA_PROF_SYNC] = "sync",
    [DIORAMA_PROF_SYNC_ATLAS] = "s.atlas",
    [DIORAMA_PROF_SYNC_TERRAIN] = "s.terrain",
    [DIORAMA_PROF_SYNC_OBJECTS] = "s.objects",
    [DIORAMA_PROF_SYNC_BUILDINGS] = "s.buildings",
    [DIORAMA_PROF_SYNC_TREES] = "s.trees",
    [DIORAMA_PROF_SYNC_PROPS] = "s.props",
    [DIORAMA_PROF_SHADOW] = "shadow",
    [DIORAMA_PROF_TERRAIN] = "terrain",
    [DIORAMA_PROF_TREES] = "trees",
    [DIORAMA_PROF_PROPS] = "props",
    [DIORAMA_PROF_BUILDINGS] = "buildings",
    [DIORAMA_PROF_OBJECTS] = "objects",
    [DIORAMA_PROF_AIR] = "air",
    [DIORAMA_PROF_FIELD_LIGHT] = "fieldlight",
    [DIORAMA_PROF_RESOLVE] = "resolve",
    [DIORAMA_PROF_2D] = "2d",
    [DIORAMA_PROF_SWAP] = "swap",
};

const char *DioramaProfile_SectionName(enum DioramaProfileSection section)
{
    return section < DIORAMA_PROF_COUNT ? sSectionNames[section] : "?";
}

static bool HasExtension(GetStringiProc getStringi, const char *name)
{
    GLint count = 0;
    GLint i;

    if (getStringi == NULL)
        return false;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (i = 0; i < count; i++)
    {
        const GLubyte *extension = getStringi(GL_EXTENSIONS, (GLuint)i);

        if (extension != NULL && strcmp((const char *)extension, name) == 0)
            return true;
    }
    return false;
}

void DioramaProfile_Init(void)
{
    const char *value = SDL_getenv("PKMN_DIORAMA_PROFILE");
    GetStringiProc getStringi = (GetStringiProc)SDL_GL_GetProcAddress("glGetStringi");

    sQueryCounter = (QueryCounterProc)SDL_GL_GetProcAddress("glQueryCounter");
    sGenQueries = (GenQueriesProc)SDL_GL_GetProcAddress("glGenQueries");
    sDeleteQueries = (DeleteQueriesProc)SDL_GL_GetProcAddress("glDeleteQueries");
    sGetQueryObjectiv = (GetQueryObjectivProc)SDL_GL_GetProcAddress("glGetQueryObjectiv");
    sGetQueryObjectui64v = (GetQueryObjectui64vProc)SDL_GL_GetProcAddress("glGetQueryObjectui64v");
    sHasTimer = sQueryCounter != NULL && sGenQueries != NULL && sDeleteQueries != NULL
             && sGetQueryObjectiv != NULL && sGetQueryObjectui64v != NULL;
    sHasNvx = HasExtension(getStringi, "GL_NVX_gpu_memory_info");
    sHasAti = HasExtension(getStringi, "GL_ATI_meminfo");
    while (glGetError() != GL_NO_ERROR)
        ;
    sLogRequested = value != NULL && value[0] == '1';
    SDL_Log("Diorama: profiling %s; GPU timer queries %s; driver memory info %s",
            sLogRequested ? "on (PKMN_DIORAMA_PROFILE=1)" : "available", sHasTimer ? "yes" : "no",
            sHasNvx ? "NVIDIA" : sHasAti ? "AMD" : "none");
    DioramaProfile_SetTiming(sLogRequested);
}

static void ResetWindow(void)
{
    sFrames = 0;
    sWindowStart = SDL_GetPerformanceCounter();
    memset(sCpuSum, 0, sizeof(sCpuSum));
    memset(sCpuMax, 0, sizeof(sCpuMax));
    memset(sGpuSum, 0, sizeof(sGpuSum));
    memset(sGpuCount, 0, sizeof(sGpuCount));
    sGpuFrames = 0;
    sSyncs = 0;
    sTicks = 0;
    sDraws = sVertices = sInstances = 0;
    sTextureUploadBytes = sBufferUploadBytes = 0;
    sUploadSiteCount = 0;
}

static void CountUpload(const char *site, uint64_t bytes)
{
    unsigned i;

    if (!sTiming || site == NULL)
        return;
    for (i = 0; i < sUploadSiteCount; i++)
        if (sUploadSites[i].site == site)
            break;
    if (i == sUploadSiteCount)
    {
        if (sUploadSiteCount == UPLOAD_SITES)
            return;
        sUploadSites[i].site = site;
        sUploadSites[i].bytes = 0;
        sUploadSites[i].calls = 0;
        sUploadSiteCount++;
    }
    sUploadSites[i].bytes += bytes;
    sUploadSites[i].calls++;
}

void DioramaProfile_SetTiming(bool on)
{
    if (on && sHasTimer && !sQueriesMade)
    {
        sGenQueries(FRAMES_IN_FLIGHT * DIORAMA_PROF_COUNT * 2, &sQueries[0][0][0]);
        sQueriesMade = true;
    }
    if (on && !sTiming)
    {
        memset(sIssued, 0, sizeof(sIssued));
        memset(sOpen, 0, sizeof(sOpen));
        memset(sCpuFrame, 0, sizeof(sCpuFrame));
        memset(sTouchedFrame, 0, sizeof(sTouchedFrame));
        ResetWindow();
    }
    sTiming = on;
}

bool DioramaProfile_Timing(void)
{
    return sTiming;
}

bool DioramaProfile_LogRequested(void)
{
    return sLogRequested;
}

void DioramaProfile_Begin(enum DioramaProfileSection section)
{
    if (!sTiming || section >= DIORAMA_PROF_COUNT || sOpen[section])
        return;
    sOpen[section] = true;
    sCpuBegin[section] = SDL_GetPerformanceCounter();
    /* The tick and the swap run outside the frame's GL work: CPU only. */
    if (sQueriesMade && section != DIORAMA_PROF_TICK && section != DIORAMA_PROF_SWAP
     && !sIssued[sSlot][section][0])
    {
        sQueryCounter(sQueries[sSlot][section][0], GL_TIMESTAMP);
        sIssued[sSlot][section][0] = true;
    }
}

void DioramaProfile_End(enum DioramaProfileSection section)
{
    double ms;

    if (!sTiming || section >= DIORAMA_PROF_COUNT || !sOpen[section])
        return;
    sOpen[section] = false;
    ms = (double)(SDL_GetPerformanceCounter() - sCpuBegin[section]) * 1000.0
       / (double)SDL_GetPerformanceFrequency();
    sCpuFrame[section] += ms;
    sTouchedFrame[section] = true;
    if (section == DIORAMA_PROF_SYNC)
        sSyncs++;
    if (section == DIORAMA_PROF_TICK)
        sTicks++;
    if (sQueriesMade && sIssued[sSlot][section][0] && !sIssued[sSlot][section][1])
    {
        sQueryCounter(sQueries[sSlot][section][1], GL_TIMESTAMP);
        sIssued[sSlot][section][1] = true;
    }
}

/* The slot about to be reused: its timestamps, if the GPU has them. */
static void ReadSlot(unsigned slot)
{
    bool any = false;
    int section;

    for (section = 0; section < DIORAMA_PROF_COUNT; section++)
    {
        GLint ready = 0;
        GLuint64 begin;
        GLuint64 end;

        if (!sIssued[slot][section][0] || !sIssued[slot][section][1])
            continue;
        sGetQueryObjectiv(sQueries[slot][section][1], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (ready)
        {
            sGetQueryObjectui64v(sQueries[slot][section][0], GL_QUERY_RESULT, &begin);
            sGetQueryObjectui64v(sQueries[slot][section][1], GL_QUERY_RESULT, &end);
            if (end >= begin)
            {
                sGpuSum[section] += (double)(end - begin) / 1.0e6;
                sGpuCount[section]++;
                any = true;
            }
        }
    }
    if (any)
        sGpuFrames++;
    memset(sIssued[slot], 0, sizeof(sIssued[slot]));
}

/* PKMN_DIORAMA_PROFILE=1: a frame whose CPU work (frame, tick, swap) took
 * three times the running average and 12 ms or more is logged with its parts:
 * the hitches (a map crossing, a warp) the averages hide. */
static void LogSpike(void)
{
    static double sAverage;
    double total = sCpuFrame[DIORAMA_PROF_FRAME] + sCpuFrame[DIORAMA_PROF_TICK] + sCpuFrame[DIORAMA_PROF_SWAP];
    char line[512];
    size_t used;
    int section;

    if (sAverage > 0.0 && total >= 12.0 && total > 3.0 * sAverage)
    {
        used = (size_t)SDL_snprintf(line, sizeof(line), "%.1f ms (average %.1f):", total, sAverage);
        for (section = 0; section < DIORAMA_PROF_COUNT && used < sizeof(line); section++)
            if (sTouchedFrame[section] && sCpuFrame[section] >= 0.5)
                used += (size_t)SDL_snprintf(line + used, sizeof(line) - used, " %s %.1f",
                                             sSectionNames[section], sCpuFrame[section]);
        SDL_Log("profile: hitch %s", line);
    }
    sAverage = sAverage > 0.0 ? sAverage * 0.95 + total * 0.05 : total;
}

void DioramaProfile_FrameDone(void)
{
    int section;

    if (!sTiming)
        return;
    if (sLogRequested)
        LogSpike();
    for (section = 0; section < DIORAMA_PROF_COUNT; section++)
    {
        if (!sTouchedFrame[section])
            continue;
        sCpuSum[section] += sCpuFrame[section];
        if (sCpuFrame[section] > sCpuMax[section])
            sCpuMax[section] = sCpuFrame[section];
        sCpuFrame[section] = 0.0;
        sTouchedFrame[section] = false;
    }
    sFrames++;
    if (sQueriesMade)
    {
        sSlot = (sSlot + 1) % FRAMES_IN_FLIGHT;
        ReadSlot(sSlot);
    }
}

void DioramaProfile_TakeStats(struct DioramaProfileStats *stats)
{
    int section;
    double frames;

    memset(stats, 0, sizeof(*stats));
    stats->frames = sFrames;
    stats->seconds = (double)(SDL_GetPerformanceCounter() - sWindowStart) / (double)SDL_GetPerformanceFrequency();
    frames = sFrames > 0 ? (double)sFrames : 1.0;
    for (section = 0; section < DIORAMA_PROF_COUNT; section++)
    {
        stats->cpuMs[section] = sCpuSum[section] / frames;
        stats->cpuMsMax[section] = sCpuMax[section];
        stats->gpuMs[section] = sGpuCount[section] > 0 ? sGpuSum[section] / sGpuFrames : -1.0;
    }
    stats->gpuFrames = sGpuFrames;
    stats->syncs = sSyncs;
    stats->ticks = sTicks;
    stats->drawsPerFrame = sDraws / frames;
    stats->verticesPerFrame = sVertices / frames;
    stats->instancesPerFrame = sInstances / frames;
    stats->textureUploadKBPerFrame = sTextureUploadBytes / 1024.0 / frames;
    stats->bufferUploadKBPerFrame = sBufferUploadBytes / 1024.0 / frames;
    {
        bool taken[UPLOAD_SITES] = {false};
        unsigned i;

        while (stats->uploadSiteCount < DIORAMA_PROFILE_UPLOAD_SITES)
        {
            int best = -1;

            for (i = 0; i < sUploadSiteCount; i++)
                if (!taken[i] && (best < 0 || sUploadSites[i].bytes > sUploadSites[best].bytes))
                    best = (int)i;
            if (best < 0 || sUploadSites[best].bytes == 0)
                break;
            taken[best] = true;
            stats->uploadSites[stats->uploadSiteCount].site = sUploadSites[best].site;
            stats->uploadSites[stats->uploadSiteCount].kbPerFrame = sUploadSites[best].bytes / 1024.0 / frames;
            stats->uploadSites[stats->uploadSiteCount].callsPerFrame = sUploadSites[best].calls / frames;
            stats->uploadSiteCount++;
        }
    }
    ResetWindow();
}

void DioramaProfile_FormatStats(const struct DioramaProfileStats *stats, char *buffer, size_t size)
{
    size_t used;
    int section;

    used = (size_t)SDL_snprintf(buffer, size, "%.1f fps; cpu ms", stats->seconds > 0.0
                                ? stats->frames / stats->seconds : 0.0);
    for (section = 0; section < DIORAMA_PROF_COUNT && used < size; section++)
        if (stats->cpuMs[section] >= 0.005 || section == DIORAMA_PROF_FRAME)
            used += (size_t)SDL_snprintf(buffer + used, size - used, " %s %.2f", sSectionNames[section],
                                         stats->cpuMs[section]);
    if (used < size && stats->syncs > 0)
        used += (size_t)SDL_snprintf(buffer + used, size - used, " (sync max %.2f, %u syncs, %u ticks)",
                                     stats->cpuMsMax[DIORAMA_PROF_SYNC], stats->syncs, stats->ticks);
    if (used < size && stats->gpuFrames > 0)
    {
        used += (size_t)SDL_snprintf(buffer + used, size - used, "; gpu ms");
        for (section = 0; section < DIORAMA_PROF_COUNT && used < size; section++)
            if (stats->gpuMs[section] >= 0.005)
                used += (size_t)SDL_snprintf(buffer + used, size - used, " %s %.2f", sSectionNames[section],
                                             stats->gpuMs[section]);
    }
    if (used < size)
        used += (size_t)SDL_snprintf(buffer + used, size - used,
                     "; per frame %.0f draws, %.0fk vertices, %.0f instances, uploads %.1f KB textures %.1f KB buffers",
                     stats->drawsPerFrame, stats->verticesPerFrame / 1000.0, stats->instancesPerFrame,
                     stats->textureUploadKBPerFrame, stats->bufferUploadKBPerFrame);
    for (section = 0; section < (int)stats->uploadSiteCount && used < size; section++)
    {
        const char *site = stats->uploadSites[section].site;
        const char *slash = SDL_strrchr(site, '/');

        used += (size_t)SDL_snprintf(buffer + used, size - used, "%s %s %.1f KB x%.2f",
                                     section == 0 ? " (most:" : ",", slash != NULL ? slash + 1 : site,
                                     stats->uploadSites[section].kbPerFrame,
                                     stats->uploadSites[section].callsPerFrame);
        if (section + 1 == (int)stats->uploadSiteCount && used < size)
            used += (size_t)SDL_snprintf(buffer + used, size - used, ")");
    }
}

void DioramaProfile_GpuMemory(struct DioramaGpuMemory *memory)
{
    memset(memory, 0, sizeof(*memory));
    memory->textureBytes = sTextures.total;
    memory->bufferBytes = sBuffers.total;
    memory->renderbufferBytes = sRenderbuffers.total;
    memory->peakBytes = sPeakBytes;
    memory->textures = sTextures.live;
    memory->buffers = sBuffers.live;
    memory->renderbuffers = sRenderbuffers.live;
    memory->driverDedicatedMB = memory->driverTotalMB = memory->driverAvailableMB = -1;
    if (sHasNvx)
    {
        GLint value = 0;

        glGetIntegerv(GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX, &value);
        memory->driverDedicatedMB = value / 1024;
        glGetIntegerv(GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &value);
        memory->driverTotalMB = value / 1024;
        glGetIntegerv(GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &value);
        memory->driverAvailableMB = value / 1024;
    }
    else if (sHasAti)
    {
        GLint values[4] = {0};

        glGetIntegerv(TEXTURE_FREE_MEMORY_ATI, values);
        memory->driverAvailableMB = values[0] / 1024;
    }
}

void DioramaProfile_FormatGpuMemory(const struct DioramaGpuMemory *memory, char *buffer, size_t size)
{
    size_t used = (size_t)SDL_snprintf(buffer, size,
        "GPU memory allocated by the game %.1f MB (textures %.1f MB in %u, buffers %.1f MB in %u, "
        "render targets %.1f MB in %u; peak %.1f MB)",
        (memory->textureBytes + memory->bufferBytes + memory->renderbufferBytes) / 1048576.0,
        memory->textureBytes / 1048576.0, memory->textures, memory->bufferBytes / 1048576.0,
        memory->buffers, memory->renderbufferBytes / 1048576.0, memory->renderbuffers,
        memory->peakBytes / 1048576.0);

    if (used >= size)
        return;
    if (memory->driverDedicatedMB >= 0)
        SDL_snprintf(buffer + used, size - used, "; driver: %d MB dedicated, %d MB available now",
                     memory->driverDedicatedMB, memory->driverAvailableMB);
    else if (memory->driverAvailableMB >= 0)
        SDL_snprintf(buffer + used, size - used, "; driver: %d MB texture memory free",
                     memory->driverAvailableMB);
    else
        SDL_snprintf(buffer + used, size - used, "; the driver does not report video memory");
}

/* ---- counting ---------------------------------------------------------- */

static void SetBytes(struct NameTable *table, GLuint name, uint64_t bytes)
{
    uint64_t sum;

    if (name == 0)
        return;
    if (name >= table->capacity)
    {
        unsigned capacity = table->capacity ? table->capacity : 256;
        uint64_t *grown;

        while (capacity <= name)
            capacity *= 2;
        grown = realloc(table->bytes, capacity * sizeof(table->bytes[0]));
        if (grown == NULL)
            return;
        memset(grown + table->capacity, 0, (capacity - table->capacity) * sizeof(grown[0]));
        table->bytes = grown;
        table->capacity = capacity;
    }
    if (table->bytes[name] == 0 && bytes != 0)
        table->live++;
    else if (table->bytes[name] != 0 && bytes == 0)
        table->live--;
    table->total = table->total - table->bytes[name] + bytes;
    table->bytes[name] = bytes;
    sum = sTextures.total + sBuffers.total + sRenderbuffers.total;
    if (sum > sPeakBytes)
        sPeakBytes = sum;
}

/* Bytes per texel the GPU keeps for an internal format (RGB is padded to 4). */
static unsigned InternalBytes(GLint internalFormat)
{
    switch (internalFormat)
    {
    case GL_RED:
    case GL_R8:
        return 1;
    case GL_RG:
    case GL_RG8:
    case GL_R16F:
    case GL_DEPTH_COMPONENT16:
        return 2;
    case GL_RG16F:
    case GL_R32F:
    case GL_R11F_G11F_B10F:
    case GL_RGB10_A2:
    case GL_DEPTH_COMPONENT:
    case GL_DEPTH_COMPONENT24:
    case GL_DEPTH_COMPONENT32F:
    case GL_DEPTH24_STENCIL8:
        return 4;
    case GL_RGBA16F:
    case GL_RG32F:
    case GL_DEPTH32F_STENCIL8:
        return 8;
    case GL_RGBA32F:
        return 16;
    default:
        return 4;
    }
}

/* Bytes per pixel of the data a program hands over (format and type). */
static unsigned ClientBytes(GLenum format, GLenum type)
{
    unsigned channels;
    unsigned size;

    switch (format)
    {
    case GL_RED:
    case GL_DEPTH_COMPONENT:
        channels = 1;
        break;
    case GL_RG:
        channels = 2;
        break;
    case GL_RGB:
    case GL_BGR:
        channels = 3;
        break;
    default:
        channels = 4;
        break;
    }
    switch (type)
    {
    case GL_UNSIGNED_BYTE:
    case GL_BYTE:
        size = 1;
        break;
    case GL_UNSIGNED_SHORT:
    case GL_SHORT:
    case GL_HALF_FLOAT:
        size = 2;
        break;
    case GL_UNSIGNED_INT_8_8_8_8:
    case GL_UNSIGNED_INT_8_8_8_8_REV:
    case GL_UNSIGNED_INT_2_10_10_10_REV:
        return 4;
    case GL_UNSIGNED_SHORT_5_6_5:
    case GL_UNSIGNED_SHORT_5_5_5_1:
    case GL_UNSIGNED_SHORT_1_5_5_5_REV:
    case GL_UNSIGNED_SHORT_4_4_4_4:
        return 2;
    default:
        size = 4;
        break;
    }
    return channels * size;
}

void DioramaProfile_DrawArrays(GLenum mode, GLint first, GLsizei count)
{
    sDraws++;
    sVertices += (uint64_t)(count > 0 ? count : 0);
    sInstances++;
    glDrawArrays(mode, first, count);
}

void DioramaProfile_DrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instances)
{
    sDraws++;
    if (count > 0 && instances > 0)
    {
        sVertices += (uint64_t)count * (uint64_t)instances;
        sInstances += (uint64_t)instances;
    }
    dglDrawArraysInstanced(mode, first, count, instances);
}

void DioramaProfile_TexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width,
                               GLsizei height, GLint border, GLenum format, GLenum type,
                               const void *pixels, const char *site)
{
    if (target == GL_TEXTURE_2D && level == 0 && sActiveUnit < TEXTURE_UNITS)
        SetBytes(&sTextures, sBoundTexture2D[sActiveUnit],
                 (uint64_t)width * (uint64_t)height * InternalBytes(internalFormat));
    if (pixels != NULL)
    {
        uint64_t bytes = (uint64_t)width * (uint64_t)height * ClientBytes(format, type);

        sTextureUploadBytes += bytes;
        CountUpload(site, bytes);
    }
    glTexImage2D(target, level, internalFormat, width, height, border, format, type, pixels);
}

void DioramaProfile_TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei width,
                                  GLsizei height, GLenum format, GLenum type, const void *pixels,
                                  const char *site)
{
    if (pixels != NULL)
    {
        uint64_t bytes = (uint64_t)width * (uint64_t)height * ClientBytes(format, type);

        sTextureUploadBytes += bytes;
        CountUpload(site, bytes);
    }
    glTexSubImage2D(target, level, x, y, width, height, format, type, pixels);
}

void DioramaProfile_BindTexture(GLenum target, GLuint texture)
{
    if (target == GL_TEXTURE_2D && sActiveUnit < TEXTURE_UNITS)
        sBoundTexture2D[sActiveUnit] = texture;
    glBindTexture(target, texture);
}

void DioramaProfile_ActiveTexture(GLenum unit)
{
    sActiveUnit = unit - GL_TEXTURE0;
    dglActiveTexture(unit);
}

void DioramaProfile_DeleteTextures(GLsizei count, const GLuint *textures)
{
    GLsizei i;
    unsigned unit;

    for (i = 0; i < count; i++)
    {
        SetBytes(&sTextures, textures[i], 0);
        for (unit = 0; unit < TEXTURE_UNITS; unit++)
            if (sBoundTexture2D[unit] == textures[i])
                sBoundTexture2D[unit] = 0;
    }
    glDeleteTextures(count, textures);
}

void DioramaProfile_BindBuffer(GLenum target, GLuint buffer)
{
    int i;

    if (target == GL_ARRAY_BUFFER)
        sBoundArrayBuffer = buffer;
    else
    {
        for (i = 0; i < 8; i++)
            if (sBoundOtherTarget[i] == target || sBoundOtherTarget[i] == 0)
            {
                sBoundOtherTarget[i] = target;
                sBoundOtherBuffer[i] = buffer;
                break;
            }
    }
    dglBindBuffer(target, buffer);
}

static GLuint BoundBuffer(GLenum target)
{
    int i;

    if (target == GL_ARRAY_BUFFER)
        return sBoundArrayBuffer;
    for (i = 0; i < 8; i++)
        if (sBoundOtherTarget[i] == target)
            return sBoundOtherBuffer[i];
    return 0;
}

void DioramaProfile_BufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage,
                               const char *site)
{
    SetBytes(&sBuffers, BoundBuffer(target), (uint64_t)(size > 0 ? size : 0));
    if (data != NULL && size > 0)
    {
        sBufferUploadBytes += (uint64_t)size;
        CountUpload(site, (uint64_t)size);
    }
    dglBufferData(target, size, data, usage);
}

void DioramaProfile_DeleteBuffers(GLsizei count, const GLuint *buffers)
{
    GLsizei i;
    int j;

    for (i = 0; i < count; i++)
    {
        SetBytes(&sBuffers, buffers[i], 0);
        if (sBoundArrayBuffer == buffers[i])
            sBoundArrayBuffer = 0;
        for (j = 0; j < 8; j++)
            if (sBoundOtherBuffer[j] == buffers[i])
                sBoundOtherBuffer[j] = 0;
    }
    dglDeleteBuffers(count, buffers);
}

void DioramaProfile_BindRenderbuffer(GLenum target, GLuint renderbuffer)
{
    sBoundRenderbuffer = renderbuffer;
    dglBindRenderbuffer(target, renderbuffer);
}

void DioramaProfile_RenderbufferStorage(GLenum target, GLenum internalFormat, GLsizei width,
                                        GLsizei height)
{
    SetBytes(&sRenderbuffers, sBoundRenderbuffer,
             (uint64_t)width * (uint64_t)height * InternalBytes((GLint)internalFormat));
    dglRenderbufferStorage(target, internalFormat, width, height);
}

void DioramaProfile_RenderbufferStorageMultisample(GLenum target, GLsizei samples,
                                                   GLenum internalFormat, GLsizei width,
                                                   GLsizei height)
{
    SetBytes(&sRenderbuffers, sBoundRenderbuffer,
             (uint64_t)width * (uint64_t)height * InternalBytes((GLint)internalFormat)
             * (uint64_t)(samples > 1 ? samples : 1));
    dglRenderbufferStorageMultisample(target, samples, internalFormat, width, height);
}

void DioramaProfile_DeleteRenderbuffers(GLsizei count, const GLuint *renderbuffers)
{
    GLsizei i;

    for (i = 0; i < count; i++)
    {
        SetBytes(&sRenderbuffers, renderbuffers[i], 0);
        if (sBoundRenderbuffer == renderbuffers[i])
            sBoundRenderbuffer = 0;
    }
    dglDeleteRenderbuffers(count, renderbuffers);
}

#endif // ENABLE_DIORAMA
