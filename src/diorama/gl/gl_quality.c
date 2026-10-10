#ifdef ENABLE_DIORAMA
/*
 * The player's graphics quality: see diorama/gl_quality.h. Project-owned.
 */
#include "diorama/gl_quality.h"

static int sShadowMapSize = 2048;
static bool sLighting = true;
static bool sEffects = true;

void DioramaGLQuality_Set(int shadowMapSize, bool lighting, bool effects)
{
    sShadowMapSize = shadowMapSize > 0 ? shadowMapSize : 0;
    sLighting = lighting;
    sEffects = effects;
}

int DioramaGLQuality_ShadowMapSize(void)
{
    return sShadowMapSize;
}

bool DioramaGLQuality_Lighting(void)
{
    return sLighting;
}

bool DioramaGLQuality_Effects(void)
{
    return sEffects;
}
#endif // ENABLE_DIORAMA
