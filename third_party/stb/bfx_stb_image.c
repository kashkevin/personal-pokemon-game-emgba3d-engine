/* Battle FX's PNG reader (bfx_stb_image.h): stb_image v2.30, PNG only. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO_OVERRIDE
#include "stb_image.h"
#include "bfx_stb_image.h"

unsigned char *BfxStb_LoadPng(const char *path, int *width, int *height, int flipY)
{
    int n = 0;
    stbi_set_flip_vertically_on_load(flipY ? 1 : 0);
    return stbi_load(path, width, height, &n, 4);
}

void BfxStb_Free(void *pixels)
{
    stbi_image_free(pixels);
}

const char *BfxStb_Error(void)
{
    const char *e = stbi_failure_reason();
    return e ? e : "unknown";
}
