#ifdef RENDERER_EASY_DRAW
#include "global.h"
#include <stdbool.h>
#include "platform/dma.h"
#ifdef ENABLE_DIORAMA
#include "diorama/battle_scene.h"
#endif
#ifdef ENABLE_BATTLE_SPRITES
#include "platform/battle_sprites.h"
#endif

#define mosaicBGEffectX (REG_MOSAIC & 0xF)
#define mosaicBGEffectY ((REG_MOSAIC >> 4) & 0xF)
#define mosaicSpriteEffectX ((REG_MOSAIC >> 8) & 0xF)
#define mosaicSpriteEffectY ((REG_MOSAIC >> 12) & 0xF)
#define applyBGHorizontalMosaicEffect(x) (x - (x % (mosaicBGEffectX+1)))
#define applyBGVerticalMosaicEffect(y) (y - (y % (mosaicBGEffectY+1)))
#define applySpriteHorizontalMosaicEffect(x) (x - (x % (mosaicSpriteEffectX+1)))
#define applySpriteVerticalMosaicEffect(y) (y - (y % (mosaicSpriteEffectY+1)))

#define getAlphaBit(x) ((x >> 15) & 1)
#define getRedChannel(x) ((x >>  0) & 0x1F)
#define getGreenChannel(x) ((x >>  5) & 0x1F)
#define getBlueChannel(x) ((x >>  10) & 0x1F)
#define isbgEnabled(x) ((REG_DISPCNT >> 8) & 0xF) & (1 << x)

#define WINMASK_BG0    (1 << 0)
#define WINMASK_BG1    (1 << 1)
#define WINMASK_BG2    (1 << 2)
#define WINMASK_BG3    (1 << 3)
#define WINMASK_OBJ    (1 << 4)
#define WINMASK_CLR    (1 << 5)
#define WINMASK_WINOUT  (1 << 6)

extern void (*const gIntrTable[])(void);

#ifdef ENABLE_DIORAMA
/*
 * The 3D battle (include/diorama/battle_scene.h): the BGs in
 * gDioramaSceneLayers are the world's. Such a layer is drawn as an opaque
 * stand-in of colour 0 everywhere it is shown, so priorities, windows and
 * blending treat it as the GBA would treat the scenery it replaces, and each
 * pixel carries how much of the world shows through it (gDioramaSceneCover,
 * 0..16): the GBA blend A * EVA + B * EVB with B the world, or the world
 * brightened or darkened, is the colour computed here plus the world times
 * the cover. With no scene layer the picture is drawn exactly as before.
 */
uint8_t gDioramaSceneLayers;
uint8_t gDioramaSceneCover[DISPLAY_HEIGHT * DISPLAY_WIDTH];
struct DioramaSceneEdge gDioramaSceneEdges[DISPLAY_HEIGHT][2];
uint32_t gDioramaSceneSkipObj[4];
int16_t gDioramaSceneLineShift[DISPLAY_HEIGHT][2];
uint16_t gDioramaSceneArt[DISPLAY_HEIGHT * DISPLAY_WIDTH];
bool gDioramaSceneArtWanted;
bool gDioramaSceneWideWanted;
uint16_t gDioramaSceneWideLeft;
uint16_t gDioramaSceneWideRight;
uint16_t gDioramaSceneWideTop;
uint16_t gDioramaSceneWide[DIORAMA_WIDE_MAX_H * DIORAMA_WIDE_MAX_W];
static bool sWideWanted;         /* gDioramaSceneWideWanted, latched per frame */
static int sWideLeft;            /* its margins, latched and limited */
static int sWideRight;
static int sWideTop;
static int sWideStride;          /* sWideLeft + 240 + sWideRight */
static bool sSkipObj;            /* gDioramaSceneSkipObj has any bit set */
static bool sArtWanted;          /* gDioramaSceneArtWanted, latched per frame */
static const uint16_t *sBgPalette; /* RenderBGScanline's palette; NULL: PLTT */
extern u16 gPlttBufferUnfaded[];
static uint16_t sLineZeroScroll[2];
static uint8_t sSceneMask;      /* gDioramaSceneLayers, latched per frame */
static uint8_t sTargetCover;    /* cover of the last target B found */

static inline unsigned clampCoeff(unsigned value)
{
    return value > 16 ? 16 : value;
}

uint16_t *memsetu16(uint16_t *dst, uint16_t fill, size_t count);
#endif

struct scanlineData {
    uint16_t layers[4][DISPLAY_WIDTH];
    uint16_t spriteLayers[4][DISPLAY_WIDTH];
#ifdef ENABLE_DIORAMA
    uint8_t spriteCover[4][DISPLAY_WIDTH];
#endif
    uint16_t bgcnts[4];
    uint16_t winMask[DISPLAY_WIDTH];
    //priority bookkeeping
    char bgtoprio[4]; //background to priority
    char prioritySortedBgs[4][4];
    char prioritySortedBgsCount[4];
};

struct bgPriority {
    char priority;
    char subPriority;
};

static const uint16_t bgMapSizes[][2] =
{
    {32, 32},
    {64, 32},
    {32, 64},
    {64, 64},
};

static void RenderBGScanline(int bgNum, uint16_t control, uint16_t hoffs, uint16_t voffs, int lineNum, uint16_t *line)
{
    unsigned int charBaseBlock = (control >> 2) & 3;
    unsigned int screenBaseBlock = (control >> 8) & 0x1F;
    unsigned int bitsPerPixel = ((control >> 7) & 1) ? 8 : 4;
    unsigned int mapWidth = bgMapSizes[control >> 14][0];
    unsigned int mapHeight = bgMapSizes[control >> 14][1];
    unsigned int mapWidthInPixels = mapWidth * 8;
    unsigned int mapHeightInPixels = mapHeight * 8;

    uint8_t *bgtiles = (uint8_t *)BG_CHAR_ADDR(charBaseBlock);
    uint16_t *pal = (uint16_t *)PLTT;
#ifdef ENABLE_DIORAMA
    if (sBgPalette != NULL)
        pal = (uint16_t *)sBgPalette;
#endif
     
    if (control & BGCNT_MOSAIC)
        lineNum = applyBGVerticalMosaicEffect(lineNum);

    hoffs &= 0x1FF;
    voffs &= 0x1FF;

    for (unsigned int x = 0; x < DISPLAY_WIDTH; x++)
    {
        uint16_t *bgmap = (uint16_t *)BG_SCREEN_ADDR(screenBaseBlock);
        // adjust for scroll
        unsigned int xx;
        if (control & BGCNT_MOSAIC)
            xx = (applyBGHorizontalMosaicEffect(x) + hoffs) & 0x1FF;
        else
            xx = (x + hoffs) & 0x1FF;
        
        unsigned int yy = (lineNum + voffs) & 0x1FF;
        
        //if x or y go above 255 pixels it goes to the next screen base which are 0x400 WORDs long
        if (xx > 255 && mapWidthInPixels > 256) {
            bgmap += 0x400;
        }
        
        if (yy > 255 && mapHeightInPixels > 256) {
            //the width check is for 512x512 mode support, it jumps by two screen bases instead
            bgmap += (mapWidthInPixels > 256) ? 0x800 : 0x400;
        }
        
        //maximum width for bgtile block is 256
        xx &= 0xFF;
        yy &= 0xFF;

        unsigned int mapX = xx / 8;
        unsigned int mapY = yy / 8;
        uint16_t entry = bgmap[mapY * 32 + mapX];

        unsigned int tileNum = entry & 0x3FF;
        unsigned int paletteNum = (entry >> 12) & 0xF;
        
        unsigned int tileX = xx % 8;
        unsigned int tileY = yy % 8;

        // Flip if necessary
        if (entry & (1 << 10))
            tileX = 7 - tileX;
        if (entry & (1 << 11))
            tileY = 7 - tileY;

        uint16_t tileLoc = tileNum * (bitsPerPixel * 8);
        uint16_t tileLocY = tileY * bitsPerPixel;
        uint16_t tileLocX = tileX;
        if (bitsPerPixel == 4)
            tileLocX /= 2;

        uint8_t pixel = bgtiles[tileLoc + tileLocY + tileLocX];

        if (bitsPerPixel == 4) {
            if (tileX & 1)
                pixel >>= 4;
            else
                pixel &= 0xF;

            if (pixel != 0)
                line[x] = pal[16 * paletteNum + pixel] | 0x8000;
        }
        else {
            line[x] = pal[pixel] | 0x8000;
        }
    }
}

