#include <cmath>
#include <pthread.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app_log.h"
#include "core/utils/rga_debug_gate.h"
#include "drmrga.h"
#include "im2d.h"
#include "image_preprocess_utils.h"

static pthread_mutex_t g_rga_mutex = PTHREAD_MUTEX_INITIALIZER;

static int crop_and_scale_image_c(int channel,
                                  unsigned char *src, int src_width, int src_height,
                                  int crop_x, int crop_y, int crop_width, int crop_height,
                                  unsigned char *dst, int dst_width, int dst_height,
                                  int dst_box_x, int dst_box_y, int dst_box_width, int dst_box_height)
{
    if (dst == nullptr)
    {
        LOGE("dst buffer is null\n");
        return -1;
    }

    const float x_ratio = (float)crop_width / (float)dst_box_width;
    const float y_ratio = (float)crop_height / (float)dst_box_height;

    for (int dst_y = dst_box_y; dst_y < dst_box_y + dst_box_height; dst_y++)
    {
        for (int dst_x = dst_box_x; dst_x < dst_box_x + dst_box_width; dst_x++)
        {
            const int dst_x_offset = dst_x - dst_box_x;
            const int dst_y_offset = dst_y - dst_box_y;

            const int src_x = (int)(dst_x_offset * x_ratio) + crop_x;
            const int src_y = (int)(dst_y_offset * y_ratio) + crop_y;

            const float x_diff = (dst_x_offset * x_ratio) - (src_x - crop_x);
            const float y_diff = (dst_y_offset * y_ratio) - (src_y - crop_y);

            const int index1 = src_y * src_width * channel + src_x * channel;
            int index2 = index1 + src_width * channel;
            if (src_y == src_height - 1)
                index2 = index1 - src_width * channel;

            int index3 = index1 + 1 * channel;
            int index4 = index2 + 1 * channel;
            if (src_x == src_width - 1)
            {
                index3 = index1 - 1 * channel;
                index4 = index2 - 1 * channel;
            }

            for (int c = 0; c < channel; c++)
            {
                const unsigned char A = src[index1 + c];
                const unsigned char B = src[index3 + c];
                const unsigned char C = src[index2 + c];
                const unsigned char D = src[index4 + c];

                const unsigned char pixel = (unsigned char)(A * (1 - x_diff) * (1 - y_diff) +
                                                            B * x_diff * (1 - y_diff) +
                                                            C * y_diff * (1 - x_diff) +
                                                            D * x_diff * y_diff);

                dst[(dst_y * dst_width + dst_x) * channel + c] = pixel;
            }
        }
    }

    return 0;
}

static int crop_and_scale_image_yuv420sp(unsigned char *src, int src_width, int src_height,
                                         int crop_x, int crop_y, int crop_width, int crop_height,
                                         unsigned char *dst, int dst_width, int dst_height,
                                         int dst_box_x, int dst_box_y, int dst_box_width, int dst_box_height)
{
    unsigned char *src_y = src;
    unsigned char *src_uv = src + src_width * src_height;

    unsigned char *dst_y = dst;
    unsigned char *dst_uv = dst + dst_width * dst_height;

    crop_and_scale_image_c(1,
                           src_y, src_width, src_height,
                           crop_x, crop_y, crop_width, crop_height,
                           dst_y, dst_width, dst_height,
                           dst_box_x, dst_box_y, dst_box_width, dst_box_height);

    crop_and_scale_image_c(2,
                           src_uv, src_width / 2, src_height / 2,
                           crop_x / 2, crop_y / 2, crop_width / 2, crop_height / 2,
                           dst_uv, dst_width / 2, dst_height / 2,
                           dst_box_x, dst_box_y, dst_box_width, dst_box_height);
    return 0;
}

int get_image_size(image_buffer_t *image)
{
    if (image == nullptr)
        return 0;
    switch (image->format)
    {
    case IMAGE_FORMAT_GRAY8:
        return image->width * image->height;
    case IMAGE_FORMAT_RGB888:
    case IMAGE_FORMAT_BGR888:
        return image->width * image->height * 3;
    case IMAGE_FORMAT_RGBA8888:
        return image->width * image->height * 4;
    case IMAGE_FORMAT_YUV420SP_NV12:
    case IMAGE_FORMAT_YUV420SP_NV21:
        return image->width * image->height * 3 / 2;
    default:
        break;
    }
    return 0;
}

