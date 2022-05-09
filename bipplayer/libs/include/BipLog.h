//
// Created by welcomeworld on 2022/5/9.
//

#ifndef BIPPLAYER_BIPLOG_H
#define BIPPLAYER_BIPLOG_H
#include <android/log.h>

#define BLOGW  true
#define BLOGE  true
#define BLOGD  true

#if BLOGE
#define LOGE(FORMAT, ...) __android_log_print(ANDROID_LOG_ERROR,"DefaultBIPPlayerNative",FORMAT,##__VA_ARGS__)
#else
#define LOGE(FORMAT, ...) loge(FORMAT,##__VA_ARGS__)
#endif
#if BLOGW
#define LOGW(FORMAT, ...) __android_log_print(ANDROID_LOG_WARN,"DefaultBIPPlayerNative",FORMAT,##__VA_ARGS__)
#else
#define LOGW(FORMAT, ...) logw(FORMAT,##__VA_ARGS__)
#endif
#if BLOGD
#define LOGD(FORMAT, ...) __android_log_print(ANDROID_LOG_DEBUG,"DefaultBIPPlayerNative",FORMAT,##__VA_ARGS__)
#else
#define LOGD(FORMAT, ...) logd(FORMAT,##__VA_ARGS__)
#endif


#endif //BIPPLAYER_BIPLOG_H
