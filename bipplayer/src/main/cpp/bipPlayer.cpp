//
// Created by welcomeworld on 2021/9/5.
//
#include "bipPlayer.h"

jclass BipPlayer::defaultBIPPlayerClass;
jmethodID BipPlayer::postEventFromNativeMethodId;
jfieldID BipPlayer::nativePlayerRefId;
JavaVM *BipPlayer::staticVm;

long BipPlayer::calculateTime(timeval startTime, timeval endTime) {
    return (endTime.tv_sec - startTime.tv_sec) * 1000 +
           (endTime.tv_usec - startTime.tv_usec) / 1000;
}

bool BipPlayer::javaExceptionCheckCatchAll(JNIEnv *env) {
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        return true;
    }
    return false;
}

void BipPlayer::release() {
    if (isRelease()) {
        notifyError(ERROR_STATE_ILLEGAL);
        return;
    }
    reset();
    playerState = STATE_DESTROY;
    stopAndClearDataSources();
    delete messageQueue;
    messageQueue = nullptr;
    setAudioTracker(nullptr);
    setVideoTracker(nullptr);
    delete shareClock;
    shareClock = nullptr;
    pthread_mutex_destroy(&avOpsMutex);
}

void BipPlayer::setOption(int category, const char *key, const char *value) {
    auto testShare = std::make_shared<std::string>(value);
    switch (category) {
        case OPT_CATEGORY_FORMAT:
            formatOps[key] = value;
            break;
        case OPT_CATEGORY_CODEC:
            codecOps[key] = value;
            break;
        case OPT_CATEGORY_PLAYER:
            if (!strcmp(key, MEDIACODEC)) {
                mediacodec = strcmp("0", value);
            } else if (!strcmp(key, START_ON_PREPARED)) {
                startOnPrepared = strcmp("0", value);
            } else if (!strcmp(key, FRAME_BUF_SIZE)) {
                maxFrameBufSize = static_cast<int>(strtol(value, nullptr, 10));
            } else if (!strcmp(key, PACKET_BUF_SIZE)) {
                maxPacketBufSize = static_cast<int>(strtol(value, nullptr, 10));
            }
            free((void *) key);
            free((void *) value);
            break;
        case OPT_CATEGORY_SWS:
            swsOps[key] = value;
            break;
        default:
            break;
    }
}

void BipPlayer::setVideoSurface(ANativeWindow *window) const {
    if (videoAvailable()) {
        bipVideoTracker->setVideoSurface(window);
    } else {
        bipNativeWindow->setWindow(window);
    }
}

void BipPlayer::postEventFromNative(int what, int arg1, int arg2, void *object) const {
    if (isRelease()) {
        return;
    }
    JNIEnv *env = nullptr;
    jint result = staticVm->GetEnv((void **) &env, JNI_VERSION_1_6);
    if (result != JNI_OK) {
        if (result == JNI_EDETACHED) {
            char thread_name[64] = {0};
            prctl(PR_GET_NAME, (char *) (thread_name));
            JavaVMAttachArgs args;
            args.version = JNI_VERSION_1_6;
            args.name = (char *) thread_name;
            args.group = nullptr;
            staticVm->AttachCurrentThread(&env, &args);
        } else {
            return;
        }
    }
    env->CallStaticVoidMethod(defaultBIPPlayerClass, postEventFromNativeMethodId,
                              (jobject) weakJavaThis, what, arg1, arg2, (jobject) object);
    javaExceptionCheckCatchAll(env);
    if (result == JNI_EDETACHED) {
        staticVm->DetachCurrentThread();
    }
}

void BipPlayer::stop() {
    if (!isStarted()) {
        return;
    }
    pause();
    requestLockWaitTime = STOP_REQUEST_WAIT;
    lock();
    requestLockWaitTime = LOCK_FREE;
    stopAndClearDataSources();
    playerState = STATE_STOP;
    if (audioAvailable()) {
        bipAudioTracker->stop();
    }
    if (videoAvailable()) {
        bipVideoTracker->stop();
    }
    if (bufferingThreadId != 0 && pthread_kill(bufferingThreadId, 0) == 0) {
        pthread_join(bufferingThreadId, nullptr);
        bufferingThreadId = 0;
    }
    unlock();
    LOGE("prepare thread stop");
    setPlaySpeed(1.0f);
}