static inline uint32_t getBgX(int bgNumber)
{
    if (bgNumber == 2)
    {
        return REG_BG2X;
    }
    else if (bgNumber == 3)
    {
        return REG_BG3X;
    }
}

static inline uint32_t getBgY(int bgNumber)
{
    if (bgNumber == 2)
    {
        return REG_BG2Y;
    }
    else if (bgNumber == 3)
    {
        return REG_BG3Y;
    }
}

static inline uint16_t getBgPA(int bgNumber)
{
    if (bgNumber == 2)
    {
        return REG_BG2PA;
    }
    else if (bgNumber == 3)
    {
        return REG_BG3PA;
    }
}

static inline uint16_t getBgPB(int bgNumber)
{
    if (bgNumber == 2)
    {
        return REG_BG2PB;
    }
    else if (bgNumber == 3)
    {
        return REG_BG3PB;
    }
}

static inline uint16_t getBgPC(int bgNumber)
{
    if (bgNumber == 2)
    {
        return REG_BG2PC;
    }
    else if (bgNumber == 3)
    {
        return REG_BG3PC;
    }
}

static inline uint16_t getBgPD(int bgNumber)
{
    if (bgNumber == 2)
    {
        return REG_BG2PD;
    }
    else if (bgNumber == 3)
    {
        return REG_BG3PD;
    }
}

static void RenderRotScaleBGScanline(int bgNum, uint16_t control, uint16_t x, uint16_t y, int lineNum, uint16_t *line)
{
    vBgCnt *bgcnt = (vBgCnt *)&control;
    unsigned int charBaseBlock = bgcnt->charBaseBlock;
    unsigned int screenBaseBlock = bgcnt->screenBaseBlock;
    unsigned int mapWidth = 1 << (4 + (bgcnt->screenSize)); // number of tiles

    uint8_t *bgtiles = (uint8_t *)(VRAM_ + charBaseBlock * 0x4000);
    uint8_t *bgmap = (uint8_t *)(VRAM_ + screenBaseBlock * 0x800);
    uint16_t *pal = (uint16_t *)PLTT;

    if (control & BGCNT_MOSAIC)
        lineNum = applyBGVerticalMosaicEffect(lineNum);
    

    s16 pa = getBgPA(bgNum);
    s16 pb = getBgPB(bgNum);
    s16 pc = getBgPC(bgNum);
    s16 pd = getBgPD(bgNum);

    int sizeX = 128;
    int sizeY = 128;

    switch (bgcnt->screenSize)
    {
    case 0:
        break;
    case 1:
        sizeX = sizeY = 256;
        break;
    case 2:
        sizeX = sizeY = 512;
        break;
    case 3:
        sizeX = sizeY = 1024;
        break;
    }

    int maskX = sizeX - 1;
    int maskY = sizeY - 1;

    int yshift = ((control >> 14) & 3) + 4;

    /*int dx = pa & 0x7FFF;
    if (pa & 0x8000)
        dx |= 0xFFFF8000;
    int dmx = pb & 0x7FFF;
    if (pb & 0x8000)
        dmx |= 0xFFFF8000;
    int dy = pc & 0x7FFF;
    if (pc & 0x8000)
        dy |= 0xFFFF8000;
    int dmy = pd & 0x7FFF;
    if (pd & 0x8000)
        dmy |= 0xFFFF8000;*/

    s32 currentX = getBgX(bgNum);
    s32 currentY = getBgY(bgNum);
    //sign extend 28 bit number
    currentX = ((currentX & (1 << 27)) ? currentX | 0xF0000000 : currentX);
    currentY = ((currentY & (1 << 27)) ? currentY | 0xF0000000 : currentY);

    currentX += lineNum * pb;
    currentY += lineNum * pd;

    int realX = currentX;
    int realY = currentY;

    if (bgcnt->areaOverflowMode)
    {
        for (int x = 0; x < DISPLAY_WIDTH; x++)
        {
            int xxx = (realX >> 8) & maskX;
            int yyy = (realY >> 8) & maskY;

            int tile = bgmap[(xxx >> 3) + ((yyy >> 3) << yshift)];

            int tileX = xxx & 7;
            int tileY = yyy & 7;

            uint8_t pixel = bgtiles[(tile << 6) + (tileY << 3) + tileX];

            if (pixel != 0) {
                line[x] = pal[pixel] | 0x8000;
            }

            realX += pa;
            realY += pc;
        }
    }
    else
    {
        for (int x = 0; x < DISPLAY_WIDTH; x++)
        {
            int xxx = (realX >> 8);
            int yyy = (realY >> 8);

            if (xxx < 0 || yyy < 0 || xxx >= sizeX || yyy >= sizeY)
            {
                //line[x] = 0x80000000;
            }
            else
            {
                int tile = bgmap[(xxx >> 3) + ((yyy >> 3) << yshift)];

                int tileX = xxx & 7;
                int tileY = yyy & 7;

                uint8_t pixel = bgtiles[(tile << 6) + (tileY << 3) + tileX];

                if (pixel != 0) {
                    line[x] = pal[pixel] | 0x8000;
                }
            }
            realX += pa;
            realY += pc;
        }
    }
    //the only way i could figure out how to get accurate mosaic on affine bgs 
    //luckily i dont think pokemon emerald uses mosaic on affine bgs
    if (control & BGCNT_MOSAIC && mosaicBGEffectX > 0)
    {
        for (int x = 0; x < DISPLAY_WIDTH; x++)
        {
            uint16_t color = line[applyBGHorizontalMosaicEffect(x)];
            line[x] = color;
            
        }
    }
}

const u8 spriteSizes[][2] =
{
    {8, 16},
    {8, 32},
    {16, 32},
    {32, 64},
};

static uint16_t alphaBlendColor(uint16_t targetA, uint16_t targetB)
{
    unsigned int eva = REG_BLDALPHA & 0x1F;
    unsigned int evb = (REG_BLDALPHA >> 8) & 0x1F;
    // shift right by 4 = division by 16
    unsigned int r = ((getRedChannel(targetA) * eva) + (getRedChannel(targetB) * evb)) >> 4;
    unsigned int g = ((getGreenChannel(targetA) * eva) + (getGreenChannel(targetB) * evb)) >> 4;
    unsigned int b = ((getBlueChannel(targetA) * eva) + (getBlueChannel(targetB) * evb)) >> 4;
    
    if (r > 31)
        r = 31;
    if (g > 31)
        g = 31;
    if (b > 31)
        b = 31;

     return r | (g << 5) | (b << 10) | (1 << 15);
}

static uint16_t alphaBrightnessIncrease(uint16_t targetA)
{
    unsigned int evy = (REG_BLDY & 0x1F);
    unsigned int r = getRedChannel(targetA) + (31 - getRedChannel(targetA)) * evy / 16;
    unsigned int g = getGreenChannel(targetA) + (31 - getGreenChannel(targetA)) * evy / 16;
    unsigned int b = getBlueChannel(targetA) + (31 - getBlueChannel(targetA)) * evy / 16;
    
    if (r > 31)
        r = 31;
    if (g > 31)
        g = 31;
    if (b > 31)
        b = 31;
    
     return r | (g << 5) | (b << 10) | (1 << 15);
}

static uint16_t alphaBrightnessDecrease(uint16_t targetA)
{
    unsigned int evy = (REG_BLDY & 0x1F);
    unsigned int r = getRedChannel(targetA) - getRedChannel(targetA) * evy / 16;
    unsigned int g = getGreenChannel(targetA) - getGreenChannel(targetA) * evy / 16;
    unsigned int b = getBlueChannel(targetA) - getBlueChannel(targetA) * evy / 16;
    
    if (r > 31)
        r = 31;
    if (g > 31)
        g = 31;
    if (b > 31)
        b = 31;
    
     return r | (g << 5) | (b << 10) | (1 << 15);
}

