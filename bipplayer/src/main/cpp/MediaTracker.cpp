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
           bipFrameQueue->getQueueMemSize() >= bipFrameQueue->maxQueueMemSize;
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

