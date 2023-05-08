#include "native-lib.h"

BipPlayer *getNativePlayer(JNIEnv *env, jobject instance) {
    return (BipPlayer *) (intptr_t) env->GetLongField(instance, BipPlayer::nativePlayerRefId);
}

void native_prepareAsync(JNIEnv *env, jobject instance, jobjectArray dataSources) {
    auto *bipPlayer = getNativePlayer(env, instance);
    auto arraySize = env->GetArrayLength(dataSources);
    if (arraySize == 0) {
        return;
    }
    auto *prepareDataSources = new std::deque<BipDataSource *>();
    for (int i = 0; i < arraySize; i++) {
        jobject dataSource = env->GetObjectArrayElement(dataSources, i);
        jclass DataSourceClass = env->GetObjectClass(dataSource);
        jfieldID sourceId = env->GetFieldID(DataSourceClass, "source", "Ljava/lang/String;");
        jfieldID isSingleSourceId = env->GetFieldID(DataSourceClass, "isSingleSource", "Z");
        jfieldID isSyncId = env->GetFieldID(DataSourceClass, "isSync", "Z");
        jfieldID videoEnableId = env->GetFieldID(DataSourceClass, "videoEnable", "Z");
        jfieldID audioEnableId = env->GetFieldID(DataSourceClass, "audioEnable", "Z");
        jfieldID seekPositionId = env->GetFieldID(DataSourceClass, "seekPosition", "J");
        jfieldID startOffsetId = env->GetFieldID(DataSourceClass, "startOffset", "J");
        auto prepareSource = new BipDataSource();
        auto sourceJava = static_cast<jstring>(env->GetObjectField(dataSource, sourceId));
        const char *cSource = env->GetStringUTFChars(sourceJava, nullptr);
        prepareSource->source = cSource;
        env->ReleaseStringUTFChars(sourceJava, cSource);
        prepareSource->isSingleSource = env->GetBooleanField(dataSource, isSingleSourceId);
        prepareSource->isSync = env->GetBooleanField(dataSource, isSyncId);
        prepareSource->videoEnable = env->GetBooleanField(dataSource, videoEnableId);
        prepareSource->audioEnable = env->GetBooleanField(dataSource, audioEnableId);
        prepareSource->seekPosition = env->GetLongField(dataSource, seekPositionId);
        prepareDataSources->push_back(prepareSource);
    }
    bipPlayer->postPrepare(prepareDataSources);
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
    LOGW("native_setOption set:%d %s %s", category, ckey, cvalue);
    bipPlayer->setOption(category, ckey, cvalue);
    env->ReleaseStringUTFChars(jkey, key);
    env->ReleaseStringUTFChars(jvalue, value);
}

void native_setup(JNIEnv *env, jobject instance, jobject weak_this) {
    auto *bipPlayer = new BipPlayer();
    BipPlayer::playerSetJavaWeak(bipPlayer, env->NewGlobalRef(weak_this));
    env->SetLongField(instance, BipPlayer::nativePlayerRefId, (intptr_t) (bipPlayer));
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
    bipPlayer->postSeekTo(time);
}

void native_stop(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    bipPlayer->postStop();
}

void native_pause(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    bipPlayer->postPause();
}

void native_start(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    bipPlayer->postStart();
}

void native_reset(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    bipPlayer->postReset();

}

void native_release(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    bipPlayer->postRelease();
}

void native_finalize(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    env->DeleteGlobalRef((jobject) bipPlayer->weakJavaThis);
    delete bipPlayer;
}

jint native_getFps(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    return bipPlayer->getFps();
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
    auto *raw = reinterpret_cast<uint8_t *>(y);
    BipPublisher *bipPublisher = getNativePublisher(env, instance);
    auto *yuvDest = static_cast<uint8_t *>(av_malloc(width * height * 3 / 2));
    long changeStartTime = av_gettime();
    libyuv::NV21ToI420(raw, width,
                       raw + (width * height), width,
                       yuvDest, width, yuvDest + (width * height), width / 2,
                       yuvDest + (width * height * 5 / 4), width / 2,
                       width, height);
    long changeEndTime = av_gettime();
    LOGE("yuv change spend %ld", (changeEndTime - changeStartTime) / 1000);
    bipPublisher->writeImage(yuvDest);
    env->ReleaseByteArrayElements(yuvData, y, JNI_COMMIT);
}