void BipPlayer::reset() {
    //todo 清空引用
    formatOps.clear();
    swsOps.clear();
    codecOps.clear();
    stop();
}

int BipPlayer::ffmpegInterruptCallback(void *ctx) {
    auto interruptContext = (InterruptContext *) ctx;
    timeval currentTime{};
    gettimeofday(&currentTime, nullptr);
    long diffTime = calculateTime(interruptContext->readStartTime, currentTime);
    BipPlayer *player = interruptContext->player;
    if (player->requestLockWaitTime != LOCK_FREE && diffTime >= player->requestLockWaitTime) {
        player->requestLockWaitTime = LOCK_BREAK;
        return 1;
    }
    if (interruptContext->readStartTime.tv_sec != 0 && diffTime > 30000) {
        return 1;
    }
    if (interruptContext->source->sourceState != BipDataSource::STATE_START) {
        return 1;
    }
    return 0;
}

void protocolHook(const char *inputPath, AVDictionary **dic) {
    if (av_stristart(inputPath, "rtmp", nullptr) || av_stristart(inputPath, "rtsp", nullptr)) {
        av_dict_set(dic, "timeout", nullptr, 0);
    }
}

void BipPlayer::prepare(BipDataSource *prepareSource) {
    auto inputPath = prepareSource->source.c_str();
    LOGE("play prepare %s", inputPath);
    prepareSource->sourceState = BipDataSource::STATE_START;
    char *errorMsg = static_cast<char *>(av_mallocz(1024));
    bool isFromErrorRestart = false;
    int retryTimes = 0;
    while (prepareSource->sourceState == BipDataSource::STATE_START && isStarted()) {
        //打开输入流
        auto *interruptContext = new InterruptContext();
        int prepareResult = 0;
        AVDictionary *formatDic = nullptr;
        FdAVIOContext *fdAvioContext = nullptr;
        int video_index = -1;
        int audio_index = -1;
        BipVideoTracker *prepareVideoTracker = nullptr;
        BipAudioTracker *prepareAudioTracker = nullptr;
        AVFormatContext *avFormatContext = avformat_alloc_context();
        LOGE("format adder %p", avFormatContext);
        interruptContext->player = this;
        interruptContext->source = prepareSource;
        avFormatContext->interrupt_callback.callback = ffmpegInterruptCallback;
        avFormatContext->interrupt_callback.opaque = interruptContext;
        av_dict_set(&formatDic, "bufsize", "655360", 0);
        std::map<const char *, const char *>::iterator iterator;
        iterator = formatOps.begin();
        while (iterator != formatOps.end()) {
            LOGE("prepare with format option %s:%s", iterator->first, iterator->second);
            av_dict_set(&formatDic, iterator->first, iterator->second, 0);
            iterator++;
        }
        if (prepareSource->startOffset != 0) {
            avFormatContext->skip_initial_bytes = prepareSource->startOffset;
        }
        protocolHook(inputPath, &formatDic);
        if (!strncmp(inputPath, "fd:", 3)) {
            fdAvioContext = new FdAVIOContext();
            fdAvioContext->openFromDescriptor(static_cast<int>(strtol(inputPath + 3, nullptr, 10)),
                                              "rb", prepareSource->startOffset);
            avFormatContext->pb = fdAvioContext->getAvioContext();
        }
        gettimeofday(&(interruptContext->readStartTime), nullptr);
        prepareResult = avformat_open_input(&avFormatContext, inputPath, nullptr, &formatDic);
        if (prepareResult == 0) {
            gettimeofday(&(interruptContext->readStartTime), nullptr);
            prepareResult = avformat_find_stream_info(avFormatContext, nullptr);
        }
        if (prepareResult != 0) {
            av_strerror(prepareResult, errorMsg, 1024);
            LOGE("prepare media error %s", errorMsg);
            if (retryTimes > MAX_ERROR_RETRY) {
                prepareSource->sourceState = BipDataSource::STATE_ERROR;
                if (playerState != STATE_ERROR) {
                    playerState = STATE_ERROR;
                    notifyError(ERROR_PREPARE_FAILED, prepareResult);
                }
            } else {
                av_usleep(300000);
                retryTimes++;
            }
            delete interruptContext;
            delete fdAvioContext;
            avformat_free_context(avFormatContext);
            continue;
        }
        duration = avFormatContext->duration / 1000;
        for (int i = 0; i < avFormatContext->nb_streams; ++i) {
            if (prepareSource->videoEnable &&
                avFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                LOGE("find video stream with index %d", i);
                video_index = i;
                double fps = av_q2d(
                        avFormatContext->streams[video_index]->avg_frame_rate);
                if (fps <= 0) {
                    fps = av_q2d(
                            avFormatContext->streams[video_index]->r_frame_rate);
                }
                prepareVideoTracker = createVideoTracker(
                        av_q2d(avFormatContext->streams[i]->time_base),
                        avFormatContext->streams[i]->codecpar,
                        fps);
                if (prepareVideoTracker->trackerState == MediaTracker::STATE_ERROR) {
                    LOGE("prepare video tracker error");
                    prepareResult = -1;
                    break;
                }
            } else if (prepareSource->audioEnable &&
                       avFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                audio_index = i;
                LOGE("find audio stream with index %d", i);
                prepareAudioTracker = createAudioTracker(
                        av_q2d(avFormatContext->streams[i]->time_base),
                        avFormatContext->streams[i]->codecpar);
                if (prepareAudioTracker->trackerState == MediaTracker::STATE_ERROR) {
                    LOGE("prepare audio tracker error");
                    prepareResult = -1;
                    break;
                }
            }
        }
        if (prepareResult != 0) {
            LOGE("prepare create tracker error %s", errorMsg);
            if (retryTimes > MAX_ERROR_RETRY) {
                prepareSource->sourceState = BipDataSource::STATE_ERROR;
                if (playerState != STATE_ERROR) {
                    playerState = STATE_ERROR;
                    notifyError(ERROR_PREPARE_FAILED, prepareResult);
                }
            } else {
                av_usleep(300000);
                retryTimes++;
            }
            delete interruptContext;
            delete fdAvioContext;
            avformat_free_context(avFormatContext);
            continue;
        }
        if (prepareSource->isSingleSource && prepareVideoTracker != nullptr &&
            prepareAudioTracker == nullptr) {
            prepareVideoTracker->clockMaintain = true;
        }

        //解码
        long syncClock = 0;
        if (prepareSource->seekPosition != BipDataSource::NO_SEEK) {
            syncClock = prepareSource->seekPosition;
            prepareSource->seekPosition = BipDataSource::NO_SEEK;
        } else if (prepareSource->isSync || isFromErrorRestart) {
            syncClock = static_cast<long>(shareClock->clock * 1000);
        }
        if (syncClock > 0 && duration > syncClock + 1000) {
            av_seek_frame(avFormatContext, -1, av_rescale(syncClock, AV_TIME_BASE, 1000),
                          AVSEEK_FLAG_BACKWARD);
        }
        LOGE("start destroy old tracker");
        if (prepareVideoTracker != nullptr) {
            prepareVideoTracker->startDecodeThread();
            setVideoTracker(prepareVideoTracker);
        }
        if (prepareAudioTracker != nullptr) {
            prepareAudioTracker->startDecodeThread();
            setAudioTracker(prepareAudioTracker);
        }
        LOGE("start read frame loop");
        retryTimes = 0;
        bool isClockMaintainThread = prepareAudioTracker != nullptr ||
                                     (prepareVideoTracker != nullptr &&
                                      prepareVideoTracker->clockMaintain);
        if (isClockMaintainThread && !hasNotifyPrepared) {
            requestBuffering();
        }
        while (prepareSource->sourceState == BipDataSource::STATE_START && isStarted()) {
            lock();
            if (prepareSource->seekPosition != BipDataSource::NO_SEEK) {
                if (prepareSource->directSeek) {
                    av_seek_frame(avFormatContext, -1,
                                  av_rescale(prepareSource->seekPosition, AV_TIME_BASE, 1000),
                                  AVSEEK_FLAG_BACKWARD);
                    prepareSource->seekPosition = BipDataSource::NO_SEEK;
                    prepareSource->directSeek = false;
                } else {
                    unlock();
                    break;
                }
            }
            auto *avPacket = av_packet_alloc();
            gettimeofday(&(interruptContext->readStartTime), nullptr);
            int readResult = av_read_frame(avFormatContext, avPacket);
            unlock();
            if (readResult >= 0) {
                if (avPacket->stream_index == video_index) {
                    prepareVideoTracker->pushPacket(avPacket);
                    updateBufferPercent();
                } else if (avPacket->stream_index == audio_index) {
                    prepareAudioTracker->pushPacket(avPacket);
                    updateBufferPercent();
                } else {
                    av_packet_free(&avPacket);
                }
            } else if (readResult == AVERROR_EOF) {
                if (prepareVideoTracker != nullptr) {
                    prepareVideoTracker->isCacheCompleted = true;
                }
                if (prepareAudioTracker != nullptr) {
                    prepareAudioTracker->isCacheCompleted = true;
                }
                av_usleep(50000);
            } else {
                isFromErrorRestart = true;
                av_strerror(readResult, errorMsg, 1024);
                LOGE("break read frame loop for read result %d  %s", readResult, errorMsg);
                break;
            }
        }
        delete interruptContext;
        delete fdAvioContext;
        avformat_free_context(avFormatContext);
    }
    prepareSource->sourceState = BipDataSource::STATE_DESTROY;
    av_freep(&errorMsg);
}

