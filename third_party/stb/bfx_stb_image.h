#ifndef BFX_STB_IMAGE_H
#define BFX_STB_IMAGE_H
/* Battle FX's PNG reader: stb_image (public domain, this folder), PNG only,
 * built outside the game's preprocessing (Makefile_pc BFX_QJS_CFLAGS). */
/* RGBA8 pixels, row 0 first (flipY: last row first); free with BfxStb_Free. NULL on an error. */
unsigned char *BfxStb_LoadPng(const char *path, int *width, int *height, int flipY);
void BfxStb_Free(void *pixels);
const char *BfxStb_Error(void);
#endif
