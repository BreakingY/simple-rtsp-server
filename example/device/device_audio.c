#include "device_audio.h"


// #define OUTPUT_FILE "output.aac"
// #define OUTPUT_FILE_PCM "output.pcm"
static audio_callback_t audio_cb = NULL;
static void *user = NULL;
static int run_flag = 1;
static void add_adts_header(uint8_t *header, int data_length){
    int profile = 1;  // AAC LC
    int freq_idx = 4; // 44100 Hz
    int chan_cfg = CHANNELS;

    int full_frame_length = data_length + 7;

    header[0] = 0xFF;
    header[1] = 0xF1;
    header[2] = (profile << 6) + (freq_idx << 2) + (chan_cfg >> 2);
    header[3] = ((chan_cfg & 3) << 6) + (full_frame_length >> 11);
    header[4] = (full_frame_length >> 3) & 0xFF;
    header[5] = ((full_frame_length & 7) << 5) + 0x1F;
    header[6] = 0xFC;
}
void setAudioCallback(audio_callback_t cb, void *arg){
    audio_cb = cb;
    user = arg;
}
int startAudioDeviceLoop(){
    avdevice_register_all();

    AVFormatContext *fmt_ctx = NULL;
    AVDictionary *options = NULL;
    av_dict_set(&options, "sample_rate", "44100", 0);
    av_dict_set(&options, "channels", "2", 0);
    av_dict_set(&options, "channel_layout", "stereo", 0);

    AVInputFormat *input_fmt = av_find_input_format(AUDIO_INPUT_FORMAT);
    if(!input_fmt){
        printf("Cannot find input format: %s\n", AUDIO_INPUT_FORMAT);
        return -1;
    }

    if(avformat_open_input(&fmt_ctx, AUDIO_INPUT_DEVICE, input_fmt, &options) != 0){
        printf("Failed to open input device: %s\n", AUDIO_INPUT_DEVICE);
        return -1;
    }

    if(avformat_find_stream_info(fmt_ctx, NULL) < 0){
        printf("Failed to get stream info\n");
        return -1;
    }

    int audio_stream_index = -1;
    AVCodecParameters *codec_par = NULL;
    for(int i = 0; i < fmt_ctx->nb_streams; i++){
        if(fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO){
            audio_stream_index = i;
            codec_par = fmt_ctx->streams[i]->codecpar;
            break;
        }
    }
    if(audio_stream_index == -1){
        printf("Failed to find audio stream\n");
        return -1;
    }

    AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if(!codec){
        printf("AAC encoder not found\n");
        return -1;
    }

    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    codec_ctx->sample_rate = SAMPLE_RATE;
    codec_ctx->channels = CHANNELS;
    codec_ctx->channel_layout = av_get_default_channel_layout(CHANNELS);
    codec_ctx->sample_fmt = SAMPLE_FMT;
    codec_ctx->bit_rate = 64000;
    codec_ctx->time_base = (AVRational){1, SAMPLE_RATE};
    codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    codec_ctx->profile = FF_PROFILE_AAC_LOW;

    if(avcodec_open2(codec_ctx, codec, NULL) < 0){
        printf("Failed to open AAC encoder\n");
        return -1;
    }


    struct SwrContext *swr_ctx = swr_alloc();
    int64_t input_channel_layout = av_get_default_channel_layout(codec_par->channels);
    enum AVSampleFormat input_sample_fmt = codec_par->format;
    int input_sample_rate = codec_par->sample_rate;

    printf("Input Channels: %d, Sample Rate: %d, Sample Format: %s\n",codec_par->channels, input_sample_rate, av_get_sample_fmt_name(input_sample_fmt));

    swr_alloc_set_opts(swr_ctx,
                       av_get_default_channel_layout(CHANNELS),
                       SAMPLE_FMT,
                       SAMPLE_RATE,
                       input_channel_layout,
                       input_sample_fmt,
                       input_sample_rate,
                       0, NULL);

    if(swr_init(swr_ctx) < 0){
        printf("Failed to initialize SwrContext\n");
        return -1;
    }

    // FILE *output = fopen(OUTPUT_FILE, "wb");
    // if(!output){
    //     printf("Failed to open output file\n");
    //     return -1;
    // }
    // FILE *output_pcm = fopen(OUTPUT_FILE_PCM, "wb");
    // if(!output_pcm){
    //     printf("Failed to open PCM output file\n");
    //     return -1;
    // }

    AVPacket *pkt = av_packet_alloc();
    AVPacket *capture_pkt = av_packet_alloc();
    int frame_count = 0;
    int sample_count = 0;
    uint8_t *out_buffer = NULL;
    int out_buffer_len = 0;
    while(run_flag == 1){
        int ret = av_read_frame(fmt_ctx, capture_pkt);
        if(ret < 0){
            printf("Error reading frame: %s\n", av_err2str(ret));
            break;
        }

        if(capture_pkt->stream_index == audio_stream_index){
            // fwrite(capture_pkt->data, 1, capture_pkt->size, output_pcm);
            int src_nb_samples = codec_ctx->frame_size;
            int n_frames = capture_pkt->size / (av_get_bytes_per_sample(input_sample_fmt) * codec_par->channels * src_nb_samples);
            for(int i = 0; i < n_frames; i++){
                const uint8_t *in_data[1] = { capture_pkt->data + i * av_get_bytes_per_sample(input_sample_fmt) * codec_par->channels * src_nb_samples};

                int dst_nb_samples = av_rescale_rnd(swr_get_delay(swr_ctx, input_sample_rate) + src_nb_samples,
                                                    SAMPLE_RATE, input_sample_rate, AV_ROUND_UP);

                AVFrame *frame = av_frame_alloc();
                frame->format = SAMPLE_FMT;
                frame->sample_rate = SAMPLE_RATE;
                frame->channels = CHANNELS;
                frame->channel_layout = av_get_default_channel_layout(CHANNELS);
                frame->nb_samples = dst_nb_samples;

                if(av_frame_get_buffer(frame, 0) < 0){
                    printf("Failed to allocate frame buffer\n");
                    return -1;
                }

                ret = swr_convert(swr_ctx, frame->data, dst_nb_samples, in_data, src_nb_samples);
                if(ret < 0){
                    printf("Error converting audio: %s\n", av_err2str(ret));
                    av_frame_free(&frame);
                    break;
                }
                frame->pts = sample_count * frame->nb_samples;

                ret = avcodec_send_frame(codec_ctx, frame);
                if (ret < 0){
                    printf("Error sending frame to encoder: %s\n", av_err2str(ret));
                    av_frame_free(&frame);
                    break;
                }

                while(avcodec_receive_packet(codec_ctx, pkt) == 0){
                    // uint8_t adts_header[7];
                    // add_adts_header(adts_header, pkt->size);
                    // fwrite(adts_header, 1, 7, output);
                    // fwrite(pkt->data, 1, pkt->size, output);
                    if((pkt->size + 7) > out_buffer_len){
                        if(out_buffer){
                            free(out_buffer);
                        }
                        out_buffer_len = pkt->size + 7 + 512; 
                        out_buffer = (uint8_t *)malloc(out_buffer_len);
                    }
                    add_adts_header(out_buffer, pkt->size);
                    memcpy(out_buffer + 7, pkt->data, pkt->size);
                    if(audio_cb){
                        audio_cb(out_buffer, pkt->size + 7, user);
                    }
                    av_packet_unref(pkt);
                }
                av_frame_free(&frame);
                sample_count++;
            }
        }
        av_packet_unref(capture_pkt);
        frame_count++;
    }
    avcodec_send_frame(codec_ctx, NULL);
    while(avcodec_receive_packet(codec_ctx, pkt) == 0){
        // uint8_t adts_header[7];
        // add_adts_header(adts_header, pkt->size);
        // fwrite(adts_header, 1, 7, output);
        // fwrite(pkt->data, 1, pkt->size, output);
        if((pkt->size + 7) > out_buffer_len){
            if(out_buffer){
                free(out_buffer);
            }
            out_buffer_len = pkt->size + 7 + 512; 
            out_buffer = (uint8_t *)malloc(out_buffer_len);
        }
        add_adts_header(out_buffer, pkt->size);
        memcpy(out_buffer + 7, pkt->data, pkt->size);
        if(audio_cb){
            audio_cb(out_buffer, pkt->size + 7, user);
        }
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    av_packet_free(&capture_pkt);
    // fclose(output);
    // fclose(output_pcm);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    swr_free(&swr_ctx);

    return 0;
}
void stopAudioDevice(){
    run_flag = 0;
}

