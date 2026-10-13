#ifdef ENABLE_DIORAMA
/*
 * The 3D scene's render target: see diorama/gl_target.h. Project-owned.
 */
#include <SDL2/SDL.h>

#include "diorama/gl_loader.h"
#include "diorama/gl_target.h"

static int sWantHeight;
static int sWantSamples;
static int sScalePercent = 100;
static bool sFailed;           /* the offscreen target could not be made: the window */
static bool sActive;           /* drawing into the offscreen target now */
static GLuint sFramebuffer;
static GLuint sColor;
static GLuint sDepthStencil;
static GLuint sResolveFramebuffer; /* multisampled and scaled: resolved here first */
static GLuint sResolveColor;
static int sWidth;
static int sHeight;
static int sSamples;
static int sLastWidth;
static int sLastHeight;
static int sLastSamples;

static void Release(void)
{
    if (sFramebuffer != 0)
        dglDeleteFramebuffers(1, &sFramebuffer);
    if (sResolveFramebuffer != 0)
        dglDeleteFramebuffers(1, &sResolveFramebuffer);
    if (sColor != 0)
        dglDeleteRenderbuffers(1, &sColor);
    if (sDepthStencil != 0)
        dglDeleteRenderbuffers(1, &sDepthStencil);
    if (sResolveColor != 0)
        dglDeleteRenderbuffers(1, &sResolveColor);
    sFramebuffer = sResolveFramebuffer = sColor = sDepthStencil = sResolveColor = 0;
    sWidth = sHeight = sSamples = 0;
}

void DioramaGLTarget_Configure(int renderHeight, int samples)
{
    if (renderHeight != sWantHeight || samples != sWantSamples)
        sFailed = false; /* a new choice may work where the last did not */
    sWantHeight = renderHeight > 0 ? renderHeight : 0;
    sWantSamples = samples > 1 ? samples : 0;
}

void DioramaGLTarget_SetScale(int percent)
{
    if (percent < 25)
        percent = 25;
    if (percent > 100)
        percent = 100;
    if (percent != sScalePercent)
        sFailed = false;
    sScalePercent = percent;
}

static bool Allocate(int width, int height, int samples)
{
    GLint maxSize = 0;
    GLint maxSamples = 0;
    GLenum status;

    Release();
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxSize);
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    if (maxSize > 0 && (width > maxSize || height > maxSize))
    {
        SDL_Log("3D view: %dx%d is larger than this GPU's %d; drawn at the window's size",
                width, height, (int)maxSize);
        return false;
    }
    if (samples > maxSamples)
        samples = maxSamples > 1 ? maxSamples : 0;
    dglGenFramebuffers(1, &sFramebuffer);
    dglGenRenderbuffers(1, &sColor);
    dglGenRenderbuffers(1, &sDepthStencil);
    dglBindRenderbuffer(GL_RENDERBUFFER, sColor);
    if (samples > 0)
        dglRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
    else
        dglRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    dglBindRenderbuffer(GL_RENDERBUFFER, sDepthStencil);
    if (samples > 0)
        dglRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);
    else
        dglRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    dglBindRenderbuffer(GL_RENDERBUFFER, 0);
    dglBindFramebuffer(GL_FRAMEBUFFER, sFramebuffer);
    dglFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, sColor);
    dglFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, sDepthStencil);
    status = dglCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status == GL_FRAMEBUFFER_COMPLETE && samples > 0)
    {
        dglGenFramebuffers(1, &sResolveFramebuffer);
        dglGenRenderbuffers(1, &sResolveColor);
        dglBindRenderbuffer(GL_RENDERBUFFER, sResolveColor);
        dglRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
        dglBindRenderbuffer(GL_RENDERBUFFER, 0);
        dglBindFramebuffer(GL_FRAMEBUFFER, sResolveFramebuffer);
        dglFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, sResolveColor);
        status = dglCheckFramebufferStatus(GL_FRAMEBUFFER);
    }
    dglBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        SDL_Log("3D view: offscreen %dx%d (%d samples) unavailable (status 0x%x); drawn at the window's size",
                width, height, samples, (unsigned)status);
        Release();
        return false;
    }
    sWidth = width;
    sHeight = height;
    sSamples = samples;
    SDL_Log("3D view: drawn at %dx%d%s", width, height,
            samples > 0 ? (samples == 2 ? ", 2x MSAA" : samples == 4 ? ", 4x MSAA" : ", 8x MSAA") : "");
    return true;
}

