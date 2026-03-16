//=====================  C++  =====================
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>
//=====================   C   =====================
#include <gtk/gtk.h>
//=====================  PRJ  =====================
#include "core/utils/rga_debug_gate.h"
#include "display.h"

static Display_t *gDispDesc = NULL;
static std::atomic<bool> gDisplayRunning(false);
static std::atomic<bool> gWindowFullscreen(false);
static std::atomic<int> gWindowPosX(48);
static std::atomic<int> gWindowPosY(48);
static char *gDispBuffer = NULL;
static char *gDispRawBuffer = NULL;
static size_t gDispPayloadBytes = 0;
static size_t gDispGuardBytes = 0;
static const unsigned char kDispGuardPattern = 0xA5;

char **dispBufferMap(Display_t *dispDesc)
{
    static Display_t stDispDesc = {0};
    if ((dispDesc->width != stDispDesc.width) || (dispDesc->height != stDispDesc.height))
    {
        if (gDispRawBuffer)
        {
            free(gDispRawBuffer);
            gDispRawBuffer = NULL;
            gDispBuffer = NULL;
            gDispPayloadBytes = 0;
            gDispGuardBytes = 0;
        }
        memcpy(&stDispDesc, dispDesc, sizeof(Display_t));
    }

    if (NULL == gDispBuffer)
    {
        gDispPayloadBytes = (size_t)(3 * dispDesc->width * dispDesc->height);
        gDispGuardBytes = rga_debug_guard_check_enabled() ? 8192 : 0;
        size_t total = gDispPayloadBytes + gDispGuardBytes * 2;
        gDispRawBuffer = (char *)malloc(total);
        if (gDispRawBuffer)
        {
            // if (gDispGuardBytes > 0) {
            //     memset(gDispRawBuffer, kDispGuardPattern, gDispGuardBytes);
            //     memset(gDispRawBuffer + gDispGuardBytes + gDispPayloadBytes,
            //            kDispGuardPattern,
            //            gDispGuardBytes);
            // }
            gDispBuffer = gDispRawBuffer + gDispGuardBytes;
        }
    }

    return &gDispBuffer;
}

bool dispBufferCheckGuard()
{
    if (!gDispDesc || !gDispBuffer || !gDispRawBuffer)
        return true;
    if (gDispGuardBytes == 0)
        return true;

    for (size_t i = 0; i < gDispGuardBytes; ++i)
    {
        if ((unsigned char)gDispRawBuffer[i] != kDispGuardPattern)
            return false;
    }
    const char *tail = gDispRawBuffer + gDispGuardBytes + gDispPayloadBytes;
    for (size_t i = 0; i < gDispGuardBytes; ++i)
    {
        if ((unsigned char)tail[i] != kDispGuardPattern)
            return false;
    }
    return true;
}
static gboolean showWidget(GtkWidget *pImage)
{
    char **ppBuf = dispBufferMap(gDispDesc);
    const guchar *pBuf = (const guchar *)*ppBuf;
    if (NULL == pBuf)
    {
        return G_SOURCE_CONTINUE;
    }
    // if (!dispBufferCheckGuard())
    // {
    //     fprintf(stderr, "display buffer guard corrupted\n");
    //     abort();
    // }

    // 创建一个GdkPixbuf
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_data(pBuf, GDK_COLORSPACE_RGB, FALSE, 8, gDispDesc->width, gDispDesc->height, 3 * gDispDesc->width, NULL, NULL);
    // 将GdkPixbuf设置到图像控件
    gtk_image_set_from_pixbuf(GTK_IMAGE(pImage), pixbuf);
    // 在使用完后释放GdkPixbuf
    g_object_unref(pixbuf);

    return G_SOURCE_CONTINUE;
}
static void on_window_destroy(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    gDisplayRunning.store(false);
    gWindowFullscreen.store(false);
    gtk_main_quit();
}

static gboolean move_window_to_saved_position(gpointer user_data)
{
    GtkWindow *window = GTK_WINDOW(user_data);
    if (window)
    {
        gtk_window_move(window, gWindowPosX.load(), gWindowPosY.load());
        g_object_unref(window);
    }
    return G_SOURCE_REMOVE;
}

static void schedule_window_move(GtkWindow *window)
{
    if (!window)
    {
        return;
    }
    g_timeout_add(50, move_window_to_saved_position, g_object_ref(window));
}

