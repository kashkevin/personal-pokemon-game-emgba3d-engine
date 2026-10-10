#include <stdio.h>
#include "global.h"
#include "platform/dma.h"

struct DMATransfer {
    union {
        const void *src;
        const u16 *src16;
        const u32 *src32;
    };
    union {
        void *dst;
        vu16 *dst16;
        vu32 *dst32;
    };
    u32 size;
    u16 control;
    u32 hblankRuns; // HBlank transfers since the game last armed this channel
} DMAList[DMA_COUNT];

// A repeating HBlank DMA whose source moves stays inside its buffer only while the
// game re-arms it every frame, as scanline effects do in their VBlank callback. One
// left running reads on through memory: harmless on the GBA, a crash natively once
// it reaches unmapped memory. Such a channel is stopped and reported so the screen
// that forgot to stop it can be fixed (KNOWN_ISSUES N25).
#define HBLANK_DMA_MAX_RUNS (3 * DISPLAY_HEIGHT)

static void StopRunawayHBlankDma(int dmaNum, struct DMATransfer *dma)
{
    dma->control &= ~DMA_ENABLE;
    fprintf(stderr, "[compat] HBlank DMA%d ran %d lines without being re-armed (source %p); stopped\n",
            dmaNum, HBLANK_DMA_MAX_RUNS, dma->src);
    fflush(stderr);
}

void RunDMAs(u32 type)
{
    for (int dmaNum = 0; dmaNum < DMA_COUNT; dmaNum++)
    {
        struct DMATransfer *dma = &DMAList[dmaNum];
        u32 dmaCntReg = (&REG_DMA0CNT)[dmaNum * 3];
        if (!((dmaCntReg >> 16) & DMA_ENABLE))
        {
            dma->control &= ~DMA_ENABLE;
        }
        
        if ( (dma->control & DMA_ENABLE) &&
           (((dma->control & DMA_START_MASK) >> 12) == type))
        {
            if (type == DMA_HBLANK && (dma->control & DMA_REPEAT)
             && (dma->control & DMA_SRC_MASK) != DMA_SRC_FIXED
             && ++dma->hblankRuns > HBLANK_DMA_MAX_RUNS)
            {
                StopRunawayHBlankDma(dmaNum, dma);
                continue;
            }
            //printf("DMA%d src=%p, dest=%p, control=%d\n", dmaNum, dma->src, dma->dest, dma->control);
            for (int i = 0; i < (dma->size); i++)
            {
                if ((dma->control) & DMA_32BIT)
                     *dma->dst32 = *dma->src32;
                else *dma->dst16 = *dma->src16;

                // process destination pointer changes
                if (((dma->control) & DMA_DEST_MASK) == DMA_DEST_INC)
                {
                    if ((dma->control) & DMA_32BIT)
                            dma->dst32++;
                    else    dma->dst16++;
                }
                else if (((dma->control) & DMA_DEST_MASK) == DMA_DEST_DEC)
                {
                    if ((dma->control) & DMA_32BIT)
                            dma->dst32--;
                    else    dma->dst16--;
                }
                else if (((dma->control) & DMA_DEST_MASK) == DMA_DEST_RELOAD) // TODO
                {
                    if ((dma->control) & DMA_32BIT)
                            dma->dst32++;
                    else    dma->dst16++;
                }

                // process source pointer changes
                if (((dma->control) & DMA_SRC_MASK) == DMA_SRC_INC)
                {
                    if ((dma->control) & DMA_32BIT)
                            dma->src32++;
                    else    dma->src16++;
                }
                else if (((dma->control) & DMA_SRC_MASK) == DMA_SRC_DEC)
                {
                    if ((dma->control) & DMA_32BIT)
                            dma->src32--;
                    else    dma->src16--;
                }
            }

            if (dma->control & DMA_REPEAT)
            {
                dma->size = ((&REG_DMA0CNT)[dmaNum * 3] & 0x1FFFF);
                if (((dma->control) & DMA_DEST_MASK) == DMA_DEST_RELOAD)
                {
                    dma->dst = (void *)(uintptr_t)((&REG_DMA0DAD)[dmaNum * 3]);
                }
            }
            else
            {
                dma->control &= ~DMA_ENABLE;
            }
        }
    }
}

void DmaSet(int dmaNum, const void *src, void *dest, u32 control)
{
    if (dmaNum >= DMA_COUNT)
    {
        DBGPRINTF("DmaSet with invalid DMA number: dmaNum=%d, src=%p, dest=%p, control=%d\n", dmaNum, src, dest, control);
        return;
    }

    (&REG_DMA0SAD)[dmaNum * 3] = (vu32)(uintptr_t)src;
    (&REG_DMA0DAD)[dmaNum * 3] = (vu32)(uintptr_t)dest;
    (&REG_DMA0CNT)[dmaNum * 3] = control;

    struct DMATransfer *dma = &DMAList[dmaNum];
    dma->src = src;
    dma->dst = dest;
    dma->size = control & 0x1ffff;
    dma->control = control >> 16;
    dma->hblankRuns = 0;

    RunDMAs(DMA_NOW);
}