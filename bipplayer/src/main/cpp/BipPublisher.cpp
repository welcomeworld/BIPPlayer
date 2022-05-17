//
// Created by welcomeworld on 2022/5/9.
//

#include "BipPublisher.h"

jfieldID nativePublisherRefId;

BipPublisher::BipPublisher() {

}

BipPublisher::~BipPublisher() {
    if (inputPath) {
        free((void *) inputPath);
        inputPath = nullptr;
    }
    if (outputPath) {
        free((void *) outputPath);
        outputPath = nullptr;
    }
}

void BipPublisher::publish() {
    int ret;
    char *errorMsg = static_cast<char *>(av_mallocz(1024));
    LOGE("open with %s to %s", inputPath, outputPath);
    ret = prepare_input();
    if (ret != 0) {
        LOGE("prepare input fail");
        return;
    }
    ret = prepare_output();
    if (ret != 0) {
        LOGE("prepare output fail");
        return;
    }
    //解码
    long startTime = av_gettime();
    int retryTime = 0;
    int video_index = 0;
    if (inputContext.avFormatContext != nullptr) {
        for (int i = 0; i < inputContext.nb_streams; i++) {
            if (inputContext.codecpars[i]->codec_type == AVMEDIA_TYPE_VIDEO) {
                video_index = i;
                break;
            }
        }
    }
    while (outputFormat != nullptr && inputContext.avFormatContext != nullptr) {
        auto *avPacket = av_packet_alloc();
        ret = av_read_frame(inputContext.avFormatContext, avPacket);
        if (ret >= 0) {
            if (avPacket->stream_index == video_index) {
                long nowTime = av_gettime() - startTime;
                long pts = avPacket->pts * av_q2d(inputContext.timebase[video_index]) * 1000 * 1000;
                if (pts > nowTime) {
                    av_usleep(pts - nowTime);
                }
            }
            writePacket(avPacket);
        } else if (ret == AVERROR_EOF) {
            writeCompleted();
        } else {
            retryTime++;
            if (retryTime > 3) {
                break;
            }
            av_usleep(50000 * retryTime);
        }
    }
    if (inputContext.avFormatContext != nullptr) {
        avformat_close_input(&inputContext.avFormatContext);
    }
    av_freep(&errorMsg);
}

