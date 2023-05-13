//
// Created by welcomeworld on 2/27/23.
//
#include "BipMessage.h"

BipMessage *MessageQueue::next() {
    pthread_mutex_lock(&msgMutex);
    if (startMsg == nullptr) {
        pthread_cond_wait(&msgCond, &msgMutex);
    }
    BipMessage *current = startMsg;
    if (current != nullptr) {
        if (startMsg == endMsg) {
            startMsg = nullptr;
            endMsg = nullptr;
        } else {
            startMsg = current->next;
        }
    }
    pthread_mutex_unlock(&msgMutex);
    return current;
}

MessageQueue::MessageQueue() {
    pthread_mutex_init(&msgMutex, nullptr);
    pthread_cond_init(&msgCond, nullptr);
}

MessageQueue::~MessageQueue() {
    while (startMsg != nullptr) {
        BipMessage *processMsg = startMsg;
        startMsg = processMsg->next;
        if (processMsg->free_l != nullptr) {
            processMsg->free_l(processMsg->obj);
            processMsg->free_l = nullptr;
        }
        delete processMsg;
    }
    endMsg = nullptr;
    pthread_mutex_destroy(&msgMutex);
    pthread_cond_destroy(&msgCond);
}

void MessageQueue::scheduleMsg(BipMessage *message) {
    pthread_mutex_lock(&msgMutex);
    if (startMsg == nullptr) {
        startMsg = message;
        endMsg = message;
    } else {
        if (startMsg->what == message->what) {
            message->next = startMsg->next;
            if (startMsg == endMsg) {
                endMsg = message;
            }
            startMsg = message;
        } else {
            BipMessage *parentMessage = startMsg;
            BipMessage *checkMessage = parentMessage->next;
            bool hasSameMessage = false;
            while (checkMessage != nullptr) {
                if (checkMessage->what == message->what) {
                    message->next = checkMessage->next;
                    parentMessage->next = message;
                    if (checkMessage == endMsg) {
                        endMsg = message;
                    }
                    hasSameMessage = true;
                    break;
                }
                parentMessage = checkMessage;
                checkMessage = parentMessage->next;
            }
            if (!hasSameMessage) {
                endMsg->next = message;
                endMsg = message;
            }
        }
    }
    pthread_cond_signal(&msgCond);
    pthread_mutex_unlock(&msgMutex);
}
