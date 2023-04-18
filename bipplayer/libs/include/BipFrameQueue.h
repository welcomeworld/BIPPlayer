//
// Created by welcomeworld on 3/7/23.
//

#ifndef BIPPLAYER_BIPFRAMEQUEUE_H
#define BIPPLAYER_BIPFRAMEQUEUE_H

#include <queue>

#ifdef __cplusplus
extern "C" {
#endif
#include "libavutil/imgutils.h"
#include "libavutil/frame.h"
#include <pthread.h>
#ifdef __cplusplus
}
#endif

class BipFrameQueue {
    std::queue<AVFrame *> audioFrameQueue{};
    pthread_mutex_t audioFrameMutex{};
    pthread_cond_t audioFrameCond{};
    long queueMemSize = 0;

    long getFrameMemSize(AVFrame *frame) const;

public:
    bool isAudioFrame = false;

    long maxQueueMemSize = 1024 * 1024 * 15;

    void clear();

    AVFrame *pop();

    AVFrame *pop(bool block);

    void push(AVFrame *frame);

    unsigned long size();

    BipFrameQueue(long maxQueueSize, bool isAudioFrame);

    long getQueueMemSize() const;

    void notifyAll();

    ~BipFrameQueue();

};

#endif //BIPPLAYER_BIPFRAMEQUEUE_H
