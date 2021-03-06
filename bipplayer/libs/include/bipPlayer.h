//
// Created by welcomeworld on 2021/9/5.
//

#ifndef BIPPLAYER_BIPPLAYER_H
#define BIPPLAYER_BIPPLAYER_H

#include <queue>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <android/native_window_jni.h>
#include <map>
#include <sys/prctl.h>
#include "libyuv.h"
#include "fdAVIOContext.h"
#include "bipPlayerOptions.h"
#include "SoundTouch.h"
#include "BipLog.h"

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

#define MSG_SEEK 0x233
#define MSG_STOP 0x234
#define MSG_START 0x235
#define MSG_PAUSE 0x236
#define MSG_RESET 0x237
#define MSG_RELEASE 0x238
#define MSG_PREPARE_NEXT 0x239
#define MSG_PREPARE 0x240
#define MSG_SET_DATA_SOURCE 0x241


extern jclass defaultBIPPlayerClass;
extern jmethodID postEventFromNativeMethodId;
extern jfieldID nativePlayerRefId;
extern JavaVM *staticVm;

void *prepareVideoThread(void *args);

void *prepareNextVideoThread(void *args);

void clear(std::queue<AVPacket *> &q);

void clear(std::queue<AVFrame *> &q);

void *cacheVideoFrameThread(void *args);

void *cacheAudioFrameThread(void *args);

void yuvToARGB(AVFrame *sourceAVFrame, uint8_t *dst_rgba);

bool matchYuv(int yuvFormat);

AVCodec *getMediaCodec(AVCodecID codecID);

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

class InterruptContext {
public:
    timeval readStartTime;
    unsigned long audioSize = 0;
    unsigned long videoSize = 0;
    bool audioBuffering = false;

    void reset();
};

class BIPMessage {
public:
    int arg1 = 0;
    int arg2 = 0;
    void *obj = nullptr;

    void (*free_l)(void *obj) = nullptr;

    int what = 0;
};

class BipPlayer {
private:

    static const int MEDIA_PREPARED = 1;
    static const int MEDIA_PLAYBACK_COMPLETE = 2;
    static const int MEDIA_BUFFERING_UPDATE = 3;
    static const int MEDIA_SEEK_COMPLETE = 4;
    static const int MEDIA_ERROR = 5;
    static const int MEDIA_PLAY_STATE_CHANGE = 6;
    static const int MEDIA_INFO = 7;
    static const int MEDIA_PLAYER_MESSAGE = 8;
    constexpr static const double AV_SYNC_THRESHOLD_MIN = 0.04;
    constexpr static const double AV_SYNC_THRESHOLD_MAX = 0.1;

    static const int OPT_CATEGORY_FORMAT = 1;
    static const int OPT_CATEGORY_CODEC = 2;
    static const int OPT_CATEGORY_SWS = 3;
    static const int OPT_CATEGORY_PLAYER = 4;

    static const int MEDIA_INFO_BUFFERING_START = 0;
    static const int MEDIA_INFO_BUFFERING_END = 1;
    static const int MEDIA_INFO_FPS = 2;


    int max_frame_buff_size = 20;
    int min_frame_buff_size = 10;
    int prepare_frame_buff_size = 10;
    int bufferPercent = 0;

    std::queue<AVPacket *> nextVideoPacketQueue;
    std::queue<AVFrame *> nextVideoFrameQueue;
    std::queue<AVPacket *> nextAudioPacketQueue;
    std::queue<AVFrame *> nextAudioFrameQueue;

    std::map<const char *, const char *, ptrCmp> formatOps;
    std::map<const char *, const char *, ptrCmp> codecOps;
    std::map<const char *, const char *, ptrCmp> swsOps;
    std::queue<BIPMessage *> msgQueue;
    pthread_mutex_t msgMutex{};
    pthread_cond_t msgCond{};

    InterruptContext *interruptContext;
    InterruptContext *nextInterruptContext;

    FdAVIOContext *fdAvioContext = nullptr;

    bool mediacodec = false;
    bool startOnPrepared = false;

    soundtouch::SoundTouch *soundtouch = nullptr;

    //创建引擎
    void createEngine();

    //创建混音器
    void createMixVolume();

