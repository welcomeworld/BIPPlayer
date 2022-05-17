//
// Created by welcomeworld on 2022/5/9.
//

#ifndef BIPPLAYER_BIPPUBLISHER_H
#define BIPPLAYER_BIPPUBLISHER_H

#include <jni.h>
#include "BipLog.h"
#include "fdAVIOContext.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "libavformat/avformat.h"
#include <libavutil/time.h>
#include <pthread.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#ifdef __cplusplus
}
#endif

extern jfieldID nativePublisherRefId;

void *publishThread(void *args);

struct PublishInputContext {
    AVFormatContext *avFormatContext = nullptr;
    unsigned int nb_streams;
    AVCodecParameters *codecpars[5];
    AVRational timebase[5];
    AVCodec *avCodec[5];
};

class BipPublisher {
public:
    const char *inputPath = nullptr;
    const char *outputPath = nullptr;
    pthread_t publishThreadId = 0;
    AVFormatContext *outputFormat = nullptr;
    PublishInputContext inputContext{};
    AVCodecContext *videoCodecContext = nullptr;
    AVCodecContext *audioCodecContext = nullptr;
    SwrContext *audioSwrContext{};
    //not use when publish file
    int fps = 0;
    int width = 0;
    int height = 0;
    int64_t video_pts = 0;
    int64_t audio_pts = 0;

    void publish();

    void writeImage(uint8_t *data);

    void writeAudio(uint8_t *data, int size);

    int prepare_input();

    int prepare_output();

    void writePacket(AVPacket *avPacket);

    void writeCompleted();

    BipPublisher();

    ~BipPublisher();
};


#endif //BIPPLAYER_BIPPUBLISHER_H