//outputs the blended pixel in colorOutput, the prxxx are the bg priority and subpriority, pixelpos is pixel offset in scanline
static bool alphaBlendSelectTargetB(struct scanlineData* scanline, uint16_t* colorOutput, char prnum, char prsub, int pixelpos, bool spriteBlendEnabled)
{   
    //iterate trough every possible bg to blend with, starting from specified priorities from arguments
    for (unsigned int blndprnum = prnum; blndprnum <= 3; blndprnum++)
    {
        //check if sprite is available to blend with, if sprite blending is enabled
        if (spriteBlendEnabled == true && getAlphaBit(scanline->spriteLayers[blndprnum][pixelpos]) == 1)
        {
            *colorOutput = scanline->spriteLayers[blndprnum][pixelpos];
#ifdef ENABLE_DIORAMA
            sTargetCover = scanline->spriteCover[blndprnum][pixelpos];
#endif
            return true;
        }
            
        for (unsigned int blndprsub = prsub; blndprsub < scanline->prioritySortedBgsCount[blndprnum]; blndprsub++)
        {
            char currLayer = scanline->prioritySortedBgs[blndprnum][blndprsub];
            if (getAlphaBit( scanline->layers[currLayer][pixelpos] ) == 1 && REG_BLDCNT & ( 1 << (8 + currLayer)) && isbgEnabled(currLayer))
            {
                *colorOutput = scanline->layers[currLayer][pixelpos];
#ifdef ENABLE_DIORAMA
                sTargetCover = (sSceneMask & (1 << currLayer)) ? 16 : 0;
#endif
                return true;
            }
            //if we hit a non target layer we should bail
            if ( getAlphaBit( scanline->layers[currLayer][pixelpos] ) == 1 && isbgEnabled(currLayer) && prnum != blndprnum )
            {
                return false;
            }
        }
        prsub = 0; //start from zero in the next iteration
    }
    //no background got hit, check if backdrop is enabled and return it if enabled otherwise fail
    if (REG_BLDCNT & BLDCNT_TGT2_BD)
    {
        *colorOutput = *(uint16_t*)PLTT;
#ifdef ENABLE_DIORAMA
        sTargetCover = 0;
#endif
        return true;
    }
    else
    {
        return false;
    }
}

//checks if window horizontal is in bounds and takes account WIN wraparound
static bool winCheckHorizontalBounds(u16 left, u16 right, u16 xpos)
{
    if (left > right)
        return (xpos >= left || xpos < right);
    else
        return (xpos >= left && xpos < right);
}

#ifdef ENABLE_DIORAMA
// The backgrounds alone at column x (the composite below without sprites),
// for carrying the picture out past its edges (gDioramaSceneEdges).
static void SceneEdge(struct scanlineData *scanline, uint16_t backdrop, int x,
                      bool windowsEnabled, unsigned blendMode, struct DioramaSceneEdge *out)
{
    uint16_t color = backdrop;
    uint8_t cover = 0;
    uint8_t layer = 4;

    for (int prnum = 3; prnum >= 0; prnum--)
    {
        for (int prsub = scanline->prioritySortedBgsCount[prnum] - 1; prsub >= 0; prsub--)
        {
            char bgnum = scanline->prioritySortedBgs[prnum][prsub];
            uint16_t pixel = scanline->layers[bgnum][x];
            uint8_t pixelCover = (sSceneMask & (1 << bgnum)) ? 16 : 0;
            bool winEffectEnable = true;
            uint16_t targetB = 0;

            if (!isbgEnabled(bgnum) || !getAlphaBit(pixel))
                continue;
            if (windowsEnabled)
            {
                winEffectEnable = (scanline->winMask[x] & WINMASK_CLR) != 0;
                if (!(scanline->winMask[x] & (1 << bgnum)))
                    continue;
            }
            if (blendMode != 0 && REG_BLDCNT & (1 << bgnum) && winEffectEnable)
            {
                switch (blendMode)
                {
                case 1:
                    if (alphaBlendSelectTargetB(scanline, &targetB, prnum, prsub + 1, x, false))
                    {
                        pixel = alphaBlendColor(pixel, targetB);
                        pixelCover = clampCoeff((pixelCover * clampCoeff(REG_BLDALPHA & 0x1F)
                                                 + sTargetCover * clampCoeff((REG_BLDALPHA >> 8) & 0x1F)) >> 4);
                    }
                    break;
                case 2:
                    pixel = alphaBrightnessIncrease(pixel);
                    pixelCover = (pixelCover * (16 - clampCoeff(REG_BLDY & 0x1F))) >> 4;
                    break;
                case 3:
                    pixel = alphaBrightnessDecrease(pixel);
                    pixelCover = (pixelCover * (16 - clampCoeff(REG_BLDY & 0x1F))) >> 4;
                    break;
                }
            }
            color = pixel;
            cover = pixelCover;
            layer = bgnum;
        }
    }
    out->color = color & 0x7FFF;
    out->cover = cover;
    out->layer = layer;
}

static inline int FloorMod(int value, int modulus)
{
    int r = value % modulus;

    return r < 0 ? r + modulus : r;
}

// One pixel of a text BG at map pixel (mx, my), 0..511: its colour | 0x8000
// from `pal`, or 0 where it is see-through (RenderBGScanline's fetch).
static uint16_t BGPixel(uint16_t control, unsigned int mx, unsigned int my, const uint16_t *pal)
{
    unsigned int charBaseBlock = (control >> 2) & 3;
    unsigned int screenBaseBlock = (control >> 8) & 0x1F;
    unsigned int bitsPerPixel = ((control >> 7) & 1) ? 8 : 4;
    unsigned int mapWidthInPixels = bgMapSizes[control >> 14][0] * 8;
    unsigned int mapHeightInPixels = bgMapSizes[control >> 14][1] * 8;
    const uint8_t *bgtiles = (const uint8_t *)BG_CHAR_ADDR(charBaseBlock);
    const uint16_t *bgmap = (const uint16_t *)BG_SCREEN_ADDR(screenBaseBlock);
    uint16_t entry;
    unsigned int tileX, tileY, tileLoc;
    uint8_t pixel;

    mx &= 0x1FF;
    my &= 0x1FF;
    if (mx > 255 && mapWidthInPixels > 256)
        bgmap += 0x400;
    if (my > 255 && mapHeightInPixels > 256)
        bgmap += (mapWidthInPixels > 256) ? 0x800 : 0x400;
    mx &= 0xFF;
    my &= 0xFF;
    entry = bgmap[(my / 8) * 32 + mx / 8];
    tileX = mx % 8;
    tileY = my % 8;
    if (entry & (1 << 10))
        tileX = 7 - tileX;
    if (entry & (1 << 11))
        tileY = 7 - tileY;
    tileLoc = (entry & 0x3FF) * (bitsPerPixel * 8) + tileY * bitsPerPixel;
    if (bitsPerPixel == 4)
    {
        pixel = bgtiles[tileLoc + tileX / 2];
        pixel = (tileX & 1) ? pixel >> 4 : pixel & 0xF;
        return pixel != 0 ? (pal[16 * ((entry >> 12) & 0xF) + pixel] | 0x8000) : 0;
    }
    pixel = bgtiles[tileLoc + tileX];
    return pixel != 0 ? (pal[pixel] | 0x8000) : 0;
}

// x reflected into 0..n-1 at the edges (-1 -> 0, n -> n-1), as often as needed.
static int MirrorInto(int x, int n)
{
    int m = FloorMod(x, 2 * n);

    return m < n ? m : 2 * n - 1 - m;
}

// How the screen goes on past its edges for a move's background in BG3: the
// part of the tilemap the move filled (DioramaBattle_MoveBgMapSize - most fill
// the rows above the text box or the screen's, not the whole map, whose rest
// holds anything) repeated, where it repeats without a seam; mirrored at the
// picture's edges where it would show one. Decided once a load, from the
// background's own colours (the unfaded palette: a fade in progress would
// hide every seam).
static struct
{
    uint32_t loads;      /* DioramaBattle_MoveBgLoads() decided for */
    uint16_t control;    /* BG3CNT then */
    bool decided;
    int width, height;   /* the filled part, pixels: the repeat */
    bool mirrorX, mirrorY;
} sWideMap;

static unsigned int ColorDistance(uint16_t a, uint16_t b)
{
    int dr = (int)(a & 0x1F) - (int)(b & 0x1F);
    int dg = (int)((a >> 5) & 0x1F) - (int)((b >> 5) & 0x1F);
    int db = (int)((a >> 10) & 0x1F) - (int)((b >> 10) & 0x1F);

    return (unsigned int)(abs(dr) + abs(dg) + abs(db));
}

