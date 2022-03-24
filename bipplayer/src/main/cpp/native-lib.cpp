#include "native-lib.h"

BipPlayer *getNativePlayer(JNIEnv *env, jobject instance) {
    return (BipPlayer *) (intptr_t) env->GetLongField(instance, nativePlayerRefId);
}

jint native_prepareAsync(JNIEnv *env, jobject instance, jstring inputPath_) {
    auto *bipPlayer = getNativePlayer(env, instance);
    const char *inputPath = env->GetStringUTFChars(inputPath_, nullptr);
    char *cinputPath = static_cast<char *>(malloc(strlen(inputPath)));
    strcpy(cinputPath, inputPath);
    auto *message = new BIPMessage();
    message->what = MSG_PREPARE;
    message->obj = cinputPath;
    bipPlayer->notifyMsg(message);
    env->ReleaseStringUTFChars(inputPath_, inputPath);
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
    char *cinputPath = static_cast<char *>(malloc(strlen(inputPath)));
    strcpy(cinputPath, inputPath);
    auto *message = new BIPMessage();
    message->what = MSG_PREPARE_NEXT;
    message->arg1 = dash;
    message->obj = cinputPath;
    bipPlayer->notifyMsg(message);
    env->ReleaseStringUTFChars(inputPath_, inputPath);
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
        {"setOption",          "(ILjava/lang/String;Ljava/lang/String;)V", (void *) native_setOption},
        {"_prepare_next",      "(Ljava/lang/String;Z)V",                   (void *) native_prepare_next}
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