bool BipPlayer::checkCachePrepared() {
    long sourceSize = activeDataSources.size();
    if (sourceSize == 1) {
        int streamSize = videoAvailable() ? 1 : 0;
        sourceSize = streamSize + (audioAvailable() ? 1 : 0);
    }
    if (videoAvailable() && bipVideoTracker->isFrameReady()) {
        sourceSize -= 1;
    }
    if (audioAvailable() && bipAudioTracker->isFrameReady()) {
        sourceSize -= 1;
    }
    return sourceSize <= 0;
}

void BipPlayer::notifyPrepared() {
    if (!hasNotifyPrepared) {
        hasNotifyPrepared = true;
        postEventFromNative(MEDIA_PREPARED, 0, 0, nullptr);
        if (startOnPrepared) {
            postStart();
        }
    }
}

void BipPlayer::notifyCompleted() {
    stop();
    postEventFromNative(MEDIA_PLAYBACK_COMPLETE, 0, 0, nullptr);
}

void BipPlayer::notifyInfo(int info) {
    postEventFromNative(MEDIA_INFO, info, 0, nullptr);
}

BipPlayer::BipPlayer() {
    pthread_mutex_init(&avOpsMutex, nullptr);
    playerState = STATE_CREATED;
    messageQueue = new MessageQueue();
    bipNativeWindow = new BipNativeWindow();
    shareClock = new SyncClock();
    pthread_create(&msgLoopThreadId, nullptr, bipMsgLoopThread, this);//开启消息线程
}