static uint16_t WideMapColor(uint16_t control, int mx, int my)
{
    uint16_t c = BGPixel(control, (unsigned int)mx, (unsigned int)my, gPlttBufferUnfaded);

    return getAlphaBit(c) ? c & 0x7FFF : gPlttBufferUnfaded[0] & 0x7FFF;
}

static void DecideWideMap(uint16_t control)
{
    unsigned int mapW = bgMapSizes[control >> 14][0], mapH = bgMapSizes[control >> 14][1];
    unsigned int entries = DioramaBattle_MoveBgMapSize() / 2;
    int w, h, shownW, shownH;
    double seamX = 0.0, innerX = 0.0, seamY = 0.0, innerY = 0.0;
    unsigned int n;

    if (entries == 0 || entries >= mapW * mapH)
    {
        w = mapW * 8;
        h = mapH * 8;
    }
    else if (entries <= 32 * 32)
    {
        w = 256;   /* the first screen block's rows, in order */
        h = (int)((entries + 31) / 32) * 8;
    }
    else
    {
        w = mapW * 8;
        h = (int)(entries / mapW) * 8;
    }
    if (h < 8)
        h = 8;
    shownW = w < DISPLAY_WIDTH ? w : DISPLAY_WIDTH;
    shownH = h < DISPLAY_HEIGHT ? h : DISPLAY_HEIGHT;
    /* Across the repeat's seam against between neighbours inside it. */
    n = 0;
    for (int y = 0; y < shownH; y++, n++)
    {
        seamX += ColorDistance(WideMapColor(control, w - 1, y), WideMapColor(control, 0, y));
        for (int x = 0; x + 1 < shownW; x++)
            innerX += ColorDistance(WideMapColor(control, x, y), WideMapColor(control, x + 1, y)) / (double)(shownW - 1);
    }
    seamX /= n;
    innerX /= n;
    n = 0;
    for (int x = 0; x < shownW; x++, n++)
    {
        seamY += ColorDistance(WideMapColor(control, x, h - 1), WideMapColor(control, x, 0));
        for (int y = 0; y + 1 < shownH; y++)
            innerY += ColorDistance(WideMapColor(control, x, y), WideMapColor(control, x, y + 1)) / (double)(shownH - 1);
    }
    seamY /= n;
    innerY /= n;
    sWideMap.width = w;
    sWideMap.height = h;
    sWideMap.mirrorX = seamX > innerX * 1.5 + 2.0;
    sWideMap.mirrorY = seamY > innerY * 1.5 + 2.0;
    sWideMap.control = control;
    sWideMap.loads = DioramaBattle_MoveBgLoads();
    sWideMap.decided = true;
}

// BG3 at columns x0..x1-1 of row lineNum - either may lie past the screen - as
// the screen would go on (sWideMap). See-through pixels are 0.
static void RenderBGRunWide(uint16_t control, uint16_t hoffs, uint16_t voffs, int lineNum,
                            int x0, int x1, uint16_t *out)
{
    const uint16_t *pal = (const uint16_t *)PLTT;
    bool mosaic = (control & BGCNT_MOSAIC) != 0;
    int sy = sWideMap.mirrorY ? MirrorInto(lineNum, DISPLAY_HEIGHT) : lineNum;
    unsigned int my;

    if (mosaic)
        sy -= FloorMod(sy, mosaicBGEffectY + 1);
    my = (unsigned int)FloorMod(sy + (int)(voffs & 0x1FF), sWideMap.height);
    for (int x = x0; x < x1; x++)
    {
        int sx = sWideMap.mirrorX ? MirrorInto(x, DISPLAY_WIDTH) : x;

        if (mosaic)
            sx -= FloorMod(sx, mosaicBGEffectX + 1);
        out[x - x0] = BGPixel(control, (unsigned int)FloorMod(sx + (int)(hoffs & 0x1FF), sWideMap.width), my, pal);
    }
}

// The wide picture's colour from BG3's pixel there (alpha bit clear: none),
// the window state that applies (WINMASK bits) and this row's backdrop
// colour (its brightness effect applied, as DrawFrame fills it).
static uint16_t WideColor(uint16_t bg3, unsigned int mask, uint16_t backdrop)
{
    unsigned int blendMode = (REG_BLDCNT >> 6) & 3;

    if (!getAlphaBit(bg3) || !(mask & WINMASK_BG3))
        return backdrop & 0x7FFF;
    if (blendMode != 0 && (REG_BLDCNT & BLDCNT_TGT1_BG3) && (mask & WINMASK_CLR))
    {
        switch (blendMode)
        {
        case 1:
            // Over the backdrop: nothing else reaches past the picture.
            if (REG_BLDCNT & BLDCNT_TGT2_BD)
                bg3 = alphaBlendColor(bg3, *(uint16_t *)PLTT);
            break;
        case 2:
            bg3 = alphaBrightnessIncrease(bg3);
            break;
        case 3:
            bg3 = alphaBrightnessDecrease(bg3);
            break;
        }
    }
    return bg3 & 0x7FFF;
}

// Columns x0..x1-1 of row lineNum of the wide picture into out: the window
// state of maskRow's column for columns inside the picture (maskRow NULL:
// none), of `mask` for the others.
static void WideRun(uint16_t *out, int x0, int x1, int lineNum, unsigned int mask,
                    const uint16_t *maskRow, uint16_t backdrop)
{
    static uint16_t bg3[DIORAMA_WIDE_MAX_W];
    bool shown = (REG_DISPCNT & 3) == 0 && (isbgEnabled(3)) != 0;

    if (x1 <= x0)
        return;
    if (shown)
        RenderBGRunWide(*(uint16_t *)(REG_ADDR_BG0CNT + 3 * 2), *(uint16_t *)(REG_ADDR_BG0HOFS + 3 * 4),
                        *(uint16_t *)(REG_ADDR_BG0VOFS + 3 * 4), lineNum, x0, x1, bg3);
    for (int x = x0; x < x1; x++)
    {
        unsigned int m = maskRow != NULL && x >= 0 && x < DISPLAY_WIDTH ? maskRow[x] : mask;

        out[x - x0] = WideColor(shown ? bg3[x - x0] : 0, m, backdrop);
    }
}

// This row's part of the wide picture (gDioramaSceneWide): BG3 past the
// picture's left and right edges with the window state of the edge column;
// with row 0, the rows above the picture too, as row 0 would continue.
static void WideLine(const uint16_t *winMask, bool windowsEnabled, uint16_t vcount, uint16_t backdrop)
{
    unsigned int leftMask = windowsEnabled ? winMask[0] : 0xFF;
    unsigned int rightMask = windowsEnabled ? winMask[DISPLAY_WIDTH - 1] : 0xFF;
    uint16_t *row = &gDioramaSceneWide[(sWideTop + vcount) * sWideStride];

    WideRun(row, -sWideLeft, 0, vcount, leftMask, NULL, backdrop);
    WideRun(row + sWideLeft + DISPLAY_WIDTH, DISPLAY_WIDTH, DISPLAY_WIDTH + sWideRight, vcount,
            rightMask, NULL, backdrop);
    if (vcount == 0)
        for (int y = -sWideTop; y < 0; y++)
        {
            uint16_t *above = &gDioramaSceneWide[(sWideTop + y) * sWideStride];

            WideRun(above, -sWideLeft, 0, y, leftMask, NULL, backdrop);
            WideRun(above + sWideLeft, 0, DISPLAY_WIDTH, y, 0xFF, windowsEnabled ? winMask : NULL, backdrop);
            WideRun(above + sWideLeft + DISPLAY_WIDTH, DISPLAY_WIDTH, DISPLAY_WIDTH + sWideRight, y,
                    rightMask, NULL, backdrop);
        }
}
#endif

// Parts of this code heavily borrowed from NanoboyAdvance.
#ifdef ENABLE_BATTLE_SPRITES
/*
 * A battler's picture drawn from the battle sprite library (platform/battle_sprites.h) in place of
 * an OAM entry's tiles: the entry's 64x64 box holds the library frame at (view->left, view->top) in
 * its texture space, as large as the frame is (not cut to the box), seen through the entry's affine
 * matrix or flips and its mosaic; each pixel is written as DrawSprites writes the entry's own
 * (keep the two in step).
 */
