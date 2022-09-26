//
// Created by welcomeworld on 2022/9/19.
//

#include "BipNativeWindow.h"

BipNativeWindow::~BipNativeWindow() {
    if (nativeWindow != nullptr) {
        ANativeWindow_release(nativeWindow);
        nativeWindow = nullptr;
    }
    pthread_mutex_destroy(&windowMutex);
}

BipNativeWindow::BipNativeWindow() {
    pthread_mutex_init(&windowMutex, nullptr);
}

void BipNativeWindow::disPlay(uint8_t *src, int srcWidth, int videoWidth, int videoHeight) {
    lock();
    if (nativeWindow == nullptr) {
        return;
    }
    //配置nativeWindow
    ANativeWindow_setBuffersGeometry(nativeWindow, videoWidth,
                                     videoHeight, WINDOW_FORMAT_RGBA_8888);
    //上锁
    if (ANativeWindow_lock(nativeWindow, &nativeWindowBuffer, nullptr)) {
        //锁定窗口失败
        return;
    }
    unlock();
    //  rgb_frame是有画面数据
    auto *dst = static_cast<uint8_t *>(nativeWindowBuffer.bits);
    //拿到一行有多少个字节 RGBA nativeWindow缓冲区中每一行长度不一定等于视频宽度
    int destStride = nativeWindowBuffer.stride * 4;
    for (int i = 0; i < videoHeight; i++) {
        //必须将rgb_frame的数据一行一行复制给nativewindow
        memcpy(dst + i * destStride, src + i * srcWidth, static_cast<size_t>(srcWidth));
    }
    lock();
    if (nativeWindow == nullptr) {
        return;
    }
    ANativeWindow_unlockAndPost(nativeWindow);
    unlock();
}

void BipNativeWindow::setWindow(ANativeWindow *window) {
    lock();
    //申请ANativeWindow
    if (nativeWindow != nullptr) {
        ANativeWindow_release(nativeWindow);
        nativeWindow = nullptr;
    }
    nativeWindow = window;
    unlock();
}

void BipNativeWindow::lock() {
    pthread_mutex_lock(&windowMutex);
}

void BipNativeWindow::unlock() {
    pthread_mutex_unlock(&windowMutex);
}
