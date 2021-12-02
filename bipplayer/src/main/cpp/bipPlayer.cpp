//
// Created by welcomeworld on 2021/9/5.
//
#include "bipPlayer.h"

jclass defaultBIPPlayerClass;
jmethodID postEventFromNativeMethodId;
jfieldID nativePlayerRefId;
JavaVM *staticVm;

bool javaExceptionCheckCatchAll(JNIEnv *env) {
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        return true;
    }
    return false;
}

void BipPlayer::createEngine() {
    slCreateEngine(&engineObject, 0, nullptr, 0, nullptr, nullptr);
    (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);//实现engineObject接口对象
    (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE,
                                  &engineEngine);//通过引擎调用接口初始化SLEngineItf
}

void BipPlayer::release() {
    if (playState == STATE_RELEASE) {
        return;
    }
    reset();
    playState = STATE_RELEASE;
    if (nativeWindow != nullptr) {
        ANativeWindow_release(nativeWindow);
        nativeWindow = nullptr;
    }
    if (rgb_frame != nullptr) {
        av_frame_free(&rgb_frame);
        rgb_frame = nullptr;
    }
    pthread_cond_destroy(&videoCond);
    pthread_cond_destroy(&audioCond);
    pthread_cond_destroy(&audioFrameCond);
    pthread_cond_destroy(&videoFrameCond);
    pthread_cond_destroy(&audioFrameEmptyCond);
    pthread_cond_destroy(&videoFrameEmptyCond);
    pthread_mutex_destroy(&videoMutex);
    pthread_mutex_destroy(&audioMutex);
    pthread_mutex_destroy(&audioFrameMutex);
    pthread_mutex_destroy(&videoFrameMutex);
    pthread_mutex_destroy(&avOpsMutex);
}

//创建混音器
void BipPlayer::createMixVolume() {
    (*engineEngine)->CreateOutputMix(engineEngine, &outputMixObject, 0, nullptr,
                                     nullptr);//用引擎对象创建混音器接口对象
    (*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE);//实现混音器接口对象
    SLresult sLresult = (*outputMixObject)->GetInterface(outputMixObject,
                                                         SL_IID_ENVIRONMENTALREVERB,
                                                         &outputMixEnvironmentalReverb);
    //设置
    if (SL_RESULT_SUCCESS == sLresult) {
        (*outputMixEnvironmentalReverb)->SetEnvironmentalReverbProperties(
                outputMixEnvironmentalReverb, &settings);
    }
}

void BipPlayer::setOption(int category, const char *key, const char *value) {
    switch (category) {
        case OPT_CATEGORY_FORMAT:
            formatOps[key] = value;
            break;
        case OPT_CATEGORY_CODEC:
            codecOps[key] = value;
            break;
        case OPT_CATEGORY_PLAYER:
            playerOps[key] = value;
            break;
        case OPT_CATEGORY_SWS:
            swsOps[key] = value;
            break;
        default:
            break;
    }
}

void bufferQueueCallback(
        SLAndroidSimpleBufferQueueItf caller,
        void *pContext
) {
    auto *bipPlayer = (BipPlayer *) pContext;
    if (bipPlayer != nullptr) {
        bipPlayer->innerBufferQueueCallback();
    }
}