    //创建播放器
    void createPlayer();

    void initSoundTouch();

    int getPcm();

    void postEventFromNative(int what, int arg1, int arg2, void *object) const;

    void notifyPrepared();

    void notifyCompleted();

    void notifyInfo(int info);

    void waitAllThreadStop();

    bool videoAvailable();

    bool audioAvailable();

    void checkPrepared();

    void lockAll();

    void unLockAll();

    void freeContexts();

    void destroyOpenSL();

    void showRGBFrame();

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

    void prepareNext();

    void msgLoop();

    void notifyMsg(BIPMessage *bipMessage);

    void notifyMsg(int what);

    BipPlayer();

    ~BipPlayer();

    int getFps() const;

    void setPlaySpeed(float speed);

public:

    //播放状态
    PlayerState playState = STATE_UN_DEFINE;
    long duration = -1;//单位毫秒
    void *weakJavaThis = nullptr;
    const char *inputPath = nullptr;
    bool nextIsDash = true;
    char *nextInputPath = nullptr;
    char *dashInputPath = nullptr;
    pthread_t prepareThreadId = 0;
    pthread_t prepareNextThreadId = 0;
    pthread_t msgLoopThreadId = 0;
    AVFormatContext *avFormatContext{};
    pthread_mutex_t avOpsMutex{};
    double baseClock{}; //基准时钟，单位位秒
    int fps = 0;
    float playSpeed = 1.0f;

    //video
    AVCodecContext *avCodecContext = nullptr;
    SwsContext *swsContext{};
    ANativeWindow *nativeWindow = nullptr;
    //视频缓冲区
    ANativeWindow_Buffer nativeWindowBuffer;
    int video_index = -1;
    //视频Packet队列
    std::queue<AVPacket *> videoPacketQueue;
    std::queue<AVFrame *> videoFrameQueue;
    pthread_t cacheVideoThreadId = 0;
    pthread_mutex_t videoMutex{};
    pthread_mutex_t videoCacheMutex{};
    pthread_cond_t videoCond{};
    pthread_mutex_t videoFrameMutex{};
    pthread_cond_t videoFrameCond{};
    pthread_cond_t videoFrameEmptyCond{};
    AVRational videoTimeBase{};
    double videoClock{};//视频时钟,单位秒
    pthread_t videoPlayId = 0;//视频处理线程id
    AVFrame *rgb_frame = nullptr;

    //audio
    //音频解码上下文
    AVCodecContext *audioCodecContext = nullptr;
    //音频重采样上下文
    SwrContext *audioSwrContext{};
    SLObjectItf engineObject{};//用SLObjectItf声明引擎接口对象
    SLEngineItf engineEngine{};//声明具体的引擎对象
    SLObjectItf outputMixObject{};//用SLObjectItf创建混音器接口对象
    SLEnvironmentalReverbItf outputMixEnvironmentalReverb{};////具体的混音器对象实例
    SLEnvironmentalReverbSettings settings = SL_I3DL2_ENVIRONMENT_PRESET_DEFAULT;//默认情况
    SLObjectItf audioPlayerObject{};//用SLObjectItf声明播放器接口对象
    SLPlayItf slPlayItf{};//播放器接口
    SLAndroidSimpleBufferQueueItf slBufferQueueItf{};//缓冲区队列接口
    //音频Packet队列
    std::queue<AVPacket *> audioPacketQueue;
    std::queue<AVFrame *> audioFrameQueue;
    pthread_t cacheAudioThreadId = 0;
    pthread_mutex_t audioMutex{};
    pthread_mutex_t audioCacheMutex{};
    pthread_cond_t audioCond{};
    pthread_mutex_t audioFrameMutex{};
    pthread_cond_t audioFrameCond{};
    pthread_cond_t audioFrameEmptyCond{};
    //音频重采样缓冲区
    uint8_t *audioBuffer{};
    int audio_index = -1;
    AVRational audioTimeBase{};
    double audioClock{};//音频时钟,单位秒
    pthread_t audioPlayId = 0;//音频处理线程id

};

void playerSetJavaWeak(BipPlayer *bipPlayer, void *weak_this);

#endif //BIPPLAYER_BIPPLAYER_H