int BipPublisher::prepare_input() {
    int ret = -1;
    video_pts = 0;
    audio_pts = 0;
    char *errorMsg = static_cast<char *>(av_mallocz(1024));
    if (inputPath != nullptr) {
        //打开输入流
        AVFormatContext *avFormatContext = avformat_alloc_context();
        if (!strncmp(inputPath, "fd:", 3)) {
            FdAVIOContext *fdAvioContext = new FdAVIOContext();
            fdAvioContext->openFromDescriptor(atoi(inputPath + 3), "rb");
            avFormatContext->pb = fdAvioContext->getAvioContext();
        }
        ret = avformat_open_input(&avFormatContext, inputPath, nullptr, nullptr);
        if (ret != 0) {
            av_strerror(ret, errorMsg, 1024);
            LOGE("open input error %s", errorMsg);
            return ret;
        }
        ret = avformat_find_stream_info(avFormatContext, nullptr);
        if (ret != 0) {
            av_strerror(ret, errorMsg, 1024);
            LOGE("find stream info error %s", errorMsg);
            return ret;
        }
        inputContext.avFormatContext = avFormatContext;
        inputContext.nb_streams = avFormatContext->nb_streams;
        for (int i = 0; i < avFormatContext->nb_streams; ++i) {
            inputContext.codecpars[i] = avFormatContext->streams[i]->codecpar;
            inputContext.timebase[i] = avFormatContext->streams[i]->time_base;
        }
    } else {
        inputContext.nb_streams = 2;
        //视频编码器
        AVCodec *videoCodec = avcodec_find_encoder_by_name("libx264");
        if (videoCodec == nullptr) {
            LOGE("can not find videoCodec");
            return ret;
        }
        videoCodecContext = avcodec_alloc_context3(videoCodec);
        videoCodecContext->codec_id = videoCodec->id;
        videoCodecContext->height = height;
        videoCodecContext->width = width;
        videoCodecContext->codec_type = AVMEDIA_TYPE_VIDEO;
        videoCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;
        videoCodecContext->time_base.num = 1;
        videoCodecContext->time_base.den = 1000;
        videoCodecContext->framerate.num = fps;
        videoCodecContext->framerate.den = 1;
        videoCodecContext->gop_size = 240;
        videoCodecContext->max_b_frames = 0;
        videoCodecContext->flags |= AV_CODEC_FLAG2_LOCAL_HEADER;
        AVDictionary *param = 0;
        if (videoCodecContext->codec_id == AV_CODEC_ID_H264) {
            av_dict_set(&param, "preset", "faster", 0);
            av_dict_set(&param, "tune", "zerolatency", 0);
        }
        videoCodecContext->thread_count = 0;
        if (videoCodec->capabilities | AV_CODEC_CAP_FRAME_THREADS) {
            videoCodecContext->thread_type = FF_THREAD_FRAME;
        } else if (videoCodec->capabilities | AV_CODEC_CAP_SLICE_THREADS) {
            videoCodecContext->thread_type = FF_THREAD_SLICE;
        } else {
            videoCodecContext->thread_count = 1;
        }
        ret = avcodec_open2(videoCodecContext, videoCodec, &param);
        if (ret < 0) {
            avcodec_free_context(&videoCodecContext);
            videoCodecContext = nullptr;
            av_strerror(ret, errorMsg, 1024);
            LOGE("open video codec failed %s", errorMsg);
            return ret;
        } else {
            LOGE("find videoCodec %s threadType: %d threadCount: %d", videoCodec->name,
                 videoCodecContext->thread_type, videoCodecContext->thread_count);
        }
        inputContext.avCodec[0] = videoCodec;
        inputContext.timebase[0] = videoCodecContext->time_base;
//        AVCodecParameters *codecpar = static_cast<AVCodecParameters *>(av_malloc(
//                sizeof(AVCodecParameters)));
//        avcodec_parameters_from_context(codecpar, videoCodecContext);
//        inputContext.codecpars[0] = codecpar;

        //音频编码器
        AVCodec *audioCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (audioCodec == nullptr) {
            LOGE("can not find audioCodec");
            return ret;
        } else {
            LOGE("find audioCodec %s", audioCodec->name);
        }
        audioCodecContext = avcodec_alloc_context3(audioCodec);
        LOGE("alloc audioCodecContext success");
        audioCodecContext->codec_id = audioCodec->id;
        audioCodecContext->codec_type = AVMEDIA_TYPE_AUDIO;
        audioCodecContext->sample_rate = 44100;
        audioCodecContext->channel_layout = AV_CH_LAYOUT_STEREO;
        audioCodecContext->sample_fmt = AV_SAMPLE_FMT_FLTP;
        audioCodecContext->channels = 2;
        audioCodecContext->bit_rate = 128000;
        audioCodecContext->time_base.num = 1;
        audioCodecContext->time_base.den = 1000;
        LOGE("start open audioCodec %s", audioCodec->name);
        ret = avcodec_open2(audioCodecContext, audioCodec, nullptr);
        if (ret < 0) {
            avcodec_free_context(&audioCodecContext);
            audioCodecContext = nullptr;
            av_strerror(ret, errorMsg, 1024);
            LOGE("open audio codec failed %s", errorMsg);
            return ret;
        } else {
            LOGE("open audioCodec %s success", audioCodec->name);
        }
        inputContext.avCodec[1] = audioCodec;
        inputContext.timebase[1] = audioCodecContext->time_base;
//        AVCodecParameters *audioCodecpar = static_cast<AVCodecParameters *>(av_malloc(
//                sizeof(AVCodecParameters)));
//        avcodec_parameters_from_context(audioCodecpar, audioCodecContext);
//        inputContext.codecpars[1] = audioCodecpar;
        audioSwrContext = swr_alloc();
        swr_alloc_set_opts(audioSwrContext, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_FLTP,
                           44100,
                           AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, 44100, 0,
                           nullptr);
        swr_init(audioSwrContext);
    }
    LOGE("prepare input success");
    av_freep(&errorMsg);
    return 0;
}

int BipPublisher::prepare_output() {
    int ret;
    ret = avformat_alloc_output_context2(&outputFormat, NULL, "flv", outputPath); //RTMP
    if (ret < 0) {
        outputFormat = nullptr;
        LOGE("Could not create output context");
        return ret;
    }
    ret = avio_open2(&outputFormat->pb, outputPath, AVIO_FLAG_WRITE, nullptr, nullptr);
    if (ret < 0) {
        avformat_free_context(outputFormat);
        outputFormat = nullptr;
        LOGE("Could not avio_open2:%s", outputPath);
        return ret;
    }
    for (int i = 0; i < inputContext.nb_streams; i++) {
        AVStream *stream = avformat_new_stream(outputFormat,
                                               inputContext.avCodec[i]);
        if (inputPath == nullptr && videoCodecContext != nullptr) {
            if (i == 0) {
                ret = avcodec_parameters_from_context(stream->codecpar, videoCodecContext);
            } else {
                ret = avcodec_parameters_from_context(stream->codecpar, audioCodecContext);
            }
        } else {
            ret = avcodec_parameters_copy(stream->codecpar, inputContext.codecpars[i]);
        }
        if (ret < 0) {
            avformat_free_context(outputFormat);
            outputFormat = nullptr;
            LOGE("copy codec context failed");
            return ret;
        }
    }
    //写入文件头
    ret = avformat_write_header(outputFormat, nullptr);
    if (ret < 0) {
        avformat_free_context(outputFormat);
        outputFormat = nullptr;
        LOGE("Could not write_header");
        return ret;
    }
    LOGE("prepare output success");
    return 0;
}

void BipPublisher::writePacket(AVPacket *avPacket) {
    if (outputFormat == nullptr) {
        return;
    }
    av_packet_rescale_ts(avPacket, inputContext.timebase[avPacket->stream_index],
                         outputFormat->streams[avPacket->stream_index]->time_base);
    int ret = av_interleaved_write_frame(outputFormat, avPacket);
    LOGD("write result %d", ret);
}

