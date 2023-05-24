//
// Created by welcomeworld on 3/7/23.
//
#include "BipFrameQueue.h"

void BipFrameQueue::clear() {
    notifyAll();
    pthread_mutex_lock(&audioFrameMutex);
    while (!audioFrameQueue.empty()) {
        AVFrame *frame = audioFrameQueue.front();
        av_frame_free(&frame);
        audioFrameQueue.pop();
    }
    queueMemSize = 0;
    pthread_mutex_unlock(&audioFrameMutex);
}

AVFrame *BipFrameQueue::pop(bool block) {
    pthread_mutex_lock(&audioFrameMutex);
    if (block && audioFrameQueue.empty()) {
        pthread_cond_wait(&audioFrameCond, &audioFrameMutex);
    }
    AVFrame *front = nullptr;
    if (!audioFrameQueue.empty()) {
        front = audioFrameQueue.front();
        audioFrameQueue.pop();
        queueMemSize -= getFrameMemSize(front);
        notifyAll();
    }
    pthread_mutex_unlock(&audioFrameMutex);
    return front;
}


BipFrameQueue::BipFrameQueue(bool isAudioFrame) {
    this->isAudioFrame = isAudioFrame;
    pthread_mutex_init(&audioFrameMutex, nullptr);
    pthread_cond_init(&audioFrameCond, nullptr);
}

BipFrameQueue::~BipFrameQueue() {
    pthread_mutex_destroy(&audioFrameMutex);
    pthread_cond_destroy(&audioFrameCond);
}

void BipFrameQueue::push(AVFrame *frame) {
    pthread_mutex_lock(&audioFrameMutex);
    if (size() > MIN_FRAME_SIZE && getQueueMemSize() > maxQueueMemSize) {
        pthread_cond_wait(&audioFrameCond, &audioFrameMutex);
    }
    audioFrameQueue.push(frame);
    queueMemSize += getFrameMemSize(frame);
    notifyAll();
    pthread_mutex_unlock(&audioFrameMutex);
}

unsigned long BipFrameQueue::size() {
    return audioFrameQueue.size();
}

AVFrame *BipFrameQueue::pop() {
    return pop(false);
}

long BipFrameQueue::getQueueMemSize() const {
    return queueMemSize;
}

long BipFrameQueue::getFrameMemSize(AVFrame *frame) const {
    if (isAudioFrame) {
        return av_samples_get_buffer_size(nullptr,
                                          frame->channels,
                                          frame->nb_samples,
                                          static_cast<AVSampleFormat>(frame->format),
                                          1);
    } else {
        return av_image_get_buffer_size(static_cast<AVPixelFormat>(frame->format),
                                        frame->width,
                                        frame->height, 1);
    }
}

void BipFrameQueue::notifyAll() {
    pthread_cond_broadcast(&audioFrameCond);
}