BipPlayer::~BipPlayer() {
    postRelease();
}

long BipPlayer::getDuration() {
    if (isRelease()) {
        notifyError(ERROR_STATE_ILLEGAL);
        return 0;
    }
    return duration;
}

long BipPlayer::getCurrentPosition() {
    if (isRelease()) {
        notifyError(ERROR_STATE_ILLEGAL);
        return 0;
    }
    return (long) (shareClock->clock * 1000);
}

int BipPlayer::getVideoHeight() const {
    return videoAvailable() ? bipVideoTracker->getVideoHeight() : 0;
}

int BipPlayer::getVideoWidth() const {
    return videoAvailable() ? bipVideoTracker->getVideoWidth() : 0;
}

bool BipPlayer::isPlaying() const {
    return playerState == STATE_PLAYING;
}

void BipPlayer::seekTo(long time) {
    if (isStarted()) {
        if (duration <= 0) {
            return;
        }
        requestLockWaitTime = SEEK_REQUEST_WAIT;
        lock();
        if (requestLockWaitTime == LOCK_BREAK) {
            if (audioAvailable()) {
                bipAudioTracker->stop();
            }
            if (videoAvailable()) {
                bipVideoTracker->stop();
            }
            for (BipDataSource *source: activeDataSources) {
                source->seekPosition = time;
                source->directSeek = false;
            }
        } else {
            pause();
            if (audioAvailable()) {
                bipAudioTracker->clearCache();
            }
            if (videoAvailable()) {
                bipVideoTracker->clearCache();
            }
            for (BipDataSource *source: activeDataSources) {
                source->seekPosition = time;
                source->directSeek = true;
            }
        }
        shareClock->clock = (double) (time) / 1000;
        requestLockWaitTime = LOCK_FREE;
        unlock();
        isSeeking = true;
        requestBuffering();
    }
}

