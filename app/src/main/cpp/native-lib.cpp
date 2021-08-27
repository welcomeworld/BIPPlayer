#include "native-lib.h"

ANativeWindow *nativeWindow;
//音频解码上下文
AVCodecContext *audioCodecContext;
//音频重采样上下文
SwrContext *audioSwrContext;
//音频重采样缓冲区
uint8_t *audioBuffer;
//音频重采样声道
uint64_t outChLayout = AV_CH_LAYOUT_STEREO;
//音频重采样通道数
int outChannelsNumber = 2;
//播放状态
int playState = 0;
pthread_t audioPlayId;//音频处理线程id
pthread_t videoPlayId;//视频处理线程id
AVFrame *rgb_frame;
AVCodecContext *avCodecContext;
SwsContext *swsContext;
//音频Packet队列
std::queue<AVPacket *> audioPacketQueue;
std::queue<AVPacket *> videoPacketQueue;
SLObjectItf engineObject;//用SLObjectItf声明引擎接口对象
SLEngineItf engineEngine;//声明具体的引擎对象
SLObjectItf outputMixObject;//用SLObjectItf创建混音器接口对象
SLEnvironmentalReverbItf outputMixEnvironmentalReverb;////具体的混音器对象实例
SLEnvironmentalReverbSettings settings = SL_I3DL2_ENVIRONMENT_PRESET_DEFAULT;//默认情况
SLObjectItf audioPlayerObject;//用SLObjectItf声明播放器接口对象
SLPlayItf slPlayItf;//播放器接口
SLAndroidSimpleBufferQueueItf slBufferQueueItf;//缓冲区队列接口

