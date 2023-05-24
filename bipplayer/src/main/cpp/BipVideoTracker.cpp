//
// Created by welcomeworld on 3/7/23.
//
#include "BipVideoTracker.h"

void BipVideoTracker::clearCache() {
    lock();
    avcodec_flush_buffers(avCodecContext);
    unlock();
    isCacheCompleted = false;
    bipFrameQueue->clear();
    bipPacketQueue->clear();
}

void BipVideoTracker::showVideoPacket() {
    if (clockMaintain && trackerCallback != nullptr) {
        trackerCallback->reportPlayStateChange(true);
    }
    double delay         //一帧占用的时间
    , diff   //音频帧与视频帧相差时间
    , sync_threshold; //音视频差距界限
    long showFrameStartTime = av_gettime();  //展示一帧开始的时间
    long showFrameEndTime; //一帧成功展示的时间
    long realSleepTime; //展示一帧后当前帧剩余时间,单位微秒
    //上次计算fps的时间节点,单位毫秒，大于九百毫秒计算一次
    long realFpsMarkTime = av_gettime();
    int realShowFrame = 0;
    while (isPlaying()) {
        AVFrame *frame = bipFrameQueue->pop();
        if (frame == nullptr) {
            if ((isCacheCompleted && bipPacketQueue->size() == 0) ||
                static_cast<long>(shareClock->clock * 1000) > duration - 500) {
                if (clockMaintain && trackerCallback != nullptr) {
                    trackerCallback->notifyCompleted();
                }
            } else {
                if (trackerCallback != nullptr && isPlaying()) {
                    trackerCallback->requestBuffering();
                }
            }
            break;
        }
        double realHopeFps = fps * playSpeed;
        delay = 1.0 / realHopeFps;
        if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
            trackerClock = static_cast<double>(frame->best_effort_timestamp) * trackTimeBase;
        } else if (frame->pts != AV_NOPTS_VALUE) {
            trackerClock = static_cast<double>(frame->pts) * trackTimeBase;
        } else {
            trackerClock += 1.0 / fps;
        }
        if (clockMaintain) {
            shareClock->clock = trackerClock;
        }
        diff = trackerClock - shareClock->clock;
        //视频过于落后，不再转换格式，直接丢帧
        if ((diff < -0.35 || (diff < -0.1 && realHopeFps > 80)) &&
            bipFrameQueue->size() > 0) {
            LOGD("video slow to skip a frame with diff %lf when fps %lf", diff, fps);
            av_frame_free(&frame);
            continue;
        } else {
            //音视频差距合理界限，超出则调整，没有则认为同步，不需要调整
            sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
            if (fabs(diff) > sync_threshold) {
                if (diff > 0.4) {
                    //视频太过超前，慢慢等待音频追赶，不直接卡死
                    delay = delay + sync_threshold;
                } else {
                    delay = delay + diff;
                }
            }
        }
        //转换成RGBA格式
        if (matchYuv(frame->format)) {
            yuvToARGB(frame, rgb_frame->data[0]);
        } else {
            sws_scale(swsContext, frame->data, frame->linesize, 0, frame->height,
                      rgb_frame->data, rgb_frame->linesize);
        }
        rgb_frame->width = frame->width;
        rgb_frame->height = frame->height;
        showRGBFrame();
        showFrameEndTime = av_gettime();
        //计算FPS
        long fpsTime = (showFrameEndTime - realFpsMarkTime) / 1000;
        if (fpsTime > 980) {
            int realFps = ++realShowFrame * 1000 / (int) fpsTime;
            if (trackerCallback != nullptr) {
                trackerCallback->reportFps(realFps);
            }
            realFpsMarkTime = showFrameEndTime;
            realShowFrame = 0;
        } else {
            realShowFrame++;
        }
        //计算一帧剩余时间决定是否休眠
        long decodeSpendTime = showFrameEndTime - showFrameStartTime;
        realSleepTime = (long) (delay * 1000000) - decodeSpendTime;
        if (realSleepTime > 1000) {
            av_usleep(realSleepTime);
        }
        showFrameStartTime = av_gettime();
        av_frame_free(&frame);
    }
    if (isPlaying()) {
        trackerState = STATE_PAUSE;
    }
    if (clockMaintain && trackerCallback != nullptr) {
        trackerCallback->reportPlayStateChange(false);
    }
}