void BipPlayer::pause() {
    if (isPlaying()) {
        playerState = STATE_PAUSE;
        if (audioAvailable()) {
            bipAudioTracker->pause();
        }
        if (videoAvailable()) {
            bipVideoTracker->pause();
        }
    }
}

void *BipPlayer::preparePlayerThread(void *args) {
    auto context = (BipPrepareContext *) args;
    auto bipPlayer = context->player;
    pthread_setname_np(context->prepareSource->prepareThreadId, "BipPrepare");
    bipPlayer->prepare(context->prepareSource);
    pthread_exit(nullptr);//退出线程
}

void *BipPlayer::bipMsgLoopThread(void *args) {
    auto bipPlayer = (BipPlayer *) args;
    pthread_setname_np(bipPlayer->msgLoopThreadId, "BipMsgLoop");
    bipPlayer->msgLoop();
    pthread_exit(nullptr);//退出线程
}

void BipPlayer::playerSetJavaWeak(BipPlayer *bipPlayer, void *weak_this) {
    bipPlayer->weakJavaThis = weak_this;
}

void BipPlayer::play() {
    playerState = STATE_PLAYING;
    if (audioAvailable()) {
        bipAudioTracker->play();
    }
    if (videoAvailable()) {
        bipVideoTracker->play();
    }
}

void BipPlayer::msgLoop() {
    while (!isRelease()) {
        BipMessage *processMsg = messageQueue->next();
        LOGE("process msg %x with arg1:%d arg2:%d", processMsg->what, processMsg->arg1,
             processMsg->arg2);
        switch (processMsg->what) {
            case MSG_SEEK:
                seekTo(processMsg->arg1);
                break;
            case MSG_STOP:
                stop();
                break;
            case MSG_START:
                play();
                break;
            case MSG_PAUSE:
                pause();
                break;
            case MSG_RESET:
                reset();
                break;
            case MSG_RELEASE:
                release();
                break;
            case MSG_PREPARE: {
                stopAndClearDataSources();
                playerState = STATE_START;
                hasNotifyPrepared = false;
                auto bipDataSource =
                        static_cast<std::deque<BipDataSource *> * > (processMsg->obj);
                for (BipDataSource *source: *bipDataSource) {
                    activeDataSources.push_front(source);
                    auto prepareContext = new BipPrepareContext(this, source);
                    pthread_create(&(source->prepareThreadId), nullptr, preparePlayerThread,
                                   prepareContext);
                }
                delete bipDataSource;
            }
                break;
            case MSG_BUFFERING:
                if (bufferingThreadId == 0) {
                    pause();
                    pthread_create(&(bufferingThreadId), nullptr, bufferingThread, this);
                }
                break;
        }

        LOGE("process msg %x completed", processMsg->what);
        //消息处理完毕 obj的释放自己处理
        delete processMsg;
    }
}

