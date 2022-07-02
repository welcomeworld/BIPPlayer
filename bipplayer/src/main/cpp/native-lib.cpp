#include "native-lib.h"

BipPlayer *getNativePlayer(JNIEnv *env, jobject instance) {
    return (BipPlayer *) (intptr_t) env->GetLongField(instance, nativePlayerRefId);
}

void native_prepareAsync(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    bipPlayer->notifyMsg(MSG_PREPARE);
}

void setVideoSurface(JNIEnv *env, jobject instance, jobject surface) {
    auto *bipPlayer = getNativePlayer(env, instance);
    if (surface != nullptr) {
        bipPlayer->setVideoSurface(ANativeWindow_fromSurface(env, surface));
    } else {
        bipPlayer->setVideoSurface(nullptr);
    }
}

void native_setOption(JNIEnv *env, jobject instance, jint category, jstring jkey, jstring jvalue) {
    auto *bipPlayer = getNativePlayer(env, instance);
    const char *key = env->GetStringUTFChars(jkey, nullptr);
    const char *value = env->GetStringUTFChars(jvalue, nullptr);
    char *ckey = static_cast<char *>(malloc(strlen(key) + 1));
    char *cvalue = static_cast<char *>(malloc(strlen(value) + 1));
    strcpy(ckey, key);
    strcpy(cvalue, value);
    bipPlayer->setOption(category, ckey, cvalue);
    env->ReleaseStringUTFChars(jkey, key);
    env->ReleaseStringUTFChars(jvalue, value);
}

void native_setup(JNIEnv *env, jobject instance, jobject weak_this) {
    auto *bipPlayer = new BipPlayer();
    playerSetJavaWeak(bipPlayer, env->NewGlobalRef(weak_this));
    env->SetLongField(instance, nativePlayerRefId, (intptr_t) (bipPlayer));
}

jlong getDuration(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    return bipPlayer->getDuration();
}

jlong getCurrentPosition(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    return bipPlayer->getCurrentPosition();
}

jint getVideoHeight(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    return bipPlayer->getVideoHeight();
}

jint getVideoWidth(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    return bipPlayer->getVideoWidth();
}

jboolean isPlaying(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    return bipPlayer->isPlaying();

}

void native_seekTo(JNIEnv *env, jobject instance, jlong time) {
    auto *bipPlayer = getNativePlayer(env, instance);
    auto *message = new BIPMessage();
    message->what = MSG_SEEK;
    message->arg1 = time;
    bipPlayer->notifyMsg(message);
}

void native_stop(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    bipPlayer->notifyMsg(MSG_STOP);
}

void native_pause(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    bipPlayer->notifyMsg(MSG_PAUSE);
}

void native_start(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    bipPlayer->notifyMsg(MSG_START);
}

void native_reset(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    bipPlayer->notifyMsg(MSG_RESET);

}

void native_release(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    bipPlayer->notifyMsg(MSG_RELEASE);
}

void native_finalize(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    env->DeleteGlobalRef((jobject) bipPlayer->weakJavaThis);
    delete bipPlayer;
}

void native_prepare_next(JNIEnv *env, jobject instance, jstring inputPath_, jboolean dash) {
    auto *bipPlayer = getNativePlayer(env, instance);
    const char *inputPath = env->GetStringUTFChars(inputPath_, nullptr);
    char *cinputPath = static_cast<char *>(malloc(strlen(inputPath) + 1));
    strcpy(cinputPath, inputPath);
    auto *message = new BIPMessage();
    message->what = MSG_PREPARE_NEXT;
    message->arg1 = dash;
    message->obj = cinputPath;
    bipPlayer->notifyMsg(message);
    env->ReleaseStringUTFChars(inputPath_, inputPath);
}

jint native_getFps(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    return bipPlayer->getFps();
}

void message_delete_str_callback(void *object) {
    free((void *) object);
}

void native_setDataSource(JNIEnv *env, jobject instance, jstring inputPath_) {
    auto *bipPlayer = getNativePlayer(env, instance);
    const char *inputPath = env->GetStringUTFChars(inputPath_, nullptr);
    char *cinputPath = static_cast<char *>(malloc(strlen(inputPath) + 1));
    strcpy(cinputPath, inputPath);
    auto *message = new BIPMessage();
    message->what = MSG_SET_DATA_SOURCE;
    message->obj = cinputPath;
    message->free_l = message_delete_str_callback;
    bipPlayer->notifyMsg(message);
    env->ReleaseStringUTFChars(inputPath_, inputPath);
}

BipPublisher *getNativePublisher(JNIEnv *env, jobject instance) {
    return (BipPublisher *) (intptr_t) env->GetLongField(instance, nativePublisherRefId);
}

