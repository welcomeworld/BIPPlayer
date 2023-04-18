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
    isRequestOptions = true;
    pthread_mutex_lock(&avOpsMutex);
    isRequestOptions = false;
    stopAndClearDataSources();
    playerState = STATE_STOP;
    notifyPlayStateChange(false);
    if (audioAvailable()) {
        bipAudioTracker->stop();
    }
    if (videoAvailable()) {
        bipVideoTracker->stop();
    }
    pthread_mutex_unlock(&avOpsMutex);
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
    if (player->isRequestOptions) {
        return 1;
    }
    if (interruptContext->readStartTime.tv_sec != 0) {
        if (diffTime > 30000) {
            return 1;
        }
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
    bool isPrepared = false;
    bool isRestartPrepare = false;
    bool isFromSeek = false;
    while (true) {
        //打开输入流
        AVFormatContext *avFormatContext = nullptr;
        auto *interruptContext = new InterruptContext();
        int prepareResult = 0;
        AVDictionary *formatDic = nullptr;
        FdAVIOContext *fdAvioContext = nullptr;
        int video_index = -1;
        int audio_index = -1;
        BipVideoTracker *prepareVideoTracker = nullptr;
        BipAudioTracker *prepareAudioTracker = nullptr;
        int bufferPercent = 0;
        avFormatContext = avformat_alloc_context();
        LOGE("format adder %p", avFormatContext);
        interruptContext->player = this;
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
        protocolHook(inputPath, &formatDic);
        if (!strncmp(inputPath, "fd:", 3)) {
            fdAvioContext = new FdAVIOContext();
            fdAvioContext->openFromDescriptor(static_cast<int>(strtol(inputPath, nullptr, 10)),
                                              "rb");
            avFormatContext->pb = fdAvioContext->getAvioContext();
        }
        gettimeofday(&(interruptContext->readStartTime), nullptr);
        prepareResult = avformat_open_input(&avFormatContext, inputPath, nullptr, &formatDic);
        if (prepareResult != 0) {
            prepareSource->sourceState = BipDataSource::STATE_ERROR;
            notifyError(ERROR_PREPARE_FAILED, prepareResult);
            av_strerror(prepareResult, errorMsg, 1024);
            LOGE("open input error %s", errorMsg);
            return;
        }
        gettimeofday(&(interruptContext->readStartTime), nullptr);
        prepareResult = avformat_find_stream_info(avFormatContext, nullptr);
        if (prepareResult != 0) {
            prepareSource->sourceState = BipDataSource::STATE_ERROR;
            notifyError(ERROR_PREPARE_FAILED, prepareResult);
            av_strerror(prepareResult, errorMsg, 1024);
            LOGE("find stream info error %s", errorMsg);
            return;
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
                    prepareSource->sourceState = BipDataSource::STATE_ERROR;
                    notifyError(ERROR_PREPARE_FAILED, 233);
                    LOGE("prepare video tracker error");
                    return;
                }
            } else if (prepareSource->audioEnable &&
                       avFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                LOGE("find audio stream with index %d", i);
                prepareAudioTracker = createAudioTracker(
                        av_q2d(avFormatContext->streams[i]->time_base),
                        avFormatContext->streams[i]->codecpar);
                audio_index = i;
            }
        }
        if (prepareSource->isSingleSource && prepareVideoTracker != nullptr &&
            prepareAudioTracker == nullptr) {
            prepareVideoTracker->clockMaintain = true;
        }

        //解码
        long syncClock = 0;
        if (prepareSource->seekPosition > 0) {
            syncClock = prepareSource->seekPosition;
            prepareSource->seekPosition = 0;
            isFromSeek = true;
        } else if (prepareSource->isSync) {
            syncClock = static_cast<long>((shareClock->clock + 1) * 1000);
        } else if (isRestartPrepare) {
            syncClock = static_cast<long>(shareClock->clock * 1000);
        }
        if (syncClock > 0 && duration > syncClock) {
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
        if (!isRestartPrepare) {
            playerState = STATE_START;
            activeDataSources.push_front(prepareSource);
            notifyInfo(MEDIA_INFO_BUFFERING_START);
        } else if (isFromSeek) {
            notifyInfo(MEDIA_INFO_BUFFERING_START);
        }
        LOGE("start read frame loop");
        while (prepareSource->sourceState == BipDataSource::STATE_START &&
               prepareSource->seekPosition == 0) {
            pthread_mutex_lock(&avOpsMutex);
            auto *avPacket = av_packet_alloc();
            gettimeofday(&(interruptContext->readStartTime), nullptr);
            int readResult = av_read_frame(avFormatContext, avPacket);
            pthread_mutex_unlock(&avOpsMutex);
            if (readResult >= 0) {
                if (avPacket->stream_index == video_index) {
                    prepareVideoTracker->pushPacket(avPacket);
                } else if (avPacket->stream_index == audio_index) {
                    if (duration > 0) {
                        double bufferPosition = prepareAudioTracker->trackTimeBase *
                                                static_cast<double>(avPacket->dts) * 100000;
                        auto durationD = static_cast<double>(duration);
                        int percent = static_cast<int>(round(bufferPosition / durationD));
                        if (percent != bufferPercent) {
                            bufferPercent = percent;
                            postEventFromNative(MEDIA_BUFFERING_UPDATE, bufferPercent, 0, nullptr);
                        }
                    }
                    prepareAudioTracker->pushPacket(avPacket);
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
                LOGE("break read frame loop for read result %d", readResult);
                isRestartPrepare = true;
                break;
            }
            if (!isPrepared) {
                isPrepared = checkCachePrepared();
                if (isPrepared) {
                    if (prepareSource->isSync) {
                        postStart();
                    } else {
                        notifyPrepared();
                    }
                }
            } else if (isFromSeek) {
                if (checkCachePrepared()) {
                    isFromSeek = false;
                    postStart();
                }
            }
        }

        delete interruptContext;
        delete fdAvioContext;
        if (avFormatContext != nullptr) {
            avformat_free_context(avFormatContext);
            avFormatContext = nullptr;
        }
        if (prepareSource->sourceState == BipDataSource::STATE_STOP) {
            prepareSource->sourceState = BipDataSource::STATE_DESTROY;
            break;
        }
    }
    av_freep(&errorMsg);
}

