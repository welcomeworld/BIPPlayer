//
// Created by welcomeworld on 2022/5/9.
//

#ifndef BIPPLAYER_BIPPUBLISHER_H
#define BIPPLAYER_BIPPUBLISHER_H
#include "BipLog.h"
#include "fdAVIOContext.h"
#ifdef __cplusplus
extern "C" {
#endif
#include "libavformat/avformat.h"
#include <libavutil/time.h>
#include <pthread.h>
#ifdef __cplusplus
}
#endif

void *publishThread(void *args);

class BipPublisher {
public:
    const char *inputPath = nullptr;
    const char *outputPath = nullptr;
    pthread_t publishThreadId = 0;

    void publish();

    AVFormatContext *prepare_out(AVFormatContext *avFormatContext,const char *output);

    void writeOut(AVFormatContext *outputFormat,AVFormatContext *avFormatContext, AVPacket *avPacket);

    void writeCompleted(AVFormatContext *outputContext);

    BipPublisher();
    ~BipPublisher();
};


#endif //BIPPLAYER_BIPPUBLISHER_H
