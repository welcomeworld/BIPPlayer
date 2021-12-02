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
#include <map>
#include <sys/prctl.h>

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

void clear(std::queue<AVFrame *> &q);

void *cacheVideoFrameThread(void *args);

void *cacheAudioFrameThread(void *args);

enum PlayerState {
    STATE_RELEASE = 0,
    STATE_UN_DEFINE, ///< Undefined
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
    ERROR_STATE_ILLEGAL,
    ERROR_PREPARE_FAILED
};

struct ptrCmp {
    bool operator()(const char *s1, const char *s2) const {
        return strcmp(s1, s2) < 0;
    }
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

    static const int OPT_CATEGORY_FORMAT = 1;
    static const int OPT_CATEGORY_CODEC = 2;
    static const int OPT_CATEGORY_SWS = 3;
    static const int OPT_CATEGORY_PLAYER = 4;

    int max_frame_buff_size = 100;
    int min_frame_buff_size = 50;

    std::map<const char *, const char *, ptrCmp> formatOps;
    std::map<const char *, const char *, ptrCmp> codecOps;
    std::map<const char *, const char *, ptrCmp> playerOps;
    std::map<const char *, const char *, ptrCmp> swsOps;

    //创建引擎
    void createEngine();

    //创建混音器
    void createMixVolume();

    //创建播放器
    void createPlayer();

    int getPcm();

    void postEventFromNative(int what, int arg1, int arg2, void *object) const;

    void notifyPrepared();

    void notifyCompleted();

    void killAllThread();

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

    void setOption(int category, const char *key, const char *value);

    void cacheVideo();

    void cacheAudio();

    BipPlayer();

    ~BipPlayer();

public:

    //播放状态
    PlayerState playState = STATE_UN_DEFINE;
    long duration = -1;//单位毫秒
    void *weakJavaThis;
    const char *inputPath;
    pthread_t prepareThreadId = 0;
    AVFormatContext *avFormatContext;
    pthread_mutex_t avOpsMutex;

    //video
    AVCodecContext *avCodecContext = nullptr;
    SwsContext *swsContext;
    ANativeWindow *nativeWindow = nullptr;
    int video_index = -1;
    //视频Packet队列
    std::queue<AVPacket *> videoPacketQueue;
    std::queue<AVFrame *> videoFrameQueue;
    pthread_t cacheVideoThreadId = 0;
    pthread_mutex_t videoMutex;
    pthread_cond_t videoCond;
    pthread_mutex_t videoFrameMutex;
    pthread_cond_t videoFrameCond;
    pthread_cond_t videoFrameEmptyCond;
    AVRational videoTimeBase;
    double videoClock;//视频时钟,单位秒
    pthread_t videoPlayId = 0;//视频处理线程id
    AVFrame *rgb_frame = nullptr;

    //audio
    //音频解码上下文
    AVCodecContext *audioCodecContext = nullptr;
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
    std::queue<AVFrame *> audioFrameQueue;
    pthread_t cacheAudioThreadId = 0;
    pthread_mutex_t audioMutex;
    pthread_cond_t audioCond;
    pthread_mutex_t audioFrameMutex;
    pthread_cond_t audioFrameCond;
    pthread_cond_t audioFrameEmptyCond;
    //音频重采样缓冲区
    uint8_t *audioBuffer;
    //音频重采样声道
    uint64_t outChLayout = AV_CH_LAYOUT_STEREO;
    //音频重采样通道数
    int outChannelsNumber = 2;
    int audio_index = -1;
    AVRational audioTimeBase;
    double audioClock;//音频时钟,单位秒
    pthread_t audioPlayId = 0;//音频处理线程id

};

void playerSetJavaWeak(BipPlayer *bipPlayer, void *weak_this);

#endif //BIPPLAYER_BIPPLAYER_H