void native_create(JNIEnv *env, jobject instance) {
    auto *bipPublisher = new BipPublisher();
    env->SetLongField(instance, nativePublisherRefId, (intptr_t) (bipPublisher));
}

void native_writeImage(JNIEnv *env, jobject instance, jbyteArray yuvData, jint width, jint height) {
    jbyte *y = env->GetByteArrayElements(yuvData, nullptr);
    uint8_t *raw = reinterpret_cast<uint8_t *>(y);
    BipPublisher *bipPublisher = getNativePublisher(env, instance);
    uint8_t *yuvDest = static_cast<uint8_t *>(av_malloc(width * height * 3 / 2));
//    uint8_t *rotateDest = static_cast<uint8_t *>(av_malloc(width * height * 3 / 2));
    long changeStartTime = av_gettime();
    libyuv::NV21ToI420(raw, width,
                       raw + (width * height), width,
                       yuvDest, width, yuvDest + (width * height), width / 2,
                       yuvDest + (width * height * 5 / 4), width / 2,
                       width, height);
    long changeEndTime = av_gettime();
    LOGE("yuv change spend %ld", (changeEndTime - changeStartTime) / 1000);
//    libyuv::I420Rotate(yuvDest, width, yuvDest + (width * height), width / 2,
//                       yuvDest + (width * height * 5 / 4), width / 2, rotateDest, height,
//                       rotateDest + (width * height), height / 2,
//                       rotateDest + (width * height * 5 / 4), height / 2, width, height,
//                       libyuv::kRotate90);
//    av_free(yuvDest);
    bipPublisher->writeImage(yuvDest);
    env->ReleaseByteArrayElements(yuvData, y, JNI_COMMIT);
}

void native_writeAudio(JNIEnv *env, jobject instance, jbyteArray pcmData, int dataSize) {
    jbyte *y = env->GetByteArrayElements(pcmData, nullptr);
    uint8_t *raw = reinterpret_cast<uint8_t *>(y);
    BipPublisher *bipPublisher = getNativePublisher(env, instance);
    uint8_t *audioDest = static_cast<uint8_t *>(av_malloc(dataSize));
    memcpy(audioDest, raw, dataSize);
    bipPublisher->writeAudio(audioDest, dataSize);
    av_free(audioDest);
    env->ReleaseByteArrayElements(pcmData, y, JNI_COMMIT);
}

void native_writeCompleted(JNIEnv *env, jobject instance) {
    BipPublisher *bipPublisher = getNativePublisher(env, instance);
    bipPublisher->writeCompleted();
}

void native_prepare_publish(JNIEnv *env, jobject instance, jstring inputPath_, jstring output_) {
    BipPublisher *bipPublisher = getNativePublisher(env, instance);
    if (inputPath_ != nullptr) {
        const char *inputPath = env->GetStringUTFChars(inputPath_, nullptr);
        char *cinputPath = static_cast<char *>(malloc(strlen(inputPath) + 1));
        strcpy(cinputPath, inputPath);
        bipPublisher->inputPath = cinputPath;
        env->ReleaseStringUTFChars(inputPath_, inputPath);
    } else {
        bipPublisher->inputPath = nullptr;
    }
    const char *output = env->GetStringUTFChars(output_, nullptr);
    char *coutput = static_cast<char *>(malloc(strlen(output) + 1));
    strcpy(coutput, output);
    bipPublisher->outputPath = coutput;
    pthread_create(&(bipPublisher->publishThreadId), nullptr, publishThread,
                   bipPublisher);//开启begin线程
    env->ReleaseStringUTFChars(output_, output);
}

void native_setPublishFps(JNIEnv *env, jobject instance, jint fps) {
    BipPublisher *bipPublisher = getNativePublisher(env, instance);
    bipPublisher->fps = fps;
}

void native_setPublishVideoInfo(JNIEnv *env, jobject instance, jint width, jint height) {
    BipPublisher *bipPublisher = getNativePublisher(env, instance);
    bipPublisher->width = width;
    bipPublisher->height = height;
}

void native_setDataSourceFd(JNIEnv *env, jobject instance, jint fd) {
    auto *bipPlayer = getNativePlayer(env, instance);
    char *cinputPath = static_cast<char *>(calloc(1, 28));
    int dupFd = dup(fd);
    snprintf(cinputPath, sizeof(cinputPath), "fd:%d", dupFd);
    auto *message = new BIPMessage();
    message->what = MSG_SET_DATA_SOURCE;
    message->obj = cinputPath;
    message->free_l = message_delete_str_callback;
    bipPlayer->notifyMsg(message);
}

void native_setSpeed(JNIEnv *env, jobject instance, jfloat speed) {
    auto *bipPlayer = getNativePlayer(env, instance);
    bipPlayer->setPlaySpeed(speed);
}