static int convert_image_cpu(image_buffer_t *src, image_buffer_t *dst,
                             image_rect_t *src_box, image_rect_t *dst_box, char color)
{
    if (dst->virt_addr == nullptr || src->virt_addr == nullptr)
        return -1;

    const int same_format = (src->format == dst->format);
    const int swap_rb =
        (src->format == IMAGE_FORMAT_BGR888 && dst->format == IMAGE_FORMAT_RGB888) ||
        (src->format == IMAGE_FORMAT_RGB888 && dst->format == IMAGE_FORMAT_BGR888);
    if (!same_format && !swap_rb)
        return -1;

    int src_box_x = 0;
    int src_box_y = 0;
    int src_box_w = src->width;
    int src_box_h = src->height;
    if (src_box != nullptr)
    {
        src_box_x = src_box->left;
        src_box_y = src_box->top;
        src_box_w = src_box->right - src_box->left + 1;
        src_box_h = src_box->bottom - src_box->top + 1;
    }

    int dst_box_x = 0;
    int dst_box_y = 0;
    int dst_box_w = dst->width;
    int dst_box_h = dst->height;
    if (dst_box != nullptr)
    {
        dst_box_x = dst_box->left;
        dst_box_y = dst_box->top;
        dst_box_w = dst_box->right - dst_box->left + 1;
        dst_box_h = dst_box->bottom - dst_box->top + 1;
    }

    if (dst_box_w != dst->width || dst_box_h != dst->height)
    {
        const int dst_size = get_image_size(dst);
        memset(dst->virt_addr, color, dst_size);
    }

    int ret = 0;
    if (src->format == IMAGE_FORMAT_RGB888 || src->format == IMAGE_FORMAT_BGR888)
    {
        ret = crop_and_scale_image_c(3, src->virt_addr, src->width, src->height,
                                     src_box_x, src_box_y, src_box_w, src_box_h,
                                     dst->virt_addr, dst->width, dst->height,
                                     dst_box_x, dst_box_y, dst_box_w, dst_box_h);
    }
    else if (src->format == IMAGE_FORMAT_RGBA8888)
    {
        ret = crop_and_scale_image_c(4, src->virt_addr, src->width, src->height,
                                     src_box_x, src_box_y, src_box_w, src_box_h,
                                     dst->virt_addr, dst->width, dst->height,
                                     dst_box_x, dst_box_y, dst_box_w, dst_box_h);
    }
    else if (src->format == IMAGE_FORMAT_GRAY8)
    {
        ret = crop_and_scale_image_c(1, src->virt_addr, src->width, src->height,
                                     src_box_x, src_box_y, src_box_w, src_box_h,
                                     dst->virt_addr, dst->width, dst->height,
                                     dst_box_x, dst_box_y, dst_box_w, dst_box_h);
    }
    else if (src->format == IMAGE_FORMAT_YUV420SP_NV12 ||
             src->format == IMAGE_FORMAT_YUV420SP_NV21)
    {
        ret = crop_and_scale_image_yuv420sp(src->virt_addr, src->width, src->height,
                                            src_box_x, src_box_y, src_box_w, src_box_h,
                                            dst->virt_addr, dst->width, dst->height,
                                            dst_box_x, dst_box_y, dst_box_w, dst_box_h);
    }
    else
    {
        LOGE("unsupported format: %d\n", src->format);
        return -1;
    }
    if (ret != 0)
    {
        LOGE("convert_image_cpu failed: %d\n", ret);
        return -1;
    }

    if (swap_rb)
    {
        const int pixels = dst->width * dst->height;
        unsigned char *p = dst->virt_addr;
        for (int i = 0; i < pixels; ++i)
        {
            const int idx = i * 3;
            const unsigned char tmp = p[idx];
            p[idx] = p[idx + 2];
            p[idx + 2] = tmp;
        }
    }
    return 0;
}

