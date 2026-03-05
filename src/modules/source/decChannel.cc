//=====================  C++  =====================
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cstring>
#include <string>
//=====================   C   =====================
#include <gst/video/video.h>
#include <pthread.h>
//=====================  PRJ  =====================
#include "decChannel.h"
#include "modules/source/rtsp_source.h"
#include "core/log/app_log.h"

typedef struct {
    gchar strFmt[32];
    gint width;
    gint height;
    gint horStride;
    gint verStride;
} FrameDesc_t;

// 错误处理函数
static gboolean on_error(GstBus *bus, GstMessage *message, gpointer data)
{
    GError *err;
    gchar *debug_info;

    // 解析错误消息
    gst_message_parse_error(message, &err, &debug_info);

    LOGE("Error received from element %s: %s", GST_OBJECT_NAME(message->src), err->message);
    LOGE("Debugging information: %s", debug_info ? debug_info : "none");

    g_error_free(err);
    g_free(debug_info);

    // 返回FALSE表示从消息处理队列中删除消息
    return TRUE;
}

/* Functions below print the Capabilities in a human-friendly format */
static gboolean print_pad_info(GQuark field, const GValue *value, gpointer pfx)
{
    gchar *str = gst_value_serialize(value);
    // g_print("%s  %15s: %s\n", (gchar *)pfx, g_quark_to_string (field), str);
    g_free(str);
    return TRUE;
}