void DioramaGLTarget_Begin(int outputWidth, int outputHeight, int *renderWidth, int *renderHeight)
{
    int width = outputWidth;
    int height = outputHeight;

    sActive = false;
    if (outputHeight > 0)
    {
        int wantHeight = sWantHeight > 0 ? sWantHeight : outputHeight;

        if (sScalePercent < 100)
        {
            wantHeight = (int)(((long long)wantHeight * sScalePercent + 50) / 100);
            if (wantHeight < 90)
                wantHeight = 90;
        }
        if (wantHeight != outputHeight)
        {
            height = wantHeight;
            width = (int)(((long long)outputWidth * height + outputHeight / 2) / outputHeight);
            if (width < 1)
                width = 1;
        }
    }
    if (!sFailed && (width != outputWidth || height != outputHeight || sWantSamples > 0))
    {
        if (sFramebuffer == 0 || width != sWidth || height != sHeight || sWantSamples != sSamples)
            sFailed = !Allocate(width, height, sWantSamples);
        if (!sFailed)
        {
            dglBindFramebuffer(GL_FRAMEBUFFER, sFramebuffer);
            glViewport(0, 0, width, height);
            glDisable(GL_SCISSOR_TEST);
            glStencilMask(0xFF);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
            sActive = true;
        }
    }
    if (!sActive)
    {
        width = outputWidth;
        height = outputHeight;
    }
    sLastWidth = width;
    sLastHeight = height;
    sLastSamples = sActive ? sSamples : 0;
    *renderWidth = width;
    *renderHeight = height;
}

unsigned DioramaGLTarget_Framebuffer(void)
{
    return sActive ? sFramebuffer : 0;
}

unsigned DioramaGLTarget_ReadableFramebuffer(unsigned current)
{
    if (!sActive || sSamples == 0 || current != sFramebuffer)
        return current;
    dglBindFramebuffer(GL_READ_FRAMEBUFFER, sFramebuffer);
    dglBindFramebuffer(GL_DRAW_FRAMEBUFFER, sResolveFramebuffer);
    dglBlitFramebuffer(0, 0, sWidth, sHeight, 0, 0, sWidth, sHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    dglBindFramebuffer(GL_DRAW_FRAMEBUFFER, sFramebuffer);
    return sResolveFramebuffer;
}

void DioramaGLTarget_End(int outputWidth, int outputHeight)
{
    GLuint source = sFramebuffer;

    if (!sActive)
        return;
    sActive = false;
    glDisable(GL_SCISSOR_TEST);
    if (sSamples > 0 && (sWidth != outputWidth || sHeight != outputHeight))
    {
        /* Multisamples resolve only at the same size: resolve, then scale. */
        dglBindFramebuffer(GL_READ_FRAMEBUFFER, sFramebuffer);
        dglBindFramebuffer(GL_DRAW_FRAMEBUFFER, sResolveFramebuffer);
        dglBlitFramebuffer(0, 0, sWidth, sHeight, 0, 0, sWidth, sHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        source = sResolveFramebuffer;
    }
    dglBindFramebuffer(GL_READ_FRAMEBUFFER, source);
    dglBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    dglBlitFramebuffer(0, 0, sWidth, sHeight, 0, 0, outputWidth, outputHeight, GL_COLOR_BUFFER_BIT,
                       sWidth == outputWidth && sHeight == outputHeight ? GL_NEAREST : GL_LINEAR);
    dglBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, outputWidth, outputHeight);
}

void DioramaGLTarget_GetLastSize(int *width, int *height, int *samples)
{
    *width = sLastWidth;
    *height = sLastHeight;
    *samples = sLastSamples;
}

void DioramaGLTarget_Shutdown(void)
{
    Release();
    sActive = false;
}
#endif // ENABLE_DIORAMA
