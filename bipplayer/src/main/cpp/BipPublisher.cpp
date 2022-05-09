//
// Created by welcomeworld on 2022/5/9.
//

#include "BipPublisher.h"

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
    //打开输入流
    AVFormatContext *avFormatContext = avformat_alloc_context();
    AVDictionary *dic = nullptr;
//    std::map<const char *, const char *>::iterator iterator;
//    iterator = formatOps.begin();
//    while (iterator != formatOps.end()) {
//        LOGE("prepare with format option %s:%s", iterator->first, iterator->second);
//        av_dict_set(&dic, iterator->first, iterator->second, 0);
//        iterator++;
//    }
    int prepareResult;
    char *errorMsg = static_cast<char *>(av_mallocz(1024));
    if (!strncmp(inputPath, "fd:", 3)) {
        FdAVIOContext *fdAvioContext = new FdAVIOContext();
        fdAvioContext->openFromDescriptor(atoi(inputPath + 3), "rb");
        avFormatContext->pb = fdAvioContext->getAvioContext();
    }
    prepareResult = avformat_open_input(&avFormatContext, inputPath, nullptr, &dic);
    if (prepareResult != 0) {
        av_strerror(prepareResult, errorMsg, 1024);
        LOGE("open input error %s", errorMsg);
        return;
    }
    prepareResult = avformat_find_stream_info(avFormatContext, nullptr);
    if (prepareResult != 0) {
        av_strerror(prepareResult, errorMsg, 1024);
        LOGE("find stream info error %s", errorMsg);
        return;
    }
    int video_index = -1;
    AVRational videoTimeBase;
    //找到视频流
    for (int i = 0; i < avFormatContext->nb_streams; ++i) {
        if (avFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            LOGE("find video stream with index %d", i);
            video_index = i;
            videoTimeBase = avFormatContext->streams[i]->time_base;
        }
    }
    //解码
    int readResult;
    long startTime = av_gettime();
    AVFormatContext *outputContext = prepare_out(avFormatContext, outputPath);
    int retryTime = 0;
    while (true && outputContext != nullptr) {
        auto *avPacket = av_packet_alloc();
        readResult = av_read_frame(avFormatContext, avPacket);
        if (readResult >= 0) {
            if (avPacket->stream_index == video_index) {
                long nowTime = av_gettime() - startTime;
                long pts = avPacket->pts * av_q2d(videoTimeBase) * 1000 * 1000;
                if (pts > nowTime) {
                    av_usleep(pts - nowTime);
                }
            }
            writeOut(outputContext, avFormatContext, avPacket);
        } else if (readResult == AVERROR_EOF) {
            writeCompleted(outputContext);
        } else {
            retryTime++;
            if (retryTime > 3) {
                break;
            }
            av_usleep(50000 * retryTime);
        }
    }
    avformat_close_input(&avFormatContext);
    if (outputContext != nullptr) {
        avio_close(outputContext->pb);
        avformat_free_context(outputContext);
    }
    av_freep(&errorMsg);

}

AVFormatContext *BipPublisher::prepare_out(AVFormatContext *avFormatContext, const char *output) {
    LOGE("prepare output:%s", output);
    int ret;
    AVFormatContext *outputFormat = nullptr;
    ret = avformat_alloc_output_context2(&outputFormat, NULL, "flv", output); //RTMP
    if (ret < 0) {
        outputFormat = nullptr;
        LOGE("Could not create output context");
        return outputFormat;
    }
    ret = avio_open2(&outputFormat->pb, output, AVIO_FLAG_WRITE, nullptr, nullptr);
    if (ret < 0) {
        avformat_free_context(outputFormat);
        outputFormat = nullptr;
        LOGE("Could not avio_open2:%s", output);
        return outputFormat;
    }
    for (int i = 0; i < avFormatContext->nb_streams; i++) {
        AVCodec *avCodec = avcodec_find_decoder(
                avFormatContext->streams[i]->codecpar->codec_id);
        AVStream *stream = avformat_new_stream(outputFormat,
                                               avCodec);
        ret = avcodec_parameters_copy(stream->codecpar, avFormatContext->streams[i]->codecpar);
        if (ret < 0) {
            avformat_free_context(outputFormat);
            outputFormat = nullptr;
            LOGE("copy codec context failed");
            return outputFormat;
        }
    }
    ret = avformat_write_header(outputFormat, nullptr);
    if (ret < 0) {
        avformat_free_context(outputFormat);
        outputFormat = nullptr;
        LOGE("Could not write_header");
        return outputFormat;
    }
    return outputFormat;
}

void BipPublisher::writeOut(AVFormatContext *outputContext, AVFormatContext *avFormatContext,
                            AVPacket *avPacket) {
    if (outputContext == nullptr) {
        return;
    }
    av_packet_rescale_ts(avPacket, avFormatContext->streams[avPacket->stream_index]->time_base,
                         outputContext->streams[avPacket->stream_index]->time_base);
    int ret = av_interleaved_write_frame(outputContext, avPacket);
    LOGD("write result %d", ret);
}

void BipPublisher::writeCompleted(AVFormatContext *outputContext) {
    if (outputContext == nullptr) {
        return;
    }
    av_write_trailer(outputContext);
    LOGE("publish completed");
}

void *publishThread(void *args) {
    auto bipPublisher = (BipPublisher *) args;
    pthread_setname_np(bipPublisher->publishThreadId, "BipPublish");
    bipPublisher->publish();
    pthread_exit(nullptr);//退出线程
}


