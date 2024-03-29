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
    pthread_mutex_t mutex{};

    MediaTracker();

    ~MediaTracker();

    void lock();

    void unlock();

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

    void setMaxFrameBufSize(long maxSize);

    void setMaxPacketBufSize(long maxSize);
};

#endif //BIPPLAYER_MEDIATRACKER_H