static void LibraryObjPixel(struct scanlineData *scanline, const struct OamData *oam, unsigned int x,
                            uint16_t color, bool windowsEnabled, unsigned int blendMode)
{
    uint16_t *pixels = scanline->spriteLayers[oam->priority];
    bool winShouldBlendPixel;
#ifdef ENABLE_DIORAMA
    uint8_t cover = 0;
#endif

    if (oam->objMode == 2)
    {
        if (scanline->winMask[x] & WINMASK_WINOUT)
            scanline->winMask[x] = (REG_WINOUT >> 8) & 0x3F;
        return;
    }
    winShouldBlendPixel = (windowsEnabled == false || scanline->winMask[x] & WINMASK_CLR);
    if ((blendMode == 1 && REG_BLDCNT & BLDCNT_TGT1_OBJ && winShouldBlendPixel) || oam->objMode == 1)
    {
        uint16_t targetB = 0;

        if (alphaBlendSelectTargetB(scanline, &targetB, oam->priority, 0, x, false))
        {
            color = alphaBlendColor(color, targetB);
#ifdef ENABLE_DIORAMA
            cover = (sTargetCover * clampCoeff((REG_BLDALPHA >> 8) & 0x1F)) >> 4;
#endif
        }
    }
    else if (REG_BLDCNT & BLDCNT_TGT1_OBJ && winShouldBlendPixel)
    {
        switch (blendMode)
        {
        case 2:
            color = alphaBrightnessIncrease(color);
            break;
        case 3:
            color = alphaBrightnessDecrease(color);
            break;
        }
    }
    pixels[x] = color | (1 << 15);
#ifdef ENABLE_DIORAMA
    scanline->spriteCover[oam->priority][x] = cover;
#endif
}

static void DrawLibraryObjLine(struct scanlineData *scanline, uint16_t vcount, bool windowsEnabled,
                               const struct OamData *oam, const struct BattleSpriteView *view)
{
    unsigned int blendMode = (REG_BLDCNT >> 6) & 3;
    int32_t ox = oam->x, oy = oam->y;
    int x, xFrom, xTo;

    if (ox >= DISPLAY_WIDTH)
        ox -= 512;
    if (oy >= DISPLAY_HEIGHT)
        oy -= 256;
    if ((int)vcount >= view->clipBottom)
        return;
    if (!(oam->affineMode & 1))
    {
        bool flipX = (oam->matrixNum >> 3) & 1, flipY = (oam->matrixNum >> 4) & 1;
        int ty = (oam->mosaic ? applySpriteVerticalMosaicEffect(vcount) : vcount) - oy;
        int py;
        const uint8_t *row;

        if (flipY)
            ty = 63 - ty;
        py = ty - view->top;
        if (py < 0 || py >= view->height)
            return;
        row = view->pixels + (size_t)py * view->stride;
        /* the screen columns the frame covers on this row */
        if (flipX)
        {
            xFrom = ox + 63 - (view->left + view->width - 1);
            xTo = ox + 63 - view->left;
        }
        else
        {
            xFrom = ox + view->left;
            xTo = ox + view->left + view->width - 1;
        }
        if (xFrom < 0)
            xFrom = 0;
        if (xTo > DISPLAY_WIDTH - 1)
            xTo = DISPLAY_WIDTH - 1;
        for (x = xFrom; x <= xTo; x++)
        {
            int tx = (oam->mosaic ? (int)applySpriteHorizontalMosaicEffect(x) : x) - ox;
            int px;
            uint8_t index;

            if (flipX)
                tx = 63 - tx;
            px = tx - view->left;
            if (px < 0 || px >= view->width)
                continue;
            index = row[px];
            if (index != 0)
                LibraryObjPixel(scanline, oam, (unsigned int)x, view->colors[index], windowsEnabled, blendMode);
        }
    }
    else
    {
        const struct OamData *base = &((const struct OamData *)OAM)[oam->matrixNum * 4];
        int32_t pa = (int16_t)base[0].affineParam, pb = (int16_t)base[1].affineParam;
        int32_t pc = (int16_t)base[2].affineParam, pd = (int16_t)base[3].affineParam;
        int32_t half = (oam->affineMode & 2) ? 64 : 32; /* double size: the centre is 64 from the corner */
        int32_t cx = ox + half, cy = oy + half;
        int64_t det = (int64_t)pa * pd - (int64_t)pb * pc;
        int ly = (oam->mosaic ? applySpriteVerticalMosaicEffect(vcount) : vcount) - cy;
        float minX = 1e9f, maxX = -1e9f;
        int corner;

        if (det == 0)
            return;
        /* The frame's corners on the screen (the inverse of the texture matrix), for the
         * columns to look at. */
        for (corner = 0; corner < 4; corner++)
        {
            float tx = (float)(view->left + ((corner & 1) ? view->width : 0) - 32);
            float ty = (float)(view->top + ((corner & 2) ? view->height : 0) - 32);
            float sx = ((float)pd * tx - (float)pb * ty) * 256.0f / (float)det;

            if (sx < minX) minX = sx;
            if (sx > maxX) maxX = sx;
        }
        xFrom = cx + (int)minX - 2;
        xTo = cx + (int)maxX + 2;
        if (xFrom < 0)
            xFrom = 0;
        if (xTo > DISPLAY_WIDTH - 1)
            xTo = DISPLAY_WIDTH - 1;
        for (x = xFrom; x <= xTo; x++)
        {
            int lx = (oam->mosaic ? (int)applySpriteHorizontalMosaicEffect(x) : x) - cx;
            int tx = ((pa * lx + pb * ly) >> 8) + 32;
            int ty = ((pc * lx + pd * ly) >> 8) + 32;
            int px = tx - view->left, py = ty - view->top;
            uint8_t index;

            if (px < 0 || px >= view->width || py < 0 || py >= view->height)
                continue;
            index = view->pixels[(size_t)py * view->stride + px];
            if (index != 0)
                LibraryObjPixel(scanline, oam, (unsigned int)x, view->colors[index], windowsEnabled, blendMode);
        }
    }
}

/*
 * A move's copy of a battler on BG1 or BG2 (MoveBattlerSpriteToBG: the picture's 8x8 tiles at the
 * layer's map origin): where the line shows the copy's tiles, the library frame instead (placed in
 * the copy's 64x64 block as in the sprite's box); around the block, the frame where the layer
 * shows nothing; anything else the move drew on the layer stays.
 */
static void LibraryBgCopyLine(uint16_t control, uint16_t hoffs, uint16_t voffs, int lineNum, uint16_t *line,
                              const struct BattleSpriteBgCopy *copy)
{
    const struct BattleSpriteView *view = copy->view;
    unsigned int mapW = bgMapSizes[control >> 14][0] * 8, mapH = bgMapSizes[control >> 14][1] * 8;
    const uint16_t *screen = (const uint16_t *)BG_SCREEN_ADDR((control >> 8) & 0x1F);
    unsigned int yy = (lineNum + voffs) & (mapH - 1);
    int v = (int)yy >= (int)(mapH / 2) ? (int)yy - (int)mapH : (int)yy;
    int py = v - view->top;
    const uint8_t *row = (py >= 0 && py < view->height) ? view->pixels + (size_t)py * view->stride : NULL;
    unsigned int x;

    for (x = 0; x < DISPLAY_WIDTH; x++)
    {
        unsigned int xx = (x + hoffs) & (mapW - 1);
        int u = (int)xx >= (int)(mapW / 2) ? (int)xx - (int)mapW : (int)xx;
        int px = u - view->left;
        uint8_t index = (row != NULL && px >= 0 && px < view->width) ? row[px] : 0;

        if (u >= 0 && u < 64 && v >= 0 && v < 64)
        {
            const uint16_t *map = screen + ((xx > 255) ? 0x400 : 0) + ((yy > 255) ? ((mapW > 256) ? 0x800 : 0x400) : 0);
            uint16_t entry = map[((yy & 0xFF) / 8) * 32 + (xx & 0xFF) / 8];

            if ((unsigned int)((entry & 0x3FF) - copy->firstTile) >= 64u)
                continue; /* something else drawn over the copy's block */
            line[x] = index != 0 ? (view->colors[index] | 0x8000) : 0;
        }
        else if (index != 0 && !(line[x] & 0x8000))
            line[x] = view->colors[index] | 0x8000;
    }
}
#endif