void BipVideoTracker::showRGBFrame() const {
    if (rgb_frame == nullptr) {
        return;
    }
    bipNativeWindow->disPlay(rgb_frame->data[0], rgb_frame->linesize[0], rgb_frame->width,
                             rgb_frame->height);
}

void BipVideoTracker::setVideoSurface(ANativeWindow *nativeWindow) {
    bipNativeWindow->setWindow(nativeWindow);
    showRGBFrame();
}

unsigned long BipVideoTracker::getFrameSize() {
    return bipFrameQueue->size();
}

void BipVideoTracker::pause() {
    if (isPlaying()) {
        trackerState = STATE_PAUSE;
        if (playThreadId != 0 && pthread_kill(playThreadId, 0) == 0) {
            pthread_join(playThreadId, nullptr);
            playThreadId = 0;
        }
    }
}

void BipVideoTracker::stop() {
    pause();
    if (isStart()) {
        trackerState = STATE_STOP;
        bipPacketQueue->notifyAll();
        bipFrameQueue->notifyAll();
        if (decodeThreadId != 0 && pthread_kill(decodeThreadId, 0) == 0) {
            pthread_join(decodeThreadId, nullptr);
            decodeThreadId = 0;
        }
        clearCache();
    }
}

BipVideoTracker::BipVideoTracker(BipNativeWindow *nativeWindow, AVCodecParameters *codecPar,
                                 AVDictionary *codecDic, AVDictionary *swsDic, bool mediacodec) {
    bipNativeWindow = nativeWindow;
    bipFrameQueue = new BipFrameQueue(false);
    bipPacketQueue = new BipPacketQueue();
    rgb_frame = av_frame_alloc();

    //打开视频解码器
    AVCodec *avCodec = nullptr;
    AVCodecID videoCodecID = codecPar->codec_id;
    if (mediacodec) {
        avCodec = getMediaCodec(videoCodecID);
    }
    if (avCodec == nullptr) {
        avCodec = avcodec_find_decoder(videoCodecID);
    }
    if (avCodec == nullptr) {
        trackerState = STATE_ERROR;
        return;
    }
    avCodecContext = avcodec_alloc_context3(avCodec);
    avcodec_parameters_to_context(avCodecContext, codecPar);
    //ffmpeg auto select thread_count
    avCodecContext->thread_count = 0;
    if (avCodec->capabilities | AV_CODEC_CAP_FRAME_THREADS) {
        avCodecContext->thread_type = FF_THREAD_FRAME;
    } else if (avCodec->capabilities | AV_CODEC_CAP_SLICE_THREADS) {
        avCodecContext->thread_type = FF_THREAD_SLICE;
    } else {
        avCodecContext->thread_count = 1;
    }

    int prepareResult = avcodec_open2(avCodecContext, avCodec, &codecDic);
    if (prepareResult != 0) {
        trackerState = STATE_ERROR;
        return;
    } else {
        LOGD("解码成功:%s threadType: %d threadCount: %d pix_format %d",
             avCodec->name,
             avCodecContext->thread_type, avCodecContext->thread_count,
             avCodecContext->pix_fmt);
    }
    if (avCodecContext->pix_fmt == -1 || avCodecContext->width == 0 ||
        avCodecContext->height == 0) {
        LOGE("解码格式出错");
        trackerState = STATE_ERROR;
        return;
    }
    auto *out_buffer = (uint8_t *) av_mallocz(
            av_image_get_buffer_size(AV_PIX_FMT_RGBA, avCodecContext->width,
                                     avCodecContext->height,
                                     1));
    av_image_fill_arrays(rgb_frame->data,
                         rgb_frame->linesize,
                         out_buffer, AV_PIX_FMT_RGBA,
                         avCodecContext->width, avCodecContext->height, 1);

    LOGE("play get swsContext from %d : %d : %d", avCodecContext->pix_fmt,
         avCodecContext->codec_id, avCodecContext->coded_height);
    //转换上下文
    swsContext = sws_getContext(avCodecContext->width, avCodecContext->height,
                                avCodecContext->pix_fmt, avCodecContext->width,
                                avCodecContext->height, AV_PIX_FMT_RGBA,
                                SWS_BICUBIC,
                                nullptr, nullptr, nullptr);
    LOGE("get swsContext success from %d", avCodecContext->pix_fmt);
    av_opt_set_dict(swsContext, &swsDic);
    trackerState = STATE_CREATED;
}

