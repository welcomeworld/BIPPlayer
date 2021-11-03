#include "native-lib.h"

BipPlayer *getNativePlayer(JNIEnv *env, jobject instance) {
    return (BipPlayer *) (intptr_t) env->GetLongField(instance, nativePlayerRefId);
}

jint native_prepareAsync(JNIEnv *env, jobject instance, jstring inputPath_) {
    auto *bipPlayer = getNativePlayer(env, instance);
    const char *inputPath = env->GetStringUTFChars(inputPath_, nullptr);
    bipPlayer->inputPath = inputPath;
    pthread_create(&(bipPlayer->prepareThreadId), nullptr, prepareVideoThread,
                   bipPlayer);//开启begin线程
//    env->ReleaseStringUTFChars(inputPath_, inputPath);
    return 0;
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
    char *ckey = static_cast<char *>(malloc(strlen(key)));
    char *cvalue = static_cast<char *>(malloc(strlen(value)));
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
    bipPlayer->seekTo(time);
}

void native_stop(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    bipPlayer->stop();
}

void native_pause(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    bipPlayer->pause();

}

void native_start(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    bipPlayer->start();
}

void native_reset(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    bipPlayer->reset();
}

void native_release(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    bipPlayer->release();
}

void native_finalize(JNIEnv *env, jobject instance) {
    auto *bipPlayer = getNativePlayer(env, instance);
    env->DeleteGlobalRef((jobject) bipPlayer->weakJavaThis);
    delete bipPlayer;
}

static JNINativeMethod methods[] = {
        {"_prepareAsync",      "(Ljava/lang/String;)I",                    (void *) native_prepareAsync},
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
        {"setOption",          "(ILjava/lang/String;Ljava/lang/String;)V", (void *) native_setOption}
};

void loadJavaId(JNIEnv *env) {
    defaultBIPPlayerClass = (jclass) env->NewGlobalRef(
            env->FindClass("com/github/welcomeworld/bipplayer/DefaultBIPPlayer"));
    postEventFromNativeMethodId = env->GetStaticMethodID(defaultBIPPlayerClass,
                                                         "postEventFromNative",
                                                         "(Ljava/lang/Object;IIILjava/lang/Object;)V");
    nativePlayerRefId = env->GetFieldID(defaultBIPPlayerClass, "nativePlayerRef", "J");
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env = nullptr;
    jclass cls;
    if ((*vm).GetEnv((void **) &env, JNI_VERSION_1_6) != JNI_OK) {
        printf("onLoad error!!");
        return JNI_ERR;
    }
    cls = (*env).FindClass("com/github/welcomeworld/bipplayer/DefaultBIPPlayer");
    (*env).RegisterNatives(cls, methods, sizeof(methods) / sizeof(JNINativeMethod));
    staticVm = vm;
    loadJavaId(env);
    return JNI_VERSION_1_6;
}