void BipPlayer::notifyMsg(BipMessage *message) {
    if (isRelease()) {
        return;
    }
    messageQueue->scheduleMsg(message);
}

void BipPlayer::notifyMsg(int what) {
    if (isRelease()) {
        return;
    }
    auto *message = new BipMessage();
    message->what = what;
    notifyMsg(message);
}

bool BipPlayer::audioAvailable() const {
    return bipAudioTracker != nullptr;
}

bool BipPlayer::videoAvailable() const {
    return bipVideoTracker != nullptr;
}

int BipPlayer::getFps() const {
    return videoAvailable() ? static_cast<int>(bipVideoTracker->fps) : -1;
}

void BipPlayer::setPlaySpeed(float speed) {
    playSpeed = speed;
    if (videoAvailable()) {
        bipVideoTracker->setPlaySpeed(speed);
    }
    if (audioAvailable()) {
        bipAudioTracker->setPlaySpeed(speed);
    }
}

void msg_delete_datasource_callback(void *object) {
    auto bipDataSource = static_cast<std::deque<BipDataSource *> * > (object);
    for (BipDataSource *source: *bipDataSource) {
        delete source;
    }
    delete bipDataSource;
}

void BipPlayer::postStop() {
    notifyMsg(MSG_STOP);
}

void BipPlayer::postStart() {
    notifyMsg(MSG_START);
}

void BipPlayer::postPause() {
    notifyMsg(MSG_PAUSE);
}

void BipPlayer::postReset() {
    notifyMsg(MSG_RESET);
}

void BipPlayer::postSeekTo(long time) {
    auto *message = new BipMessage();
    message->what = MSG_SEEK;
    message->arg1 = (int) time;
    notifyMsg(message);
}

void BipPlayer::postRelease() {
    notifyMsg(MSG_RELEASE);
}

void BipPlayer::postPrepare(std::deque<BipDataSource *> *bipDataSources) {
    auto *message = new BipMessage();
    message->what = MSG_PREPARE;
    message->obj = bipDataSources;
    message->free_l = msg_delete_datasource_callback;
    notifyMsg(message);
}

bool BipPlayer::isStarted() const {
    return playerState >= STATE_START && playerState < STATE_STOP;
}

bool BipPlayer::isRelease() const {
    return playerState == STATE_DESTROY;
}

void BipPlayer::reportFps(int fps) {
    postEventFromNative(MEDIA_INFO, MEDIA_INFO_FPS, fps, nullptr);
}

void BipPlayer::requestBuffering() {
    notifyMsg(MSG_BUFFERING);
}

void *BipPlayer::bufferingThread(void *args) {
    auto bipPlayer = (BipPlayer *) args;
    pthread_setname_np(bipPlayer->bufferingThreadId, "BipBufferCheckThread");
    bipPlayer->checkBuffering();
    bipPlayer->bufferingThreadId = 0;
    pthread_exit(nullptr);//退出线程
}

void BipPlayer::checkBuffering() {
    notifyInfo(MEDIA_INFO_BUFFERING_START);
    while (isStarted()) {
        if (checkCachePrepared()) {
            if (hasNotifyPrepared) {
                postStart();
                if (isSeeking) {
                    isSeeking = false;
                    postEventFromNative(MEDIA_SEEK_COMPLETE, 0, 0, nullptr);
                }
            } else {
                notifyPrepared();
            }
            break;
        }
        av_usleep(200000);
    }
    if (videoAvailable()) {
        bipVideoTracker->bufferingTimes++;
    }
    if (audioAvailable()) {
        bipAudioTracker->bufferingTimes++;
    }
    notifyInfo(MEDIA_INFO_BUFFERING_END);
    bufferingThreadId = 0;
}

void BipPlayer::notifyError(int errorCode, int errorExtra) {
    postEventFromNative(MEDIA_ERROR, errorCode, errorExtra, nullptr);
}

void BipPlayer::setVideoTracker(BipVideoTracker *videoTracker) {
    if (videoTracker != bipVideoTracker) {
        delete bipVideoTracker;
        bipVideoTracker = videoTracker;
    }
}

