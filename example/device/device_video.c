#include "device_video.h"
// #define OUTPUT_FILE "output.h264"
#define FRAME_RATE 30
static video_callback_t video_cb = NULL;
static void *user = NULL;
static int run_flag = 1;
void setVideoCallback(video_callback_t cb, void *arg){
    video_cb = cb;
    user = arg;
}
int startVideoDeviceLoop(){
    avdevice_register_all();

    AVFormatContext *fmt_ctx = NULL;
    AVDictionary *options = NULL;
    char str[20];
    snprintf(str, sizeof(str), "%d", FRAME_RATE);
    av_dict_set(&options, "framerate", str, 0);
	// av_dict_set(&options, "video_size", "640x480", 0);

    AVInputFormat *input_fmt = av_find_input_format(VIDEO_INPUT_FORMAT);
    if(!input_fmt){
        printf("Cannot find input format: %s\n", VIDEO_INPUT_FORMAT);
        return -1;
    }

    if(avformat_open_input(&fmt_ctx, VIDEO_INPUT_DEVICE, input_fmt, &options) != 0){
        printf("Failed to open video device: %s\n", VIDEO_INPUT_DEVICE);
        return -1;
    }

    if(avformat_find_stream_info(fmt_ctx, NULL) < 0){
        printf("Failed to get stream info\n");
        return -1;
    }
    int video_stream_index = -1;
    AVCodecParameters *codec_par = NULL;
    for(int i = 0; i < fmt_ctx->nb_streams; i++){
        if(fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO){
            video_stream_index = i;
            codec_par = fmt_ctx->streams[i]->codecpar;
            break;
        }
    }
    if(video_stream_index == -1){
        printf("Failed to find video stream\n");
        return -1;
    }

    int width = codec_par->width;
    int height = codec_par->height;
    enum AVPixelFormat src_pix_fmt = codec_par->format;
    AVRational frame_rate = fmt_ctx->streams[video_stream_index]->r_frame_rate;
    printf("Camera resolution: %dx%d, FPS: %d/%d, Format: %s\n", width, height, frame_rate.num, frame_rate.den, av_get_pix_fmt_name(src_pix_fmt));

    AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if(!codec){
        printf("H264 encoder not found\n");
        return -1;
    }

    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    codec_ctx->width = width;
    codec_ctx->height = height;
    codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    codec_ctx->time_base = (AVRational){frame_rate.den, frame_rate.num};
    codec_ctx->framerate = frame_rate;
    codec_ctx->bit_rate = 4000000;	
    codec_ctx->gop_size = 2 * FRAME_RATE;
    codec_ctx->thread_count = 1;
    codec_ctx->slices = 1;
    codec_ctx->flags |= AV_CODEC_FLAG2_LOCAL_HEADER;
    codec_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    AVDictionary *param = 0;
    av_opt_set(codec_ctx->priv_data, "preset", "ultrafast", 0);
    av_opt_set(codec_ctx->priv_data, "tune", "zerolatency", 0);
	av_opt_set(codec_ctx->priv_data, "profile", "baseline", 0);
    if(avcodec_open2(codec_ctx, codec, NULL) < 0){
        printf("Failed to open encoder\n");
        return -1;
    }

    struct SwsContext *sws_ctx = NULL;
    if(src_pix_fmt != AV_PIX_FMT_YUV420P){
        sws_ctx = sws_getContext(width, height, src_pix_fmt,
                                 width, height, AV_PIX_FMT_YUV420P,
                                 SWS_BICUBIC, NULL, NULL, NULL);
        if(!sws_ctx){
            printf("Failed to create SWS context\n");
            return -1;
        }
    }

    // FILE *output = fopen(OUTPUT_FILE, "wb");
    // if(!output){
    //     printf("Failed to open output file\n");
    //     return -1;
    // }

    AVFrame *frame = av_frame_alloc();
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = width;
    frame->height = height;
    av_frame_get_buffer(frame, 1);

    AVPacket *pkt = av_packet_alloc();
	AVPacket *capture_pkt = av_packet_alloc();
    int frame_count = 0;
    while(run_flag == 1){
        if (av_read_frame(fmt_ctx, capture_pkt) >= 0) {
            uint8_t *data[4] = {capture_pkt->data, NULL, NULL, NULL};
            int linesize[4] = {width * 2, 0, 0, 0};

            if(src_pix_fmt == AV_PIX_FMT_YUYV422){
                linesize[0] = width * 2;
            }
            else if(src_pix_fmt == AV_PIX_FMT_YUV420P){
                linesize[0] = width;
                linesize[1] = width / 2;
                linesize[2] = width / 2;
            }
            else if (src_pix_fmt == AV_PIX_FMT_NV12){
                linesize[0] = width;
                linesize[1] = width;
            }
            else{
                printf("Unsupported pixel format: %d\n", src_pix_fmt);
                return -1;
            }

            if(sws_ctx){
                sws_scale(sws_ctx, (const uint8_t * const*)data, linesize, 0, height,
                          frame->data, frame->linesize);
            }
            else{
                memcpy(frame->data[0], capture_pkt->data, width * height); // Y
                memcpy(frame->data[1], capture_pkt->data + width * height, width * height / 4); // U
                memcpy(frame->data[2], capture_pkt->data + width * height * 5 / 4, width * height / 4); // V
            }

            frame->pts = frame_count;
            avcodec_send_frame(codec_ctx, frame);
            while(avcodec_receive_packet(codec_ctx, pkt) == 0){
                // fwrite(pkt->data, 1, pkt->size, output);
                if(video_cb){
                    video_cb(pkt->data, pkt->size, user);
                }
                av_packet_unref(pkt);
            }

            av_packet_unref(capture_pkt);
            frame_count++;
        }
    }
	avcodec_send_frame(codec_ctx, NULL);
    while(avcodec_receive_packet(codec_ctx, pkt) == 0){
		// fwrite(pkt->data, 1, pkt->size, output);
        if(video_cb){
            video_cb(pkt->data, pkt->size, user);
        }
		av_packet_unref(pkt);
	}
	av_packet_free(&pkt);
	av_packet_free(&capture_pkt);
    // fclose(output);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    av_frame_free(&frame);
    if(sws_ctx){
		sws_freeContext(sws_ctx);
	}
    return 0;
}
void stopVideoDevice(){
    run_flag = 0;
}