//创建播放器
void BipPlayer::createPlayer() {
    SLDataLocator_AndroidBufferQueue androidBufferQueue = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
                                                           2};
    SLDataFormat_PCM pcm = {
            SL_DATAFORMAT_PCM, 2, SL_SAMPLINGRATE_44_1, SL_PCMSAMPLEFORMAT_FIXED_16,
            SL_PCMSAMPLEFORMAT_FIXED_16, SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,
            SL_BYTEORDER_LITTLEENDIAN
    };
    SLDataSource dataSource = {&androidBufferQueue, &pcm};
    SLDataLocator_OutputMix slDataLocatorOutputMix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObject};
    SLDataSink slDataSink = {&slDataLocatorOutputMix, nullptr};
    const SLInterfaceID ids[3] = {SL_IID_BUFFERQUEUE, SL_IID_EFFECTSEND, SL_IID_VOLUME};
    const SLboolean req[3] = {SL_BOOLEAN_FALSE, SL_BOOLEAN_FALSE, SL_BOOLEAN_FALSE};

    SLresult playerResult = (*engineEngine)->CreateAudioPlayer(engineEngine, &audioPlayerObject,
                                                               &dataSource, &slDataSink, 3, ids,
                                                               req);
    if (playerResult != SL_RESULT_SUCCESS) {
        return;
    }
    (*audioPlayerObject)->Realize(audioPlayerObject, SL_BOOLEAN_FALSE);
    (*audioPlayerObject)->GetInterface(audioPlayerObject, SL_IID_PLAY, &slPlayItf);
    (*audioPlayerObject)->GetInterface(audioPlayerObject, SL_IID_BUFFERQUEUE, &slBufferQueueItf);
    (*slBufferQueueItf)->RegisterCallback(slBufferQueueItf, bufferQueueCallback, this);
}


void BipPlayer::innerBufferQueueCallback() {
    int bufferSize = getPcm();
    if (bufferSize != 0) {
        //将得到的数据加入到队列中
        (*slBufferQueueItf)->Enqueue(slBufferQueueItf, audioBuffer, bufferSize);
    }
}

int BipPlayer::getPcm() {
    while (playState == STATE_PLAYING) {
        pthread_mutex_lock(&audioFrameMutex);
        if (playState != STATE_PLAYING) {
            pthread_mutex_unlock(&audioFrameMutex);
            break;
        }
        if (audioFrameQueue.empty()) {
            pthread_cond_wait(&audioFrameCond, &audioFrameMutex);
        }
        if (!audioFrameQueue.empty() && playState == STATE_PLAYING) {
            AVFrame *audioFrame = audioFrameQueue.front();
            audioFrameQueue.pop();
            if (audioFrameQueue.size() <= min_frame_buff_size) {
                pthread_cond_signal(&audioFrameEmptyCond);
            }
            pthread_mutex_unlock(&audioFrameMutex);
            if (audioSwrContext == nullptr) {
                return 0;
            }
            int outSample = swr_convert(audioSwrContext, &audioBuffer, 4096,
                                        (const uint8_t **) (audioFrame->data),
                                        audioFrame->nb_samples);
            int size = av_samples_get_buffer_size(nullptr, outChannelsNumber,
                                                  outSample,
                                                  AV_SAMPLE_FMT_S16, 1);
            if (audioFrame->pts != AV_NOPTS_VALUE) {
                //这一帧的起始时间
                audioClock = audioFrame->pts * av_q2d(audioTimeBase);
                //这一帧数据的时间
                double time = size / ((double) 44100 * 2 * 2);
                //最终音频时钟
                audioClock = time + audioClock;

            }
            av_frame_free(&audioFrame);
            return size;
        } else {
            pthread_mutex_unlock(&audioFrameMutex);
        }
    }
    return 0;
}