static void copy_frame_format(gchar *dst, size_t dst_len, const gchar *src)
{
    if (!dst || dst_len == 0)
    {
        return;
    }
    const gchar *safe_src = (src && src[0] != '\0') ? src : "UNKNOWN";
    std::strncpy(dst, safe_src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

static GstBuffer *sample_get_buffer_with_desc(GstSample *sample, FrameDesc_t *frame_desc)
{
    if (frame_desc)
    {
        std::memset(frame_desc, 0, sizeof(*frame_desc));
    }
    if (!sample)
    {
        return NULL;
    }

    GstBuffer *buffer = gst_sample_get_buffer(sample);
    if (!buffer || !frame_desc)
    {
        return buffer;
    }

    GstCaps *caps = gst_sample_get_caps(sample);
    GstVideoInfo video_info;
    std::memset(&video_info, 0, sizeof(video_info));
    if (caps && gst_video_info_from_caps(&video_info, caps))
    {
        frame_desc->width = static_cast<gint>(GST_VIDEO_INFO_WIDTH(&video_info));
        frame_desc->height = static_cast<gint>(GST_VIDEO_INFO_HEIGHT(&video_info));
        frame_desc->horStride = static_cast<gint>(GST_VIDEO_INFO_PLANE_STRIDE(&video_info, 0));
        frame_desc->verStride = frame_desc->height;
        copy_frame_format(frame_desc->strFmt,
                          sizeof(frame_desc->strFmt),
                          gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(&video_info)));
    }
    else
    {
        copy_frame_format(frame_desc->strFmt, sizeof(frame_desc->strFmt), "UNKNOWN");
    }

    GstVideoMeta *video_meta = gst_buffer_get_video_meta(buffer);
    if (video_meta)
    {
        if (video_meta->width > 0)
        {
            frame_desc->width = static_cast<gint>(video_meta->width);
        }
        if (video_meta->height > 0)
        {
            frame_desc->height = static_cast<gint>(video_meta->height);
            frame_desc->verStride = static_cast<gint>(video_meta->height);
        }
        if (video_meta->n_planes > 0 && video_meta->stride[0] > 0)
        {
            frame_desc->horStride = static_cast<gint>(video_meta->stride[0]);
        }
        if (video_meta->n_planes > 1 &&
            video_meta->offset[1] > video_meta->offset[0] &&
            frame_desc->horStride > 0)
        {
            const guint y_plane_bytes = video_meta->offset[1] - video_meta->offset[0];
            const gint y_rows = static_cast<gint>(y_plane_bytes / static_cast<guint>(frame_desc->horStride));
            if (y_rows > 0)
            {
                frame_desc->verStride = y_rows;
            }
        }
        copy_frame_format(frame_desc->strFmt,
                          sizeof(frame_desc->strFmt),
                          gst_video_format_to_string(video_meta->format));
    }

    if (frame_desc->horStride <= 0 && frame_desc->width > 0)
    {
        frame_desc->horStride = frame_desc->width;
    }
    if (frame_desc->verStride <= 0 && frame_desc->height > 0)
    {
        frame_desc->verStride = frame_desc->height;
    }
    if (frame_desc->strFmt[0] == '\0')
    {
        copy_frame_format(frame_desc->strFmt, sizeof(frame_desc->strFmt), "UNKNOWN");
    }
    return buffer;
}

/* This function will be called by the pad-added signal */
static void pad_added_handler(GstElement *src, GstPad *new_pad, gpointer user_data)
{
    DecChannel *channel = static_cast<DecChannel *>(user_data);
    if (!channel || channel->IsClosing())
    {
        return;
    }

    GstChannel_t *data = &channel->mGstChn;
    GstPadLinkReturn ret = GST_PAD_LINK_OK;
    GstPad *video_sinkPad = NULL;

    LOGI("Received new pad '%s' from '%s':", GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));

    /* Check the new pad's type */
    GstCaps *new_pad_caps = gst_pad_get_current_caps(new_pad);
    if (!new_pad_caps)
    {
        LOGW("pad has no caps, ignore.");
        return;
    }
    GstStructure *new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
    if (!new_pad_struct)
    {
        gst_caps_unref(new_pad_caps);
        return;
    }

    // 把Pads的信息都打印出来
    gst_structure_foreach(new_pad_struct, print_pad_info, (gpointer) "     ");

    const gchar *new_pad_type = gst_structure_get_name(new_pad_struct);
    const gchar *new_pad_media_type = gst_structure_get_string(new_pad_struct, "media");
    if (new_pad_media_type && g_str_has_prefix(new_pad_media_type, "video"))
    {
        video_sinkPad = gst_element_get_static_pad(data->h26xRTPDepay, "sink");
        if (!video_sinkPad)
        {
            LOGW("video sink pad is null, ignore.");
            goto exit;
        }
        if (gst_pad_is_linked(video_sinkPad))
        {
            LOGI("We are '%s' already linked. Ignoring.", new_pad_media_type);
            goto exit;
        }
        ret = gst_pad_link(new_pad, video_sinkPad);
    }
    else
    {
        LOGW("Ignore non-video pad type '%s-[%s]'.",
             new_pad_type ? new_pad_type : "unknown",
             new_pad_media_type ? new_pad_media_type : "unknown");
        goto exit;
    }

    if (GST_PAD_LINK_FAILED(ret))
    {
        LOGE("Type is '%s-[%s]' but link failed.", new_pad_type, new_pad_media_type);
    }
    else
    {
        LOGI("Link succeeded (type '%s-[%s]').", new_pad_type, new_pad_media_type);
    }

exit:
    /* Unreference the new pad's caps, if we got them */
    if (new_pad_caps != NULL)
        gst_caps_unref(new_pad_caps);

    /* Unreference the sink pad */
    if (video_sinkPad)
        gst_object_unref(video_sinkPad);
}
static GstFlowReturn new_sample(GstElement *sink, gpointer user_data)
{
    DecChannel *channel = static_cast<DecChannel *>(user_data);
    if (!channel || !channel->EnterCallback())
    {
        return GST_FLOW_EOS;
    }
    struct CallbackGuard
    {
        explicit CallbackGuard(DecChannel *owner) : owner(owner) {}
        ~CallbackGuard() { owner->LeaveCallback(); }
        DecChannel *owner;
    } guard(channel);
    if (channel->IsClosing())
    {
        return GST_FLOW_EOS;
    }

    GstChannel_t *data = &channel->mGstChn;

    /* Retrieve the buffer */
    GstSample *sample = NULL;
    g_signal_emit_by_name(sink, "pull-sample", &sample);
    if (sample)
    {
        FrameDesc_t stFrameDesc;
        // 提取一帧sample中的buffer, 注意:这个buffer是无法直接用的,它不是char类型
        GstBuffer *buffer = sample_get_buffer_with_desc(sample, &stFrameDesc);
        if (!buffer)
        {
            LOGE("gst_sample_get_buffer fail");
            gst_sample_unref(sample);
            return GST_FLOW_ERROR;
        }

        // 把buffer映射到map，这样我们就可以通过map.data取到buffer的数据
        GstMapInfo map;
        if (gst_buffer_map(buffer, &map, GST_MAP_READ))
        {
            // g_print("data size = %ld , info_size = %ld\n", map.size, GST_VIDEO_INFO_SIZE(&video_info));
            modules::source::RtspImgDesc_t imgDesc = {0};
            imgDesc.chnId = data->chnId;
            imgDesc.width = stFrameDesc.width;
            imgDesc.height = stFrameDesc.height;
            imgDesc.horStride = stFrameDesc.horStride;
            imgDesc.verStride = stFrameDesc.verStride;
            imgDesc.dataSize = static_cast<int>(map.size);
            std::strncpy(imgDesc.fmt, stFrameDesc.strFmt, sizeof(imgDesc.fmt) - 1);
            imgDesc.fmt[sizeof(imgDesc.fmt) - 1] = '\0';
            modules::source::rtspVideoOutHandle((char *)map.data, imgDesc);

            gst_buffer_unmap(buffer, &map); // 解除映射
        }
        gst_sample_unref(sample); // 释放资源
        return GST_FLOW_OK;
    }
    return GST_FLOW_OK;
}