jfloat native_getSpeed(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    if (bipPlayer != nullptr) {
        return bipPlayer->playSpeed;
    }
    return 1;
}

static JNINativeMethod methods[] = {
        {"_prepareAsync",      "()V",                                      (void *) native_prepareAsync},
        {"_setVideoSurface",   "(Landroid/view/Surface;)V",                (void *) setVideoSurface},
        {"native_setup",       "(Ljava/lang/Object;)V",                    (void *) native_setup},
        {"getDuration",        "()J",                                      (void *) getDuration},
        {"getCurrentPosition", "()J",                                      (void *) getCurrentPosition},
        {"getVideoHeight",     "()I",                                      (void *) getVideoHeight},
        {"getVideoWidth",      "()I",                                      (void *) getVideoWidth},
        {"isPlaying",          "()Z",                                      (void *) isPlaying},
        {"seekTo",             "(J)V",                                     (void *) native_seekTo},
        {"pause",              "()V",                                      (void *) native_pause},
        {"start",              "()V",                                      (void *) native_start},
        {"stop",               "()V",                                      (void *) native_stop},
        {"_reset",             "()V",                                      (void *) native_reset},
        {"_release",           "()V",                                      (void *) native_release},
        {"native_finalize",    "()V",                                      (void *) native_finalize},
        {"setOption",          "(ILjava/lang/String;Ljava/lang/String;)V", (void *) native_setOption},
        {"_prepare_next",      "(Ljava/lang/String;Z)V",                   (void *) native_prepare_next},
        {"getFps",             "()I",                                      (void *) native_getFps},
        {"_setDataSource",     "(Ljava/lang/String;)V",                    (void *) native_setDataSource},
        {"_setDataSourceFd",   "(I)V",                                     (void *) native_setDataSourceFd},
        {"getSpeed",           "()F",                                      (void *) native_getSpeed},
        {"setSpeed",           "(F)V",                                     (void *) native_setSpeed}
};

static JNINativeMethod publishMethods[] = {
        {"writeImage",          "([BII)V",                                 (void *) native_writeImage},
        {"native_setup",        "()V",                                     (void *) native_create},
        {"writeCompleted",      "()V",                                     (void *) native_writeCompleted},
        {"writeAudio",          "([BI)V",                                  (void *) native_writeAudio},
        {"prepare",             "(Ljava/lang/String;Ljava/lang/String;)V", (void *) native_prepare_publish},
        {"setFps",              "(I)V",                                    (void *) native_setPublishFps},
        {"setPublishVideoInfo", "(II)V",                                   (void *) native_setPublishVideoInfo}
};

void loadJavaId(JNIEnv *env) {
    defaultBIPPlayerClass = (jclass) env->NewGlobalRef(
            env->FindClass("com/github/welcomeworld/bipplayer/DefaultBIPPlayer"));
    postEventFromNativeMethodId = env->GetStaticMethodID(defaultBIPPlayerClass,
                                                         "postEventFromNative",
                                                         "(Ljava/lang/Object;IIILjava/lang/Object;)V");
    nativePlayerRefId = env->GetFieldID(defaultBIPPlayerClass, "nativePlayerRef", "J");

    jclass publishCls = (*env).FindClass("com/github/welcomeworld/bipplayer/DefaultBipPublisher");
    nativePublisherRefId = env->GetFieldID(publishCls, "nativePublisherRef", "J");
}

void custom_log(void *ptr, int level, const char *fmt, va_list vl) {
    if (level == AV_LOG_ERROR) {
        __android_log_vprint(ANDROID_LOG_ERROR, FFMPEG_LOG_TAG, fmt, vl);
    } else if (level == AV_LOG_WARNING) {
        __android_log_vprint(ANDROID_LOG_WARN, FFMPEG_LOG_TAG, fmt, vl);
    } else {
        __android_log_vprint(ANDROID_LOG_DEBUG, FFMPEG_LOG_TAG, fmt, vl);
    }
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env = nullptr;
    jclass cls;
    if ((*vm).GetEnv((void **) &env, JNI_VERSION_1_6) != JNI_OK) {
        printf("onLoad error!!");
        return JNI_ERR;
    }
    cls = (*env).FindClass("com/github/welcomeworld/bipplayer/DefaultBIPPlayer");
    jclass publishCls = (*env).FindClass("com/github/welcomeworld/bipplayer/DefaultBipPublisher");
    (*env).RegisterNatives(cls, methods, sizeof(methods) / sizeof(JNINativeMethod));
    (*env).RegisterNatives(publishCls, publishMethods,
                           sizeof(publishMethods) / sizeof(JNINativeMethod));
    staticVm = vm;
    av_jni_set_java_vm(vm, 0);
    loadJavaId(env);
    av_log_set_callback(custom_log);
    return JNI_VERSION_1_6;
}