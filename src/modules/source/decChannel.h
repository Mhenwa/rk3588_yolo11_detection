#ifndef __DECODECHANNEL_H__
#define __DECODECHANNEL_H__

#include <stdbool.h>
#include <stdint.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>

#include <gst/gst.h>

typedef struct {
    const char *srcType;
    const char *loaction;
    const char *videoEncType;
    const char *audioEncType;
}SrcCfg_t;

/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct gst_Channel {
    int chnId;
    GstElement *pipeline;
    GstElement *source;
    // ====== video ======
    GstElement *h26xRTPDepay;
    GstElement *h26xParse;
    GstElement *vDec;
    GstElement *vScale;
    GstElement *vCapsfilter;
    GstElement *vSink;
    // ====== audio ======
    GstElement *aQueue;
    GstElement *audioRTPDepay;
    GstElement *aDec;
    GstElement *aConvert;
    GstElement *aResample;
    GstElement *aSink;
} GstChannel_t;

class DecChannel
{
public:
	DecChannel(int chnId, std::string strUrl, std::string strVedioFmt = "h264");
	~DecChannel();

	int init();
	int32_t IsInited(){return bObjIsInited;}
    int32_t channelId(){return mGstChn.chnId;}
    bool EnterCallback();
    void LeaveCallback();
    bool IsClosing() const;

    GstChannel_t mGstChn;

protected:
	int createVideoDecChannel();
    int createAudioDecChannel();
    void Shutdown();
private:
	int bObjIsInited;
    gulong mPadAddedHandlerId;
    gulong mNewSampleHandlerId;
    std::atomic<bool> mClosing;
    std::mutex mCallbackMu;
    std::condition_variable mCallbackCv;
    int mCallbackInflight;

    std::string mStrUrl;
    std::string mStrVideoFmt;
};

#endif
