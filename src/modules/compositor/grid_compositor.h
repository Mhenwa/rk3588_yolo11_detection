#ifndef __GRID_COMPOSITOR_H__
#define __GRID_COMPOSITOR_H__

#include <stdbool.h>
#include <stdint.h>

extern int grid_compositor_init(char **ppDispBuf, int chnNums);
extern void grid_compositor_set_channel_count(int chnNums);
extern void grid_compositor_set_display_size(int width, int height);

typedef struct
{
    char fmt[16];
    int chnId;
    int width;
    int height;
    int horStride;
    int verStride;
    int dataSize;
} GridCompositorImgDesc_t;
extern int grid_compositor_submit_frame(char *imgData, GridCompositorImgDesc_t imgDesc);

#endif

