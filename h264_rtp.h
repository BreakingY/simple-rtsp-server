#ifndef _H264RTP_H_
#define _H264RTP_H_
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

/*buf和frame的状态*/
enum BufFrame_e {
    READ = 1,
    WRITE,
    OVER, // 文件读取完毕
};
/*MP4缓冲区*/
struct buf_st {
    unsigned char *buf;
    int buf_size;
    int stat; // buf状态,READ表示可读 WRITE表示可写
    int pos;  // frame读取buf的位置记录
};
/*NALU数据读取*/
struct frame_st {
    unsigned char *frame;
    int frame_size;
    int start_code;
    int stat;
};
/*从AVPacket中解析NALU，保存到pBuf中*/
int h264Mp4ToAnnexb(AVFormatContext *context, AVPacket *pAvPkt, struct buf_st *pBuf);

/*从buf中读取一个NALU数据到frame中*/
int getNALUFromBuf(unsigned char *frame, int size, struct buf_st *buf);
int rtpSendH264Frame(int sd, struct rtp_tcp_header *tcp_header, struct RtpPacket *rtp_packet, uint8_t *frame, uint32_t frame_size, int fps, int sig_0, char *client_ip, int client_rtp_port);

int startCode3(char *buf);

int startCode4(char *buf);

#endif