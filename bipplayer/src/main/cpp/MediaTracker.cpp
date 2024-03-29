//
// Created by welcomeworld on 3/17/23.
//
#include "MediaTracker.h"

bool MediaTracker::isStart() {
    return trackerState >= STATE_START && trackerState < STATE_STOP;
}

bool MediaTracker::isPlaying() {
    return trackerState == STATE_PLAYING;
}

bool MediaTracker::isFrameReady() {
    return getFrameSize() >= bufferingTimes * bufferingReadyFrameSize ||
           bipFrameQueue->getQueueMemSize() >= bipFrameQueue->maxQueueMemSize ||
           isCacheCompleted;
}

void MediaTracker::pushPacket(AVPacket *packet) {
    if (!isStart()) {
        return;
    }
    bipPacketQueue->push(packet);
    if (duration > 0 && packet->dts != AV_NOPTS_VALUE) {
        double bufferPosition = trackTimeBase *
                                static_cast<double>(packet->dts) * 1000;
        auto durationD = static_cast<double>(duration);
        bufferPercent = static_cast<int>(round(bufferPosition * 100 / durationD));
    }
}

MediaTracker::MediaTracker() {
    pthread_mutex_init(&mutex, nullptr);
}

MediaTracker::~MediaTracker() {
    pthread_mutex_destroy(&mutex);
}

void MediaTracker::lock() {
    pthread_mutex_lock(&mutex);
}

void MediaTracker::unlock() {
    pthread_mutex_unlock(&mutex);
}

void MediaTracker::setMaxFrameBufSize(long maxSize) {
    bipFrameQueue->maxQueueMemSize = maxSize;
}

void MediaTracker::setMaxPacketBufSize(long maxSize) {
    bipPacketQueue->maxQueueMemSize = maxSize;
}

