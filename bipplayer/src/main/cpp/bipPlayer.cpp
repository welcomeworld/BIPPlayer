//
// Created by welcomeworld on 2021/9/5.
//
#include "bipPlayer.h"

jclass defaultBIPPlayerClass;
jmethodID postEventFromNativeMethodId;
jfieldID nativePlayerRefId;
JavaVM *staticVm;

void *bipMsgLoopThread(void *args);

long calculateTime(timeval startTime, timeval endTime) {
    return (endTime.tv_sec - startTime.tv_sec) * 1000 +
           (endTime.tv_usec - startTime.tv_usec) / 1000;
}

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
    pthread_cond_destroy(&msgCond);
    pthread_mutex_destroy(&videoMutex);
    pthread_mutex_destroy(&videoCacheMutex);
    pthread_mutex_destroy(&audioMutex);
    pthread_mutex_destroy(&audioCacheMutex);
    pthread_mutex_destroy(&audioFrameMutex);
    pthread_mutex_destroy(&videoFrameMutex);
    pthread_mutex_destroy(&avOpsMutex);
    pthread_mutex_destroy(&msgMutex);
    delete interruptContext;
    delete nextInterruptContext;
    delete soundtouch;
    destroyOpenSL();
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
            if (!strcmp(key, MEDIACODEC)) {
                mediacodec = strcmp("0", value);
            } else if (!strcmp(key, START_ON_PREPARED)) {
                startOnPrepared = strcmp("0", value);
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
            if (audioAvailable()) {
                if (baseClock * 1000 > duration || FFABS(baseClock * 1000 - duration) < 500) {
                    notifyCompleted();
                    pthread_mutex_unlock(&audioFrameMutex);
                    break;
                } else {
                    if (!interruptContext->audioBuffering) {
                        notifyInfo(MEDIA_INFO_BUFFERING_START);
                        interruptContext->audioBuffering = true;
                    }
                }
                LOGW("audio play empty wait");
                pthread_cond_wait(&audioFrameCond, &audioFrameMutex);
                LOGW("audio play empty wait success");
            } else {
                pthread_mutex_unlock(&audioFrameMutex);
                break;
            }
        }
        while (audioAvailable() && !audioFrameQueue.empty() && playState == STATE_PLAYING) {
            AVFrame *audioFrame = audioFrameQueue.front();
            audioFrameQueue.pop();
            interruptContext->audioSize = audioFrameQueue.size();
            int outSample = swr_convert(audioSwrContext, &audioBuffer, 4096 * 2,
                                        (const uint8_t **) (audioFrame->data),
                                        audioFrame->nb_samples);
            int size = 0;
            soundtouch->putSamples(reinterpret_cast<const soundtouch::SAMPLETYPE *>(audioBuffer),
                                   outSample);
            int soundOutSample = 0;
            do {
                soundOutSample = soundtouch->receiveSamples(
                        reinterpret_cast<soundtouch::SAMPLETYPE *>(audioBuffer + size),
                        OUT_SAMPLE_RATE / OUT_CHANNEL_NUMBER);
                size += soundOutSample * OUT_CHANNEL_NUMBER * BYTES_PER_SAMPLE;
            } while (soundOutSample != 0);
            if (size == 0) {
                av_frame_free(&audioFrame);
                continue;
            }
            pthread_cond_signal(&audioFrameEmptyCond);
            pthread_mutex_unlock(&audioFrameMutex);
//            不使用soundTouch则取消注释并将上面soundTouch部分注释掉
//            size = av_samples_get_buffer_size(nullptr, OUT_CHANNEL_NUMBER,
//                                              outSample,
//                                              AV_SAMPLE_FMT_S16, 1);
            if (audioFrame->pts != AV_NOPTS_VALUE) {
                //这一帧的起始时间
                audioClock = audioFrame->pts * av_q2d(audioTimeBase);
                //这一帧数据的时间
                double time =
                        size / ((double) OUT_SAMPLE_RATE * OUT_CHANNEL_NUMBER * BYTES_PER_SAMPLE);
                //最终音频时钟
                audioClock = time + audioClock;
                baseClock = audioClock;
            }
            av_frame_free(&audioFrame);
            return size;
        }
        pthread_mutex_unlock(&audioFrameMutex);
    }
    return 0;
}

