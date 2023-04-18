//
// Created by welcomeworld on 3/10/23.
//
#include "BipPacketQueue.h"

void BipPacketQueue::clear() {
    notifyAll();
    pthread_mutex_lock(&packetMutex);
    while (!packetQueue.empty()) {
        AVPacket *packet = packetQueue.front();
        av_packet_free(&packet);
        packetQueue.pop();
    }
    pthread_mutex_unlock(&packetMutex);
}

AVPacket *BipPacketQueue::pop(bool block) {
    pthread_mutex_lock(&packetMutex);
    if (block && packetQueue.empty()) {
        pthread_cond_wait(&packetCond, &packetMutex);
    }
    AVPacket *front = nullptr;
    if (!packetQueue.empty()) {
        front = packetQueue.front();
        packetQueue.pop();
        queueMemSize -= front->size;
        notifyAll();
    }
    pthread_mutex_unlock(&packetMutex);
    return front;
}


BipPacketQueue::BipPacketQueue(long maxQueueSize) {
    this->maxQueueMemSize = maxQueueSize;
    pthread_mutex_init(&packetMutex, nullptr);
    pthread_cond_init(&packetCond, nullptr);
}

BipPacketQueue::~BipPacketQueue() {
    pthread_mutex_destroy(&packetMutex);
    pthread_cond_destroy(&packetCond);
}

void BipPacketQueue::push(AVPacket *packet) {
    pthread_mutex_lock(&packetMutex);
    if (getQueueMemSize() > maxQueueMemSize) {
        pthread_cond_wait(&packetCond, &packetMutex);
    }
    packetQueue.push(packet);
    queueMemSize += packet->size;
    notifyAll();
    pthread_mutex_unlock(&packetMutex);
}

unsigned long BipPacketQueue::size() {
    return packetQueue.size();
}

AVPacket *BipPacketQueue::pop() {
    return pop(false);
}

long BipPacketQueue::getQueueMemSize() const {
    return queueMemSize;
}

void BipPacketQueue::notifyAll() {
    pthread_cond_broadcast(&packetCond);
}

