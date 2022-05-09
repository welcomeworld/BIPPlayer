//
// Created by welcomeworld on 2021/8/17.
//

#ifndef BIPPLAYER_NATIVE_LIB_H
#define BIPPLAYER_NATIVE_LIB_H

#include <jni.h>
#include <string>
#include <android/native_window_jni.h>
#include "bipPlayer.h"
#include <unistd.h>
#include "BipPublisher.h"

#ifdef __cplusplus
extern "C" {
#endif
#include <pthread.h>
#include <libavcodec/jni.h>
#ifdef __cplusplus
};
#endif

void setVideoSurface(JNIEnv *env, jobject instance, jobject surface);

#endif //BIPPLAYER_NATIVE_LIB_H
