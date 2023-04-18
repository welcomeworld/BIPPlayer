//
// Created by welcomeworld on 3/7/23.
//
#include "BipAudioTracker.h"

void BipAudioTracker::createEngine() {
    slCreateEngine(&engineObject, 0, nullptr, 0, nullptr, nullptr);
    (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);//实现engineObject接口对象
    (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE,
                                  &engineEngine);//通过引擎调用接口初始化SLEngineItf
}

void BipAudioTracker::createMixVolume() {
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

void BipAudioTracker::createPlayer() {
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

void BipAudioTracker::initSoundTouch() {
    soundtouch = new soundtouch::SoundTouch();
    soundtouch->setSampleRate(OUT_SAMPLE_RATE);
    soundtouch->setChannels(OUT_CHANNEL_NUMBER);
    soundtouch->setSetting(SETTING_USE_QUICKSEEK, 1);
}

void BipAudioTracker::bufferQueueCallback(SLAndroidSimpleBufferQueueItf caller, void *pContext) {
    auto *audioTracker = (BipAudioTracker *) pContext;
    if (audioTracker != nullptr) {
        audioTracker->innerBufferQueueCallback();
    }
}

void BipAudioTracker::innerBufferQueueCallback() {
    int bufferSize = getPcm();
    if (bufferSize != 0) {
        //将得到的数据加入到队列中
        (*slBufferQueueItf)->Enqueue(slBufferQueueItf, audioBuffer, bufferSize);
    }
}

int BipAudioTracker::getPcm() {
    while (isPlaying()) {
        AVFrame *audioFrame = bipFrameQueue->pop();
        if (audioFrame == nullptr) {
            if ((isCacheCompleted && bipPacketQueue->size() == 0) ||
                static_cast<long>(shareClock->clock * 1000) > duration - 500) {
                if (clockMaintain && trackerCallback != nullptr) {
                    trackerCallback->notifyCompleted();
                }
                break;
            } else {
                if (trackerCallback != nullptr && isPlaying()) {
                    trackerCallback->requestBuffering();
                }
                break;
            }
        }
        int outSample = swr_convert(audioSwrContext, &audioBuffer, 4096 * 2,
                                    (const uint8_t **) (audioFrame->data),
                                    audioFrame->nb_samples);
        int size = 0;
        soundtouch->putSamples(reinterpret_cast<const soundtouch::SAMPLETYPE *>(audioBuffer),
                               outSample);
        int soundOutSample = 0;
        do {
            soundOutSample = static_cast<int>(soundtouch->receiveSamples(
                    reinterpret_cast<soundtouch::SAMPLETYPE *>(audioBuffer + size),
                    OUT_SAMPLE_RATE / OUT_CHANNEL_NUMBER));
            size += soundOutSample * OUT_CHANNEL_NUMBER * BYTES_PER_SAMPLE;
        } while (soundOutSample != 0);
        if (size == 0) {
            av_frame_free(&audioFrame);
            continue;
        }
        if (audioFrame->pts != AV_NOPTS_VALUE) {
            //这一帧的起始时间
            trackerClock = static_cast<double>(audioFrame->pts) * trackTimeBase;
            //这一帧数据的时间
            double time =
                    size / ((double) OUT_SAMPLE_RATE * OUT_CHANNEL_NUMBER * BYTES_PER_SAMPLE);
            //最终音频时钟
            trackerClock = time + trackerClock;
            if (clockMaintain && shareClock != nullptr) {
                shareClock->clock = trackerClock;
            }
        }
        av_frame_free(&audioFrame);
        return size;
    }
    return 0;
}

BipAudioTracker::BipAudioTracker(AVCodecParameters *codecPar) {
    //打开音频解码器
    AVCodec *audioCodec = avcodec_find_decoder(codecPar->codec_id);
    audioCodecContext = avcodec_alloc_context3(audioCodec);
    avcodec_parameters_to_context(audioCodecContext,
                                  codecPar);
    avcodec_open2(audioCodecContext, audioCodec, nullptr);
    //打开音频转换器
    audioSwrContext = swr_alloc();
    swr_alloc_set_opts(audioSwrContext,
                       OUT_CHANNEL_LAYOUT,
                       AV_SAMPLE_FMT_S16,
                       OUT_SAMPLE_RATE,
                       static_cast<int64_t>(audioCodecContext->channel_layout),
                       audioCodecContext->sample_fmt,
                       audioCodecContext->sample_rate, 0,
                       nullptr);
    swr_init(audioSwrContext);
    createEngine();
    createMixVolume();
    createPlayer();
    initSoundTouch();
    bipFrameQueue = new BipFrameQueue(maxFrameBufSize, true);
    bipPacketQueue = new BipPacketQueue(maxPacketBufSize);
    audioBuffer = static_cast<uint8_t *>(av_mallocz(1024 * 48));
    trackerState = STATE_CREATED;
}

BipAudioTracker::~BipAudioTracker() {
    stop();
    trackerState = STATE_DESTROY;
    destroyOpenSL();
    delete soundtouch;
    delete bipFrameQueue;
    delete bipPacketQueue;
    shareClock = nullptr;
    av_freep(&audioBuffer);
    trackerCallback = nullptr;
    if (audioSwrContext) {
        swr_free(&audioSwrContext);
        audioSwrContext = nullptr;
    }
    if (audioCodecContext != nullptr) {
        avcodec_free_context(&audioCodecContext);
        audioCodecContext = nullptr;
    }
}

void BipAudioTracker::destroyOpenSL() {
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

void BipAudioTracker::playAudio() {
    (*slPlayItf)->SetPlayState(slPlayItf, SL_PLAYSTATE_PLAYING);
    innerBufferQueueCallback();
}

void BipAudioTracker::setPlaySpeed(float speed) {
    if (speed <= 0) {
        speed = 0.25;
    } else if (speed >= 4) {
        speed = 4;
    }
    soundtouch->setTempo(speed);
}

void BipAudioTracker::clearCache() {
    avcodec_flush_buffers(audioCodecContext);
    bipFrameQueue->clear();
    bipPacketQueue->clear();
}

unsigned long BipAudioTracker::getFrameSize() {
    return bipFrameQueue->size();
}

void BipAudioTracker::pause() {
    trackerState = STATE_PAUSE;
    (*slPlayItf)->SetPlayState(slPlayItf, SL_PLAYSTATE_PAUSED);
}

void BipAudioTracker::stop() {
    trackerState = STATE_STOP;
    (*slPlayItf)->SetPlayState(slPlayItf, SL_PLAYSTATE_STOPPED);
    trackerClock = 0;
    bipPacketQueue->notifyAll();
    bipFrameQueue->notifyAll();
    if (decodeThreadId != 0 && pthread_kill(decodeThreadId, 0) == 0) {
        pthread_join(decodeThreadId, nullptr);
        decodeThreadId = 0;
    }
    clearCache();
}

void BipAudioTracker::decodeInner() {
    trackerState = STATE_START;
    while (isStart()) {
        AVPacket *packet = bipPacketQueue->pop(true);
        if (packet == nullptr) {
            continue;
        }
        AVFrame *frame = av_frame_alloc();
        avcodec_send_packet(audioCodecContext, packet);
        av_packet_free(&packet);
        int receiveResult = avcodec_receive_frame(audioCodecContext, frame);
        if (!receiveResult) {
            bipFrameQueue->push(frame);
        } else {
            LOGW("free audio frame %p", &frame);
            av_frame_free(&frame);
        }
    }
}

void BipAudioTracker::startDecodeThread() {
    pthread_create(&decodeThreadId, nullptr, decodeThread, this);
}

void *BipAudioTracker::decodeThread(void *args) {
    auto audioTracker = (BipAudioTracker *) args;
    pthread_setname_np(audioTracker->decodeThreadId, "BipCacheAudio");
    audioTracker->decodeInner();
    pthread_exit(nullptr);//退出线程
}

void BipAudioTracker::play() {
    trackerState = STATE_PLAYING;
    pthread_create(&playThreadId, nullptr, playThread, this);//开启begin线程
}

void *BipAudioTracker::playThread(void *args) {
    auto tracker = (BipAudioTracker *) args;
    pthread_setname_np(tracker->playThreadId, "BipPlayAudio");
    tracker->playAudio();
    pthread_exit(nullptr);//退出线程
}
