#ifndef _MEDIA_H_
#define _MEDIA_H_
#ifdef RTSP_FILE_SERVER
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#if defined(__linux__) || defined(__linux)
#include <signal.h>
#endif

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libavutil/time.h>

#include "common.h"
#include "mthread.h"
/*The status of buf and frame*/
enum BufFrame_e
{
    READ = 1,
    WRITE,
    OVER, //File read completed
};
/*MP4 buffer*/
struct buf_st
{
    unsigned char *buf;
    int buf_size;
    int stat; // Buf status, READ stands for readable, WRITE stands for writable
    int pos;  //Read the position record of buf from the frame
};
/*NALU data reading*/
struct frame_st
{
    unsigned char *frame;
    int frame_size;
    int start_code;
    int stat;
};
struct mediainfo_st
{
    AVFormatContext *context;
    AVPacket av_pkt;
    AVBSFContext *bsf_ctx;
    int video_stream_index;
    int audio_stream_index;
    int now_stream_index;
    int64_t curtimestamp;
    int fps;
    char *filename;
    struct buf_st *buffer;
    struct frame_st *frame;
    char *buffer_audio; // not free
    int buffer_audio_size;
    int video_type; // VIDEO_e
    int audio_type; // AUDIO_e
    void (*data_call_back)(void *arg);
    void (*reloop_call_back)(void *arg);
    void *arg;
    mthread_t tid;
    int run_flag;
};
struct audioinfo_st{
    int sample_rate;
    int channels;
    int profile;
};
void *creatMedia(char *path_filename, void *data_call_back, void *close_call_back, void *arg);
enum VIDEO_e getVideoType(void *context);
int nowStreamIsVideo(void *context);
enum AUDIO_e getAudioType(void *context);
int nowStreamIsAudio(void *context);
struct audioinfo_st getAudioInfo(void *context);
int getVideoNALUWithoutStartCode(void *context, char **ptr, int *ptr_len);
int getAudioWithoutADTS(void *context, char **ptr, int *ptr_len);
void destroyMedia(void *context);
#endif
#endif