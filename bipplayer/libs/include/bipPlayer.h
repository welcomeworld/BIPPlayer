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
#include "fdAVIOContext.h"
#include "bipPlayerOptions.h"
#include "SoundTouch.h"
#include "BipLog.h"
#include "BipNativeWindow.h"
#include "BipMessage.h"
#include "BipAudioTracker.h"
#include "BipVideoTracker.h"
#include "BipFrameQueue.h"
#include "string.h"

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
#include <unistd.h>
#ifdef __cplusplus
}
#endif

class BipDataSource {
public:
    static const int STATE_ERROR = -1;
    static const int STATE_CREATED = 0;
    static const int STATE_START = 1;
    static const int STATE_STOP = 2;
    static const int STATE_DESTROY = 3;
    std::string source;
    pthread_t prepareThreadId = 0;
    bool isSingleSource = true;
    bool isSync = false;
    bool videoEnable = true;
    bool audioEnable = true;
    std::atomic<int> sourceState = {STATE_CREATED};
    long seekPosition = 0;
    bool directSeek = false;
    long startOffset = 0;
};

class BipPlayer : public BipTrackerCallback {
private:
    static const int MAX_ERROR_RETRY = 3;
    //播放器消息码
    static const int MSG_SEEK = 0x233;
    static const int MSG_STOP = 0x234;
    static const int MSG_START = 0x235;
    static const int MSG_PAUSE = 0x236;
    static const int MSG_RESET = 0x237;
    static const int MSG_RELEASE = 0x238;
    static const int MSG_PREPARE = 0x240;
    static const int MSG_BUFFERING = 0x241;

    //播放器状态
    static const int STATE_CREATED = 0;
    static const int STATE_START = 1;
    static const int STATE_PLAYING = 2;
    static const int STATE_PAUSE = 3;
    static const int STATE_STOP = 4;
    static const int STATE_DESTROY = 5;
    std::atomic<int> playerState{};

    //错误码
    static const int ERROR_STATE_ILLEGAL = 0;
    static const int ERROR_PREPARE_FAILED = 1;

    //字符串比较器
    struct ptrCmp {
        bool operator()(const char *s1, const char *s2) const {
            return strcmp(s1, s2) < 0;
        }
    };

    //ffmpeg超时检测回调上下文
    class InterruptContext {
    public:
        timeval readStartTime;
        BipPlayer *player = nullptr;
    };

    //播放器事件码
    static const int MEDIA_PREPARED = 1;
    static const int MEDIA_PLAYBACK_COMPLETE = 2;
    static const int MEDIA_BUFFERING_UPDATE = 3;
    static const int MEDIA_SEEK_COMPLETE = 4;
    static const int MEDIA_ERROR = 5;
    static const int MEDIA_PLAY_STATE_CHANGE = 6;
    static const int MEDIA_INFO = 7;
    //播放器参数设置类别
    static const int OPT_CATEGORY_FORMAT = 1;
    static const int OPT_CATEGORY_CODEC = 2;
    static const int OPT_CATEGORY_SWS = 3;
    static const int OPT_CATEGORY_PLAYER = 4;
    //播放器Info码
    static const int MEDIA_INFO_BUFFERING_START = 0;
    static const int MEDIA_INFO_BUFFERING_END = 1;
    static const int MEDIA_INFO_FPS = 2;

    static const int LOCK_BREAK = -1; //已发生中断
    static const int LOCK_FREE = 0; //没有操作等待锁
    static const int STOP_REQUEST_WAIT = 1; //stop操作锁等待时间
    static const int SEEK_REQUEST_WAIT = 100; //seek操作锁等待时间

    //AVFrame缓存大小，单位字节,默认25M
    int maxFrameBufSize = 1024 * 1024 * 25;
    //AVPacket缓存大小，单位字节,默认15M
    int maxPacketBufSize = 1024 * 1024 * 15;
    std::map<const char *, const char *, ptrCmp> formatOps;
    std::map<const char *, const char *, ptrCmp> codecOps;
    std::map<const char *, const char *, ptrCmp> swsOps;
    std::map<const char *, const char *, ptrCmp> playerOps;
    MessageQueue *messageQueue = nullptr;
    bool mediacodec = false;
    bool startOnPrepared = false;
    BipVideoTracker *bipVideoTracker = nullptr;
    BipAudioTracker *bipAudioTracker = nullptr;
    BipNativeWindow *bipNativeWindow = nullptr;
    SyncClock *shareClock = nullptr; //基准时钟，单位位秒
    long duration = -1;//单位毫秒
    pthread_t msgLoopThreadId = 0;
    pthread_t bufferingThreadId = 0;
    int bufferPercent = 0;
    int requestLockWaitTime = LOCK_FREE;  //请求锁时的超时中断时间
    pthread_mutex_t avOpsMutex{};
    std::deque<BipDataSource *> activeDataSources{};

    static int ffmpegInterruptCallback(void *ctx);

    static long calculateTime(timeval startTime, timeval endTime);

    static void *bipMsgLoopThread(void *args);

    static bool javaExceptionCheckCatchAll(JNIEnv *env);

    static void *preparePlayerThread(void *args);

    static void *bufferingThread(void *args);

    void notifyError(int errorCode, int errorExtra = 0);

    void postEventFromNative(int what, int arg1, int arg2, void *object) const;

    void notifyPrepared();

    void notifyCompleted();

    void reportFps(int fps);

    void notifyInfo(int info);

    bool videoAvailable() const;

    bool audioAvailable() const;

    bool checkCachePrepared();

    void setVideoTracker(BipVideoTracker *videoTracker);

    void setAudioTracker(BipAudioTracker *audioTracker);

    BipAudioTracker *createAudioTracker(double trackTimeBase, AVCodecParameters *codecpar);

    BipVideoTracker *
    createVideoTracker(double trackTimeBase, AVCodecParameters *codecPar, double fps);

    void stopAndClearDataSources();

    void msgLoop();

    void checkBuffering();

    void updateBufferPercent();

public:
    static jclass defaultBIPPlayerClass;
    static jmethodID postEventFromNativeMethodId;
    static jfieldID nativePlayerRefId;
    static JavaVM *staticVm;
    void *weakJavaThis = nullptr;
    float playSpeed = 1.0f;

    class BipPrepareContext {
    public:
        BipPrepareContext(BipPlayer *pPlayer, BipDataSource *pSource);

        BipPlayer *player;
        BipDataSource *prepareSource;
    };

    void requestBuffering();

    void reportPlayStateChange(bool isPlaying);

    void play();

    void pause();

    void stop();

    void reset();

    void seekTo(long time);

    bool isPlaying() const;

    bool isStarted() const;

    int getVideoWidth() const;

    int getVideoHeight() const;

    long getCurrentPosition();

    long getDuration();

    void prepare(BipDataSource *prepareSource);

    void setVideoSurface(ANativeWindow *nativeWindow) const;

    void release();

    void setOption(int category, const char *key, const char *value);

    void notifyMsg(BipMessage *bipMessage);

    void notifyMsg(int what);

    BipPlayer();

    ~BipPlayer();

    int getFps() const;

    void setPlaySpeed(float speed);

    static void playerSetJavaWeak(BipPlayer *bipPlayer, void *weak_this);

    void postStop();

    void postStart();

    void postPause();

    void postReset();

    void postSeekTo(long time);

    void postRelease();

    void postPrepare(std::deque<BipDataSource *> *bipDataSources);

    bool isRelease() const;
};

#endif //BIPPLAYER_BIPPLAYER_H