void BipPlayer::showVideoPacket() {
    postEventFromNative(MEDIA_PLAY_STATE_CHANGE, 1, 0, nullptr);
    //视频缓冲区
    ANativeWindow_Buffer nativeWindowBuffer;
    double delay         //线程休眠时间
    , diff   //音频帧与视频帧相差时间
    , sync_threshold; //音视频差距界限
    long decodeStartTime = 0;
    long decodeEndTime = 0;
    long decodeSpendTime;
    long realSleepTime; //计算后的实际线程休眠时间,单位微秒
    //上次计算fps的时间节点,单位毫秒，大于九百毫秒计算一次
    long realFpsMarkTime = av_gettime();
    int realShowFrame = 0;
    while (playState == STATE_PLAYING) {
        decodeStartTime = av_gettime();
        pthread_mutex_lock(&videoFrameMutex);
        if (playState != STATE_PLAYING) {
            pthread_mutex_unlock(&videoFrameMutex);
            break;
        }
        if (videoFrameQueue.empty()) {
            if (!audioAvailable()) {
                if (baseClock * 1000 > duration || FFABS(baseClock * 1000 - duration) < 500) {
                    notifyCompleted();
                    pthread_mutex_unlock(&videoFrameMutex);
                    break;
                } else {
                    if (!interruptContext->audioBuffering) {
                        notifyInfo(MEDIA_INFO_BUFFERING_START);
                        interruptContext->audioBuffering = true;
                    }
                }
            }
            pthread_cond_wait(&videoFrameCond, &videoFrameMutex);
        }
        if (!videoFrameQueue.empty()) {
            AVFrame *frame;
            while (!videoFrameQueue.empty()) {
                frame = videoFrameQueue.front();
                videoFrameQueue.pop();
                interruptContext->videoSize = videoFrameQueue.size();
                double realHopeFps = fps * playSpeed;
                delay = 1.0 / realHopeFps;
                if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                    videoClock = frame->best_effort_timestamp * av_q2d(videoTimeBase);
                } else if (frame->pts != AV_NOPTS_VALUE) {
                    videoClock = frame->pts * av_q2d(videoTimeBase);
                } else {
                    videoClock += 1.0 / fps;
                }
                if (!audioAvailable()) {
                    baseClock = videoClock;
                }
                diff = videoClock - baseClock;
                //视频过于落后，不再转换格式，直接丢帧
                if ((diff < -0.35 || (diff < -0.1 && realHopeFps > 80)) &&
                    !videoFrameQueue.empty()) {
                    LOGE("video slow to skip a frame with diff %lf when fps %d", diff, fps);
                    av_frame_free(&frame);
                    frame = nullptr;
                    continue;
                } else {
                    break;
                }
            }
            //转换成RGBA格式
            long scaleStartTime = av_gettime();
            if (matchYuv(frame->format)) {
                yuvToARGB(frame, rgb_frame->data[0]);
            } else {
                sws_scale(swsContext, frame->data, frame->linesize, 0, frame->height,
                          rgb_frame->data, rgb_frame->linesize);
            }
            long scaleEndTime = av_gettime();
            LOGD("video scale spend time %ld when real fps %lf",
                 (scaleEndTime - scaleStartTime) / 1000,
                 av_q2d(avFormatContext->streams[video_index]->r_frame_rate));
            pthread_cond_signal(&videoFrameEmptyCond);
            pthread_mutex_unlock(&videoFrameMutex);
            //音视频差距合理界限，超出则调整，没有则认为同步，不需要调整
            sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
            if (fabs(diff) > sync_threshold) {
                LOGD("video change with diff %lf when delay %lf", diff, delay);
                if (diff > 0.4) {
                    //视频太过超前，慢慢等待音频追赶，不直接卡死
                    delay = delay + sync_threshold;
                } else {
                    delay = delay + diff;
                }
            }
            //配置nativeWindow
            if (nativeWindow != nullptr) {
                ANativeWindow_setBuffersGeometry(nativeWindow, frame->width,
                                                 frame->height, WINDOW_FORMAT_RGBA_8888);
            }
            //上锁
            if (ANativeWindow_lock(nativeWindow, &nativeWindowBuffer, nullptr)) {
                //锁定窗口失败
                decodeEndTime = av_gettime();
                decodeSpendTime = decodeEndTime - decodeStartTime;
                realSleepTime = (long) (delay * 1000000) - decodeSpendTime;
                if (realSleepTime > 1000) {
                    av_usleep(realSleepTime);
                }
                av_frame_free(&frame);
                continue;
            }
            //  rgb_frame是有画面数据
            auto *dst = static_cast<uint8_t *>(nativeWindowBuffer.bits);
            //拿到一行有多少个字节 RGBA nativeWindow缓冲区中每一行长度不一定等于视频宽度
            int destStride = nativeWindowBuffer.stride * 4;
            //像素数据的首地址
            uint8_t *src = rgb_frame->data[0];
            //实际内存一行数量
            int srcStride = rgb_frame->linesize[0];
            for (int i = 0; i < frame->height; i++) {
                //必须将rgb_frame的数据一行一行复制给nativewindow
                memcpy(dst + i * destStride, src + i * srcStride, srcStride);
            }
            ANativeWindow_unlockAndPost(nativeWindow);
            decodeEndTime = av_gettime();
            decodeSpendTime = decodeEndTime - decodeStartTime;
            realSleepTime = (long) (delay * 1000000) - decodeSpendTime;
            long fpsTime = (decodeEndTime - realFpsMarkTime) / 1000;
            if (fpsTime > 950) {
                int realFps = ++realShowFrame * 1000 / fpsTime;
                postEventFromNative(MEDIA_INFO, MEDIA_INFO_FPS, realFps, nullptr);
                realFpsMarkTime = decodeEndTime;
                realShowFrame = 0;
            } else {
                realShowFrame++;
            }
            if (realSleepTime > 1000) {
                av_usleep(realSleepTime);
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
    if (playState == STATE_RELEASE || playState == STATE_UN_DEFINE) {
        return;
    }
    lockAll();
    playState = STATE_UN_DEFINE;
    (*slPlayItf)->SetPlayState(slPlayItf, SL_PLAYSTATE_STOPPED);
    unLockAll();
    waitAllThreadStop();
    freeContexts();
    if (inputPath) {
        free((void *) inputPath);
        inputPath = nullptr;
    }
    if (nextInputPath) {
        free((void *) nextInputPath);
        nextInputPath = nullptr;
    }
    if (dashInputPath) {
        free((void *) dashInputPath);
        dashInputPath = nullptr;
    }
    clear(audioPacketQueue);
    clear(videoPacketQueue);
    clear(videoFrameQueue);
    clear(audioFrameQueue);
    av_freep(&audioBuffer);
    videoClock = 0;
    audioClock = 0;
    fps = 0;
    setPlaySpeed(1.0f);
    if (interruptContext->audioBuffering) {
        interruptContext->audioBuffering = false;
        notifyInfo(MEDIA_INFO_BUFFERING_END);
    }
}

void BipPlayer::reset() {
    if (playState == STATE_RELEASE || playState == STATE_UN_DEFINE) {
        return;
    }
    lockAll();
    playState = STATE_UN_DEFINE;
    (*slPlayItf)->SetPlayState(slPlayItf, SL_PLAYSTATE_STOPPED);
    unLockAll();
    waitAllThreadStop();
    freeContexts();
    if (inputPath) {
        free((void *) inputPath);
        inputPath = nullptr;
    }
    if (nextInputPath) {
        free((void *) nextInputPath);
        nextInputPath = nullptr;
    }
    if (dashInputPath) {
        free((void *) dashInputPath);
        dashInputPath = nullptr;
    }
    clear(audioPacketQueue);
    clear(videoPacketQueue);
    clear(videoFrameQueue);
    clear(audioFrameQueue);
    av_freep(&audioBuffer);
    videoClock = 0;
    audioClock = 0;
    fps = 0;
    setPlaySpeed(1.0f);
    if (interruptContext->audioBuffering) {
        interruptContext->audioBuffering = false;
        notifyInfo(MEDIA_INFO_BUFFERING_END);
    }
    //todo 清空引用
    formatOps.clear();
    swsOps.clear();
    codecOps.clear();
}

int avFormatInterrupt(void *ctx) {
    auto interruptContext = (InterruptContext *) ctx;
    timeval currentTime{};
    gettimeofday(&currentTime, nullptr);
    long diffTime = calculateTime(interruptContext->readStartTime, currentTime);
    if (interruptContext->readStartTime.tv_sec != 0) {
        if (diffTime > 555) {
            LOGE("no interrupt because may cause seek fail");
            return 0;
        }
    }
    return 0;
}

void BipPlayer::prepare() {
    if (playState != STATE_UN_DEFINE || inputPath == nullptr) {
        postEventFromNative(MEDIA_ERROR, ERROR_STATE_ILLEGAL, 0, nullptr);
        return;
    }
    LOGE("start prepare %s", inputPath);
    playState = STATE_PREPARING;
    //打开输入流
    avFormatContext = avformat_alloc_context();
    interruptContext->reset();
    avFormatContext->interrupt_callback.callback = avFormatInterrupt;
    avFormatContext->interrupt_callback.opaque = interruptContext;
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
    char *errorMsg = static_cast<char *>(av_mallocz(1024));
    if (!strncmp(inputPath, "fd:", 3)) {
        fdAvioContext = new FdAVIOContext();
        fdAvioContext->openFromDescriptor(atoi(inputPath + 3), "rb");
        avFormatContext->pb = fdAvioContext->getAvioContext();
    }
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
    video_index = -1;
    audio_index = -1;
    //找到视频流
    for (int i = 0; i < avFormatContext->nb_streams; ++i) {
        if (avFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            LOGE("find video stream with index %d", i);
            video_index = i;
            videoTimeBase = avFormatContext->streams[i]->time_base;
        } else if (avFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            LOGE("find audio stream with index %d", i);
            audio_index = i;
            audioTimeBase = avFormatContext->streams[i]->time_base;
        }
    }
    //找得到视频流
    if (videoAvailable()) {
        AVDictionary *codecDic = nullptr;
        std::map<const char *, const char *>::iterator codecIterator;
        codecIterator = codecOps.begin();
        while (codecIterator != codecOps.end()) {
            LOGE("prepare with codec option %s:%s", codecIterator->first, codecIterator->second);
            av_dict_set(&codecDic, codecIterator->first, codecIterator->second, 0);
            codecIterator++;
        }
        //打开视频解码器
        AVCodec *avCodec = nullptr;
        AVCodecID videoCodecID = avFormatContext->streams[video_index]->codecpar->codec_id;
        if (mediacodec) {
            avCodec = getMediaCodec(videoCodecID);
            if (avCodec == nullptr) {
                avCodec = avcodec_find_decoder(videoCodecID);
            }
        } else {
            avCodec = avcodec_find_decoder(videoCodecID);
        }
        avCodecContext = avcodec_alloc_context3(avCodec);
        avcodec_parameters_to_context(avCodecContext,
                                      avFormatContext->streams[video_index]->codecpar);
        //ffmpeg auto select thread_count
        avCodecContext->thread_count = 0;
        if (avCodec->capabilities | AV_CODEC_CAP_FRAME_THREADS) {
            avCodecContext->thread_type = FF_THREAD_FRAME;
        } else if (avCodec->capabilities | AV_CODEC_CAP_SLICE_THREADS) {
            avCodecContext->thread_type = FF_THREAD_SLICE;
        } else {
            avCodecContext->thread_count = 1;
        }
        prepareResult = avcodec_open2(avCodecContext, avCodec, &codecDic);
        if (prepareResult != 0) {
            av_strerror(prepareResult, errorMsg, 1024);
            LOGE("打开解码失败: %s", errorMsg);
            playState = STATE_ERROR;
            postEventFromNative(MEDIA_ERROR, ERROR_PREPARE_FAILED, prepareResult, nullptr);
            return;
        } else {
            LOGE("解码成功:%s threadType: %d threadCount: %d pix_format %d",
                 avCodec->name,
                 avCodecContext->thread_type, avCodecContext->thread_count,
                 avCodecContext->pix_fmt);
        }
        if (avCodecContext->pix_fmt == -1 || avCodecContext->width == 0 ||
            avCodecContext->height == 0) {
            LOGE("解码格式出错");
            playState = STATE_ERROR;
            postEventFromNative(MEDIA_ERROR, ERROR_PREPARE_FAILED, 233, nullptr);
            return;
        }
        fps = av_q2d(avFormatContext->streams[video_index]->avg_frame_rate);
        if (fps <= 0) {
            fps = av_q2d(avFormatContext->streams[video_index]->r_frame_rate);
        }
    }
    //找得到音频流
    if (audioAvailable()) {
        //打开音频解码器
        AVCodec *audioCodec = avcodec_find_decoder(
                avFormatContext->streams[audio_index]->codecpar->codec_id);
        audioCodecContext = avcodec_alloc_context3(audioCodec);
        avcodec_parameters_to_context(audioCodecContext,
                                      avFormatContext->streams[audio_index]->codecpar);
        avcodec_open2(audioCodecContext, audioCodec, nullptr);
        //申请音频的AVPacket和AVFrame
        audioSwrContext = swr_alloc();
        swr_alloc_set_opts(audioSwrContext, OUT_CHANNEL_LAYOUT, AV_SAMPLE_FMT_S16,
                           OUT_SAMPLE_RATE,
                           audioCodecContext->channel_layout, audioCodecContext->sample_fmt,
                           audioCodecContext->sample_rate, 0,
                           nullptr);
        swr_init(audioSwrContext);
    }
    audioBuffer = static_cast<uint8_t *>(av_mallocz(1024 * 48));





    //申请视频的AVPacket和AVFrame
    rgb_frame = av_frame_alloc();
    if (videoAvailable()) {
        auto *out_buffer = (uint8_t *) av_mallocz(
                av_image_get_buffer_size(AV_PIX_FMT_RGBA, avCodecContext->width,
                                         avCodecContext->height,
                                         1));
        av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, out_buffer, AV_PIX_FMT_RGBA,
                             avCodecContext->width, avCodecContext->height, 1);

        LOGE("start get swsContext from %d : %d : %d", avCodecContext->pix_fmt,
             avCodecContext->codec_id, avCodecContext->coded_height);
        //转换上下文
        swsContext = sws_getContext(avCodecContext->width, avCodecContext->height,
                                    avCodecContext->pix_fmt, avCodecContext->width,
                                    avCodecContext->height, AV_PIX_FMT_RGBA, SWS_BICUBIC,
                                    nullptr, nullptr, nullptr);
        LOGE("get swsContext success from %d", avCodecContext->pix_fmt);
        AVDictionary *swsDic = nullptr;
        std::map<const char *, const char *>::iterator swsIterator;
        swsIterator = swsOps.begin();
        while (swsIterator != swsOps.end()) {
            LOGE("prepare with sws option %s:%s", swsIterator->first, swsIterator->second);
            av_dict_set(&swsDic, swsIterator->first, swsIterator->second, 0);
            swsIterator++;
        }
        av_opt_set_dict(swsContext, &swsDic);
    }
    //解码
    int readResult;
    playState = STATE_BUFFERING;
    pthread_create(&cacheVideoThreadId, nullptr, cacheVideoFrameThread, this);
    pthread_create(&cacheAudioThreadId, nullptr, cacheAudioFrameThread, this);
    notifyInfo(MEDIA_INFO_BUFFERING_START);
    interruptContext->audioBuffering = true;
    AVFormatContext *rootFormatContext;
    while (playState > STATE_ERROR) {
        pthread_mutex_lock(&avOpsMutex);
        auto *avPacket = av_packet_alloc();
        gettimeofday(&(interruptContext->readStartTime), nullptr);
        rootFormatContext = avFormatContext;
        readResult = av_read_frame(avFormatContext, avPacket);
        pthread_mutex_unlock(&avOpsMutex);
        if (readResult >= 0) {
            if (avPacket->stream_index == video_index) {
                if (!audioAvailable()) {
                    if (duration > 0) {
                        int percent = round(
                                avPacket->pts * av_q2d(videoTimeBase) * 100000 / duration);
                        if (percent != bufferPercent) {
                            bufferPercent = percent;
                            postEventFromNative(MEDIA_BUFFERING_UPDATE, bufferPercent, 0, nullptr);
                        }
                    }
                }
                pthread_mutex_lock(&videoMutex);
                if (rootFormatContext == nullptr || rootFormatContext != avFormatContext) {
                    av_packet_free(&avPacket);
                    pthread_cond_signal(&videoCond);
                    pthread_mutex_unlock(&videoMutex);
                    continue;
                }
                videoPacketQueue.push(avPacket);
                pthread_cond_signal(&videoCond);
                pthread_mutex_unlock(&videoMutex);
            } else if (avPacket->stream_index == audio_index) {
                if (duration > 0) {
                    int percent = round(avPacket->pts * av_q2d(audioTimeBase) * 100000 / duration);
                    if (percent != bufferPercent) {
                        bufferPercent = percent;
                        postEventFromNative(MEDIA_BUFFERING_UPDATE, bufferPercent, 0, nullptr);
                    }
                }
                pthread_mutex_lock(&audioMutex);
                if (rootFormatContext == nullptr || rootFormatContext != avFormatContext) {
                    av_packet_free(&avPacket);
                    pthread_cond_signal(&audioCond);
                    pthread_mutex_unlock(&audioMutex);
                    continue;
                }
                audioPacketQueue.push(avPacket);
                pthread_cond_signal(&audioCond);
                pthread_mutex_unlock(&audioMutex);
            }
        } else {
            av_usleep(50000);
        }
        checkPrepared();
    }
    av_freep(&errorMsg);
}

void BipPlayer::checkPrepared() {
    if (playState == STATE_BUFFERING) {
        bool completed = true;
        if (videoAvailable()) {
            pthread_mutex_lock(&videoFrameMutex);
            if (videoFrameQueue.size() < prepare_frame_buff_size) {
                completed = false;
            }
            pthread_mutex_unlock(&videoFrameMutex);
        }
        if (audioAvailable()) {
            pthread_mutex_lock(&audioFrameMutex);
            if (audioFrameQueue.size() < prepare_frame_buff_size) {
                completed = false;
            }
            pthread_mutex_unlock(&audioFrameMutex);
        }
        if (completed) {
            notifyPrepared();
        }
    }
}

/**
 * must call with unLockAll()
 */
void BipPlayer::lockAll() {
    pthread_mutex_lock(&avOpsMutex);
    pthread_mutex_lock(&videoMutex);
    pthread_mutex_lock(&audioMutex);
    pthread_mutex_lock(&videoFrameMutex);
    pthread_mutex_lock(&audioFrameMutex);
    pthread_mutex_lock(&videoCacheMutex);
    pthread_mutex_lock(&audioCacheMutex);
}

/**
 * must call with lockAll() before
 */
void BipPlayer::unLockAll() {
    /** seek only signal two cond test
    pthread_mutex_unlock(&avOpsMutex);
    pthread_cond_signal(&videoFrameEmptyCond);
    pthread_cond_signal(&audioFrameEmptyCond);
    pthread_mutex_unlock(&audioMutex);
    pthread_mutex_unlock(&videoMutex);
    pthread_mutex_unlock(&audioFrameMutex);
    pthread_mutex_unlock(&videoFrameMutex);
     **/

    pthread_mutex_unlock(&avOpsMutex);
    pthread_cond_signal(&audioCond);
    pthread_mutex_unlock(&audioMutex);
    pthread_cond_signal(&videoCond);
    pthread_mutex_unlock(&videoMutex);
    pthread_cond_signal(&audioFrameEmptyCond);
    pthread_cond_signal(&audioFrameCond);
    pthread_mutex_unlock(&audioFrameMutex);
    pthread_cond_signal(&videoFrameEmptyCond);
    pthread_cond_signal(&videoFrameCond);
    pthread_mutex_unlock(&videoFrameMutex);
    pthread_mutex_unlock(&videoCacheMutex);
    pthread_mutex_unlock(&audioCacheMutex);
}

void BipPlayer::notifyPrepared() {
    playState = STATE_PREPARED;
    postEventFromNative(MEDIA_PREPARED, 0, 0, nullptr);
    if (startOnPrepared) {
        start();
    }
}

void BipPlayer::notifyCompleted() {
    if (playState != STATE_COMPLETED) {
        playState = STATE_COMPLETED;
        postEventFromNative(MEDIA_PLAYBACK_COMPLETE, 0, 0, nullptr);
    }
}

void BipPlayer::notifyInfo(int info) {
    postEventFromNative(MEDIA_INFO, info, 0, nullptr);
}

void BipPlayer::playAudio() {
    (*slPlayItf)->SetPlayState(slPlayItf, SL_PLAYSTATE_PLAYING);
    innerBufferQueueCallback();
}

BipPlayer::BipPlayer() {
    interruptContext = new InterruptContext();
    nextInterruptContext = new InterruptContext;
    createEngine();
    createMixVolume();
    createPlayer();
    initSoundTouch();
    pthread_mutex_init(&videoMutex, nullptr);
    pthread_mutex_init(&videoCacheMutex, nullptr);
    pthread_cond_init(&videoCond, nullptr);
    pthread_mutex_init(&audioMutex, nullptr);
    pthread_mutex_init(&audioCacheMutex, nullptr);
    pthread_cond_init(&audioCond, nullptr);
    pthread_mutex_init(&audioFrameMutex, nullptr);
    pthread_cond_init(&audioFrameCond, nullptr);
    pthread_mutex_init(&videoFrameMutex, nullptr);
    pthread_cond_init(&videoFrameCond, nullptr);
    pthread_mutex_init(&avOpsMutex, nullptr);
    pthread_cond_init(&audioFrameEmptyCond, nullptr);
    pthread_cond_init(&videoFrameEmptyCond, nullptr);
    pthread_mutex_init(&msgMutex, nullptr);
    pthread_cond_init(&msgCond, nullptr);
    pthread_create(&msgLoopThreadId, nullptr, bipMsgLoopThread, this);//开启消息线程
};

BipPlayer::~BipPlayer() {
    release();
};

long BipPlayer::getDuration() const {
    return duration;
}

long BipPlayer::getCurrentPosition() const {
    return (long) (baseClock * 1000);
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
        if (duration <= 0) {
            return;
        }
        lockAll();
        baseClock = (double) (time) / 1000;
        audioClock = baseClock;
        videoClock = baseClock;
        notifyInfo(MEDIA_INFO_BUFFERING_START);
        interruptContext->audioBuffering = true;
        av_seek_frame(avFormatContext, -1, av_rescale(time, AV_TIME_BASE, 1000),
                      AVSEEK_FLAG_BACKWARD);
        if (audioAvailable()) {
            avcodec_flush_buffers(audioCodecContext);
        }
        if (videoAvailable()) {
            avcodec_flush_buffers(avCodecContext);
        }
        clear(videoPacketQueue);
        clear(audioPacketQueue);
        clear(videoFrameQueue);
        clear(audioFrameQueue);
        unLockAll();
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

void *prepareNextVideoThread(void *args) {
    auto bipPlayer = (BipPlayer *) args;
    pthread_setname_np(bipPlayer->prepareNextThreadId, "BipPrepareNext");
    bipPlayer->prepareNext();
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

void *bipMsgLoopThread(void *args) {
    auto bipPlayer = (BipPlayer *) args;
    pthread_setname_np(bipPlayer->msgLoopThreadId, "BipMsgLoop");
    bipPlayer->msgLoop();
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
    AVFormatContext *rootFormatContext = nullptr;
    long decodeSpendTime;
    int64_t startTime;
    int64_t endTime;
    int sendResult;
    int decodeFrameCount = 0;
    AVPacket *lastCachePacket = nullptr;
    std::queue<AVFrame *> tempVideoFrameQueue{};
    while (playState >= STATE_PREPARING) {
        if (pthread_mutex_lock(&videoMutex)) {
            continue;
        }
        if (playState < STATE_PREPARING) {
            pthread_mutex_unlock(&videoMutex);
            break;
        }
        if (videoPacketQueue.empty()) {
            LOGD("video decode wait because packet empty");
            if (pthread_cond_wait(&videoCond, &videoMutex)) {
                continue;
            }
        }
        if (!videoPacketQueue.empty()) {
            AVPacket *packet = nullptr;
            if (lastCachePacket != nullptr) {
                if (rootFormatContext == avFormatContext) {
                    packet = lastCachePacket;
                    lastCachePacket = nullptr;
                } else {
                    av_packet_free(&lastCachePacket);
                    lastCachePacket = nullptr;
                    packet = videoPacketQueue.front();
                    videoPacketQueue.pop();
                }
            } else {
                packet = videoPacketQueue.front();
                videoPacketQueue.pop();
            }
            rootFormatContext = avFormatContext;
            pthread_mutex_unlock(&videoMutex);
            pthread_mutex_lock(&videoCacheMutex);
            if (rootFormatContext == nullptr || rootFormatContext != avFormatContext) {
                av_packet_free(&packet);
                pthread_mutex_unlock(&videoCacheMutex);
                continue;
            }
            timeval decodeStartTime{};
            timeval decodeEndTime{};
            gettimeofday(&decodeStartTime, nullptr);
            AVFrame *frame = av_frame_alloc();
            sendResult = avcodec_send_packet(avCodecContext, packet);
            if (sendResult != 0) {
                lastCachePacket = packet;
            } else {
                av_packet_free(&packet);
            }
            gettimeofday(&decodeEndTime, nullptr);
            startTime = av_gettime();
            while (!avcodec_receive_frame(avCodecContext, frame)) {
                tempVideoFrameQueue.push(frame);
                frame = av_frame_alloc();
            }
            endTime = av_gettime();
            decodeSpendTime = calculateTime(decodeStartTime, decodeEndTime);
            LOGD("decode send %d with time %ld and receive %ld with time %ld", sendResult,
                 decodeSpendTime, tempVideoFrameQueue.size(), (endTime - startTime) / 1000);
            pthread_mutex_unlock(&videoCacheMutex);
            if (!tempVideoFrameQueue.empty()) {
                pthread_mutex_lock(&videoFrameMutex);
                while (!tempVideoFrameQueue.empty()) {
                    videoFrameQueue.push(tempVideoFrameQueue.front());
                    tempVideoFrameQueue.pop();
                }
                interruptContext->videoSize = videoFrameQueue.size();
                if (!audioAvailable()) {
                    if (videoFrameQueue.size() >= min_frame_buff_size) {
                        if (interruptContext->audioBuffering) {
                            interruptContext->audioBuffering = false;
                            notifyInfo(MEDIA_INFO_BUFFERING_END);
                        }
                        pthread_cond_signal(&videoFrameCond);
                    } else {
                        if (interruptContext->audioBuffering) {
                            LOGE("buffering video size %ld", videoFrameQueue.size());
                        }
                    }
                } else {
                    pthread_cond_signal(&videoFrameCond);
                }
                if (videoFrameQueue.size() >= max_frame_buff_size) {
                    LOGD("video decode wait because frame max");
                    if (pthread_cond_wait(&videoFrameEmptyCond, &videoFrameMutex)) {
                        continue;
                    }
                }
                pthread_mutex_unlock(&videoFrameMutex);
            } else {
                av_frame_free(&frame);
            }
        } else {
            pthread_mutex_unlock(&videoMutex);
        }
    }
}

void BipPlayer::cacheAudio() {
    AVFormatContext *rootFormatContext;
    while (playState >= STATE_PREPARING) {
        pthread_mutex_lock(&audioMutex);
        if (playState < STATE_PREPARING) {
            pthread_mutex_unlock(&audioMutex);
            break;
        }
        if (audioPacketQueue.empty()) {
            pthread_cond_wait(&audioCond, &audioMutex);
        }
        if (!audioPacketQueue.empty()) {
            AVPacket *packet = audioPacketQueue.front();
            audioPacketQueue.pop();
            rootFormatContext = avFormatContext;
            pthread_mutex_unlock(&audioMutex);
            pthread_mutex_lock(&audioCacheMutex);
            if (rootFormatContext == nullptr || rootFormatContext != avFormatContext) {
                av_packet_free(&packet);
                pthread_mutex_unlock(&audioCacheMutex);
                continue;
            }
            AVFrame *frame = av_frame_alloc();
            avcodec_send_packet(audioCodecContext, packet);
            int receiveResult = avcodec_receive_frame(audioCodecContext, frame);
            pthread_mutex_unlock(&audioCacheMutex);
            av_packet_free(&packet);
            if (!receiveResult) {
                pthread_mutex_lock(&audioFrameMutex);
                audioFrameQueue.push(frame);
                interruptContext->audioSize = audioFrameQueue.size();
                if (audioFrameQueue.size() >= min_frame_buff_size) {
                    if (interruptContext->audioBuffering) {
                        interruptContext->audioBuffering = false;
                        notifyInfo(MEDIA_INFO_BUFFERING_END);
                        pthread_cond_signal(&audioFrameCond);
                    }
                } else {
                    if (interruptContext->audioBuffering) {
                        LOGE("buffering audio size %ld", audioFrameQueue.size());
                    }
                }
                if (audioFrameQueue.size() >= max_frame_buff_size) {
                    pthread_cond_wait(&audioFrameEmptyCond, &audioFrameMutex);
                }
                pthread_mutex_unlock(&audioFrameMutex);
            } else {
                LOGW("free audio frame %p", &frame);
                av_frame_free(&frame);
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

void BipPlayer::waitAllThreadStop() {
    if (prepareThreadId != 0 && pthread_kill(prepareThreadId, 0) == 0) {
        pthread_join(prepareThreadId, nullptr);
        prepareThreadId = 0;
    }
    LOGE("prepare thread stop");
    if (videoPlayId != 0 && pthread_kill(videoPlayId, 0) == 0) {
        pthread_join(videoPlayId, nullptr);
        videoPlayId = 0;
    }
    LOGE("videoPlay thread stop");
    if (cacheAudioThreadId != 0 && pthread_kill(cacheAudioThreadId, 0) == 0) {
        pthread_join(cacheAudioThreadId, nullptr);
        cacheAudioThreadId = 0;
    }
    LOGE("audioCache thread stop");
    if (cacheVideoThreadId != 0 && pthread_kill(cacheVideoThreadId, 0) == 0) {
        pthread_join(cacheVideoThreadId, nullptr);
        cacheVideoThreadId = 0;
    }
    LOGE("videoCache thread stop");
}

void BipPlayer::prepareNext() {
    //打开输入流
    AVFormatContext *nextAvFormatContext = avformat_alloc_context();
    nextAvFormatContext->interrupt_callback.callback = avFormatInterrupt;
    nextAvFormatContext->interrupt_callback.opaque = nextInterruptContext;
    nextInterruptContext->reset();
    AVDictionary *dic = nullptr;
    av_dict_set(&dic, "bufsize", "655360", 0);
    std::map<const char *, const char *>::iterator iterator;
    iterator = formatOps.begin();
    while (iterator != formatOps.end()) {
        LOGE("prepare next with format option %s:%s", iterator->first, iterator->second);
        av_dict_set(&dic, iterator->first, iterator->second, 0);
        iterator++;
    }
    int prepareResult;
    char *errorMsg = static_cast<char *>(av_mallocz(1024));
    FdAVIOContext *nextFdAvioContext = nullptr;
    if (nextIsDash) {
        LOGE("start dash prepare %s", dashInputPath);
        if (!strncmp(dashInputPath, "fd:", 3)) {
            nextFdAvioContext = new FdAVIOContext();
            nextFdAvioContext->openFromDescriptor(atoi(dashInputPath + 3), "rb");
            nextAvFormatContext->pb = nextFdAvioContext->getAvioContext();
        }
        prepareResult = avformat_open_input(&nextAvFormatContext, dashInputPath, nullptr, &dic);
    } else {
        LOGE("start next prepare %s", nextInputPath);
        if (!strncmp(nextInputPath, "fd:", 3)) {
            nextFdAvioContext = new FdAVIOContext();
            nextFdAvioContext->openFromDescriptor(atoi(nextInputPath + 3), "rb");
            nextAvFormatContext->pb = nextFdAvioContext->getAvioContext();
        }
        prepareResult = avformat_open_input(&nextAvFormatContext, nextInputPath, nullptr, &dic);
    }
    if (prepareResult != 0) {
        postEventFromNative(MEDIA_ERROR, ERROR_PREPARE_FAILED, prepareResult, nullptr);
        av_strerror(prepareResult, errorMsg, 1024);
        LOGE("open next input error %s", errorMsg);
        return;
    }
    prepareResult = avformat_find_stream_info(nextAvFormatContext, nullptr);
    if (prepareResult != 0) {
        postEventFromNative(MEDIA_ERROR, ERROR_PREPARE_FAILED, prepareResult, nullptr);
        av_strerror(prepareResult, errorMsg, 1024);
        LOGE("find next stream info error %s", errorMsg);
        return;
    }

    int nextVideoIndex = -1;
    AVRational nextVideoTimeBase;
    int nextFps = 0;
    int nextAudioIndex = -1;
    AVRational nextAudioTimeBase;
    for (int i = 0; i < nextAvFormatContext->nb_streams; ++i) {
        if (nextAvFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            LOGD("find next video stream with index %d", i);
            nextVideoIndex = i;
            nextVideoTimeBase = nextAvFormatContext->streams[i]->time_base;
        } else if (nextAvFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            LOGD("find next audio stream with index %d", i);
            nextAudioIndex = i;
            nextAudioTimeBase = nextAvFormatContext->streams[i]->time_base;
        }
    }
    AVCodecContext *nextAvCodecContext = nullptr;
    SwsContext *nextSwsContext = nullptr;
    //申请视频的AVPacket和AVFrame
    AVFrame *nextRGBFrame = av_frame_alloc();
    //找得到视频流
    if (nextVideoIndex != -1) {
        AVDictionary *codecDic = nullptr;
        std::map<const char *, const char *>::iterator codecIterator;
        codecIterator = codecOps.begin();
        while (codecIterator != codecOps.end()) {
            LOGE("prepare next with codec option %s:%s", codecIterator->first,
                 codecIterator->second);
            av_dict_set(&codecDic, codecIterator->first, codecIterator->second, 0);
            codecIterator++;
        }
        //打开视频解码器
        AVCodec *avCodec = avcodec_find_decoder(
                nextAvFormatContext->streams[nextVideoIndex]->codecpar->codec_id);
        nextAvCodecContext = avcodec_alloc_context3(avCodec);
        avcodec_parameters_to_context(nextAvCodecContext,
                                      nextAvFormatContext->streams[nextVideoIndex]->codecpar);
        //ffmpeg auto select thread_count
        nextAvCodecContext->thread_count = 0;
        if (avCodec->capabilities | AV_CODEC_CAP_FRAME_THREADS) {
            nextAvCodecContext->thread_type = FF_THREAD_FRAME;
        } else if (avCodec->capabilities | AV_CODEC_CAP_SLICE_THREADS) {
            nextAvCodecContext->thread_type = FF_THREAD_SLICE;
        } else {
            nextAvCodecContext->thread_count = 1;
        }
        prepareResult = avcodec_open2(nextAvCodecContext, avCodec, &codecDic);
        if (prepareResult != 0) {
            LOGE("打开解码失败");
            postEventFromNative(MEDIA_ERROR, ERROR_PREPARE_FAILED, prepareResult, nullptr);
            return;
        }
        if (nextAvCodecContext->pix_fmt == -1 || nextAvCodecContext->width == 0 ||
            nextAvCodecContext->height == 0) {
            LOGE("解码格式出错");
            postEventFromNative(MEDIA_ERROR, ERROR_PREPARE_FAILED, 233, nullptr);
            return;
        }
        auto *out_buffer = (uint8_t *) av_mallocz(
                av_image_get_buffer_size(AV_PIX_FMT_RGBA, nextAvCodecContext->width,
                                         nextAvCodecContext->height,
                                         1));
        av_image_fill_arrays(nextRGBFrame->data, nextRGBFrame->linesize, out_buffer,
                             AV_PIX_FMT_RGBA,
                             nextAvCodecContext->width, nextAvCodecContext->height, 1);

        //转换上下文
        nextSwsContext = sws_getContext(nextAvCodecContext->width,
                                        nextAvCodecContext->height,
                                        nextAvCodecContext->pix_fmt,
                                        nextAvCodecContext->width,
                                        nextAvCodecContext->height, AV_PIX_FMT_RGBA,
                                        SWS_BICUBIC,
                                        nullptr, nullptr, nullptr);
        AVDictionary *swsDic = nullptr;
        std::map<const char *, const char *>::iterator swsIterator;
        swsIterator = swsOps.begin();
        while (swsIterator != swsOps.end()) {
            LOGE("prepare next with sws option %s:%s", swsIterator->first, swsIterator->second);
            av_dict_set(&swsDic, swsIterator->first, swsIterator->second, 0);
            swsIterator++;
        }
        av_opt_set_dict(nextSwsContext, &swsDic);
        nextFps = av_q2d(nextAvFormatContext->streams[nextVideoIndex]->avg_frame_rate);
        if (nextFps <= 0) {
            nextFps = av_q2d(nextAvFormatContext->streams[nextVideoIndex]->r_frame_rate);
        }
    }
    AVCodecContext *nextAudioCodecContext = nullptr;
    SwrContext *nextAudioSwrContext = nullptr;
    //找得到音频流
    if (nextAudioIndex != -1) {
        //打开音频解码器
        AVCodec *audioCodec = avcodec_find_decoder(
                nextAvFormatContext->streams[nextAudioIndex]->codecpar->codec_id);
        nextAudioCodecContext = avcodec_alloc_context3(audioCodec);
        avcodec_parameters_to_context(nextAudioCodecContext,
                                      nextAvFormatContext->streams[nextAudioIndex]->codecpar);
        avcodec_open2(nextAudioCodecContext, audioCodec, nullptr);

        nextAudioSwrContext = swr_alloc();
        swr_alloc_set_opts(nextAudioSwrContext, OUT_CHANNEL_LAYOUT, AV_SAMPLE_FMT_S16,
                           OUT_SAMPLE_RATE,
                           nextAudioCodecContext->channel_layout, nextAudioCodecContext->sample_fmt,
                           nextAudioCodecContext->sample_rate, 0,
                           nullptr);
        swr_init(nextAudioSwrContext);
    }
    //解码
    int readResult;
    if (nextIsDash) {
        double dashClock = baseClock + 1;
        if (duration > (long) dashClock * 1000) {
            if (nextAudioIndex != -1) {
                av_seek_frame(nextAvFormatContext, nextAudioIndex,
                              dashClock / av_q2d(nextAudioTimeBase),
                              AVSEEK_FLAG_BACKWARD);
            }
            if (nextVideoIndex != -1) {
                av_seek_frame(nextAvFormatContext, nextVideoIndex,
                              dashClock / av_q2d(nextVideoTimeBase),
                              AVSEEK_FLAG_BACKWARD);
            }
        }
    }
    int nextBufferPercent = 0;
    long nextDuration = nextAvFormatContext->duration / 1000;//单位毫秒
    while (playState > STATE_ERROR) {
        auto *avPacket = av_packet_alloc();
        readResult = av_read_frame(nextAvFormatContext, avPacket);
        if (readResult >= 0) {
            if (avPacket->stream_index == nextVideoIndex) {
                if (nextAudioIndex == -1) {
                    if (nextDuration > 0) {
                        int percent = round(
                                avPacket->pts * av_q2d(nextVideoTimeBase) * 100000 / nextDuration);
                        if (percent != nextBufferPercent) {
                            nextBufferPercent = percent;
                        }
                    }
                }
                if (nextVideoFrameQueue.size() >= min_frame_buff_size) {
                    if (nextAudioFrameQueue.size() < min_frame_buff_size && nextAudioIndex != -1) {
                        nextVideoPacketQueue.push(avPacket);
                        continue;
                    } else {
                        nextVideoPacketQueue.push(avPacket);
                        break;
                    }
                }
                AVFrame *frame = av_frame_alloc();
                avcodec_send_packet(nextAvCodecContext, avPacket);
                int receiveResult = avcodec_receive_frame(nextAvCodecContext, frame);
                av_packet_free(&avPacket);
                if (!receiveResult) {
                    nextVideoFrameQueue.push(frame);
                } else {
                    av_frame_free(&frame);
                }
            } else if (avPacket->stream_index == nextAudioIndex) {
                if (nextDuration > 0) {
                    int percent = round(
                            avPacket->pts * av_q2d(nextAudioTimeBase) * 100000 / nextDuration);
                    if (percent != nextBufferPercent) {
                        nextBufferPercent = percent;
                    }
                }
                if (nextAudioFrameQueue.size() >= min_frame_buff_size) {
                    if (nextVideoFrameQueue.size() < min_frame_buff_size && nextVideoIndex != -1) {
                        nextAudioPacketQueue.push(avPacket);
                        continue;
                    } else {
                        nextAudioPacketQueue.push(avPacket);
                        break;
                    }
                }
                AVFrame *frame = av_frame_alloc();
                avcodec_send_packet(nextAudioCodecContext, avPacket);
                int receiveResult = avcodec_receive_frame(nextAudioCodecContext, frame);
                av_packet_free(&avPacket);
                if (!receiveResult) {
                    nextAudioFrameQueue.push(frame);
                } else {
                    av_frame_free(&frame);
                }
            }
        } else {
            break;
        }
    }
    if (playState <= STATE_ERROR) {
        //todo 释放上面空间
        return;
    }
    free((void *) inputPath);
    if (nextIsDash) {
        inputPath = dashInputPath;
        dashInputPath = nullptr;
    } else {
        inputPath = nextInputPath;
        nextInputPath = nullptr;
    }
    lockAll();
    //切换播放器
    freeContexts();
    if (rgb_frame != nullptr) {
        av_frame_free(&rgb_frame);
        rgb_frame = nullptr;
    }
    if (nextAudioIndex != -1 && audio_index == -1 && playState == STATE_PLAYING) {
        pthread_create(&audioPlayId, nullptr, playAudioThread, this);//开启begin线程
    }
    avFormatContext = nextAvFormatContext;
    video_index = nextVideoIndex;
    videoTimeBase = nextVideoTimeBase;
    audio_index = nextAudioIndex;
    audioTimeBase = nextAudioTimeBase;
    avCodecContext = nextAvCodecContext;
    audioCodecContext = nextAudioCodecContext;
    audioSwrContext = nextAudioSwrContext;
    rgb_frame = nextRGBFrame;
    swsContext = nextSwsContext;
    duration = nextDuration;
    bufferPercent = nextBufferPercent;
    postEventFromNative(MEDIA_BUFFERING_UPDATE, bufferPercent, 0, nullptr);
    //切换中断上下文
    auto tempInterruptContext = nextInterruptContext;
    nextInterruptContext = interruptContext;
    interruptContext = tempInterruptContext;
    fps = nextFps;

    clear(videoPacketQueue);
    clear(audioPacketQueue);
    clear(videoFrameQueue);
    clear(audioFrameQueue);
    LOGE("change packet queue");
    swap(videoPacketQueue, nextVideoPacketQueue);
    swap(audioPacketQueue, nextAudioPacketQueue);
    swap(videoFrameQueue, nextVideoFrameQueue);
    swap(audioFrameQueue, nextAudioFrameQueue);
    //切换自定义Fd上下文
    fdAvioContext = nextFdAvioContext;
    unLockAll();
    //切换播放器完成
    av_freep(&errorMsg);
    LOGE("completed prepare next");
}

void BipPlayer::msgLoop() {
    while (playState != STATE_RELEASE) {
        pthread_mutex_lock(&msgMutex);
        if (!msgQueue.empty()) {
            BIPMessage *processMsg = msgQueue.front();
            msgQueue.pop();
            pthread_mutex_unlock(&msgMutex);
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
                    start();
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
                case MSG_PREPARE_NEXT:
                    if (processMsg->arg1) {
                        dashInputPath = static_cast<char *>(processMsg->obj);
                    } else {
                        nextInputPath = static_cast<char *>(processMsg->obj);
                    }
                    nextIsDash = processMsg->arg1;
                    pthread_create(&(prepareNextThreadId), nullptr, prepareNextVideoThread,
                                   this);//开启begin线程
                    break;
                case MSG_PREPARE:
                    pthread_create(&(prepareThreadId), nullptr, prepareVideoThread,
                                   this);//开启begin线程
                    break;
                case MSG_SET_DATA_SOURCE:
                    inputPath = static_cast<char *>(processMsg->obj);
                    break;
            }
            LOGE("process msg %x completed", processMsg->what);
            //消息处理完毕 obj的释放自己处理
            delete processMsg;
        } else {
            pthread_cond_wait(&msgCond, &msgMutex);
            pthread_mutex_unlock(&msgMutex);
            continue;
        }
    }
    while (!msgQueue.empty()) {
        BIPMessage *processMsg = msgQueue.front();
        msgQueue.pop();
        if (processMsg->free_l != nullptr) {
            processMsg->free_l(processMsg->obj);
            processMsg->free_l = nullptr;
        }
        delete processMsg;
    }
}

void BipPlayer::notifyMsg(BIPMessage *message) {
    if (playState == STATE_RELEASE) {
        return;
    }
    pthread_mutex_lock(&msgMutex);
    msgQueue.push(message);
    pthread_cond_signal(&msgCond);
    pthread_mutex_unlock(&msgMutex);
}

void BipPlayer::notifyMsg(int what) {
    if (playState == STATE_RELEASE) {
        return;
    }
    auto *message = new BIPMessage();
    message->what = what;
    notifyMsg(message);
}

bool BipPlayer::audioAvailable() {
    return audio_index != -1;
}

bool BipPlayer::videoAvailable() {
    return video_index != -1;
}

void InterruptContext::reset() {
    readStartTime.tv_sec = 0;
    audioBuffering = false;
    audioSize = 0;
    videoSize = 0;
}

int BipPlayer::getFps() const {
    return fps;
}

void BipPlayer::freeContexts() {
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
    if (audioSwrContext) {
        swr_free(&audioSwrContext);
        audioSwrContext = nullptr;
    }
    if (swsContext) {
        sws_freeContext(swsContext);
        swsContext = nullptr;
    }
    if (fdAvioContext != nullptr) {
        delete fdAvioContext;
        fdAvioContext = nullptr;
    }
}

void BipPlayer::destroyOpenSL() {
    //销毁播放器
    if (audioPlayerObject) {
        (*audioPlayerObject)->Destroy(audioPlayerObject);
        audioPlayerObject = nullptr;
        slBufferQueueItf = nullptr;
    }
//销毁混音器
    if (outputMixObject) {
        (*outputMixObject)->Destroy(outputMixObject);
        outputMixObject = nullptr;
    }
//销毁引擎
    if (engineObject) {
        (*engineObject)->Destroy(engineObject);
        engineObject = nullptr;
        engineEngine = nullptr;
    }
}

void BipPlayer::initSoundTouch() {
    soundtouch = new soundtouch::SoundTouch();
    soundtouch->setSampleRate(OUT_SAMPLE_RATE);
    soundtouch->setChannels(OUT_CHANNEL_NUMBER);
    soundtouch->setSetting(SETTING_USE_QUICKSEEK, 1);
}

void BipPlayer::setPlaySpeed(float speed) {
    if (speed <= 0) {
        speed = 0.25;
    } else if (speed >= 4) {
        speed = 4;
    }
    playSpeed = speed;
    soundtouch->setTempo(speed);
}


void yuvToARGB(AVFrame *sourceAVFrame, uint8_t *dst_rgba) {
    switch (sourceAVFrame->format) {
        case AV_PIX_FMT_YUV420P:
            libyuv::I420ToABGR(sourceAVFrame->data[0], sourceAVFrame->linesize[0],
                               sourceAVFrame->data[1], sourceAVFrame->linesize[1],
                               sourceAVFrame->data[2], sourceAVFrame->linesize[2],
                               dst_rgba, sourceAVFrame->width * 4,
                               sourceAVFrame->width,
                               sourceAVFrame->height);
            break;
        case AV_PIX_FMT_YUV422P:
            libyuv::I422ToABGR(sourceAVFrame->data[0], sourceAVFrame->linesize[0],
                               sourceAVFrame->data[1], sourceAVFrame->linesize[1],
                               sourceAVFrame->data[2], sourceAVFrame->linesize[2],
                               dst_rgba, sourceAVFrame->width * 4,
                               sourceAVFrame->width,
                               sourceAVFrame->height);
            break;
        case AV_PIX_FMT_YUV444P:
            libyuv::I444ToABGR(sourceAVFrame->data[0], sourceAVFrame->linesize[0],
                               sourceAVFrame->data[1], sourceAVFrame->linesize[1],
                               sourceAVFrame->data[2], sourceAVFrame->linesize[2],
                               dst_rgba, sourceAVFrame->width * 4,
                               sourceAVFrame->width,
                               sourceAVFrame->height);
            break;
        case AV_PIX_FMT_NV12:
            libyuv::NV12ToABGR(sourceAVFrame->data[0], sourceAVFrame->linesize[0],
                               sourceAVFrame->data[1], sourceAVFrame->linesize[1],
                               dst_rgba, sourceAVFrame->width * 4,
                               sourceAVFrame->width, sourceAVFrame->height);
            break;
        case AV_PIX_FMT_NV21:
            libyuv::NV21ToABGR(sourceAVFrame->data[0], sourceAVFrame->linesize[0],
                               sourceAVFrame->data[1], sourceAVFrame->linesize[1],
                               dst_rgba, sourceAVFrame->width * 4,
                               sourceAVFrame->width, sourceAVFrame->height);
            break;
        default:
            break;
    }
}

bool matchYuv(int yuvFormat) {
    switch (yuvFormat) {
        case AV_PIX_FMT_YUV420P:
        case AV_PIX_FMT_YUV422P:
        case AV_PIX_FMT_YUV444P:
        case AV_PIX_FMT_NV12:
        case AV_PIX_FMT_NV21:
            return true;
        default:
            return false;
    }
}

AVCodec *getMediaCodec(AVCodecID codecID) {
    switch (codecID) {
        case AV_CODEC_ID_H264:
            return avcodec_find_decoder_by_name("h264_mediacodec");
        case AV_CODEC_ID_HEVC:
            return avcodec_find_decoder_by_name("hevc_mediacodec");
        case AV_CODEC_ID_MPEG4:
            return avcodec_find_decoder_by_name("mpeg4_mediacodec");
        default:
            return nullptr;
    }
}