// =======================================================================================================
void *busListen(void *para)
{
    GstElement *pPipeLine = (GstElement *)para;
    if (NULL == pPipeLine)
        pthread_exit(NULL);

    /* Listen to the bus */
    GstBus *bus = gst_element_get_bus(pPipeLine);
    // 连接到错误信号
    gst_bus_add_signal_watch(bus);
    g_signal_connect(bus, "message::error", G_CALLBACK(on_error), NULL);

    gboolean terminate = FALSE;
    GstMessage *msg;
    do
    {
        msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, (GstMessageType)(GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_WARNING | GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
        /* Parse message */
        if (msg != NULL)
        {
            GError *err;
            gchar *debug_info;

            switch (GST_MESSAGE_TYPE(msg))
            {
            case GST_MESSAGE_ERROR:
                gst_message_parse_error(msg, &err, &debug_info);
                LOGE("Error received from element %s: %s", GST_OBJECT_NAME(msg->src), err->message);
                LOGE("Debugging information: %s", debug_info ? debug_info : "none");
                g_clear_error(&err);
                g_free(debug_info);
                terminate = TRUE;
                break;
            case GST_MESSAGE_EOS:
                LOGI("End-Of-Stream reached.");
                terminate = TRUE;
                break;
            case GST_MESSAGE_STATE_CHANGED:
                /* We are only interested in state-changed messages from the pipeline */
                if (GST_MESSAGE_SRC(msg) == GST_OBJECT(pPipeLine))
                {
                    GstState old_state, new_state, pending_state;
                    gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
                    LOGI("Pipeline state changed from %s to %s:",
                         gst_element_state_get_name(old_state),
                         gst_element_state_get_name(new_state));
                }
                break;
            default:
                /* We should not reach here */
                LOGW("Unexpected message received.");
                break;
            }
            gst_message_unref(msg);
        }
    } while (!terminate);

    /* Free resources */
    gst_object_unref(bus);
    gst_element_set_state(pPipeLine, GST_STATE_NULL);
    gst_object_unref(pPipeLine);

    pthread_exit(NULL);
}

DecChannel::DecChannel(int chnId, std::string strUrl, std::string strVedioFmt) : bObjIsInited(false),
                                                                                 mPadAddedHandlerId(0),
                                                                                 mNewSampleHandlerId(0),
                                                                                 mClosing(false),
                                                                                 mCallbackInflight(0),
                                                                                 mStrUrl(strUrl),
                                                                                 mStrVideoFmt(strVedioFmt)
{
    memset(&mGstChn, 0, sizeof(mGstChn));
    mGstChn.chnId = chnId;
}
DecChannel::~DecChannel()
{
    Shutdown();
}
int DecChannel::init()
{
    // CustomData data;
    GstStateChangeReturn ret;

    /* Create the empty pipeline */
    mGstChn.pipeline = gst_pipeline_new("test-pipeline");
    /* Create the elements */
    mGstChn.source = gst_element_factory_make("rtspsrc", "source");
    if (!mGstChn.pipeline || !mGstChn.source)
    {
        LOGE("Not all elements could be created.");
        return -1;
    }

    /* Build the pipeline. Note that we are NOT linking the source at this
     * point. We will do it later. */
    gst_bin_add_many(GST_BIN(mGstChn.pipeline), mGstChn.source, NULL);

    /* Create video & audio decode channel */
    if (0 != createVideoDecChannel())
    {
        Shutdown();
        return -1;
    }
    // if(0 != createAudioDecChannel()){
    //     gst_object_unref (mGstChn.pipeline);
    //     return -1;
    // }

    /* Set the URL to play */
    g_object_set(mGstChn.source, "location", mStrUrl.c_str(), NULL);
    /* Connect to the pad-added signal */
    mPadAddedHandlerId = g_signal_connect(mGstChn.source, "pad-added", G_CALLBACK(pad_added_handler), this);

    /* Start playing */
    ret = gst_element_set_state(mGstChn.pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        LOGE("Unable to set the pipeline to the playing state.");
        Shutdown();
        return -1;
    }

    bObjIsInited = true;

    return 0;
}

bool DecChannel::EnterCallback()
{
    std::lock_guard<std::mutex> lock(mCallbackMu);
    if (mClosing.load())
    {
        return false;
    }
    ++mCallbackInflight;
    return true;
}

void DecChannel::LeaveCallback()
{
    std::lock_guard<std::mutex> lock(mCallbackMu);
    if (mCallbackInflight > 0)
    {
        --mCallbackInflight;
    }
    if (mCallbackInflight == 0)
    {
        mCallbackCv.notify_all();
    }
}

bool DecChannel::IsClosing() const
{
    return mClosing.load();
}

void DecChannel::Shutdown()
{
    if (mClosing.exchange(true))
    {
        return;
    }

    bObjIsInited = false;
    if (mGstChn.vSink)
    {
        g_object_set(mGstChn.vSink, "emit-signals", FALSE, NULL);
        if (mNewSampleHandlerId != 0)
        {
            g_signal_handler_disconnect(mGstChn.vSink, mNewSampleHandlerId);
            mNewSampleHandlerId = 0;
        }
    }
    if (mGstChn.source && mPadAddedHandlerId != 0)
    {
        g_signal_handler_disconnect(mGstChn.source, mPadAddedHandlerId);
        mPadAddedHandlerId = 0;
    }

    if (mGstChn.pipeline)
    {
        gst_element_set_state(mGstChn.pipeline, GST_STATE_NULL);
        gst_element_get_state(mGstChn.pipeline, NULL, NULL, GST_SECOND);
    }

    std::unique_lock<std::mutex> lock(mCallbackMu);
    mCallbackCv.wait_for(lock, std::chrono::milliseconds(200), [this]()
                         { return mCallbackInflight == 0; });
    lock.unlock();

    if (mGstChn.pipeline)
    {
        gst_object_unref(mGstChn.pipeline);
    }
    memset(&mGstChn, 0, sizeof(mGstChn));
}

int DecChannel::createVideoDecChannel()
{
    if (0 == strcmp(mStrVideoFmt.c_str(), "h264"))
    {
        mGstChn.h26xRTPDepay = gst_element_factory_make("rtph264depay", "h26xRTPDepay");
        mGstChn.h26xParse = gst_element_factory_make("h264parse", "h26xParse");
    }
    else if (0 == strcmp(mStrVideoFmt.c_str(), "h265"))
    {
        mGstChn.h26xRTPDepay = gst_element_factory_make("rtph265depay", "h26xRTPDepay");
        mGstChn.h26xParse = gst_element_factory_make("h265parse", "h26xParse");
    }
    else
    {
        LOGE("invaild stream format.");
        return -1;
    }
    mGstChn.vDec = gst_element_factory_make("mppvideodec", "vDec");
    mGstChn.vScale = gst_element_factory_make("videoscale", "vScale");
    mGstChn.vCapsfilter = gst_element_factory_make("capsfilter", "vCapsfilter");
    mGstChn.vSink = gst_element_factory_make("appsink", "vSink");

    if (!mGstChn.h26xRTPDepay || !mGstChn.h26xParse || !mGstChn.vDec || !mGstChn.vScale || !mGstChn.vCapsfilter || !mGstChn.vSink)
    {
        LOGE("Not all video elements could be created.");
        return -1;
    }

#if 0 // 由于图像缩放比较吃CPU，因此这里暂不开放
    // vCapsfilter要与vScale搭配使用，此处操作是设置vScale的属性，例如 width 和 height。
    // <==> gst-launch-1.0命令的"... ! videoscale ! video/x-raw,width=1280,height=720 ! ..."
    GstCaps *caps = gst_caps_new_simple("video/x-raw",   "width",G_TYPE_INT,1280,   "height",G_TYPE_INT,720,   NULL);
    g_object_set(mGstChn.vCapsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);
#endif

//  用appsink接住输出的视频流，用作后续处理
//  参考：https://blog.csdn.net/qq_41563600/article/details/121257849
#if 0
    // 把解码器的输出也要对应改成BGR格式
    g_object_set(mGstChn.vDec, "format", GST_VIDEO_FORMAT_BGR, NULL);
    //GstCaps *caps = gst_caps_new_simple("video/x-raw",   "format",G_TYPE_STRING,"BGR",   NULL);
    GstCaps *caps = gst_caps_from_string(g_strdup_printf("video/x-raw,format=BGR"));
    if(caps){
        g_object_set(mGstChn.vSink, "caps", caps, NULL);
        gst_caps_unref(caps);
    }
#endif
    g_object_set(mGstChn.vSink, "sync", FALSE, NULL);
    g_object_set(mGstChn.vSink, "emit-signals", TRUE, NULL);
    mNewSampleHandlerId = g_signal_connect(mGstChn.vSink, "new-sample", G_CALLBACK(new_sample), this);

    // 把一个个【元素】添加进【管线】
    gst_bin_add(GST_BIN(mGstChn.pipeline), mGstChn.h26xRTPDepay);
    gst_bin_add(GST_BIN(mGstChn.pipeline), mGstChn.h26xParse);
    gst_bin_add(GST_BIN(mGstChn.pipeline), mGstChn.vDec);
    // gst_bin_add(GST_BIN(mGstChn.pipeline), mGstChn.vScale);
    // gst_bin_add(GST_BIN(mGstChn.pipeline), mGstChn.vCapsfilter);
    gst_bin_add(GST_BIN(mGstChn.pipeline), mGstChn.vSink);

    if (!gst_element_link_many(mGstChn.h26xRTPDepay, mGstChn.h26xParse, mGstChn.vDec, /*mGstChn.vScale, mGstChn.vCapsfilter,*/ mGstChn.vSink, NULL))
    {
        LOGE("Video Elements could not be linked.");
        return -1;
    }

    return 0;
}
int DecChannel::createAudioDecChannel()
{
    mGstChn.aQueue = gst_element_factory_make("queue", "aQueue");

    mGstChn.audioRTPDepay = gst_element_factory_make("rtppcmadepay", "pcmaRTPDepay");
    mGstChn.aDec = gst_element_factory_make("alawdec", "aDec");

    mGstChn.aConvert = gst_element_factory_make("audioconvert", "aConvert");
    mGstChn.aResample = gst_element_factory_make("audioresample", "aResample");
    mGstChn.aSink = gst_element_factory_make("autoaudiosink", "aSink");

    if (!mGstChn.aQueue || !mGstChn.audioRTPDepay || !mGstChn.aDec || !mGstChn.aConvert || !mGstChn.aResample || !mGstChn.aSink)
    {
        LOGE("Not all audio elements could be created.");
        return -1;
    }

    // gst_bin_add_many(GST_BIN(mGstChn.pipeline), mGstChn.audioRTPDepay, mGstChn.aDec, mGstChn.aConvert, mGstChn.aResample, mGstChn.aSink, NULL);
    gst_bin_add(GST_BIN(mGstChn.pipeline), mGstChn.aQueue);
    gst_bin_add(GST_BIN(mGstChn.pipeline), mGstChn.audioRTPDepay);
    gst_bin_add(GST_BIN(mGstChn.pipeline), mGstChn.aDec);
    gst_bin_add(GST_BIN(mGstChn.pipeline), mGstChn.aConvert);
    gst_bin_add(GST_BIN(mGstChn.pipeline), mGstChn.aResample);
    gst_bin_add(GST_BIN(mGstChn.pipeline), mGstChn.aSink);

    if (!gst_element_link_many(mGstChn.aQueue, mGstChn.audioRTPDepay, mGstChn.aDec, mGstChn.aConvert, mGstChn.aResample, mGstChn.aSink, NULL))
    {
        LOGE("Audio Elements could not be linked.");
        return -1;
    }
    return 0;
}