void BipPlayer::showVideoPacket() {
    postEventFromNative(MEDIA_PLAY_STATE_CHANGE, 1, 0, nullptr);
    //视频缓冲区
    ANativeWindow_Buffer nativeWindowBuffer;
    double last_play  //上一帧的播放时间
    , play             //当前帧的播放时间
    , last_delay    // 上一次播放视频的两帧视频间隔时间
    , delay         //线程休眠时间
    , diff   //音频帧与视频帧相差时间
    , sync_threshold //合理的范围
    , pts
    , frame_time_stamp = av_q2d(videoTimeBase); //时间戳的实际时间单位
    while (playState == STATE_PLAYING) {
        pthread_mutex_lock(&videoFrameMutex);
        if (playState != STATE_PLAYING) {
            pthread_mutex_unlock(&videoFrameMutex);
            break;
        }
        if (videoFrameQueue.empty()) {
            if (FFABS(audioClock * 1000 - duration) < 500) {
                notifyCompleted();
            } else {
                pthread_cond_wait(&videoFrameCond, &videoFrameMutex);
            }
        }
        if (!videoFrameQueue.empty()) {
            AVFrame *frame = videoFrameQueue.front();
            videoFrameQueue.pop();
            if (videoFrameQueue.size() <= min_frame_buff_size) {
                pthread_cond_signal(&videoFrameEmptyCond);
            }
            pthread_mutex_unlock(&videoFrameMutex);
            if ((pts = frame->best_effort_timestamp) == AV_NOPTS_VALUE) {
                pts = videoClock;
            }
            play = pts * frame_time_stamp;
            videoClock =
                    play + (frame->repeat_pict * 0.5 * frame_time_stamp + frame_time_stamp);
            delay = play - last_play;
            diff = videoClock - audioClock;
            sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
            if (fabs(diff) < 10) {
                if (diff <= -sync_threshold) {
                    delay = FFMAX(0.01, delay + diff);
                } else if (diff >= sync_threshold) {
                    delay = delay + diff;
                }
            }
            if (delay <= 0 || delay > 1) {
                delay = last_delay;
            }
            last_delay = delay;
            if (delay < 0) {
                delay = 0;
            }
            last_play = play;
            //上锁
            if (ANativeWindow_lock(nativeWindow, &nativeWindowBuffer, nullptr)) {
                if (delay > 0.001) {
                    av_usleep((int) (delay * 1000000));
                }
                av_frame_free(&frame);
                continue;
            }
            //转换成RGBA格式
            sws_scale(swsContext, frame->data, frame->linesize, 0, frame->height,
                      rgb_frame->data, rgb_frame->linesize);
            //  rgb_frame是有画面数据
            auto *dst = static_cast<uint8_t *>(nativeWindowBuffer.bits);
            //拿到一行有多少个字节 RGBA
            int destStride = nativeWindowBuffer.stride * 4;
            //像素数据的首地址
            uint8_t *src = rgb_frame->data[0];
            //实际内存一行数量
            int srcStride = rgb_frame->linesize[0];
            for (int i = 0; i < avCodecContext->height; i++) {
                //将rgb_frame中每一行的数据复制给nativewindow
                memcpy(dst + i * destStride, src + i * srcStride, srcStride);
            }
            ANativeWindow_unlockAndPost(nativeWindow);
            if (delay > 0.001) {
                av_usleep((int) (delay * 1000000));
            }
            av_frame_free(&frame);
        } else {
            pthread_mutex_unlock(&videoFrameMutex);
        }
    }
    postEventFromNative(MEDIA_PLAY_STATE_CHANGE, 0, 0, nullptr);
}

void BipPlayer::setVideoSurface(ANativeWindow *window) {
    //申请ANativeWindow
    if (nativeWindow != nullptr && nativeWindow != window) {
        ANativeWindow_release(nativeWindow);
        nativeWindow = nullptr;
    }
    nativeWindow = window;
    LOGE("set Surface with window %d", nativeWindow);
    if (avCodecContext != nullptr && nativeWindow != nullptr) {
        //配置nativeWindow
        ANativeWindow_setBuffersGeometry(nativeWindow, avCodecContext->width,
                                         avCodecContext->height, WINDOW_FORMAT_RGBA_8888);
    }
}

