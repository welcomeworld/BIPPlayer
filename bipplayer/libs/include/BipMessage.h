//
// Created by welcomeworld on 2/27/23.
//

#ifndef BIPPLAYER_BIPMESSAGE_H
#define BIPPLAYER_BIPMESSAGE_H
#ifdef __cplusplus
extern "C" {
#endif
#include <pthread.h>
#ifdef __cplusplus
}
#endif

class BipMessage {
public:
    int arg1 = 0;
    int arg2 = 0;
    void *obj = nullptr;

    //仅用于消息没有被处理时释放对象，如果消息已被接收对象的释放与否由消息处理者负责
    void (*free_l)(void *obj) = nullptr;

    int what = 0;
    BipMessage *next = nullptr;
};

class MessageQueue {
public:
    BipMessage *next();

    void scheduleMsg(BipMessage *message);

    MessageQueue();

    ~MessageQueue();

private:
    BipMessage *startMsg = nullptr;
    BipMessage *endMsg = nullptr;
    pthread_cond_t msgCond{};
    pthread_mutex_t msgMutex{};
};

#endif //BIPPLAYER_BIPMESSAGE_H
