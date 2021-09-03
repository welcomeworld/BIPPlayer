//
// Created by welcomeworld on 2021/8/17.
//

#ifndef BIPPLAYER_NATIVE_LIB_H
#define BIPPLAYER_NATIVE_LIB_H

#include <jni.h>
#include <string>
#include <android/native_window_jni.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <queue>
#include <android/log.h>

enum PlayerState {
    STATE_UN_DEFINE = 0, ///< Undefined
    STATE_PREPARING,
    STATE_PLAYING,
    STATE_PAUSED,
    STATE_BUFFERING,
    STATE_ERROR,
    STATE_COMPLETED
};

#define LOGE(FORMAT, ...) __android_log_print(ANDROID_LOG_ERROR,"BipPlayerNativeLib",FORMAT,##__VA_ARGS__)
#define LOGD(FORMAT, ...) __android_log_print(ANDROID_LOG_DEBUG,"BipPlayerNativeLib",FORMAT,##__VA_ARGS__)
/* no AV sync correction is done if below the minimum AV sync threshold */
#define AV_SYNC_THRESHOLD_MIN 0.04
/* AV sync correction is done if above the maximum AV sync threshold */
#define AV_SYNC_THRESHOLD_MAX 0.1
/* If a frame duration is longer than this, it will not be duplicated to compensate AV sync */
#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1



#ifdef __cplusplus
extern "C" {
#endif
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
#include <libavutil/time.h>
#include <unistd.h>
#include <libswresample/swresample.h>
#include <pthread.h>
#ifdef __cplusplus
};
#endif

void createPlayer();

void createEngine();

void createMixVolume();

void *playAudio(void *args);

void *showVideoPacket(void *args);

void setVideoSurface(JNIEnv *env, jobject instance, jobject surface);

void bufferQueueCallback(
        SLAndroidSimpleBufferQueueItf caller,
        void *pContext
);

#endif //BIPPLAYER_NATIVE_LIB_H