static int get_rga_fmt(image_format_t fmt)
{
    switch (fmt)
    {
    case IMAGE_FORMAT_RGB888:
        return RK_FORMAT_RGB_888;
    case IMAGE_FORMAT_BGR888:
        return RK_FORMAT_BGR_888;
    case IMAGE_FORMAT_RGBA8888:
        return RK_FORMAT_RGBA_8888;
    case IMAGE_FORMAT_YUV420SP_NV12:
        return RK_FORMAT_YCbCr_420_SP;
    case IMAGE_FORMAT_YUV420SP_NV21:
        return RK_FORMAT_YCrCb_420_SP;
    default:
        return -1;
    }
}

static int convert_image_rga(image_buffer_t *src_img, image_buffer_t *dst_img,
                             image_rect_t *src_box, image_rect_t *dst_box, char color)
{
    rga_debug_lock();

    int ret = 0;
    const int srcWidth = src_img->width;
    const int srcHeight = src_img->height;
    void *src = src_img->virt_addr;
    const int src_fd = src_img->fd;
    void *src_phy = nullptr;
    const int srcFmt = get_rga_fmt(src_img->format);

    const int dstWidth = dst_img->width;
    const int dstHeight = dst_img->height;
    void *dst = dst_img->virt_addr;
    const int dst_fd = dst_img->fd;
    void *dst_phy = nullptr;
    const int dstFmt = get_rga_fmt(dst_img->format);

    const int rotate = 0;

    int use_handle = 0;
#if defined(LIBRGA_IM2D_HANDLE)
    use_handle = 1;
#endif

    int usage = rotate;
    IM_STATUS ret_rga = IM_STATUS_SUCCESS;
    im_rect srect;
    im_rect drect;
    im_rect prect;
    memset(&srect, 0, sizeof(im_rect));
    memset(&drect, 0, sizeof(im_rect));
    memset(&prect, 0, sizeof(im_rect));

    if (src_box != nullptr)
    {
        srect.x = src_box->left;
        srect.y = src_box->top;
        srect.width = src_box->right - src_box->left + 1;
        srect.height = src_box->bottom - src_box->top + 1;
    }
    else
    {
        srect.x = 0;
        srect.y = 0;
        srect.width = srcWidth;
        srect.height = srcHeight;
    }

    if (dst_box != nullptr)
    {
        drect.x = dst_box->left;
        drect.y = dst_box->top;
        drect.width = dst_box->right - dst_box->left + 1;
        drect.height = dst_box->bottom - dst_box->top + 1;
    }
    else
    {
        drect.x = 0;
        drect.y = 0;
        drect.width = dstWidth;
        drect.height = dstHeight;
    }

    rga_buffer_t rga_buf_src = {0};
    rga_buffer_t rga_buf_dst = {0};
    rga_buffer_t pat;
    rga_buffer_handle_t rga_handle_src = 0;
    rga_buffer_handle_t rga_handle_dst = 0;
    memset(&pat, 0, sizeof(rga_buffer_t));

    im_handle_param_t in_param;
    memset(&in_param, 0, sizeof(im_handle_param_t));
    in_param.width = srcWidth;
    in_param.height = srcHeight;
    in_param.format = srcFmt;

    im_handle_param_t dst_param;
    memset(&dst_param, 0, sizeof(im_handle_param_t));
    dst_param.width = dstWidth;
    dst_param.height = dstHeight;
    dst_param.format = dstFmt;

    if (use_handle)
    {
        if (src_phy != nullptr)
        {
            rga_handle_src = importbuffer_physicaladdr((uint64_t)src_phy, &in_param);
        }
        else if (src_fd > 0)
        {
            rga_handle_src = importbuffer_fd(src_fd, &in_param);
        }
        else
        {
            rga_handle_src = importbuffer_virtualaddr(src, &in_param);
        }
        if (rga_handle_src <= 0)
        {
            LOGE("src handle error %d\n", rga_handle_src);
            ret = -1;
            goto err;
        }
        rga_buf_src = wrapbuffer_handle(
            rga_handle_src, srcWidth, srcHeight, srcFmt, srcWidth, srcHeight);
    }
    else
    {
        if (src_phy != nullptr)
        {
            rga_buf_src = wrapbuffer_physicaladdr(
                src_phy, srcWidth, srcHeight, srcFmt, srcWidth, srcHeight);
        }
        else if (src_fd > 0)
        {
            rga_buf_src = wrapbuffer_fd(
                src_fd, srcWidth, srcHeight, srcFmt, srcWidth, srcHeight);
        }
        else
        {
            rga_buf_src = wrapbuffer_virtualaddr(
                src, srcWidth, srcHeight, srcFmt, srcWidth, srcHeight);
        }
    }

    if (use_handle)
    {
        if (dst_phy != nullptr)
        {
            rga_handle_dst = importbuffer_physicaladdr((uint64_t)dst_phy, &dst_param);
        }
        else if (dst_fd > 0)
        {
            rga_handle_dst = importbuffer_fd(dst_fd, &dst_param);
        }
        else
        {
            rga_handle_dst = importbuffer_virtualaddr(dst, &dst_param);
        }
        if (rga_handle_dst <= 0)
        {
            LOGE("dst handle error %d\n", rga_handle_dst);
            ret = -1;
            goto err;
        }
        rga_buf_dst = wrapbuffer_handle(
            rga_handle_dst, dstWidth, dstHeight, dstFmt, dstWidth, dstHeight);
    }
    else
    {
        if (dst_phy != nullptr)
        {
            rga_buf_dst = wrapbuffer_physicaladdr(
                dst_phy, dstWidth, dstHeight, dstFmt, dstWidth, dstHeight);
        }
        else if (dst_fd > 0)
        {
            rga_buf_dst = wrapbuffer_fd(
                dst_fd, dstWidth, dstHeight, dstFmt, dstWidth, dstHeight);
        }
        else
        {
            rga_buf_dst = wrapbuffer_virtualaddr(
                dst, dstWidth, dstHeight, dstFmt, dstWidth, dstHeight);
        }
    }

    if (drect.width != dstWidth || drect.height != dstHeight)
    {
        im_rect dst_whole_rect = {0, 0, dstWidth, dstHeight};
        int imcolor;
        char *p_imcolor = (char *)&imcolor;
        p_imcolor[0] = color;
        p_imcolor[1] = color;
        p_imcolor[2] = color;
        p_imcolor[3] = color;
        const IM_STATUS ret_fill = imfill(rga_buf_dst, dst_whole_rect, imcolor);
        if (ret_fill <= 0)
        {
            if (dst != nullptr)
            {
                const size_t dst_size = (size_t)get_image_size(dst_img);
                memset(dst, color, dst_size);
            }
            else
            {
                LOGW("cannot fill color on target image\n");
            }
        }
    }

    ret_rga = improcess(rga_buf_src, rga_buf_dst, pat, srect, drect, prect, usage);
    if (ret_rga <= 0)
    {
        LOGE("improcess failed, status=%d, msg=%s\n", ret_rga, imStrError(ret_rga));
        ret = -1;
    }

err:
    if (rga_handle_src > 0)
    {
        releasebuffer_handle(rga_handle_src);
    }
    if (rga_handle_dst > 0)
    {
        releasebuffer_handle(rga_handle_dst);
    }
    rga_debug_unlock();
    return ret;
}

