//
// Created by welcomeworld on 3/5/23.
//

#ifndef BIPPLAYER_BIPVIDEOTRACKER_H
#define BIPPLAYER_BIPVIDEOTRACKER_H

#include "BipFrameQueue.h"
#include "BipPacketQueue.h"
#include "BipClock.h"
#include "BipNativeWindow.h"
#include "BipLog.h"
#include "libyuv.h"
#include "MediaTracker.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libavutil/time.h"
#include "libavutil/opt.h"
#ifdef __cplusplus
}
#endif

void yuvToARGB(AVFrame *sourceAVFrame, uint8_t *dst_rgba);

bool matchYuv(int yuvFormat);

class BipVideoTracker : public MediaTracker {
private:
    AVFrame *rgb_frame = nullptr;
    float playSpeed = 1.0f;
    constexpr static const double AV_SYNC_THRESHOLD_MIN = 0.04;
    constexpr static const double AV_SYNC_THRESHOLD_MAX = 0.1;
    BipNativeWindow *bipNativeWindow = nullptr;
    AVCodecContext *avCodecContext = nullptr;
    SwsContext *swsContext{};

    void showRGBFrame() const;

    static AVCodec *getMediaCodec(AVCodecID codecID);

    static void *decodeThread(void *args);

    static void *playVideoThread(void *args);

public:
    double fps = 0;

    void pause();

    void setPlaySpeed(float speed);

    void decodeInner();

    void clearCache();

    void showVideoPacket();

    void setVideoSurface(ANativeWindow *nativeWindow);

    unsigned long getFrameSize();

    void play();

    void start();

    void stop();

    int getVideoWidth() const;

    int getVideoHeight() const;

    BipVideoTracker(BipNativeWindow *nativeWindow, AVCodecParameters *codecPar,
                    AVDictionary *codecDic, AVDictionary *swsDic, bool mediacodec);

    ~BipVideoTracker();

    void startDecodeThread();
};

#endif //BIPPLAYER_BIPVIDEOTRACKER_H