void *showVideoPacket(void *args) {
    //视频缓冲区
    ANativeWindow_Buffer nativeWindowBuffer;
    while (!playState) {
        if (!videoPacketQueue.empty()) {
            AVPacket *packet = videoPacketQueue.front();
            videoPacketQueue.pop();
            avcodec_send_packet(avCodecContext, packet);
            AVFrame *frame = av_frame_alloc();
            if (!avcodec_receive_frame(avCodecContext, frame) && (nativeWindow != nullptr)) {
                //配置nativeWindow
                ANativeWindow_setBuffersGeometry(nativeWindow, avCodecContext->width,
                                                 avCodecContext->height, WINDOW_FORMAT_RGBA_8888);
                //上锁
                if (ANativeWindow_lock(nativeWindow, &nativeWindowBuffer, nullptr)) {
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
                usleep(1000 * 16);
            } else {
                usleep(100);
            }
            av_frame_free(&frame);
            av_packet_unref(packet);
        }
    }
    pthread_exit(nullptr);//退出线程
}

int getPcm() {
    while (!playState) {
        if (!audioPacketQueue.empty()) {
//            LOGE("获取音频队列数据");
            AVFrame *audioFrame = av_frame_alloc();
            AVPacket *packet = audioPacketQueue.front();
            audioPacketQueue.pop();
            avcodec_send_packet(audioCodecContext, packet);
            int receiveResult = avcodec_receive_frame(audioCodecContext, audioFrame);
            if (!receiveResult) {
                swr_convert(audioSwrContext, &audioBuffer, 44100 * 2,
                            (const uint8_t **) (audioFrame->data), audioFrame->nb_samples);
                int size = av_samples_get_buffer_size(nullptr, outChannelsNumber,
                                                      audioFrame->nb_samples,
                                                      AV_SAMPLE_FMT_S16, 1);
                LOGE("获取音频队列数据 %d", size);
                av_frame_free(&audioFrame);
                av_packet_unref(packet);
                return size;
            }
            av_frame_free(&audioFrame);
            av_packet_unref(packet);
        } else {
            usleep(100);
        }
    }
    return 0;
}

//创建引擎
void createEngine() {
    slCreateEngine(&engineObject, 0, nullptr, 0, nullptr, nullptr);
    (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);//实现engineObject接口对象
    (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE,
                                  &engineEngine);//通过引擎调用接口初始化SLEngineItf
}

//创建混音器
void createMixVolume() {
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

//创建播放器
void createPlayer() {
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
        LOGE("创建播放器失败 %d", playerResult);
        return;
    }
    (*audioPlayerObject)->Realize(audioPlayerObject, SL_BOOLEAN_FALSE);
    (*audioPlayerObject)->GetInterface(audioPlayerObject, SL_IID_PLAY, &slPlayItf);
    (*audioPlayerObject)->GetInterface(audioPlayerObject, SL_IID_BUFFERQUEUE, &slBufferQueueItf);
    (*slBufferQueueItf)->RegisterCallback(slBufferQueueItf, bufferQueueCallback, nullptr);
}

void *playAudio(void *args) {
    (*slPlayItf)->SetPlayState(slPlayItf, SL_PLAYSTATE_PLAYING);
    bufferQueueCallback(slBufferQueueItf, nullptr);
    pthread_exit(nullptr);//退出线程
}

void bufferQueueCallback(
        SLAndroidSimpleBufferQueueItf caller,
        void *pContext
) {
    int bufferSize = getPcm();
    if (bufferSize != 0) {
        //将得到的数据加入到队列中
        (*slBufferQueueItf)->Enqueue(slBufferQueueItf, audioBuffer, bufferSize);
    }
}

jint native_play(JNIEnv *env, jobject instance, jstring inputPath_) {
    //打开输入流
    AVFormatContext *avFormatContext = avformat_alloc_context();
    const char *inputPath = env->GetStringUTFChars(inputPath_, nullptr);
    avformat_open_input(&avFormatContext, inputPath, nullptr, nullptr);
    avformat_find_stream_info(avFormatContext, nullptr);
    //找到视频流
    int video_index = -1;
    int audio_index = -1;
    for (int i = 0; i < avFormatContext->nb_streams; ++i) {
        if (avFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_index = i;
        } else if (avFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_index = i;
        }
    }
    //打开视频解码器
    AVCodec *avCodec = avcodec_find_decoder(
            avFormatContext->streams[video_index]->codecpar->codec_id);
    avCodecContext = avcodec_alloc_context3(avCodec);
    avcodec_parameters_to_context(avCodecContext, avFormatContext->streams[video_index]->codecpar);
    if (avcodec_open2(avCodecContext, avCodec, nullptr) < 0) {
        return -1;
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
    while (av_read_frame(avFormatContext, packet) >= 0) {
        if (packet->stream_index == video_index) {
            auto *videoPacket = (AVPacket *) av_mallocz(sizeof(AVPacket));
            //克隆
            if (av_packet_ref(videoPacket, packet)) {
                //克隆失败
                return 0;
            }
            LOGE("获得数据视频index %d", packet->stream_index);
            videoPacketQueue.push(videoPacket);
        } else if (packet->stream_index == audio_index) {
            auto *audioPacket = (AVPacket *) av_mallocz(sizeof(AVPacket));
            //克隆
            if (av_packet_ref(audioPacket, packet)) {
                //克隆失败
                return 0;
            }
            LOGE("获得数据音频index %d", packet->stream_index);
            audioPacketQueue.push(audioPacket);
        } else {
            LOGE("获得数据index %d", packet->stream_index);
        }
        av_packet_unref(packet);
    }
    pthread_create(&audioPlayId, nullptr, playAudio, nullptr);//开启begin线程
    pthread_create(&videoPlayId, nullptr, showVideoPacket, nullptr);//开启begin线程
    while (true) {
        if (audioPacketQueue.empty() && videoPacketQueue.empty()) {
            break;
        }
    }
    playState = 1;

    ANativeWindow_release(nativeWindow);
    av_frame_free(&rgb_frame);
    avcodec_close(avCodecContext);
    avformat_free_context(avFormatContext);
    env->ReleaseStringUTFChars(inputPath_, inputPath);
    LOGE("播放完成");
    return 0;
}

void setVideoSurface(JNIEnv *env, jobject instance, jobject surface) {
    //申请ANativeWindow
    if (nativeWindow) {
        ANativeWindow_release(nativeWindow);
        nativeWindow = nullptr;
    }
    nativeWindow = ANativeWindow_fromSurface(env, surface);
}

void init(JNIEnv *env, jobject instance) {
    createEngine();
    createMixVolume();
    createPlayer();
    LOGE("播放器初始化");
}

static JNINativeMethod methods[] = {
        {"play",             "(Ljava/lang/String;)I",     (void *) native_play},
        {"_setVideoSurface", "(Landroid/view/Surface;)V", (void *) setVideoSurface},
        {"initPlayer",       "()V",                       (void *) init}
};

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env = nullptr;
    jclass cls;
    if ((*vm).GetEnv((void **) &env, JNI_VERSION_1_6) != JNI_OK) {
        printf("onLoad error!!");
        return JNI_ERR;
    }
    cls = (*env).FindClass("com/github/welcomeworld/bipplayer/DefaultBIPPlayer");
    (*env).RegisterNatives(cls, methods, sizeof(methods) / sizeof(JNINativeMethod));
    return JNI_VERSION_1_6;
}