static void DrawSprites(struct scanlineData* scanline, uint16_t vcount, bool windowsEnabled)
{
    int i;
    unsigned int x;
    unsigned int y;
    void *objtiles = VRAM_ + 0x10000;
    unsigned int blendMode = (REG_BLDCNT >> 6) & 3;
    bool winShouldBlendPixel = true;

    int16_t matrix[2][2] = {};

    if (!(REG_DISPCNT & (1 << 6)))
    {
        puts("2-D OBJ Character mapping not supported.");
    }

    for (i = 127; i >= 0; i--)
    {
        struct OamData *oam = &((struct OamData *)OAM)[i];
        unsigned int width;
        unsigned int height;
        uint16_t *pixels;
#ifdef ENABLE_DIORAMA
        /* The map's sprites: the 3D view draws them in its own scene. */
        if (sSkipObj && (gDioramaSceneSkipObj[i >> 5] >> (i & 31)) & 1)
            continue;
#endif

        bool isAffine  = oam->affineMode & 1;
        bool doubleSizeOrDisabled = (oam->affineMode >> 1) & 1;
        bool isSemiTransparent = (oam->objMode == 1);
        bool isObjWin = (oam->objMode == 2);

        if (!(isAffine) && doubleSizeOrDisabled) // disable for non-affine
        {
            continue;
        }
#ifdef ENABLE_BATTLE_SPRITES
        /* A battler's picture: its animated library sprite instead (platform/battle_sprites.h). */
        if (gBattleSpriteObjViews[i] != NULL)
        {
            DrawLibraryObjLine(scanline, vcount, windowsEnabled, oam, gBattleSpriteObjViews[i]);
            continue;
        }
#endif

        if (oam->shape == 0)
        {
            width = (1 << oam->size) * 8;
            height = (1 << oam->size) * 8;
        }
        else if (oam->shape == 1) // wide
        {
            width = spriteSizes[oam->size][1];
            height = spriteSizes[oam->size][0];
        }
        else if (oam->shape == 2) // tall
        {
            width = spriteSizes[oam->size][0];
            height = spriteSizes[oam->size][1];
        }
        else
        {
            continue; // prohibited, do not draw
        }

        int rect_width = width;
        int rect_height = height;

        int half_width = width / 2;
        int half_height = height / 2;

        pixels = scanline->spriteLayers[oam->priority];

        int32_t x = oam->x;
        int32_t y = oam->y;

        if (x >= DISPLAY_WIDTH)
            x -= 512;
        if (y >= DISPLAY_HEIGHT)
            y -= 256;

        if (isAffine)
        {
            //TODO: there is probably a better way to do this
            u8 matrixNum = oam->matrixNum * 4;

            struct OamData *oam1 = &((struct OamData *)OAM)[matrixNum];
            struct OamData *oam2 = &((struct OamData *)OAM)[matrixNum + 1];
            struct OamData *oam3 = &((struct OamData *)OAM)[matrixNum + 2];
            struct OamData *oam4 = &((struct OamData *)OAM)[matrixNum + 3];

            matrix[0][0] = oam1->affineParam;
            matrix[0][1] = oam2->affineParam;
            matrix[1][0] = oam3->affineParam;
            matrix[1][1] = oam4->affineParam;

            if (doubleSizeOrDisabled) // double size for affine
            {
                rect_width *= 2;
                rect_height *= 2;
                half_width *= 2;
                half_height *= 2;
            }
        }
        else
        {
            // Identity
            matrix[0][0] = 0x100;
            matrix[0][1] = 0;
            matrix[1][0] = 0;
            matrix[1][1] = 0x100;
        }

        x += half_width;
        y += half_height;

        // Does this sprite actually draw on this scanline?
        if (vcount >= (y - half_height) && vcount < (y + half_height))
        {
            int local_y = (oam->mosaic == 1) ? applySpriteVerticalMosaicEffect(vcount) - y : vcount - y;
            int number  = oam->tileNum;
            int palette = oam->paletteNum;
            bool flipX  = !isAffine && ((oam->matrixNum >> 3) & 1);
            bool flipY  = !isAffine && ((oam->matrixNum >> 4) & 1);
            bool is8BPP  = oam->bpp & 1;

            for (int local_x = -half_width; local_x <= half_width; local_x++)
            {
                uint8_t *tiledata = (uint8_t *)objtiles;
                uint16_t *palette = (uint16_t *)(PLTT + 0x200);
                int local_mosaicX;
                int tex_x;
                int tex_y;

                unsigned int global_x = local_x + x;

                if (global_x < 0 || global_x >= DISPLAY_WIDTH)
                    continue;

                if (oam->mosaic == 1)
                {
                    //mosaic effect has to be applied to global coordinates otherwise the mosaic will scroll
                    local_mosaicX = applySpriteHorizontalMosaicEffect(global_x) - x;
                    tex_x = ((matrix[0][0] * local_mosaicX + matrix[0][1] * local_y) >> 8) + (width / 2);
                    tex_y = ((matrix[1][0] * local_mosaicX + matrix[1][1] * local_y) >> 8) + (height / 2);
                }else{
                    tex_x = ((matrix[0][0] * local_x + matrix[0][1] * local_y) >> 8) + (width / 2);
                    tex_y = ((matrix[1][0] * local_x + matrix[1][1] * local_y) >> 8) + (height / 2);
                }

                /* Check if transformed coordinates are inside bounds. */

                if (tex_x >= width || tex_y >= height || tex_x < 0 || tex_y < 0)
                    continue;

                if (flipX)
                    tex_x = width  - tex_x - 1;
                if (flipY)
                    tex_y = height - tex_y - 1;

                int tile_x = tex_x % 8;
                int tile_y = tex_y % 8;
                int block_x = tex_x / 8;
                int block_y = tex_y / 8;
                int block_offset = ((block_y * (REG_DISPCNT & 0x40 ? (width / 8) : 16)) + block_x);
                uint16_t pixel = 0;

                if (!is8BPP)
                {
                    pixel = tiledata[(block_offset + oam->tileNum) * 32 + (tile_y * 4) + (tile_x / 2)];
                    if (tile_x & 1)
                        pixel >>= 4;
                    else
                        pixel &= 0xF;
                    palette += oam->paletteNum * 16;
                }
                else
                {
                    pixel = tiledata[(block_offset * 2 + oam->tileNum) * 32 + (tile_y * 8) + tile_x];
                }

                if (pixel != 0)
                {
                    uint16_t color = palette[pixel];;
                    
                    //if sprite mode is 2 then write to the window mask instead
                    if (isObjWin)
                    {
                        if (scanline->winMask[global_x] & WINMASK_WINOUT)
                        scanline->winMask[global_x] = (REG_WINOUT >> 8) & 0x3F;
                        continue;
                    }
                    //this code runs if pixel is to be drawn
                    if (global_x < DISPLAY_WIDTH && global_x >= 0)
                    {
                        //check if its enabled in the window (if window is enabled)
                        winShouldBlendPixel = (windowsEnabled == false || scanline->winMask[global_x] & WINMASK_CLR);
                        
#ifdef ENABLE_DIORAMA
                        uint8_t cover = 0;
#endif
                        //has to be separated from the blend mode switch statement because of OBJ semi transparancy feature
                        if ((blendMode == 1 && REG_BLDCNT & BLDCNT_TGT1_OBJ && winShouldBlendPixel) || isSemiTransparent)
                        {
                            uint16_t targetA = color;
                            uint16_t targetB = 0;
                            if (alphaBlendSelectTargetB(scanline, &targetB, oam->priority, 0, global_x, false))
                            {
                                color = alphaBlendColor(targetA, targetB);
#ifdef ENABLE_DIORAMA
                                cover = (sTargetCover * clampCoeff((REG_BLDALPHA >> 8) & 0x1F)) >> 4;
#endif
                            }
                        }
                        else if (REG_BLDCNT & BLDCNT_TGT1_OBJ && winShouldBlendPixel)
                        {
                            switch (blendMode)
                            {
                            case 2:
                                color = alphaBrightnessIncrease(color);
                                break;
                            case 3:
                                color = alphaBrightnessDecrease(color);
                                break;
                            }
                        }
                        
                        //write pixel to pixel framebuffer
                        pixels[global_x] = color | (1 << 15);
#ifdef ENABLE_DIORAMA
                        scanline->spriteCover[oam->priority][global_x] = cover;
#endif
                    }
                }
            }
        }
    }
}

