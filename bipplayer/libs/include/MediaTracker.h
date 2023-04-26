//
// Created by welcomeworld on 3/13/23.
//

#ifndef BIPPLAYER_MEDIATRACKER_H
#define BIPPLAYER_MEDIATRACKER_H

#include <atomic>
#include "BipTrackerCallback.h"
#include "BipFrameQueue.h"
#include "BipPacketQueue.h"
#include "BipClock.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

class MediaTracker {
protected:
    BipFrameQueue *bipFrameQueue = nullptr;
    BipPacketQueue *bipPacketQueue = nullptr;
    double trackerClock{};//内部时钟,单位秒
    pthread_t decodeThreadId = 0;
    pthread_t playThreadId = 0;//音频处理线程id
public:
    static const int STATE_ERROR = -1;
    static const int STATE_CREATED = 0;
    static const int STATE_START = 1;
    static const int STATE_PLAYING = 2;
    static const int STATE_PAUSE = 3;
    static const int STATE_STOP = 4;
    static const int STATE_DESTROY = 5;
    std::atomic<int> trackerState;
    SyncClock *shareClock = nullptr;
    double trackTimeBase{};

    //AVFrame缓存大小，单位字节,默认25M
    int maxFrameBufSize = 1024 * 1024 * 25;
    //AVPacket缓存大小，单位字节,默认15M
    int maxPacketBufSize = 1024 * 1024 * 15;

    bool isCacheCompleted = false;

    bool clockMaintain = false;

    int bufferingTimes = 1;

    int bufferingReadyFrameSize = 5;

    long duration = -1;//单位毫秒

    int bufferPercent = 0;

    BipTrackerCallback *trackerCallback = nullptr;

    bool isStart();

    bool isPlaying();

    virtual void play() = 0;

    virtual void pause() = 0;

    void pushPacket(AVPacket *packet);

    virtual void setPlaySpeed(float speed) = 0;

    virtual unsigned long getFrameSize() = 0;

    bool isFrameReady();

    virtual void startDecodeThread() = 0;
};

#endif //BIPPLAYER_MEDIATRACKER_H
