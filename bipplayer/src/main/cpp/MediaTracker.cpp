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
    bipPacketQueue->push(packet);
}