void BipPlayer::postEventFromNative(int what, int arg1, int arg2, void *object) const {
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
    if (playState <= STATE_ERROR) {
        if (playState == STATE_ERROR) {
            reset();
        }
        return;
    }
    pthread_mutex_lock(&videoMutex);
    pthread_mutex_lock(&audioMutex);
    pthread_mutex_lock(&videoFrameMutex);
    pthread_mutex_lock(&audioFrameMutex);
    playState = STATE_UN_DEFINE;
    (*slPlayItf)->SetPlayState(slPlayItf, SL_PLAYSTATE_STOPPED);
    pthread_cond_signal(&audioCond);
    pthread_mutex_unlock(&audioMutex);
    pthread_cond_signal(&videoCond);
    pthread_mutex_unlock(&videoMutex);
    pthread_cond_signal(&audioFrameEmptyCond);
    pthread_mutex_unlock(&audioFrameMutex);
    pthread_cond_signal(&videoFrameEmptyCond);
    pthread_mutex_unlock(&videoFrameMutex);
    killAllThread();
    if (avCodecContext != nullptr) {
        avcodec_free_context(&avCodecContext);
        avCodecContext = nullptr;
    }
    if (audioCodecContext != nullptr) {
        avcodec_free_context(&audioCodecContext);
        audioCodecContext = nullptr;
    }
    if (avFormatContext != nullptr) {
        avformat_free_context(avFormatContext);
        avFormatContext = nullptr;
    }
    clear(audioPacketQueue);
    clear(videoPacketQueue);
    clear(videoFrameQueue);
    clear(audioFrameQueue);
    videoClock = 0;
    audioClock = 0;
}

void BipPlayer::reset() {
    if (playState == STATE_RELEASE || playState == STATE_UN_DEFINE) {
        return;
    }
    pthread_mutex_lock(&videoMutex);
    pthread_mutex_lock(&audioMutex);
    pthread_mutex_lock(&videoFrameMutex);
    pthread_mutex_lock(&audioFrameMutex);
    playState = STATE_UN_DEFINE;
    (*slPlayItf)->SetPlayState(slPlayItf, SL_PLAYSTATE_STOPPED);
    pthread_cond_signal(&audioCond);
    pthread_mutex_unlock(&audioMutex);
    pthread_cond_signal(&videoCond);
    pthread_mutex_unlock(&videoMutex);
    pthread_cond_signal(&audioFrameEmptyCond);
    pthread_mutex_unlock(&audioFrameMutex);
    pthread_cond_signal(&videoFrameEmptyCond);
    pthread_mutex_unlock(&videoFrameMutex);
    killAllThread();
    if (avCodecContext != nullptr) {
        avcodec_free_context(&avCodecContext);
        avCodecContext = nullptr;
    }
    if (audioCodecContext != nullptr) {
        avcodec_free_context(&audioCodecContext);
        audioCodecContext = nullptr;
    }
    if (avFormatContext != nullptr) {
        avformat_free_context(avFormatContext);
        avFormatContext = nullptr;
    }
    clear(audioPacketQueue);
    clear(videoPacketQueue);
    clear(videoFrameQueue);
    clear(audioFrameQueue);
    videoClock = 0;
    audioClock = 0;
}

