//
// Created by welcomeworld on 2022/9/19.
//

#ifndef BIPPLAYER_BIPNATIVEWINDOW_H
#define BIPPLAYER_BIPNATIVEWINDOW_H

#include <android/native_window_jni.h>
#include <cstring>
#include <pthread.h>
class BipNativeWindow {
private:
    ANativeWindow *nativeWindow = nullptr;
    //视频缓冲区
    ANativeWindow_Buffer nativeWindowBuffer;

    pthread_mutex_t windowMutex{};
    
    void lock();
    
    void unlock();
public:
    void disPlay(uint8_t *src,int srcWidth,int videoWidth,int videoHeight);
    
    void setWindow(ANativeWindow *window);

    BipNativeWindow();
    ~BipNativeWindow();
};


#endif //BIPPLAYER_BIPNATIVEWINDOW_H