static void DrawScanline(uint16_t *pixels, uint16_t vcount)
{
    unsigned int mode = REG_DISPCNT & 3;
    unsigned char numOfBgs = (mode == 0 ? 4 : 3);
    int bgnum, prnum;
    struct scanlineData scanline;
    unsigned int blendMode = (REG_BLDCNT >> 6) & 3;
    unsigned int xpos;


    //initialize all priority bookkeeping data
    memset(scanline.layers, 0, sizeof(scanline.layers));
    memset(scanline.winMask, 0, sizeof(scanline.winMask));
    memset(scanline.spriteLayers, 0, sizeof(scanline.spriteLayers));
    memset(scanline.prioritySortedBgsCount, 0, sizeof(scanline.prioritySortedBgsCount));
#ifdef ENABLE_DIORAMA
    uint8_t *cover = sSceneMask ? &gDioramaSceneCover[vcount * DISPLAY_WIDTH] : NULL;

    memset(scanline.spriteCover, 0, sizeof(scanline.spriteCover));
    if (sSceneMask)
    {
        /* How far this line's field layers are moved from line 0's (a
         * transition's waves and slices move BG1-3 together). */
        uint16_t hofs = *(uint16_t *)(REG_ADDR_BG0HOFS + 4), vofs = *(uint16_t *)(REG_ADDR_BG0VOFS + 4);

        if (vcount == 0)
        {
            sLineZeroScroll[0] = hofs;
            sLineZeroScroll[1] = vofs;
        }
        gDioramaSceneLineShift[vcount][0] = (int16_t)((((hofs - sLineZeroScroll[0]) & 0x1FF) ^ 0x100) - 0x100);
        gDioramaSceneLineShift[vcount][1] = (int16_t)((((vofs - sLineZeroScroll[1]) & 0x1FF) ^ 0x100) - 0x100);
    }
#endif

    for (bgnum = 0; bgnum < numOfBgs; bgnum++)
    {
        uint16_t bgcnt = *(uint16_t*)(REG_ADDR_BG0CNT + bgnum * 2);
        uint16_t priority;
        scanline.bgcnts[bgnum] = bgcnt;
        scanline.bgtoprio[bgnum] = priority = (bgcnt & 3);
        
        char priorityCount = scanline.prioritySortedBgsCount[priority];
        scanline.prioritySortedBgs[priority][priorityCount] = bgnum;
        scanline.prioritySortedBgsCount[priority]++;
    }
    
    switch (mode)
    {
    case 0:
        // All backgrounds are text mode
        for (bgnum = 3; bgnum >= 0; bgnum--)
        {
#ifdef ENABLE_DIORAMA
            if (isbgEnabled(bgnum) && (sSceneMask & (1 << bgnum)))
            {
                // What the scenery itself would show there, kept aside for
                // where the world has nothing (gDioramaSceneArt).
                if (bgnum == 3 && sArtWanted)
                {
                    uint16_t *art = &gDioramaSceneArt[vcount * DISPLAY_WIDTH];

                    // Its own colours, before any fade: the 3D battle fades
                    // the world and this together (battle_scene.h).
                    sBgPalette = gPlttBufferUnfaded;
                    memsetu16(art, gPlttBufferUnfaded[0] | 0x8000, DISPLAY_WIDTH);
                    RenderBGScanline(bgnum, scanline.bgcnts[bgnum], *(uint16_t *)(REG_ADDR_BG0HOFS + bgnum * 4),
                                     *(uint16_t *)(REG_ADDR_BG0VOFS + bgnum * 4), vcount, art);
                    sBgPalette = NULL;
                }
                // The world's: an opaque stand-in, colour 0, across the line.
                memsetu16(scanline.layers[bgnum], 0x8000, DISPLAY_WIDTH);
                continue;
            }
#endif
            if (isbgEnabled(bgnum))
            {
                uint16_t bghoffs = *(uint16_t *)(REG_ADDR_BG0HOFS + bgnum * 4);
                uint16_t bgvoffs = *(uint16_t *)(REG_ADDR_BG0VOFS + bgnum * 4);
                
                RenderBGScanline(bgnum, scanline.bgcnts[bgnum], bghoffs, bgvoffs, vcount, scanline.layers[bgnum]);
#ifdef ENABLE_BATTLE_SPRITES
                // A move's copy of a battler on this layer: its library sprite instead.
                if (gBattleSpriteBgCopies[bgnum].view != NULL)
                    LibraryBgCopyLine(scanline.bgcnts[bgnum], bghoffs, bgvoffs, vcount, scanline.layers[bgnum],
                                      &gBattleSpriteBgCopies[bgnum]);
#endif
            }
        }
        
        break;
    case 1:
        // BG2 is affine
        bgnum = 2;
#ifdef ENABLE_DIORAMA
        if (isbgEnabled(bgnum) && (sSceneMask & (1 << bgnum)))
            memsetu16(scanline.layers[bgnum], 0x8000, DISPLAY_WIDTH);
        else
#endif
        if (isbgEnabled(bgnum))
        {
            RenderRotScaleBGScanline(bgnum, scanline.bgcnts[bgnum], REG_BG2X, REG_BG2Y, vcount, scanline.layers[bgnum]);
        }
        // BG0 and BG1 are text mode
        for (bgnum = 1; bgnum >= 0; bgnum--)
        {
            if (isbgEnabled(bgnum))
            {
                uint16_t bghoffs = *(uint16_t *)(REG_ADDR_BG0HOFS + bgnum * 4);
                uint16_t bgvoffs = *(uint16_t *)(REG_ADDR_BG0VOFS + bgnum * 4);
                
                RenderBGScanline(bgnum, scanline.bgcnts[bgnum], bghoffs, bgvoffs, vcount, scanline.layers[bgnum]);
            }
        }
        break;
    default:
        DBGPRINTF("Video mode %u is unsupported.\n", mode);
        break;
    }
    
    bool windowsEnabled = false;
    uint16_t WIN0bottom, WIN0top, WIN0right, WIN0left;
    uint16_t WIN1bottom, WIN1top, WIN1right, WIN1left;
    bool WIN0enable, WIN1enable;
    WIN0enable = false;
    WIN1enable = false;

    //figure out if WIN0 masks on this scanline
    if (REG_DISPCNT & DISPCNT_WIN0_ON)
    {
        //acquire the window coordinates
        WIN0bottom = (REG_WIN0V & 0xFF); //y2;
        WIN0top = (REG_WIN0V & 0xFF00) >> 8; //y1;
        WIN0right = (REG_WIN0H & 0xFF); //x2
        WIN0left = (REG_WIN0H & 0xFF00) >> 8; //x1
        
        //figure out WIN Y wraparound and check bounds accordingly
        if (WIN0top > WIN0bottom) {
            if (vcount >= WIN0top || vcount < WIN0bottom)
                WIN0enable = true;
        } else {
            if (vcount >= WIN0top && vcount < WIN0bottom)
                WIN0enable = true;
        }
        
        windowsEnabled = true;
    }
    //figure out if WIN1 masks on this scanline
    if (REG_DISPCNT & DISPCNT_WIN1_ON)
    {
        WIN1bottom = (REG_WIN0V & 0xFF); //y2;
        WIN1top = (REG_WIN0V & 0xFF00) >> 8; //y1;
        WIN1right = (REG_WIN0H & 0xFF); //x2
        WIN1left = (REG_WIN0H & 0xFF00) >> 8; //x1
        
        if (WIN1top > WIN1bottom) {
            if (vcount >= WIN1top || vcount < WIN1bottom)
                WIN1enable = true;
        } else {
            if (vcount >= WIN1top && vcount < WIN1bottom)
                WIN1enable = true;
        }
        
        windowsEnabled = true;
    }
    //enable windows if OBJwin is enabled
    if (REG_DISPCNT & DISPCNT_OBJWIN_ON && REG_DISPCNT & DISPCNT_OBJ_ON)
    {
        windowsEnabled = true;
    }
    
    //draw to pixel mask
    if (windowsEnabled)
    {
        for (xpos = 0; xpos < DISPLAY_WIDTH; xpos++)
        {
            //win0 checks
            if (WIN0enable && winCheckHorizontalBounds(WIN0left, WIN0right, xpos))
                scanline.winMask[xpos] = REG_WININ & 0x3F;
            //win1 checks
            else if (WIN1enable && winCheckHorizontalBounds(WIN1left, WIN1right, xpos))
                scanline.winMask[xpos] = (REG_WININ >> 8) & 0x3F;
            else
                scanline.winMask[xpos] = (REG_WINOUT & 0x3F) | WINMASK_WINOUT;
        }
    }

#ifdef ENABLE_DIORAMA
    if (cover != NULL || sWideWanted)
    {
        // The window mask without the OBJ window (that is sprites, left out).
        SceneEdge(&scanline, pixels[0], 0, windowsEnabled, blendMode, &gDioramaSceneEdges[vcount][0]);
        SceneEdge(&scanline, pixels[DISPLAY_WIDTH - 1], DISPLAY_WIDTH - 1, windowsEnabled, blendMode,
                  &gDioramaSceneEdges[vcount][1]);
    }
    // pixels still holds this row's backdrop colour (DrawFrame).
    if (sWideWanted)
        WideLine(scanline.winMask, windowsEnabled, vcount, pixels[0]);
#endif
    if (REG_DISPCNT & DISPCNT_OBJ_ON)
        DrawSprites(&scanline, vcount, windowsEnabled);

    //iterate trough every priority in order
    for (prnum = 3; prnum >= 0; prnum--)
    {
        for (int prsub = scanline.prioritySortedBgsCount[prnum] - 1; prsub >= 0; prsub--)
        {
            char bgnum = scanline.prioritySortedBgs[prnum][prsub];
            //if background is enabled then draw it
            if (isbgEnabled(bgnum))
            {
                uint16_t *src = scanline.layers[bgnum];
                //copy all pixels to framebuffer 
                for (xpos = 0; xpos < DISPLAY_WIDTH; xpos++)
                {
                    uint16_t color = src[xpos];
                    bool winEffectEnable = true;
                    
                    if (!getAlphaBit(color))
                        continue; //do nothing if alpha bit is not set
                    
                    if (windowsEnabled)
                    {
                        winEffectEnable = ((scanline.winMask[xpos] & WINMASK_CLR) >> 5);
                        //if bg is disabled inside the window then do not draw the pixel
                        if ( !(scanline.winMask[xpos] & 1 << bgnum) )
                            continue;
                    }
                    
#ifdef ENABLE_DIORAMA
                    // A pixel of the world's layer is all world (cover 16),
                    // any other starts opaque; blending mixes the covers as
                    // it mixes the colours.
                    uint8_t pixelCover = (sSceneMask & (1 << bgnum)) ? 16 : 0;
#endif
                    //blending code
                    if (blendMode != 0 && REG_BLDCNT & (1 << bgnum) && winEffectEnable)
                    {
                        uint16_t targetA = color;
                        uint16_t targetB = 0;
                        char isSpriteBlendingEnabled;
                        
                        switch (blendMode)
                        {
                        case 1:
                            isSpriteBlendingEnabled = REG_BLDCNT & BLDCNT_TGT2_OBJ ? 1 : 0;
                            //find targetB and blend it
                            if (alphaBlendSelectTargetB(&scanline, &targetB, prnum, prsub+1, xpos, isSpriteBlendingEnabled))
                            {
                                color = alphaBlendColor(targetA, targetB);
#ifdef ENABLE_DIORAMA
                                pixelCover = clampCoeff((pixelCover * clampCoeff(REG_BLDALPHA & 0x1F)
                                                         + sTargetCover * clampCoeff((REG_BLDALPHA >> 8) & 0x1F)) >> 4);
#endif
                            }
                            break;
                        case 2:
                            color = alphaBrightnessIncrease(targetA);
#ifdef ENABLE_DIORAMA
                            pixelCover = (pixelCover * (16 - clampCoeff(REG_BLDY & 0x1F))) >> 4;
#endif
                            break;
                        case 3:
                            color = alphaBrightnessDecrease(targetA);
#ifdef ENABLE_DIORAMA
                            pixelCover = (pixelCover * (16 - clampCoeff(REG_BLDY & 0x1F))) >> 4;
#endif
                            break;
                        }
                    }
                    //write the pixel to scanline buffer output
                    pixels[xpos] = color;
#ifdef ENABLE_DIORAMA
                    if (cover != NULL)
                        cover[xpos] = pixelCover;
#endif
                }
            }
        }
        //draw sprites on current priority
        uint16_t *src = scanline.spriteLayers[prnum];
        for (xpos = 0; xpos < DISPLAY_WIDTH; xpos++)
        {
            if (getAlphaBit(src[xpos]))
            {
                //check if sprite pixel draws inside window
                if (windowsEnabled && !(scanline.winMask[xpos] & WINMASK_OBJ))
                        continue;
                //draw the pixel
                pixels[xpos] = src[xpos];
#ifdef ENABLE_DIORAMA
                if (cover != NULL)
                    cover[xpos] = scanline.spriteCover[prnum][xpos];
#endif
            }
        }
    }
}