void BipPlayer::prepare() {
    if (playState != STATE_UN_DEFINE) {
        postEventFromNative(MEDIA_ERROR, ERROR_STATE_ILLEGAL, 0, nullptr);
        return;
    }
    playState = STATE_PREPARING;
    //打开输入流
    avFormatContext = avformat_alloc_context();
    AVDictionary *dic = nullptr;
    av_dict_set(&dic, "bufsize", "655360", 0);
    std::map<const char *, const char *>::iterator iterator;
    iterator = formatOps.begin();
    while (iterator != formatOps.end()) {
        LOGE("prepare with format option %s:%s", iterator->first, iterator->second);
        av_dict_set(&dic, iterator->first, iterator->second, 0);
        iterator++;
    }
    int prepareResult;
    char *errorMsg = static_cast<char *>(av_malloc(1024));
    prepareResult = avformat_open_input(&avFormatContext, inputPath, nullptr, &dic);
    if (prepareResult != 0) {
        playState = STATE_ERROR;
        postEventFromNative(MEDIA_ERROR, ERROR_PREPARE_FAILED, prepareResult, nullptr);
        av_strerror(prepareResult, errorMsg, 1024);
        LOGE("open input error %s", errorMsg);
        return;
    }
    prepareResult = avformat_find_stream_info(avFormatContext, nullptr);
    if (prepareResult != 0) {
        playState = STATE_ERROR;
        postEventFromNative(MEDIA_ERROR, ERROR_PREPARE_FAILED, prepareResult, nullptr);
        av_strerror(prepareResult, errorMsg, 1024);
        LOGE("find stream info error %s", errorMsg);
        return;
    }
    duration = avFormatContext->duration / 1000;
    //找到视频流
    for (int i = 0; i < avFormatContext->nb_streams; ++i) {
        if (avFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_index = i;
            videoTimeBase = avFormatContext->streams[i]->time_base;
        } else if (avFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_index = i;
            audioTimeBase = avFormatContext->streams[i]->time_base;
        }
    }
    AVDictionary *codecDic = nullptr;
    std::map<const char *, const char *>::iterator codecIterator;
    codecIterator = codecOps.begin();
    while (codecIterator != codecOps.end()) {
        LOGE("prepare with codec option %s:%s", codecIterator->first, codecIterator->second);
        av_dict_set(&codecDic, codecIterator->first, codecIterator->second, 0);
        codecIterator++;
    }
    //打开视频解码器
    AVCodec *avCodec = avcodec_find_decoder(
            avFormatContext->streams[video_index]->codecpar->codec_id);
    avCodecContext = avcodec_alloc_context3(avCodec);
    avcodec_parameters_to_context(avCodecContext, avFormatContext->streams[video_index]->codecpar);
    if (avcodec_open2(avCodecContext, avCodec, &codecDic) < 0) {
        return;
    }
    if (nativeWindow != nullptr) {
        //配置nativeWindow
        ANativeWindow_setBuffersGeometry(nativeWindow, avCodecContext->width,
                                         avCodecContext->height, WINDOW_FORMAT_RGBA_8888);
    }
    //打开音频解码器
    AVCodec *audioCodec = avcodec_find_decoder(
            avFormatContext->streams[audio_index]->codecpar->codec_id);
    audioCodecContext = avcodec_alloc_context3(audioCodec);
    avcodec_parameters_to_context(audioCodecContext,
                                  avFormatContext->streams[audio_index]->codecpar);
    avcodec_open2(audioCodecContext, audioCodec, nullptr);

    //申请音频的AVPacket和AVFrame
    audioSwrContext = swr_alloc();
    audioBuffer = static_cast<uint8_t *>(av_mallocz(4096 * 4));
    swr_alloc_set_opts(audioSwrContext, outChLayout, AV_SAMPLE_FMT_S16,
                       44100,
                       audioCodecContext->channel_layout, audioCodecContext->sample_fmt,
                       audioCodecContext->sample_rate, 0,
                       nullptr);
    swr_init(audioSwrContext);
    outChannelsNumber = av_get_channel_layout_nb_channels(outChLayout);





    //申请视频的AVPacket和AVFrame
    rgb_frame = av_frame_alloc();
    auto *out_buffer = (uint8_t *) av_malloc(
            av_image_get_buffer_size(AV_PIX_FMT_RGBA, avCodecContext->width, avCodecContext->height,
                                     1));
    av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, out_buffer, AV_PIX_FMT_RGBA,
                         avCodecContext->width, avCodecContext->height, 1);

    //转换上下文
    swsContext = sws_getContext(avCodecContext->width, avCodecContext->height,
                                avCodecContext->pix_fmt, avCodecContext->width,
                                avCodecContext->height, AV_PIX_FMT_RGBA, SWS_BICUBIC,
                                nullptr, nullptr, nullptr);
    AVDictionary *swsDic = nullptr;
    std::map<const char *, const char *>::iterator swsIterator;
    swsIterator = swsOps.begin();
    while (swsIterator != swsOps.end()) {
        LOGE("prepare with sws option %s:%s", swsIterator->first, swsIterator->second);
        av_dict_set(&swsDic, swsIterator->first, swsIterator->second, 0);
        swsIterator++;
    }
    av_opt_set_dict(swsContext, &swsDic);


    //解码
    int readResult;
    int videoCache = 0;
    int audioCache = 0;
    playState = STATE_BUFFERING;
    pthread_create(&cacheVideoThreadId, nullptr, cacheVideoFrameThread, this);
    pthread_create(&cacheAudioThreadId, nullptr, cacheAudioFrameThread, this);
    while (playState > STATE_ERROR) {
        pthread_mutex_lock(&avOpsMutex);
        auto *avPacket = av_packet_alloc();
        readResult = av_read_frame(avFormatContext, avPacket);
        pthread_mutex_unlock(&avOpsMutex);
        if (readResult >= 0) {
            if (avPacket->stream_index == video_index) {
                pthread_mutex_lock(&videoMutex);
                videoPacketQueue.push(avPacket);
                pthread_cond_signal(&videoCond);
                pthread_mutex_unlock(&videoMutex);
                if (videoCache != -1) {
                    videoCache++;
                }
                if (videoCache > 100 && audioCache > 100) {
                    videoCache = -1;
                    audioCache = -1;
                    notifyPrepared();
                }
            } else if (avPacket->stream_index == audio_index) {
                pthread_mutex_lock(&audioMutex);
                audioPacketQueue.push(avPacket);
                pthread_cond_signal(&audioCond);
                pthread_mutex_unlock(&audioMutex);
                if (audioCache != -1) {
                    audioCache++;
                }
                if (videoCache > 100 && audioCache > 100) {
                    videoCache = -1;
                    audioCache = -1;
                    notifyPrepared();
                }
            }
        } else {
            av_usleep(5000);
        }
//        av_packet_unref(packet);
    }
    av_free(errorMsg);
}

