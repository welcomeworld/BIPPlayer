//
// Created by welcomeworld on 2021/9/5.
//
#include "bipPlayer.h"

jclass defaultBIPPlayerClass;
jmethodID postEventFromNativeMethodId;
jfieldID nativePlayerRefId;
JavaVM *staticVm;

void BipPlayer::createEngine() {
    slCreateEngine(&engineObject, 0, nullptr, 0, nullptr, nullptr);
    (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);//实现engineObject接口对象
    (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE,
                                  &engineEngine);//通过引擎调用接口初始化SLEngineItf
}

void BipPlayer::release() {
    reset();
    ANativeWindow_release(nativeWindow);
    av_frame_free(&rgb_frame);
    avcodec_free_context(&avCodecContext);
    avformat_free_context(avFormatContext);
    pthread_cond_destroy(&videoCond);
    pthread_cond_destroy(&audioCond);
    pthread_mutex_destroy(&videoMutex);
    pthread_mutex_destroy(&audioMutex);
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
        pthread_mutex_lock(&audioMutex);
        if (playState != STATE_PLAYING) {
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
            avcodec_send_packet(audioCodecContext, packet);
            int receiveResult = avcodec_receive_frame(audioCodecContext, audioFrame);
            if (!receiveResult) {
                swr_convert(audioSwrContext, &audioBuffer, 44100 * 2,
                            (const uint8_t **) (audioFrame->data), audioFrame->nb_samples);
                int size = av_samples_get_buffer_size(nullptr, outChannelsNumber,
                                                      audioFrame->nb_samples,
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
                av_packet_unref(packet);
                return size;
            }
            av_frame_free(&audioFrame);
            av_packet_unref(packet);
        } else {
            pthread_mutex_unlock(&audioMutex);
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
    , decodeStartTime //每一帧解码开始时间
    , frame_time_stamp = av_q2d(videoTimeBase); //时间戳的实际时间单位
    while (playState == STATE_PLAYING) {
        pthread_mutex_lock(&videoMutex);
        if (playState != STATE_PLAYING) {
            pthread_mutex_unlock(&videoMutex);
            break;
        }
        if (videoPacketQueue.empty()) {
            if (FFABS(audioClock * 1000 - duration) < 500) {
                playState = STATE_COMPLETED;
                postEventFromNative(MEDIA_PLAYBACK_COMPLETE, 0, 0, nullptr);
            } else {
                pthread_cond_wait(&videoCond, &videoMutex);
            }
        }
        if (!videoPacketQueue.empty()) {
            AVPacket *packet = videoPacketQueue.front();
            videoPacketQueue.pop();
            pthread_mutex_unlock(&videoMutex);
            decodeStartTime = av_gettime() / 1000000.0;
            avcodec_send_packet(avCodecContext, packet);
            AVFrame *frame = av_frame_alloc();
            if (!avcodec_receive_frame(avCodecContext, frame)) {
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
                //减去解码消耗时间
                delay = delay + (decodeStartTime - av_gettime() / 1000000.0);
                if (delay < 0) {
                    delay = 0;
                }
                last_play = play;
                //上锁
                if (ANativeWindow_lock(nativeWindow, &nativeWindowBuffer, nullptr)) {
                    if (delay > 0.001) {
                        av_usleep(delay * 1000000);
                    }
                    av_frame_free(&frame);
                    av_packet_unref(packet);
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
                    av_usleep(delay * 1000000);
                }
            } else {
                if (FFABS(audioClock * 1000 - duration) < 500) {
                    playState = STATE_COMPLETED;
                    postEventFromNative(MEDIA_PLAYBACK_COMPLETE, 0, 0, nullptr);
                }
                av_usleep(1000);
            }
            av_frame_free(&frame);
            av_packet_unref(packet);
        } else {
            pthread_mutex_unlock(&videoMutex);
        }
    }
    postEventFromNative(MEDIA_PLAY_STATE_CHANGE, 0, 0, nullptr);
}

void BipPlayer::setVideoSurface(ANativeWindow *window) {
    //申请ANativeWindow
    if (nativeWindow != nullptr) {
        ANativeWindow_release(nativeWindow);
    }
    nativeWindow = window;
    if (avCodecContext != nullptr) {
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
            staticVm->AttachCurrentThread(&env, nullptr);
        } else {
            return;
        }
    }
    env->CallStaticVoidMethod(defaultBIPPlayerClass, postEventFromNativeMethodId,
                              (jobject) weakJavaThis, what, arg1, arg2, (jobject) object);
    if (result == JNI_EDETACHED) {
        staticVm->DetachCurrentThread();
    }
}

void BipPlayer::stop() {
    if (playState <= STATE_ERROR) {
        postEventFromNative(MEDIA_ERROR, ERROR_NOT_PREPARED, 0, nullptr);
        return;
    }
    pthread_mutex_lock(&videoMutex);
    pthread_mutex_lock(&audioMutex);
    playState = STATE_UN_DEFINE;
    (*slPlayItf)->SetPlayState(slPlayItf, SL_PLAYSTATE_STOPPED);
    pthread_cond_signal(&audioCond);
    pthread_mutex_unlock(&audioMutex);
    pthread_cond_signal(&videoCond);
    pthread_mutex_unlock(&videoMutex);
    if (pthread_kill(prepareThreadId, 0) == 0) {
        pthread_join(prepareThreadId, nullptr);
    }
    if (pthread_kill(videoPlayId, 0) == 0) {
        pthread_join(videoPlayId, nullptr);
    }
    avcodec_free_context(&avCodecContext);
    avcodec_free_context(&audioCodecContext);
    avformat_free_context(avFormatContext);
    clear(audioPacketQueue);
    clear(videoPacketQueue);
    videoClock = 0;
    audioClock = 0;
}

void BipPlayer::reset() {
    pthread_mutex_lock(&videoMutex);
    pthread_mutex_lock(&audioMutex);
    playState = STATE_UN_DEFINE;
    (*slPlayItf)->SetPlayState(slPlayItf, SL_PLAYSTATE_STOPPED);
    pthread_cond_signal(&audioCond);
    pthread_mutex_unlock(&audioMutex);
    pthread_cond_signal(&videoCond);
    pthread_mutex_unlock(&videoMutex);
    if (pthread_kill(prepareThreadId, 0) == 0) {
        pthread_join(prepareThreadId, nullptr);
    }
    if (pthread_kill(videoPlayId, 0) == 0) {
        pthread_join(videoPlayId, nullptr);
    }
    avcodec_free_context(&avCodecContext);
    avcodec_free_context(&audioCodecContext);
    avformat_free_context(avFormatContext);
    clear(audioPacketQueue);
    clear(videoPacketQueue);
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
//    av_dict_set(&dic,"stimeout","2000000",0);
    avformat_open_input(&avFormatContext, inputPath, nullptr, &dic);
    avformat_find_stream_info(avFormatContext, nullptr);
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
    //打开视频解码器
    AVCodec *avCodec = avcodec_find_decoder(
            avFormatContext->streams[video_index]->codecpar->codec_id);
    avCodecContext = avcodec_alloc_context3(avCodec);
    avcodec_parameters_to_context(avCodecContext, avFormatContext->streams[video_index]->codecpar);
    if (avcodec_open2(avCodecContext, avCodec, nullptr) < 0) {
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
    audioBuffer = static_cast<uint8_t *>(av_mallocz(44100 * 2));
    swr_alloc_set_opts(audioSwrContext, outChLayout, AV_SAMPLE_FMT_S16,
                       audioCodecContext->sample_rate,
                       audioCodecContext->channel_layout, audioCodecContext->sample_fmt,
                       audioCodecContext->sample_rate, 0,
                       nullptr);
    swr_init(audioSwrContext);
    outChannelsNumber = av_get_channel_layout_nb_channels(outChLayout);





    //申请视频的AVPacket和AVFrame
    auto packet = static_cast<AVPacket *>(av_malloc(sizeof(AVPacket)));
    av_init_packet(packet);
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


    //解码
    int readResult;
    int videoCache = 0;
    int audioCache = 0;
    playState = STATE_BUFFERING;
    while (playState > STATE_ERROR) {
        readResult = av_read_frame(avFormatContext, packet);
        if (readResult >= 0) {
            if (packet->stream_index == video_index) {
                auto *videoPacket = (AVPacket *) av_mallocz(sizeof(AVPacket));
                if (av_packet_ref(videoPacket, packet)) {
                    return;
                }
                pthread_mutex_lock(&videoMutex);
                videoPacketQueue.push(videoPacket);
                pthread_cond_signal(&videoCond);
                pthread_mutex_unlock(&videoMutex);
                if (videoCache != -1) {
                    videoCache++;
                }
                if (videoCache > 100 && audioCache > 100) {
                    videoCache = -1;
                    audioCache = -1;
                    playState = STATE_PREPARED;
                    postEventFromNative(MEDIA_PREPARED, 0, 0, nullptr);
                }
            } else if (packet->stream_index == audio_index) {
                auto *audioPacket = (AVPacket *) av_mallocz(sizeof(AVPacket));
                if (av_packet_ref(audioPacket, packet)) {
                    return;
                }
                pthread_mutex_lock(&audioMutex);
                audioPacketQueue.push(audioPacket);
                pthread_cond_signal(&audioCond);
                pthread_mutex_unlock(&audioMutex);
                if (audioCache != -1) {
                    audioCache++;
                }
                if (videoCache > 100 && audioCache > 100) {
                    videoCache = -1;
                    audioCache = -1;
                    playState = STATE_PREPARED;
                    postEventFromNative(MEDIA_PREPARED, 0, 0, nullptr);
                }
            }
        } else {
            av_usleep(5000);
        }
        av_packet_unref(packet);
    }
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
};

BipPlayer::~BipPlayer() {
    release();
};

long BipPlayer::getDuration() const {
    return duration;
}

long BipPlayer::getCurrentPosition() const {
    return audioClock * 1000;
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
    std::queue<AVPacket *> empty;
    std::swap(empty, q);
}

void BipPlayer::seekTo(long time) {
    if (playState >= STATE_PREPARED) {
        pthread_mutex_lock(&videoMutex);
        pthread_mutex_lock(&audioMutex);
        av_seek_frame(avFormatContext, audio_index, time / av_q2d(audioTimeBase) / 1000,
                      AVSEEK_FLAG_BACKWARD);
        av_seek_frame(avFormatContext, video_index, time / av_q2d(videoTimeBase) / 1000,
                      AVSEEK_FLAG_BACKWARD);
        clear(videoPacketQueue);
        clear(audioPacketQueue);
        audioClock = time / 1000;
        videoClock = time / 1000;
        pthread_mutex_unlock(&audioMutex);
        pthread_mutex_unlock(&videoMutex);
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
    bipPlayer->prepare();
    pthread_exit(nullptr);//退出线程
}

void *playAudioThread(void *args) {
    auto bipPlayer = (BipPlayer *) args;
    bipPlayer->playAudio();
    pthread_exit(nullptr);//退出线程
}

void *playVideoThread(void *args) {
    auto bipPlayer = (BipPlayer *) args;
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
    } else {
        postEventFromNative(MEDIA_ERROR, ERROR_NOT_PREPARED, 0, nullptr);
    }
}