bool BipPlayer::checkCachePrepared() {
    bool completed = true;
    if (videoAvailable() && !bipVideoTracker->isFrameReady()) {
        completed = false;
    }
    if (audioAvailable() && !bipAudioTracker->isFrameReady()) {
        completed = false;
    }
    if (completed) {
        if (videoAvailable()) {
            bipVideoTracker->bufferingTimes++;
        }
        if (audioAvailable()) {
            bipAudioTracker->bufferingTimes++;
        }
        notifyInfo(MEDIA_INFO_BUFFERING_END);
    }
    return completed;
}

void BipPlayer::notifyPrepared() {
    postEventFromNative(MEDIA_PREPARED, 0, 0, nullptr);
    if (startOnPrepared) {
        postStart();
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
        isRequestOptions = true;
        pthread_mutex_lock(&avOpsMutex);
        isRequestOptions = false;
        if (audioAvailable()) {
            bipAudioTracker->stop();
        }
        if (videoAvailable()) {
            bipVideoTracker->stop();
        }
        shareClock->clock = (double) (time) / 1000;
        for (BipDataSource *source: activeDataSources) {
            source->seekPosition = time;
        }
        pthread_mutex_unlock(&avOpsMutex);
        postEventFromNative(MEDIA_SEEK_COMPLETE, 0, 0, nullptr);
    }
}

void BipPlayer::pause() {
    playerState = STATE_PAUSE;
    notifyPlayStateChange(false);
    if (audioAvailable()) {
        bipAudioTracker->pause();
    }
    if (videoAvailable()) {
        bipVideoTracker->pause();
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
    notifyPlayStateChange(true);
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
                auto realDataSource = static_cast<BipDataSource *>(processMsg->obj);
                auto prepareContext = new BipPrepareContext(this, realDataSource);
                pthread_create(&(realDataSource->prepareThreadId), nullptr, preparePlayerThread,
                               prepareContext);
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
    delete (BipDataSource *) object;
}

void BipPlayer::setDataSource(char *inputSource) {
    tempDataSource = new BipDataSource();
    tempDataSource->source = inputSource;
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

void BipPlayer::postPrepare() {
    auto *message = new BipMessage();
    message->what = MSG_PREPARE;
    message->obj = tempDataSource;
    message->free_l = msg_delete_datasource_callback;
    notifyMsg(message);
}

void BipPlayer::postPrepareNext(char *inputSource, bool isSync) {
    auto *message = new BipMessage();
    message->what = MSG_PREPARE;
    auto dataSource = new BipDataSource();
    dataSource->source = inputSource;
    dataSource->isSync = isSync;
    message->obj = dataSource;
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
    while (true) {
        if (checkCachePrepared()) {
            postStart();
            break;
        }
        av_usleep(500000);
    }
    bufferingThreadId = 0;
}

void BipPlayer::notifyError(int errorCode, int errorExtra) {
    postEventFromNative(MEDIA_ERROR, errorCode, errorExtra, nullptr);
}

void BipPlayer::notifyPlayStateChange(bool isPlaying) {
    postEventFromNative(MEDIA_PLAY_STATE_CHANGE, isPlaying ? 1 : 0, 0, nullptr);
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
    audioTracker->maxPacketBufSize = maxPacketBufSize;
    audioTracker->maxFrameBufSize = maxFrameBufSize;
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
    videoTracker->maxPacketBufSize = maxPacketBufSize;
    videoTracker->maxFrameBufSize = maxFrameBufSize;
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


BipPlayer::BipPrepareContext::BipPrepareContext(BipPlayer *pPlayer, BipDataSource *pSource) {
    this->player = pPlayer;
    this->prepareSource = pSource;
}