void BipPlayer::notifyPrepared() {
    playState = STATE_PREPARED;
    postEventFromNative(MEDIA_PREPARED, 0, 0, nullptr);
    auto iterator = playerOps.find("start-on-prepared");
    if (iterator != playerOps.end()) {
        if (iterator->second != nullptr && strcmp("1", iterator->second) == 0) {
            start();
        }
    }
}

void BipPlayer::notifyCompleted() {
    playState = STATE_COMPLETED;
    postEventFromNative(MEDIA_PLAYBACK_COMPLETE, 0, 0, nullptr);
}

void BipPlayer::playAudio() {
    (*slPlayItf)->SetPlayState(slPlayItf, SL_PLAYSTATE_PLAYING);
    innerBufferQueueCallback();
}

BipPlayer::BipPlayer() {
    createEngine();
    createMixVolume();
    createPlayer();
    pthread_mutex_init(&videoMutex, nullptr);
    pthread_cond_init(&videoCond, nullptr);
    pthread_mutex_init(&audioMutex, nullptr);
    pthread_cond_init(&audioCond, nullptr);
    pthread_mutex_init(&audioFrameMutex, nullptr);
    pthread_cond_init(&audioFrameCond, nullptr);
    pthread_mutex_init(&videoFrameMutex, nullptr);
    pthread_cond_init(&videoFrameCond, nullptr);
    pthread_mutex_init(&avOpsMutex, nullptr);
    pthread_cond_init(&audioFrameEmptyCond, nullptr);
    pthread_cond_init(&videoFrameEmptyCond, nullptr);
};

BipPlayer::~BipPlayer() {
    release();
};

long BipPlayer::getDuration() const {
    return duration;
}

long BipPlayer::getCurrentPosition() const {
    return (long) (audioClock * 1000);
}

int BipPlayer::getVideoHeight() const {
    if (avCodecContext != nullptr) {
        return avCodecContext->height;
    }
    return 0;
}

int BipPlayer::getVideoWidth() const {
    if (avCodecContext != nullptr) {
        return avCodecContext->width;
    }
    return 0;
}

