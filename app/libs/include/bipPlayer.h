//
// Created by welcomeworld on 2021/9/5.
//

#ifndef BIPPLAYER_BIPPLAYER_H
#define BIPPLAYER_BIPPLAYER_H

#include <queue>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <android/native_window_jni.h>
#include <android/log.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "libavformat/avformat.h"
#include <libswresample/swresample.h>
#include "libswscale/swscale.h"
#include <libavutil/time.h>
#include <pthread.h>
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#ifdef __cplusplus
};
#endif

#define LOGE(FORMAT, ...) __android_log_print(ANDROID_LOG_ERROR,"DefaultBIPPlayerNative",FORMAT,##__VA_ARGS__)
#define LOGW(FORMAT, ...) __android_log_print(ANDROID_LOG_WARN,"DefaultBIPPlayerNative",FORMAT,##__VA_ARGS__)


extern jclass defaultBIPPlayerClass;
extern jmethodID postEventFromNativeMethodId;
extern jfieldID nativePlayerRefId;
extern JavaVM *staticVm;

void *prepareVideoThread(void *args);
void clear(std::queue<AVPacket *> &q);

enum PlayerState {
    STATE_UN_DEFINE = 0, ///< Undefined
    STATE_ERROR,
    STATE_PREPARING,
    STATE_BUFFERING,
    STATE_PREPARED,
    STATE_PAUSED,
    STATE_PLAYING,
    STATE_COMPLETED
};

enum ErrorState {
    ERROR_NOT_PREPARED,
    ERROR_STATE_ILLEGAL
};

class BipPlayer {
private:

    static const int MEDIA_PREPARED = 1;
    static const int MEDIA_PLAYBACK_COMPLETE = 2;
    static const int MEDIA_BUFFERING_UPDATE = 3;
    static const int MEDIA_SEEK_COMPLETE = 4;
    static const int MEDIA_ERROR = 5;
    static const int MEDIA_PLAY_STATE_CHANGE = 6;

    constexpr static const double AV_SYNC_THRESHOLD_MIN = 0.04;
    constexpr static const double AV_SYNC_THRESHOLD_MAX = 0.1;


    //创建引擎
    void createEngine();

    //创建混音器
    void createMixVolume();

    //创建播放器
    void createPlayer();

    int getPcm();

    void postEventFromNative(int what, int arg1, int arg2, void *object) const;

public:

    void showVideoPacket();

    void playAudio();

    void start();

    void pause();

    void stop();

    void reset();

    void seekTo(long time);

    bool isPlaying() const;

    int getVideoWidth() const;

    int getVideoHeight() const;

    long getCurrentPosition() const;

    long getDuration() const;

    void prepare();

    void setVideoSurface(ANativeWindow *nativeWindow);

    void innerBufferQueueCallback();

    void release();

    BipPlayer();

    ~BipPlayer();

public:

    //播放状态
    PlayerState playState = STATE_UN_DEFINE;
    long duration = -1;//单位毫秒
    void *weakJavaThis;
    const char *inputPath;
    pthread_t prepareThreadId;
    AVFormatContext *avFormatContext;

    //video
    AVCodecContext *avCodecContext;
    SwsContext *swsContext;
    ANativeWindow *nativeWindow;
    int video_index = -1;
    //视频Packet队列
    std::queue<AVPacket *> videoPacketQueue;
    pthread_mutex_t videoMutex;
    pthread_cond_t videoCond;
    AVRational videoTimeBase;
    double videoClock;//视频时钟,单位秒
    pthread_t videoPlayId;//视频处理线程id
    AVFrame *rgb_frame;

    //audio
    //音频解码上下文
    AVCodecContext *audioCodecContext;
    //音频重采样上下文
    SwrContext *audioSwrContext;
    SLObjectItf engineObject;//用SLObjectItf声明引擎接口对象
    SLEngineItf engineEngine;//声明具体的引擎对象
    SLObjectItf outputMixObject;//用SLObjectItf创建混音器接口对象
    SLEnvironmentalReverbItf outputMixEnvironmentalReverb;////具体的混音器对象实例
    SLEnvironmentalReverbSettings settings = SL_I3DL2_ENVIRONMENT_PRESET_DEFAULT;//默认情况
    SLObjectItf audioPlayerObject;//用SLObjectItf声明播放器接口对象
    SLPlayItf slPlayItf;//播放器接口
    SLAndroidSimpleBufferQueueItf slBufferQueueItf;//缓冲区队列接口
    //音频Packet队列
    std::queue<AVPacket *> audioPacketQueue;
    pthread_mutex_t audioMutex;
    pthread_cond_t audioCond;
    //音频重采样缓冲区
    uint8_t *audioBuffer;
    //音频重采样声道
    uint64_t outChLayout = AV_CH_LAYOUT_STEREO;
    //音频重采样通道数
    int outChannelsNumber = 2;
    int audio_index = -1;
    AVRational audioTimeBase;
    double audioClock;//音频时钟,单位秒
    pthread_t audioPlayId;//音频处理线程id

};

void playerSetJavaWeak(BipPlayer *bipPlayer, void *weak_this);

#endif //BIPPLAYER_BIPPLAYER_H