uint16_t *memsetu16(uint16_t *dst, uint16_t fill, size_t count)
{
    for (int i = 0; i < count; i++)
    {
        *dst++ = fill;
    }
}

void DrawFrame(uint16_t *pixels)
{
    int i;
    int j;
#ifdef ENABLE_BATTLE_SPRITES
    BattleSprites_BeginFrame(); // which battler pictures come from the library this frame
#endif
#ifdef ENABLE_DIORAMA
    sSceneMask = gDioramaSceneLayers & 0x0F;
    sArtWanted = gDioramaSceneArtWanted;
    sWideWanted = gDioramaSceneWideWanted;
    if (sWideWanted)
    {
        sWideLeft = gDioramaSceneWideLeft < DIORAMA_WIDE_MAX_SIDE ? gDioramaSceneWideLeft : DIORAMA_WIDE_MAX_SIDE;
        sWideRight = gDioramaSceneWideRight < DIORAMA_WIDE_MAX_SIDE ? gDioramaSceneWideRight : DIORAMA_WIDE_MAX_SIDE;
        sWideTop = gDioramaSceneWideTop < DIORAMA_WIDE_MAX_TOP ? gDioramaSceneWideTop : DIORAMA_WIDE_MAX_TOP;
        sWideStride = sWideLeft + DISPLAY_WIDTH + sWideRight;
        if (!sWideMap.decided || sWideMap.loads != DioramaBattle_MoveBgLoads()
         || sWideMap.control != *(uint16_t *)(REG_ADDR_BG0CNT + 3 * 2))
            DecideWideMap(*(uint16_t *)(REG_ADDR_BG0CNT + 3 * 2));
    }
    sSkipObj = (gDioramaSceneSkipObj[0] | gDioramaSceneSkipObj[1]
              | gDioramaSceneSkipObj[2] | gDioramaSceneSkipObj[3]) != 0;
    if (sSceneMask)
        memset(gDioramaSceneCover, 0, sizeof(gDioramaSceneCover));
#endif
    for (i = 0; i < DISPLAY_HEIGHT; i++)
    {
        REG_VCOUNT = i;
        if(((REG_DISPSTAT >> 8) & 0xFF) == REG_VCOUNT)
        {
            REG_DISPSTAT |= INTR_FLAG_VCOUNT;
#ifdef __ANDROID__
            if (REG_IE & INTR_FLAG_VCOUNT)
#else
            if (REG_DISPSTAT & DISPSTAT_VCOUNT_INTR)
#endif
                    gIntrTable[0]();
        }

        // Render the backdrop color before the each individual scanline.
        // backdrop color brightness effects
        unsigned int blendMode = (REG_BLDCNT >> 6) & 3;
        uint16_t backdropColor = *(uint16_t *)PLTT;
        if (REG_BLDCNT & BLDCNT_TGT1_BD)
        {
            switch (blendMode)
            {
            case 2:
                backdropColor = alphaBrightnessIncrease(backdropColor);
                break;
            case 3:
                backdropColor = alphaBrightnessDecrease(backdropColor);
                break;
            }
        }

        memsetu16(&pixels[i * DISPLAY_WIDTH], backdropColor, DISPLAY_WIDTH);
        DrawScanline(&pixels[i * DISPLAY_WIDTH], i);
        
        REG_DISPSTAT |= INTR_FLAG_HBLANK;

        RunDMAs(DMA_HBLANK);
        
        if (REG_DISPSTAT & DISPSTAT_HBLANK_INTR)
            gIntrTable[3]();

        REG_DISPSTAT &= ~INTR_FLAG_HBLANK;
        REG_DISPSTAT &= ~INTR_FLAG_VCOUNT;
    }
#ifdef ENABLE_BATTLE_SPRITES
    BattleSprites_EndFrame(pixels);
#endif
}
#endif