static int convert_image_internal(image_buffer_t *src_img, image_buffer_t *dst_img,
                                  image_rect_t *src_box, image_rect_t *dst_box, char color)
{
#if defined(DISABLE_RGA)
    return convert_image_cpu(src_img, dst_img, src_box, dst_box, color);
#else
    if (rga_debug_preprocess_disabled())
    {
        static int warned = 0;
        if (!warned)
        {
            warned = 1;
            LOGW("DISABLE_PREPROCESS_RGA enabled, preprocess uses CPU path\n");
        }
        return convert_image_cpu(src_img, dst_img, src_box, dst_box, color);
    }

    if (src_img->width % 16 == 0 && dst_img->width % 16 == 0)
    {
        int ret = convert_image_rga(src_img, dst_img, src_box, dst_box, color);
        if (ret != 0)
        {
            LOGW("convert_image_rga failed, fallback to cpu\n");
            ret = convert_image_cpu(src_img, dst_img, src_box, dst_box, color);
        }
        return ret;
    }
    LOGW("src/dst width is not 16-aligned, use cpu path\n");
    return convert_image_cpu(src_img, dst_img, src_box, dst_box, color);
#endif
}

int convert_image_with_letterbox(image_buffer_t *src_image, image_buffer_t *dst_image,
                                 letterbox_t *letterbox, char color)
{
    int ret = 0;
    const int allow_slight_change = 1;
    const int src_w = src_image->width;
    const int src_h = src_image->height;
    const int dst_w = dst_image->width;
    const int dst_h = dst_image->height;
    int resize_w = dst_w;
    int resize_h = dst_h;

    int padding_w = 0;
    int padding_h = 0;
    int left_offset = 0;
    int top_offset = 0;
    float scale = 1.0f;

    image_rect_t src_box;
    src_box.left = 0;
    src_box.top = 0;
    src_box.right = src_image->width - 1;
    src_box.bottom = src_image->height - 1;

    image_rect_t dst_box;
    dst_box.left = 0;
    dst_box.top = 0;
    dst_box.right = dst_image->width - 1;
    dst_box.bottom = dst_image->height - 1;

    const float scale_w = (float)dst_w / src_w;
    const float scale_h = (float)dst_h / src_h;
    if (scale_w < scale_h)
    {
        scale = scale_w;
        resize_h = (int)(src_h * scale);
    }
    else
    {
        scale = scale_h;
        resize_w = (int)(src_w * scale);
    }
    if (allow_slight_change == 1 && (resize_w % 4 != 0))
    {
        resize_w -= resize_w % 4;
    }
    if (allow_slight_change == 1 && (resize_h % 2 != 0))
    {
        resize_h -= resize_h % 2;
    }

    padding_h = dst_h - resize_h;
    padding_w = dst_w - resize_w;
    if (scale_w < scale_h)
    {
        dst_box.top = padding_h / 2;
        if (dst_box.top % 2 != 0)
        {
            dst_box.top -= dst_box.top % 2;
            if (dst_box.top < 0)
                dst_box.top = 0;
        }
        dst_box.bottom = dst_box.top + resize_h - 1;
        top_offset = dst_box.top;
    }
    else
    {
        dst_box.left = padding_w / 2;
        if (dst_box.left % 2 != 0)
        {
            dst_box.left -= dst_box.left % 2;
            if (dst_box.left < 0)
                dst_box.left = 0;
        }
        dst_box.right = dst_box.left + resize_w - 1;
        left_offset = dst_box.left;
    }

    if (letterbox != nullptr)
    {
        letterbox->scale = scale;
        letterbox->x_pad = left_offset;
        letterbox->y_pad = top_offset;
    }

    if (dst_image->virt_addr == nullptr && dst_image->fd <= 0)
    {
        const int dst_size = get_image_size(dst_image);
        dst_image->virt_addr = (uint8_t *)malloc((size_t)dst_size);
        if (dst_image->virt_addr == nullptr)
        {
            LOGE("malloc size %d failed\n", dst_size);
            return -1;
        }
    }

    // int rga_locked = 0;
    // const int lock_ret = pthread_mutex_lock(&g_rga_mutex);
    // if (lock_ret != 0)
    // {
    //     LOGE("RGA mutex lock failed: %d\n", lock_ret);
    //     return -1;
    // }
    // rga_locked = 1;
    // LOGI("RGA locked\n");

    ret = convert_image_internal(src_image, dst_image, &src_box, &dst_box, color);

    // LOGI("Release RGA lock\n");
    // if (rga_locked)
    // {
    //     const int unlock_ret = pthread_mutex_unlock(&g_rga_mutex);
    //     if (unlock_ret != 0)
    //     {
    //         LOGE("RGA mutex unlock failed: %d\n", unlock_ret);
    //     }
    // }
    return ret;
}