void BipPublisher::writeCompleted() {
    if (outputFormat == nullptr) {
        return;
    }
    av_write_trailer(outputFormat);
    if (outputFormat != nullptr) {
        avio_close(outputFormat->pb);
        avformat_free_context(outputFormat);
        outputFormat = nullptr;
    }
    LOGE("publish completed");
}

void BipPublisher::writeImage(uint8_t *data) {
    char *errorMsg = static_cast<char *>(av_mallocz(1024));
    AVFrame *frame = av_frame_alloc();
    av_image_fill_arrays(frame->data, frame->linesize, data, AV_PIX_FMT_YUV420P, width,
                         height, 1);
    //默认 timebase是1000
    frame->pts = video_pts * 1000 / fps;
    video_pts++;
    frame->data[0] = data;
    frame->data[1] = data + (width * height);
    frame->data[2] = data + (width * height * 5 / 4);
    long changeStartTime = av_gettime();
    int ret = avcodec_send_frame(videoCodecContext, frame);
    long changeEndTime = av_gettime();
    LOGE("encode send spend %ld", (changeEndTime - changeStartTime) / 1000);
    if (ret != 0) {
        av_strerror(ret, errorMsg, 1024);
        LOGD("send fail %s", errorMsg);
    }
    auto *avPacket = av_packet_alloc();
    changeStartTime = av_gettime();
    ret = avcodec_receive_packet(videoCodecContext, avPacket);
    changeEndTime = av_gettime();
    LOGE("encode receive spend %ld", (changeEndTime - changeStartTime) / 1000);
    if (ret == 0) {
        changeStartTime = av_gettime();
        av_packet_rescale_ts(avPacket, videoCodecContext->time_base,
                             outputFormat->streams[0]->time_base);
        ret = av_interleaved_write_frame(outputFormat, avPacket);
        changeEndTime = av_gettime();
        LOGE("encode write spend %ld", (changeEndTime - changeStartTime) / 1000);
        if (ret != 0) {
            av_strerror(ret, errorMsg, 1024);
            LOGE("video write fail %s", errorMsg);
        }
    } else {
        av_strerror(ret, errorMsg, 1024);
        LOGE("video receive fail %s", errorMsg);
    }
    av_freep(&errorMsg);
}

int getSamples(int size) {
    return size / av_get_bytes_per_sample(AV_SAMPLE_FMT_S16) / 2;
}

void BipPublisher::writeAudio(uint8_t *data, int size) {
    char *errorMsg = static_cast<char *>(av_mallocz(1024));
    AVFrame *frame = av_frame_alloc();
    int ret;
    //默认 timebase是1000
    frame->pts = audio_pts * 1000 / (44100 / audioCodecContext->frame_size);
    audio_pts++;
    frame->format = AV_SAMPLE_FMT_FLTP;
    frame->channel_layout = AV_CH_LAYOUT_STEREO;
    frame->sample_rate = 44100;
    frame->channels = 2;
    int rawSample = getSamples(size);
    frame->nb_samples = audioCodecContext->frame_size;
    int testsize = av_samples_get_buffer_size(NULL, 2, audioCodecContext->frame_size,
                                              AV_SAMPLE_FMT_FLTP, 1);
    uint8_t *out_buffer = (uint8_t *) av_malloc(testsize);
    ret = avcodec_fill_audio_frame(frame, 2, AV_SAMPLE_FMT_FLTP, out_buffer, testsize, 1);
    if (ret < 0) {
        av_strerror(ret, errorMsg, 1024);
        LOGE("fill fail %s", errorMsg);
    }
    uint8_t **datas = (uint8_t **) av_calloc(2, sizeof(*datas));
    av_samples_alloc(datas, NULL, 2, rawSample, AV_SAMPLE_FMT_S16, 1);
    memcpy(datas[0], data, size);
    swr_convert(audioSwrContext, frame->data, audioCodecContext->frame_size,
                (const uint8_t **) (datas), rawSample);
    ret = avcodec_send_frame(audioCodecContext, frame);
    if (ret != 0) {
        av_strerror(ret, errorMsg, 1024);
        LOGE("send fail %s", errorMsg);
    }
    auto *avPacket = av_packet_alloc();
    ret = avcodec_receive_packet(audioCodecContext, avPacket);
    if (ret == 0) {
        avPacket->stream_index = 1;
        ret = av_interleaved_write_frame(outputFormat, avPacket);
        if (ret != 0) {
            av_strerror(ret, errorMsg, 1024);
            LOGE("audio write fail %s", errorMsg);
        }
    } else {
        av_strerror(ret, errorMsg, 1024);
        LOGE("audio receive fail %s", errorMsg);
    }
    av_freep(&errorMsg);
}

void *publishThread(void *args) {
    auto bipPublisher = (BipPublisher *) args;
    pthread_setname_np(bipPublisher->publishThreadId, "BipPublish");
    bipPublisher->publish();
    pthread_exit(nullptr);//退出线程
}


