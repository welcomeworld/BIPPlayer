#include <jni.h>
#include <string>
#include "native-lib.h"
#include <android/native_window_jni.h>


extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
#include <unistd.h>
}

ANativeWindow *nativeWindow;

void setVideoSurface(JNIEnv *env, jobject instance, jobject surface) {
    //申请ANativeWindow
    if (nativeWindow) {
        ANativeWindow_release(nativeWindow);
        nativeWindow = nullptr;
    }
    nativeWindow = ANativeWindow_fromSurface(env, surface);
}

jint native_play(JNIEnv *env, jobject instance, jstring inputPath_) {
    //打开输入流
    AVFormatContext *avFormatContext = avformat_alloc_context();
    const char *inputPath = env->GetStringUTFChars(inputPath_, nullptr);
    avformat_open_input(&avFormatContext, inputPath, nullptr, nullptr);
    avformat_find_stream_info(avFormatContext, nullptr);
    //找到视频流
    int video_index = -1;
    for (int i = 0; i < avFormatContext->nb_streams; ++i) {
        if (avFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_index = i;
        }
    }
    //打开解码器
    AVCodec *avCodec = avcodec_find_decoder(
            avFormatContext->streams[video_index]->codecpar->codec_id);
    AVCodecContext *avCodecContext = avcodec_alloc_context3(avCodec);
    avcodec_parameters_to_context(avCodecContext, avFormatContext->streams[video_index]->codecpar);
    if (avcodec_open2(avCodecContext, avCodec, nullptr) < 0) {
        return -1;
    }
    //申请AVPacket和AVFrame
    auto packet = static_cast<AVPacket *>(av_malloc(sizeof(AVPacket)));
    av_init_packet(packet);
    AVFrame *frame = av_frame_alloc();
    AVFrame *rgb_frame = av_frame_alloc();
    auto *out_buffer = (uint8_t *) av_malloc(
            av_image_get_buffer_size(AV_PIX_FMT_RGBA, avCodecContext->width, avCodecContext->height,
                                     1));
    av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, out_buffer, AV_PIX_FMT_RGBA,
                         avCodecContext->width, avCodecContext->height, 1);

    //转换上下文
    SwsContext *swsContext = sws_getContext(avCodecContext->width, avCodecContext->height,
                                            avCodecContext->pix_fmt, avCodecContext->width,
                                            avCodecContext->height, AV_PIX_FMT_RGBA, SWS_BICUBIC,
                                            nullptr, nullptr, nullptr);

    //视频缓冲区
    ANativeWindow_Buffer nativeWindowBuffer;
    //解码
    while (av_read_frame(avFormatContext, packet) >= 0) {
        if (packet->stream_index == video_index) {
            avcodec_send_packet(avCodecContext, packet);
            if (!avcodec_receive_frame(avCodecContext, frame) && (nativeWindow != nullptr)) {
                //配置nativeWindow
                ANativeWindow_setBuffersGeometry(nativeWindow, avCodecContext->width,
                                                 avCodecContext->height, WINDOW_FORMAT_RGBA_8888);
                //上锁
                if(ANativeWindow_lock(nativeWindow, &nativeWindowBuffer, nullptr)){
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

            }
        }
        av_packet_unref(packet);
    }
    ANativeWindow_release(nativeWindow);
    av_frame_free(&frame);
    av_frame_free(&rgb_frame);
    avcodec_close(avCodecContext);
    avformat_free_context(avFormatContext);
    env->ReleaseStringUTFChars(inputPath_, inputPath);
    return 0;
}

static JNINativeMethod methods[] = {
        {"play",             "(Ljava/lang/String;)I",     (void *) native_play},
        {"_setVideoSurface", "(Landroid/view/Surface;)V", (void *) setVideoSurface}
};

void init() {
}

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