bool BipPlayer::isPlaying() const {
    return playState == STATE_PLAYING;
}

void clear(std::queue<AVPacket *> &q) {
    while (!q.empty()) {
        AVPacket *packet = q.front();
        av_packet_free(&packet);
        q.pop();
    }
}

void clear(std::queue<AVFrame *> &q) {
    while (!q.empty()) {
        AVFrame *frame = q.front();
        av_frame_free(&frame);
        q.pop();
    }
}

void BipPlayer::seekTo(long time) {
    if (playState >= STATE_PREPARED) {
        pthread_mutex_lock(&videoMutex);
        pthread_mutex_lock(&audioMutex);
        pthread_mutex_lock(&videoFrameMutex);
        pthread_mutex_lock(&audioFrameMutex);
        pthread_mutex_lock(&avOpsMutex);
        av_seek_frame(avFormatContext, audio_index, (double) (time) / av_q2d(audioTimeBase) / 1000,
                      AVSEEK_FLAG_BACKWARD);
        av_seek_frame(avFormatContext, video_index, (double) (time) / av_q2d(videoTimeBase) / 1000,
                      AVSEEK_FLAG_BACKWARD);
        clear(videoPacketQueue);
        clear(audioPacketQueue);
        clear(videoFrameQueue);
        clear(audioFrameQueue);
        audioClock = (double) (time) / 1000;
        videoClock = (double) (time) / 1000;
        pthread_mutex_unlock(&avOpsMutex);
        pthread_cond_signal(&videoFrameEmptyCond);
        pthread_cond_signal(&audioFrameEmptyCond);
        pthread_mutex_unlock(&audioMutex);
        pthread_mutex_unlock(&videoMutex);
        pthread_mutex_unlock(&audioFrameMutex);
        pthread_mutex_unlock(&videoFrameMutex);
        if (playState != STATE_PLAYING) {

        }
        postEventFromNative(MEDIA_SEEK_COMPLETE, 0, 0, nullptr);
    }
}

void BipPlayer::pause() {
    if (playState == STATE_PLAYING) {
        (*slPlayItf)->SetPlayState(slPlayItf, SL_PLAYSTATE_PAUSED);
        playState = STATE_PAUSED;
    }
}

void *prepareVideoThread(void *args) {
    auto bipPlayer = (BipPlayer *) args;
    pthread_setname_np(bipPlayer->prepareThreadId, "BipPrepare");
    bipPlayer->prepare();
    pthread_exit(nullptr);//退出线程
}

void *playAudioThread(void *args) {
    auto bipPlayer = (BipPlayer *) args;
    pthread_setname_np(bipPlayer->audioPlayId, "BipPlayAudio");
    bipPlayer->playAudio();
    pthread_exit(nullptr);//退出线程
}

void *playVideoThread(void *args) {
    auto bipPlayer = (BipPlayer *) args;
    pthread_setname_np(bipPlayer->videoPlayId, "BipPlayVideo");
    bipPlayer->showVideoPacket();
    pthread_exit(nullptr);//退出线程
}

void playerSetJavaWeak(BipPlayer *bipPlayer, void *weak_this) {
    bipPlayer->weakJavaThis = weak_this;
}

void BipPlayer::start() {
    if (playState == STATE_PLAYING) {
        return;
    }
    if (playState >= STATE_PREPARED) {
        playState = STATE_PLAYING;
        pthread_create(&audioPlayId, nullptr, playAudioThread, this);//开启begin线程
        pthread_create(&videoPlayId, nullptr, playVideoThread, this);//开启begin线程
    }
}

