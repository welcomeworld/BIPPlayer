//
// Created by welcomeworld on 3/5/23.
//

#ifndef BIPPLAYER_BIPAUDIOTRACKER_H
#define BIPPLAYER_BIPAUDIOTRACKER_H

#include <queue>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include "SoundTouch.h"
#include "BipFrameQueue.h"
#include "BipClock.h"
#include "BipPacketQueue.h"
#include "BipLog.h"
#include "MediaTracker.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
#include "libswscale/swscale.h"
#include "libavutil/time.h"
#include <pthread.h>
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/avstring.h"
#ifdef __cplusplus
}
#endif

//音频重采样声道
#define OUT_CHANNEL_LAYOUT AV_CH_LAYOUT_STEREO
//音频重采样声道数
#define OUT_CHANNEL_NUMBER 2
//音频重采样率
#define OUT_SAMPLE_RATE 44100
//每个样本字节数
#define BYTES_PER_SAMPLE 2

class BipAudioTracker : public MediaTracker {
private:
    SLObjectItf engineObject{};//用SLObjectItf声明引擎接口对象
    SLEngineItf engineEngine{};//声明具体的引擎对象
    SLObjectItf outputMixObject{};//用SLObjectItf创建混音器接口对象
    SLEnvironmentalReverbItf outputMixEnvironmentalReverb{};////具体的混音器对象实例
    SLEnvironmentalReverbSettings settings = SL_I3DL2_ENVIRONMENT_PRESET_DEFAULT;//默认情况
    SLObjectItf audioPlayerObject{};//用SLObjectItf声明播放器接口对象
    SLPlayItf slPlayItf{};//播放器接口
    SLAndroidSimpleBufferQueueItf slBufferQueueItf{};//缓冲区队列接口
    //音频重采样上下文
    SwrContext *audioSwrContext{};
    //音频解码上下文
    AVCodecContext *audioCodecContext = nullptr;
    //音频重采样缓冲区
    uint8_t *audioBuffer{};
    soundtouch::SoundTouch *soundtouch = nullptr;

    //创建引擎
    bool createEngine();

    //创建混音器
    bool createMixVolume();

    //创建播放器
    bool createPlayer();

    void initSoundTouch();

    void destroyOpenSL();

    static void bufferQueueCallback(SLAndroidSimpleBufferQueueItf caller, void *pContext);

    void innerBufferQueueCallback();

    int getPcm();

    static void *decodeThread(void *args);

    static void *playThread(void *args);

public:

    void stop();

    void pause();

    void play();

    BipAudioTracker(AVCodecParameters *codecPar);

    ~BipAudioTracker();

    void playAudio();

    void setPlaySpeed(float speed);

    void clearCache();

    unsigned long getFrameSize();

    void decodeInner();

    void startDecodeThread();
};

#endif //BIPPLAYER_BIPAUDIOTRACKER_H