BipVideoTracker::~BipVideoTracker() {
    stop();
    trackerState = STATE_DESTROY;
    if (avCodecContext != nullptr) {
        avcodec_free_context(&avCodecContext);
        avCodecContext = nullptr;
    }
    if (swsContext) {
        sws_freeContext(swsContext);
        swsContext = nullptr;
    }
    delete bipFrameQueue;
    delete bipPacketQueue;
    trackerCallback = nullptr;
    if (rgb_frame != nullptr) {
        av_frame_free(&rgb_frame);
        rgb_frame = nullptr;
    }
}

int BipVideoTracker::getVideoWidth() const {
    if (avCodecContext != nullptr) {
        return avCodecContext->width;
    }
    return 0;
}

int BipVideoTracker::getVideoHeight() const {
    if (avCodecContext != nullptr) {
        return avCodecContext->height;
    }
    return 0;
}

void BipVideoTracker::decodeInner() {
    trackerState = STATE_START;
    while (isStart()) {
        AVPacket *packet = bipPacketQueue->pop(true);
        if (packet == nullptr) {
            continue;
        }
        lock();
        avcodec_send_packet(avCodecContext, packet);
        unlock();
        av_packet_free(&packet);
        AVFrame *frame = av_frame_alloc();
        while (true) {
            lock();
            int result = avcodec_receive_frame(avCodecContext, frame);
            unlock();
            if (!result) {
                double decodeTime = static_cast<double>(frame->pts) * trackTimeBase;
                bool discard = frame->pts != AV_NOPTS_VALUE &&
                               decodeTime < shareClock->clock;
                if (discard) {
                    continue;
                }
                bipFrameQueue->push(frame);
                frame = av_frame_alloc();
            } else {
                break;
            }
        }
        av_frame_free(&frame);
    }
}

void BipVideoTracker::startDecodeThread() {
    pthread_create(&decodeThreadId, nullptr, decodeThread, this);
}

void *BipVideoTracker::decodeThread(void *args) {
    auto videoTracker = (BipVideoTracker *) args;
    pthread_setname_np(videoTracker->decodeThreadId, "BipCacheVideo");
    videoTracker->decodeInner();
    pthread_exit(nullptr);//退出线程
}

void BipVideoTracker::play() {
    if (isStart() && !isPlaying()) {
        trackerState = STATE_PLAYING;
        pthread_create(&playThreadId, nullptr, playVideoThread, this);
    }
}

void BipVideoTracker::start() {
    trackerState = STATE_START;
}

void *BipVideoTracker::playVideoThread(void *args) {
    auto videoTracker = (BipVideoTracker *) args;
    pthread_setname_np(videoTracker->playThreadId, "BipPlayVideo");
    LOGW("BipPlayVideo thread start");
    videoTracker->showVideoPacket();
    LOGW("BipPlayVideo thread stop");
    pthread_exit(nullptr);//退出线程
}

void BipVideoTracker::setPlaySpeed(float speed) {
    if (speed <= 0) {
        speed = 0.25;
    } else if (speed >= 4) {
        speed = 4;
    }
    playSpeed = speed;
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

AVCodec *BipVideoTracker::getMediaCodec(AVCodecID codecID) {
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