void BipPlayer::setAudioTracker(BipAudioTracker *audioTracker) {
    if (audioTracker != bipAudioTracker) {
        delete bipAudioTracker;
        bipAudioTracker = audioTracker;
    }
}

BipAudioTracker *BipPlayer::createAudioTracker(double trackTimeBase, AVCodecParameters *codecPar) {
    auto *audioTracker = new BipAudioTracker(codecPar);
    audioTracker->shareClock = shareClock;
    audioTracker->clockMaintain = true;
    audioTracker->trackTimeBase = trackTimeBase;
    audioTracker->duration = duration;
    audioTracker->trackerCallback = this;
    audioTracker->setMaxPacketBufSize(maxPacketBufSize);
    audioTracker->setMaxFrameBufSize(maxFrameBufSize);
    return audioTracker;
}

BipVideoTracker *
BipPlayer::createVideoTracker(double trackTimeBase, AVCodecParameters *codecPar, double fps) {
    AVDictionary *codecDic = nullptr;
    std::map<const char *, const char *>::iterator codecIterator;
    codecIterator = codecOps.begin();
    while (codecIterator != codecOps.end()) {
        LOGE("prepare with codec option %s:%s", codecIterator->first,
             codecIterator->second);
        av_dict_set(&codecDic, codecIterator->first, codecIterator->second, 0);
        codecIterator++;
    }
    AVDictionary *swsDic = nullptr;
    std::map<const char *, const char *>::iterator swsIterator;
    swsIterator = swsOps.begin();
    while (swsIterator != swsOps.end()) {
        LOGE("prepare with sws option %s:%s", swsIterator->first, swsIterator->second);
        av_dict_set(&swsDic, swsIterator->first, swsIterator->second, 0);
        swsIterator++;
    }
    auto *videoTracker = new BipVideoTracker(bipNativeWindow, codecPar, codecDic, swsDic,
                                             mediacodec);
    videoTracker->shareClock = shareClock;
    videoTracker->trackTimeBase = trackTimeBase;
    videoTracker->duration = duration;
    videoTracker->trackerCallback = this;
    videoTracker->setMaxPacketBufSize(maxPacketBufSize);
    videoTracker->setMaxFrameBufSize(maxFrameBufSize);
    videoTracker->fps = fps;
    return videoTracker;
}

void BipPlayer::stopAndClearDataSources() {
    for (BipDataSource *source: activeDataSources) {
        source->sourceState = BipDataSource::STATE_STOP;
        if (source->prepareThreadId != 0 && pthread_kill(source->prepareThreadId, 0) == 0) {
            pthread_join(source->prepareThreadId, nullptr);
            source->prepareThreadId = 0;
        }
    }
    activeDataSources.clear();
}

void BipPlayer::reportPlayStateChange(bool isPlaying) {
    postEventFromNative(MEDIA_PLAY_STATE_CHANGE, isPlaying ? 1 : 0, 0, nullptr);
}

void BipPlayer::updateBufferPercent() {
    auto percent = bufferPercent;
    if (audioAvailable()) {
        if (videoAvailable()) {
            percent = std::min(bipAudioTracker->bufferPercent, bipVideoTracker->bufferPercent);
        } else {
            percent = bipAudioTracker->bufferPercent;
        }
    } else if (videoAvailable()) {
        percent = bipVideoTracker->bufferPercent;
    }
    if (bufferPercent != percent) {
        bufferPercent = percent;
        postEventFromNative(MEDIA_BUFFERING_UPDATE, bufferPercent, 0, nullptr);
    }
}

void BipPlayer::lock() {
    pthread_mutex_lock(&avOpsMutex);
}

void BipPlayer::unlock() {
    pthread_mutex_unlock(&avOpsMutex);
}


BipPlayer::BipPrepareContext::BipPrepareContext(BipPlayer *pPlayer, BipDataSource *pSource) {
    this->player = pPlayer;
    this->prepareSource = pSource;
}
