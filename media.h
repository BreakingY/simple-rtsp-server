#ifndef _MEDIA_H_
#define _MEDIA_H_
#include "common.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libavutil/time.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
enum VIDEO_e
{
    VIDEO_H264 = 1,
    VIDEO_H265,
    VIDEO_NONE,
};
enum AUDIO_e
{
    AUDIO_AAC = 1,
    AUDIO_PCMA,
    AUDIO_NONE,
};
/*记录文件状态*/
enum File_e
{
    ALIVE = 1,
    ISOVER,

};
/*buf和frame的状态*/
enum BufFrame_e
{
    READ = 1,
    WRITE,
    OVER, // 文件读取完毕
};
/*MP4缓冲区*/
struct buf_st
{
    unsigned char *buf;
    int buf_size;
    int stat; // buf状态,READ表示可读 WRITE表示可写
    int pos;  // frame读取buf的位置记录
};
/*NALU数据读取*/
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
    AVBitStreamFilterContext *h26xbsfc;
    int video_stream_index;
    int audio_stream_index;
    int now_stream_index;
    int64_t curtimestamp;
    int fps;
    char *filename;
    int stat; // enum File_e
    struct buf_st *buffer;
    struct frame_st *frame;
    char *buffer_audio; // not free
    int buffer_audio_size;
    int video_type; // VIDEO_e
    int audio_type; // AUDIO_e
    void (*data_call_back)(void *arg);
    void *arg;
    pthread_t tid;
    int run_flag;
};
struct audioinfo_st{
    int sample_rate;
    int channels;
    int profile;
};
void *creatMedia(char *path_filename, void *call_back, void *arg);
enum VIDEO_e getVideoType(void *context);
int nowStreamIsVideo(void *context);
enum AUDIO_e getAudioType(void *context);
int nowStreamIsAudio(void *context);
struct audioinfo_st getAudioInfo(void *context);
int getVideoNALUWithoutStartCode(void *context, char **ptr, int *ptr_len);
int getAudioWithoutADTS(void *context, char **ptr, int *ptr_len);
void destroyMedia(void *context);
#endif