static void toggle_window_fullscreen(GtkWindow *window)
{
    if (!window)
    {
        return;
    }

    const bool is_fullscreen = gWindowFullscreen.load();
    if (is_fullscreen)
    {
        gtk_window_unfullscreen(window);
        schedule_window_move(window);
    }
    else
    {
        gtk_window_fullscreen(window);
    }
    gWindowFullscreen.store(!is_fullscreen);
}

static void on_image_multi_press(GtkGestureMultiPress *gesture,
                                 gint n_press,
                                 gdouble x,
                                 gdouble y,
                                 gpointer user_data)
{
    (void)gesture;
    (void)x;
    (void)y;
    if (!user_data)
    {
        return;
    }

    bool should_toggle = (n_press == 2);
    GdkEvent *event = gtk_get_current_event();
    if (event)
    {
        GdkDevice *device = gdk_event_get_source_device(event);
        if (!device)
        {
            device = gdk_event_get_device(event);
        }
        if (device && gdk_device_get_source(device) == GDK_SOURCE_TOUCHSCREEN)
        {
            should_toggle = (n_press == 1);
        }
        gdk_event_free(event);
    }

    if (!should_toggle)
    {
        return;
    }

    toggle_window_fullscreen(GTK_WINDOW(user_data));
}

static GtkWidget *disp_init(const char *strWinTitle,
                            int32_t pos_x,
                            int32_t pos_y,
                            int32_t width,
                            int32_t height,
                            bool fullscreen)
{
    gtk_init(NULL, NULL); // 初始化 GTK+ 库

    static GtkWidget *pWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    if (pWindow)
    {
        g_signal_connect(pWindow, "destroy", G_CALLBACK(on_window_destroy), NULL);
        gtk_window_set_title(GTK_WINDOW(pWindow), strWinTitle);
        gtk_window_set_default_size(GTK_WINDOW(pWindow), width, height);
        gWindowPosX.store(pos_x);
        gWindowPosY.store(pos_y);
        gWindowFullscreen.store(fullscreen);
        if (fullscreen)
        {
            gtk_window_fullscreen(GTK_WINDOW(pWindow));
        }
        else
        {
            gtk_window_unfullscreen(GTK_WINDOW(pWindow));
            gtk_window_move(GTK_WINDOW(pWindow), pos_x, pos_y);
        }
    }
    else
    {
        return NULL;
    }

    return pWindow;
}
static int32_t disp_set_loop(GtkWidget *pWindow, GSourceFunc pCamUpdate)
{
    if (NULL == pWindow)
    {
        return -1;
    }
#if 0
    // 创建一个绘图区域
    //GtkWidget *drawing_area = gtk_drawing_area_new();
#else
    // 创建一个Image对象
    GtkWidget *image = gtk_image_new();
#endif
    GtkWidget *event_box = gtk_event_box_new();
    gtk_widget_add_events(event_box, GDK_BUTTON_PRESS_MASK | GDK_TOUCH_MASK);
    GtkGesture *multi_press = gtk_gesture_multi_press_new(event_box);
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(multi_press), 0);
    g_signal_connect(multi_press, "pressed", G_CALLBACK(on_image_multi_press), pWindow);
    gtk_container_add(GTK_CONTAINER(event_box), image);
    gtk_container_add(GTK_CONTAINER(pWindow), event_box /*drawing_area*/);
    // 启动一个循环，定期更新绘图区域显示视频帧
    GSource *gSource = g_timeout_source_new(33); // 33ms，30帧/秒
    g_source_set_callback(gSource, pCamUpdate, image /*drawing_area*/, NULL);
    g_source_attach(gSource, NULL);
    return 0;
}
int display(Display_t *disp)
{
    GtkWidget *pWindow = disp_init(disp->winTitle,
                                   disp->x,
                                   disp->y,
                                   disp->width,
                                   disp->height,
                                   disp->fullscreen);
    if (pWindow)
    {
        gDisplayRunning.store(true);
        gDispDesc = disp;
        disp_set_loop(pWindow, (GSourceFunc)showWidget);

        gtk_widget_show_all(pWindow);
    }
    else
    {
        gDisplayRunning.store(false);
    }
    // 进入主循环，等待用户操作
    gtk_main();
    gDisplayRunning.store(false);

    return 0;
}

bool displayIsRunning()
{
    return gDisplayRunning.load();
}
