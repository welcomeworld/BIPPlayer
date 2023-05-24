//
// Created by welcomeworld on 3/10/23.
//

#ifndef BIPPLAYER_BIPPACKETQUEUE_H
#define BIPPLAYER_BIPPACKETQUEUE_H

#include <queue>

#ifdef __cplusplus
extern "C" {
#endif
#include "libavcodec/avcodec.h"
#include <pthread.h>
#ifdef __cplusplus
}
#endif

class BipPacketQueue {
    static const int MIN_PACKET_SIZE = 30;
    std::queue<AVPacket *> packetQueue{};
    pthread_mutex_t packetMutex{};
    pthread_cond_t packetCond{};
    long queueMemSize = 0;
public:
    long maxQueueMemSize = 1024 * 1024 * 15;

    void clear();

    AVPacket *pop();

    AVPacket *pop(bool block);

    void push(AVPacket *packet);

    unsigned long size();

    long getQueueMemSize() const;

    void notifyAll();

    BipPacketQueue();

    ~BipPacketQueue();

};

#endif //BIPPLAYER_BIPPACKETQUEUE_H