void BipPlayer::cacheVideo() {
    while (true) {
        pthread_mutex_lock(&videoMutex);
        if (playState < STATE_PREPARING || playState >= STATE_COMPLETED) {
            pthread_mutex_unlock(&videoMutex);
            break;
        }
        if (videoPacketQueue.empty()) {
            pthread_cond_wait(&videoCond, &videoMutex);
        }
        if (!videoPacketQueue.empty()) {
            AVPacket *packet = videoPacketQueue.front();
            videoPacketQueue.pop();
            pthread_mutex_unlock(&videoMutex);
            AVFrame *frame = av_frame_alloc();
            pthread_mutex_lock(&avOpsMutex);
            avcodec_send_packet(avCodecContext, packet);
            int receiveResult = avcodec_receive_frame(avCodecContext, frame);
            pthread_mutex_unlock(&avOpsMutex);
            av_packet_free(&packet);
            if (!receiveResult) {
                pthread_mutex_lock(&videoFrameMutex);
                videoFrameQueue.push(frame);
                pthread_cond_signal(&videoFrameCond);
                if (videoFrameQueue.size() >= max_frame_buff_size) {
                    pthread_cond_wait(&videoFrameEmptyCond, &videoFrameMutex);
                }
                pthread_mutex_unlock(&videoFrameMutex);
            }
        } else {
            pthread_mutex_unlock(&videoMutex);
        }
    }
}

void BipPlayer::cacheAudio() {
    while (true) {
        pthread_mutex_lock(&audioMutex);
        if (playState < STATE_PREPARING || playState >= STATE_COMPLETED) {
            pthread_mutex_unlock(&audioMutex);
            break;
        }
        if (audioPacketQueue.empty()) {
            pthread_cond_wait(&audioCond, &audioMutex);
        }
        if (!audioPacketQueue.empty()) {
            AVPacket *packet = audioPacketQueue.front();
            audioPacketQueue.pop();
            pthread_mutex_unlock(&audioMutex);
            AVFrame *audioFrame = av_frame_alloc();
            pthread_mutex_lock(&avOpsMutex);
            avcodec_send_packet(audioCodecContext, packet);
            int receiveResult = avcodec_receive_frame(audioCodecContext, audioFrame);
            pthread_mutex_unlock(&avOpsMutex);
            av_packet_free(&packet);
            if (!receiveResult) {
                pthread_mutex_lock(&audioFrameMutex);
                audioFrameQueue.push(audioFrame);
                pthread_cond_signal(&audioFrameCond);
                if (audioFrameQueue.size() >= max_frame_buff_size) {
                    pthread_cond_wait(&audioFrameEmptyCond, &audioFrameMutex);
                }
                pthread_mutex_unlock(&audioFrameMutex);
            }
        } else {
            pthread_mutex_unlock(&audioMutex);
        }
    }
}


void *cacheVideoFrameThread(void *args) {
    auto bipPlayer = (BipPlayer *) args;
    pthread_setname_np(bipPlayer->cacheVideoThreadId, "BipCacheVideo");
    bipPlayer->cacheVideo();
    pthread_exit(nullptr);//退出线程
}

void *cacheAudioFrameThread(void *args) {
    auto bipPlayer = (BipPlayer *) args;
    pthread_setname_np(bipPlayer->cacheAudioThreadId, "BipCacheAudio");
    bipPlayer->cacheAudio();
    pthread_exit(nullptr);//退出线程
}

void BipPlayer::killAllThread() {
    if (prepareThreadId != 0 && pthread_kill(prepareThreadId, 0) == 0) {
        pthread_join(prepareThreadId, nullptr);
        prepareThreadId = 0;
    }
    if (videoPlayId != 0 && pthread_kill(videoPlayId, 0) == 0) {
        pthread_join(videoPlayId, nullptr);
        videoPlayId = 0;
    }
    if (cacheAudioThreadId != 0 && pthread_kill(cacheAudioThreadId, 0) == 0) {
        pthread_join(cacheAudioThreadId, nullptr);
        cacheAudioThreadId = 0;
    }
    if (cacheVideoThreadId != 0 && pthread_kill(cacheVideoThreadId, 0) == 0) {
        pthread_join(cacheVideoThreadId, nullptr);
        cacheVideoThreadId = 0;
    }
}


