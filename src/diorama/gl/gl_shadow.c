#ifdef ENABLE_DIORAMA
/*
 * The sun's depth map (see diorama/gl_shadow.h).
 */
#include <math.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "diorama/gl_loader.h"
#include "diorama/gl_shadow.h"

#define SHADOW_SIZE 2048
/* Half the side of the square of ground the map covers, in tiles: the
 * camera sees about 15 tiles to each side and 20 ahead. */
#define SHADOW_HALF_EXTENT 30.0f
/* How far ahead of the camera target (north, +z) the square is centred. */
#define SHADOW_AHEAD 6.0f
#define SHADOW_EYE_DISTANCE 60.0f
#define SHADOW_FAR 140.0f

/* Towards the sun, in the scene's axes (x east, y up, z north): the 3DS
 * port's (-VOXEL_SUN_DX, 1, -VOXEL_SUN_DZ) with its z pointing south. */
static const float sSunX = -0.85f;
static const float sSunY = 1.0f;
static const float sSunZ = 0.55f;

static bool sEnabled;
static bool sReady;
static bool sActiveThisFrame;
static GLuint sFramebuffer;
static GLuint sDepthTexture;
static GLfloat sLightMatrix[16];

static void Normalize(float v[3])
{
    float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

    if (length > 0.0f)
    {
        v[0] /= length;
        v[1] /= length;
        v[2] /= length;
    }
}

static void Cross(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

/* out = a * b, column major */
static void Multiply(const GLfloat a[16], const GLfloat b[16], GLfloat out[16])
{
    int row;
    int column;
    int k;

    for (column = 0; column < 4; column++)
        for (row = 0; row < 4; row++)
        {
            float sum = 0.0f;

            for (k = 0; k < 4; k++)
                sum += a[k * 4 + row] * b[column * 4 + k];
            out[column * 4 + row] = sum;
        }
}

bool DioramaGLShadow_Init(void)
{
    const char *setting = SDL_getenv("PKMN_DIORAMA_SHADOWS");
    static const GLfloat border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    GLenum status;

    sReady = false;
    sEnabled = setting == NULL || setting[0] != '0';
    if (!sEnabled)
        return true;
    glGenTextures(1, &sDepthTexture);
    glBindTexture(GL_TEXTURE_2D, sDepthTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_SIZE, SHADOW_SIZE, 0,
                 GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    dglGenFramebuffers(1, &sFramebuffer);
    dglBindFramebuffer(GL_FRAMEBUFFER, sFramebuffer);
    dglFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sDepthTexture, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    status = dglCheckFramebufferStatus(GL_FRAMEBUFFER);
    dglBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK);
    glReadBuffer(GL_BACK);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        SDL_Log("Diorama shadows unavailable (framebuffer status 0x%x)", (unsigned)status);
        DioramaGLShadow_Shutdown();
        sEnabled = false;
        return false;
    }
    sReady = true;
    return true;
}

bool DioramaGLShadow_IsEnabled(void)
{
    return sEnabled && sReady;
}

bool DioramaGLShadow_Begin(float cameraX, float cameraZ)
{
    float sun[3] = {sSunX, sSunY, sSunZ};
    float up[3] = {0.0f, 1.0f, 0.0f};
    float forward[3];
    float right[3];
    float upward[3];
    float center[3] = {cameraX, 0.0f, cameraZ + SHADOW_AHEAD};
    float eye[3];
    float texel = 2.0f * SHADOW_HALF_EXTENT / SHADOW_SIZE;
    float cx;
    float cy;
    GLfloat view[16];
    GLfloat projection[16];

    sActiveThisFrame = false;
    if (!DioramaGLShadow_IsEnabled())
        return false;
    Normalize(sun);
    forward[0] = -sun[0];
    forward[1] = -sun[1];
    forward[2] = -sun[2];
    Cross(forward, up, right);
    Normalize(right);
    Cross(right, forward, upward);
    /* Keep the map's texels fixed on the ground as the camera moves: snap
     * the centre to whole texels across the sun's view, or every shadow edge
     * crawls as the player walks. */
    cx = center[0] * right[0] + center[1] * right[1] + center[2] * right[2];
    cy = center[0] * upward[0] + center[1] * upward[1] + center[2] * upward[2];
    cx = floorf(cx / texel) * texel;
    cy = floorf(cy / texel) * texel;
    {
        float along = center[0] * forward[0] + center[1] * forward[1] + center[2] * forward[2];

        center[0] = right[0] * cx + upward[0] * cy + forward[0] * along;
        center[1] = right[1] * cx + upward[1] * cy + forward[1] * along;
        center[2] = right[2] * cx + upward[2] * cy + forward[2] * along;
    }
    eye[0] = center[0] + sun[0] * SHADOW_EYE_DISTANCE;
    eye[1] = center[1] + sun[1] * SHADOW_EYE_DISTANCE;
    eye[2] = center[2] + sun[2] * SHADOW_EYE_DISTANCE;
    memset(view, 0, sizeof(view));
    view[0] = right[0];  view[4] = right[1];  view[8] = right[2];
    view[1] = upward[0]; view[5] = upward[1]; view[9] = upward[2];
    view[2] = -forward[0]; view[6] = -forward[1]; view[10] = -forward[2];
    view[12] = -(right[0] * eye[0] + right[1] * eye[1] + right[2] * eye[2]);
    view[13] = -(upward[0] * eye[0] + upward[1] * eye[1] + upward[2] * eye[2]);
    view[14] = forward[0] * eye[0] + forward[1] * eye[1] + forward[2] * eye[2];
    view[15] = 1.0f;
    memset(projection, 0, sizeof(projection));
    projection[0] = 1.0f / SHADOW_HALF_EXTENT;
    projection[5] = 1.0f / SHADOW_HALF_EXTENT;
    projection[10] = -2.0f / SHADOW_FAR;
    projection[14] = -1.0f;
    projection[15] = 1.0f;
    Multiply(projection, view, sLightMatrix);

    dglBindFramebuffer(GL_FRAMEBUFFER, sFramebuffer);
    glViewport(0, 0, SHADOW_SIZE, SHADOW_SIZE);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    sActiveThisFrame = true;
    return true;
}

void DioramaGLShadow_End(int outputWidth, int outputHeight)
{
    if (!sActiveThisFrame)
        return;
    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    dglBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, outputWidth, outputHeight);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, outputWidth, outputHeight);
    glDisable(GL_DEPTH_TEST);
}

const GLfloat *DioramaGLShadow_LightMatrix(void)
{
    return sLightMatrix;
}

bool DioramaGLShadow_BindForLighting(void)
{
    if (!sActiveThisFrame)
        return false;
    dglActiveTexture(GL_TEXTURE0 + DIORAMA_SHADOW_TEXTURE_UNIT);
    glBindTexture(GL_TEXTURE_2D, sDepthTexture);
    dglActiveTexture(GL_TEXTURE0);
    return true;
}

void DioramaGLShadow_Shutdown(void)
{
    if (sFramebuffer != 0)
        dglDeleteFramebuffers(1, &sFramebuffer);
    if (sDepthTexture != 0)
        glDeleteTextures(1, &sDepthTexture);
    sFramebuffer = 0;
    sDepthTexture = 0;
    sReady = false;
    sActiveThisFrame = false;
}
#endif // ENABLE_DIORAMA