void native_writeAudio(JNIEnv *env, jobject instance, jbyteArray pcmData, int dataSize) {
    jbyte *y = env->GetByteArrayElements(pcmData, nullptr);
    auto *raw = reinterpret_cast<uint8_t *>(y);
    BipPublisher *bipPublisher = getNativePublisher(env, instance);
    auto *audioDest = static_cast<uint8_t *>(av_malloc(dataSize));
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
        {"_prepareAsync",      "([Lcom/github/welcomeworld/bipplayer/BipDataSource;)V", (void *) native_prepareAsync},
        {"_setVideoSurface",   "(Landroid/view/Surface;)V",                             (void *) setVideoSurface},
        {"native_setup",       "(Ljava/lang/Object;)V",                                 (void *) native_setup},
        {"getDuration",        "()J",                                                   (void *) getDuration},
        {"getCurrentPosition", "()J",                                                   (void *) getCurrentPosition},
        {"getVideoHeight",     "()I",                                                   (void *) getVideoHeight},
        {"getVideoWidth",      "()I",                                                   (void *) getVideoWidth},
        {"isPlaying",          "()Z",                                                   (void *) isPlaying},
        {"seekTo",             "(J)V",                                                  (void *) native_seekTo},
        {"pause",              "()V",                                                   (void *) native_pause},
        {"start",              "()V",                                                   (void *) native_start},
        {"stop",               "()V",                                                   (void *) native_stop},
        {"_reset",             "()V",                                                   (void *) native_reset},
        {"_release",           "()V",                                                   (void *) native_release},
        {"native_finalize",    "()V",                                                   (void *) native_finalize},
        {"setOption",          "(ILjava/lang/String;Ljava/lang/String;)V",              (void *) native_setOption},
        {"getFps",             "()I",                                                   (void *) native_getFps},
        {"getSpeed",           "()F",                                                   (void *) native_getSpeed},
        {"setSpeed",           "(F)V",                                                  (void *) native_setSpeed}
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
    BipPlayer::defaultBIPPlayerClass = (jclass) env->NewGlobalRef(
            env->FindClass("com/github/welcomeworld/bipplayer/DefaultBIPPlayer"));
    BipPlayer::postEventFromNativeMethodId = env->GetStaticMethodID(
            BipPlayer::defaultBIPPlayerClass,
            "postEventFromNative",
            "(Ljava/lang/Object;IIILjava/lang/Object;)V");
    BipPlayer::nativePlayerRefId = env->GetFieldID(BipPlayer::defaultBIPPlayerClass,
                                                   "nativePlayerRef", "J");

    jclass publishCls = (*env).FindClass("com/github/welcomeworld/bipplayer/DefaultBipPublisher");
    nativePublisherRefId = env->GetFieldID(publishCls, "nativePublisherRef", "J");
}

void custom_log(void *ptr, int level, const char *fmt, va_list vl) {
    if (level == AV_LOG_ERROR) {
        __android_log_vprint(ANDROID_LOG_ERROR, FFMPEG_LOG_TAG, fmt, vl);
    } else if (level == AV_LOG_WARNING) {
        __android_log_vprint(ANDROID_LOG_WARN, FFMPEG_LOG_TAG, fmt, vl);
    } else if (level == AV_LOG_DEBUG || level == AV_LOG_INFO) {
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
    BipPlayer::staticVm = vm;
    av_jni_set_java_vm(vm, nullptr);
    loadJavaId(env);
    av_log_set_callback(custom_log);
    return JNI_VERSION_1_6;
}