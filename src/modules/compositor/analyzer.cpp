//=====================  C++  =====================
#include <algorithm>
#include <string>
//=====================   C   =====================
#include "system.h"
//=====================  PRJ  =====================
#include "analyzer.h"

//#define PTRINT uint32_t
#define PTRINT uint64_t
#define WIN_WIDTH 1920
#define WIN_HEIGHT 1080
/* rotate source image 0 degrees clockwise */
#define HAL_TRANSFORM_ROT_0 0x00

static pthread_mutex_t gmutex;
static bool gMutexInited = false;
static char **gppDispMap = nullptr;
static int gChnNums = 1;

int analyzer_init(char **ppDispBuf, int chnNums)
{
    gppDispMap = ppDispBuf;
    gChnNums = std::max(1, chnNums);

    if (!gMutexInited)
    {
        pthread_mutex_init(&gmutex, nullptr);
        gMutexInited = true;
    }

    return 0;
}

void analyzer_set_channel_count(int chnNums)
{
    if (!gMutexInited)
    {
        return;
    }
    pthread_mutex_lock(&gmutex);
    gChnNums = std::max(1, chnNums);
    pthread_mutex_unlock(&gmutex);
}

typedef struct
{
    RgaSURF_FORMAT fmt;
    int width;
    int height;
    int hor_stride;
    int ver_stride;
    int rotation;
    void *pBuf;
} Image;

static int srcImg_ConvertTo_dstImg(Image *pDst, Image *pSrc)
{
    rga_info_t src;
    rga_info_t dst;
    int ret = -1;

    if (!pSrc || !pDst)
    {
        printf("%s: NULL PTR!\n", __func__);
        return -1;
    }

    pthread_mutex_lock(&gmutex);
    memset(&src, 0, sizeof(rga_info_t));
    src.fd = -1;
    src.virAddr = pSrc->pBuf;
    src.mmuFlag = 1;
    src.rotation = pSrc->rotation;
    rga_set_rect(&src.rect, 0, 0, pSrc->width, pSrc->height,
                 pSrc->hor_stride, pSrc->ver_stride, pSrc->fmt);

    memset(&dst, 0, sizeof(rga_info_t));
    dst.fd = -1;
    dst.virAddr = pDst->pBuf;
    dst.mmuFlag = 1;
    dst.rotation = pDst->rotation;
    rga_set_rect(&dst.rect, 0, 0, pDst->width, pDst->height,
                 pDst->hor_stride, pDst->ver_stride, pDst->fmt);
    if (c_RkRgaBlit(&src, &dst, nullptr))
    {
        printf("%s: rga fail\n", __func__);
        ret = -1;
    }
    else
    {
        ret = 0;
    }
    pthread_mutex_unlock(&gmutex);

    return ret;
}

static RgaSURF_FORMAT rgaFmt(char *strFmt)
{
    if (0 == strcmp(strFmt, "NV12"))
    {
        return RK_FORMAT_YCbCr_420_SP;
    }
    if (0 == strcmp(strFmt, "NV21"))
    {
        return RK_FORMAT_YCrCb_420_SP;
    }
    if (0 == strcmp(strFmt, "BGR"))
    {
        return RK_FORMAT_BGR_888;
    }
    if (0 == strcmp(strFmt, "RGB"))
    {
        return RK_FORMAT_RGB_888;
    }
    return RK_FORMAT_UNKNOWN;
}

static PTRINT calcBufMapOffset(int chnId, int units)
{
    int xUnitOffset = chnId % units;
    int yUnitOffset = chnId / units;

    int winWidth = WIN_WIDTH / units;
    int winHeight = WIN_HEIGHT / units;

    PTRINT bufMapOffset =
        3 * (yUnitOffset * winHeight * WIN_WIDTH + xUnitOffset * winWidth);
    return bufMapOffset;
}

static void commitImgtoDispBufMap(int chnId, void *pSrcData, RgaSURF_FORMAT srcFmt,
                                  int srcWidth, int srcHeight,
                                  int srcHStride, int srcVStride)
{
    if (gChnNums <= 0)
    {
        return;
    }

    int units = 0;
    while (1)
    {
        units++;
        if (gChnNums <= (units * units))
        {
            break;
        }
    }

    Image srcImage;
    Image dstImage;
    memset(&srcImage, 0, sizeof(srcImage));
    memset(&dstImage, 0, sizeof(dstImage));

    srcImage.fmt = srcFmt;
    srcImage.width = srcWidth;
    srcImage.height = srcHeight;
    srcImage.hor_stride = srcHStride;
    srcImage.ver_stride = srcVStride;
    srcImage.rotation = HAL_TRANSFORM_ROT_0;
    srcImage.pBuf = pSrcData;

    PTRINT dstBufPtr = (PTRINT)*gppDispMap + calcBufMapOffset(chnId, units);
    dstImage.fmt = RK_FORMAT_RGB_888;
    dstImage.width = WIN_WIDTH / units;
    dstImage.height = WIN_HEIGHT / units;
    dstImage.hor_stride = WIN_WIDTH;
    dstImage.ver_stride = WIN_HEIGHT / units;
    dstImage.rotation = HAL_TRANSFORM_ROT_0;
    dstImage.pBuf = (void *)dstBufPtr;

    srcImg_ConvertTo_dstImg(&dstImage, &srcImage);
}

int videoOutHandle(char *imgData, ImgDesc_t imgDesc)
{
    if (!gppDispMap || !(*gppDispMap))
    {
        return -1;
    }

    RgaSURF_FORMAT srcFmt = rgaFmt(imgDesc.fmt);
    if (srcFmt == RK_FORMAT_UNKNOWN)
    {
        return -1;
    }

    commitImgtoDispBufMap(imgDesc.chnId, (void *)imgData, srcFmt,
                          imgDesc.width, imgDesc.height,
                          imgDesc.horStride, imgDesc.verStride);
    return 0;
}

