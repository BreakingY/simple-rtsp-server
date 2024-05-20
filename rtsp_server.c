#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libavutil/time.h>
#include <malloc.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "common.h"
#define BUF_MAX_SIZE (1024 * 1024)
#define CLIENTMAX 1024
#define FILEMAX 1024
#define VIDEO_DATA_MAX_SIZE 32 * 1024 * 1024

/*记录文件状态*/
enum File_e {
    ALIVE = 1,
    ISOVER,

};
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
/*记录客户端的数据通道及数据包*/
struct clientinfo_st {
    int sd;
    int sig_0;
    int playflag;

    void *arg;                         // 指向自己结构体指针
    void (*send_call_back)(void *arg); // 回调函数,目前只针对数据发送回调函数，如果需要关注EPOLLIN事件，可以再定义一个回调函数接口，负责接收客户端发送过来的数据
    int events;                        // 对应的监听事件，EPOLLIN和EPLLOUT，目前只关心EPOLLOUT事件
    struct mp4info_st *mp4info;        // 指向上层结构体(视频回放任务)

    struct RtpPacket *rtp_packet; // 每一个客户端对应一个rtp 和tcp数据包，否则不同客户端共用数据包会导致序列号不连续
    struct rtp_tcp_header *tcp_header;
    int IDRflag; // 表示客户端是否都到过关键帧，如果没有收到过关键帧，将丢弃所有非关键正，如果收到过关键帧，就持续发送数据，-1表示没有收到过，1表示收到过
};

/*视频回放任务结构体*/
struct mp4info_st {
    AVFormatContext *context;
    AVPacket av_pkt;
    int64_t curtimestamp;
    int64_t pertimestamp;
    int video_stream_index;
    int fps;
    char *filename;
    int stat;
    struct buf_st *buffer;
    struct frame_st *frame;
    pthread_mutex_t mut;
    struct clientinfo_st clientinfo[CLIENTMAX]; // 请求回放当前视频文件的rtsp客户端
    int count;
    int epfd;
    int timestamp;
};

/*doClientThd线程参数*/
struct thd_arg_st {
    int client_sock_fd;
    char client_ip[30];
    int client_port;
};

struct mp4info_st *mp4info_arr[FILEMAX]; // 视频回放任务数组，动态添加删除
pthread_mutex_t mut_mp4 = PTHREAD_MUTEX_INITIALIZER;

char *mp4Dir = "mp4path/\0"; // MP4文件存放位置

int sum_client = 0; // 记录一共有多少个客户端正在连接服务器
pthread_mutex_t mut_clientcount = PTHREAD_MUTEX_INITIALIZER;

void sig_handler(int s)
{
    printf("catch signal %d,rtsp exit\n", s);
    moduleDel();
    printf("%s\n", __func__);
    exit(1);
}

static inline int startCode3(char *buf)
{
    if (buf[0] == 0 && buf[1] == 0 && buf[2] == 1)
        return 1;
    else
        return 0;
}

static inline int startCode4(char *buf)
{
    if (buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] == 1)
        return 1;
    else
        return 0;
}

/*从buf中读取一个NALU数据到frame中*/
int getNALUFromBuf(unsigned char *frame, int size, struct buf_st *buf)
{
    int startCode;
    char *pstart;
    char *tmp;
    int frame_size;
    int bufoverflag = 1;

    if (buf->pos >= buf->buf_size) {
        buf->stat = WRITE;
        printf("h264buf empty\n");
        return -1;
    }

    if (!startCode3(buf->buf + buf->pos) && !startCode4(buf->buf + buf->pos)) {
        printf("statrcode err\n");
        return -1;
    }

    if (startCode3(buf->buf + buf->pos)) {
        startCode = 3;
    } else
        startCode = 4;

    pstart = buf->buf + buf->pos; // pstart跳过之前已经读取的数据指向本次NALU的起始码

    tmp = pstart + startCode; // 指向NALU数据

    for (int i = 0; i < buf->buf_size - buf->pos - 3; i++) // pos表示起始码的位置，所以已经读取的数据长度也应该是pos个字节
    {
        if (startCode3(tmp) || startCode4(tmp)) // 此时tmp指向下一个起始码位置
        {
            frame_size = tmp - pstart; // 包含起始码的NALU长度
            bufoverflag = 0;
            break;
        }
        tmp++;
    }
    if (bufoverflag == 1) {
        frame_size = buf->buf_size - buf->pos;
    }

    memcpy(frame, buf->buf + buf->pos, frame_size);

    buf->pos += frame_size;
    /*buf中的数据全部读取完毕，设置buf状态为WREIT*/
    if (buf->pos >= buf->buf_size) {
        buf->stat = WRITE;
    }
    return frame_size;
}

/*从buf中解析NALU数据*/
void praseFrame(int i)
{
    if (mp4info_arr[i] == NULL) {
        return;
    }
    /*frame==READ,buffer==WRITE状态不可访问*/
    if (mp4info_arr[i]->frame->stat == READ || mp4info_arr[i]->buffer->stat == WRITE || mp4info_arr[i]->frame->stat == READ) {
        return;
    }

    /*buf处于可读状态并且frame处于可写状态*/
    mp4info_arr[i]->frame->frame_size = getNALUFromBuf(mp4info_arr[i]->frame->frame, VIDEO_DATA_MAX_SIZE, mp4info_arr[i]->buffer);

    if (mp4info_arr[i]->frame->frame_size < 0) {
        return;
    }

    if (startCode3(mp4info_arr[i]->frame->frame))
        mp4info_arr[i]->frame->start_code = 3;
    else
        mp4info_arr[i]->frame->start_code = 4;

    mp4info_arr[i]->frame->frame_size -= mp4info_arr[i]->frame->start_code;
    mp4info_arr[i]->frame->stat = READ;

    return;
}

static double r2d(AVRational r)
{
    return r.den == 0 ? 0 : (double)r.num / (double)r.den;
}

/* 获取SPS与PPS 并添加起始码*/
static int h264_extradata_to_annexb(const unsigned char *pCodecExtraData, const int codecExtraDataSize,
                                    AVPacket *pOutExtradata, int padding)
{
    const unsigned char *pExtraData = NULL; /* 前四个字节没用 */
    int len = 0;
    int spsUnitNum, ppsUnitNum;
    int unitSize, totolSize = 0;
    unsigned char startCode[] = {0, 0, 0, 1};
    unsigned char *pOut = NULL;
    int err;
    pExtraData = pCodecExtraData + 4; // pExtraData在第5个字节
    len = (*pExtraData++ & 0x3) + 1;  // 用于指示表示编码数据长度所需字节数
    /*前5个字节没有用从第6个字节开始解析sps和pps*/

    /*1  获取SPS */
    spsUnitNum = (*pExtraData++ & 0x1f); /* SPS数量 */

    while (spsUnitNum--) {
        unitSize = (pExtraData[0] << 8 | pExtraData[1]); /* 两个字节表示这个unit的长度*/
        pExtraData += 2;
        totolSize += unitSize + sizeof(startCode);
        // printf("unitSize:%d\n", unitSize);

        if (totolSize > INT_MAX - padding) {
            av_log(NULL, AV_LOG_ERROR,
                   "Too big extradata size, corrupted stream or invalid MP4/AVCC bitstream\n");
            av_free(pOut);
            return AVERROR(EINVAL);
        }

        if (pExtraData + unitSize > pCodecExtraData + codecExtraDataSize) {
            av_log(NULL, AV_LOG_ERROR, "Packet header is not contained in global extradata, "
                                       "corrupted stream or invalid MP4/AVCC bitstream\n");
            av_free(pOut);
            return AVERROR(EINVAL);
        }

        if ((err = av_reallocp(&pOut, totolSize + padding)) < 0)
            return err;

        memcpy(pOut + totolSize - unitSize - sizeof(startCode), startCode, sizeof(startCode));
        memcpy(pOut + totolSize - unitSize, pExtraData, unitSize);

        pExtraData += unitSize;
    }

    /*2 获取PPS */
    ppsUnitNum = (*pExtraData++ & 0x1f); /* PPS数量*/
    while (ppsUnitNum--) {
        unitSize = (pExtraData[0] << 8 | pExtraData[1]); /* 两个字节表示这个unit的长度*/
        pExtraData += 2;
        totolSize += unitSize + sizeof(startCode);
        // printf("unitSize:%d\n", unitSize);

        if (totolSize > INT_MAX - padding) {
            av_log(NULL, AV_LOG_ERROR,
                   "Too big extradata size, corrupted stream or invalid MP4/AVCC bitstream\n");
            av_free(pOut);
            return AVERROR(EINVAL);
        }

        if (pExtraData + unitSize > pCodecExtraData + codecExtraDataSize) {
            av_log(NULL, AV_LOG_ERROR, "Packet header is not contained in global extradata, "
                                       "corrupted stream or invalid MP4/AVCC bitstream\n");
            av_free(pOut);
            return AVERROR(EINVAL);
        }

        if ((err = av_reallocp(&pOut, totolSize + padding)) < 0)
            return err;

        memcpy(pOut + totolSize - unitSize - sizeof(startCode), startCode, sizeof(startCode));
        memcpy(pOut + totolSize - unitSize, pExtraData, unitSize);

        pExtraData += unitSize;
    }

    pOutExtradata->data = pOut;
    pOutExtradata->size = totolSize;

    return len;
}

/* 将数据复制 */
static int alloc_and_copy(AVPacket *pOutPkt, const uint8_t *spspps, uint32_t spsppsSize,
                          const uint8_t *pIn, uint32_t inSize)
{
    int err;
    int startCodeLen = 3;

    /* 给pOutPkt->data分配内存，spsppsSize-spspps长度，如果不是关键帧则为0，inSize一帧的数据(pIn)长度--也就是一个NALU长度，startCodeLen起始码长度--这里除了sps pps是四字节起始码，其余都按照3字节处理*/
    err = av_grow_packet(pOutPkt, spsppsSize + inSize + startCodeLen);
    if (err < 0)
        return err;
    /*如果spspps参数不为NULL，表示，此时的pIn为关键正，在关键帧之前要添加sps pps*/
    if (spspps) {
        memcpy(pOutPkt->data, spspps, spsppsSize); /* 拷贝SPS与PPS(前面分离的时候已经加了startcode(00 00 00 01)) */
    }

    /* 将真正的原始数据写入packet中 */
    /*写入起始码*/
    (pOutPkt->data + spsppsSize)[0] = 0;
    (pOutPkt->data + spsppsSize)[1] = 0;
    (pOutPkt->data + spsppsSize)[2] = 1;
    /*写入帧--NALU数据*/
    memcpy(pOutPkt->data + spsppsSize + startCodeLen, pIn, inSize);

    return 0;
}

/*从MP4文件中解析NALU，保存到pBuf中*/
int h264Mp4ToAnnexb(AVFormatContext *context, AVPacket *pAvPkt, struct buf_st *pBuf)
{
    /*接收AVPacket数据包里面的数据*/
    unsigned char *pData = pAvPkt->data; /* 帧数据 */
    unsigned char *pEnd = NULL;
    int dataSize = pAvPkt->size; /* pAvPkt->data的数据量 */
    int curSize = 0;
    int naluSize = 0;
    int i;
    unsigned char nalHeader, nalType;
    AVPacket spsppsPkt;
    AVPacket *pOutPkt;
    int ret;
    int len;
    /*初始化NALU数据接收包*/
    pOutPkt = av_packet_alloc();
    pOutPkt->data = NULL;
    pOutPkt->size = 0;
    spsppsPkt.data = NULL;
    spsppsPkt.size = 0;

    pEnd = pData + dataSize;
    int pos = 0;

    while (curSize < dataSize) {
        if (pEnd - pData < 4)
            goto fail;

        /* 前四个字节表示当前NALU的大小 */

        for (i = 0; i < 4; i++) {
            naluSize <<= 8;
            naluSize |= pData[i];
        }

        pData += 4;

        if (naluSize > (pEnd - pData + 1) || naluSize <= 0) {
            goto fail;
        }

        nalHeader = *pData;
        nalType = nalHeader & 0x1F;
        if (nalType == 5) // 关键帧
        {
            /* 得到SPS与PPS（存在与codec->extradata中） */
            h264_extradata_to_annexb(context->streams[pAvPkt->stream_index]->codec->extradata,
                                     context->streams[pAvPkt->stream_index]->codec->extradata_size,
                                     &spsppsPkt, AV_INPUT_BUFFER_PADDING_SIZE);
            /*至此spsppsPkt里面存储了所有sps和pps并已经添加号起始码---startCode+sps+...+startCode+pps+...*/

            /* 添加start code */
            ret = alloc_and_copy(pOutPkt, spsppsPkt.data, spsppsPkt.size, pData, naluSize);
            if (ret < 0)
                goto fail;

        } else // 非关键帧
        {
            /* 添加start code */
            ret = alloc_and_copy(pOutPkt, NULL, 0, pData, naluSize);
            if (ret < 0)
                goto fail;
        }

        /* 将处理好的数据缓冲区中 */
        /*
         *两种情况：
         *1、关键帧：pOutPkt中存储了startCode+sps+...+startCode+pps+startCode+关键帧NALU
         *2、非关键帧：pOutPkt中存储了startCode+非关键正NALU
         */

        /*将数据拷贝到缓冲区中*/

        memcpy(pBuf->buf + pos, pOutPkt->data, pOutPkt->size);

        pos += (pOutPkt->size);
        pBuf->buf_size += (pOutPkt->size);

        /*处理下一个NALU
         *一般来说ffmpeg读取的AVPacket *pAvPkt一帧数据里面就包含一个NALU，但是对于关键正的数据包会包含格外的非关键帧SEI SPS PPS，需要循环判断，保证AVPacket *pAvPkt中有几个NALU就解析几个
         */
        curSize += (naluSize + 4);
        pData += naluSize;
    }

fail:
    av_packet_free(&pOutPkt);
    if (spsppsPkt.data) {
        free(spsppsPkt.data);
        spsppsPkt.data = NULL;
    }

    return 0;
}


int rtpSendH264FrameTcp(int sd, struct rtp_tcp_header *tcp_header, struct RtpPacket *rtp_packet, uint8_t *frame, uint32_t frame_size, int sig_0, int fps, long timestamp)
{
    uint8_t nalu_type; // nalu第一个字节
    int send_bytes = 0;
    int ret;

    nalu_type = frame[0];
    rtp_packet->rtpHeader.marker = 0;
    memset(rtp_packet->payload, 0, strlen(rtp_packet->payload));

    if (frame == NULL) {
        printf("frame=NULL\n");
        return -1;
    }
    tcp_header->magic = '$';

    tcp_header->rtp_len16 = 0;

    if (frame_size <= PTK_RTP_TCP_MAX) // nalu长度小于最大包场：单一NALU单元模式
    {
        /*
         *   0 1 2 3 4 5 6 7 8 9
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         *  |F|NRI|  Type   | a single NAL unit ... |--NALU头部+NAL
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         */
        /*单一模式m位为1*/
        rtp_packet->rtpHeader.marker = 1;
        // 数据初始化,网络字节序转换

        tcp_header->rtp_len16 = (uint16_t)RTP_HEADER_SIZE + (uint16_t)frame_size;

        tcp_header->rtp_len16 = htons(tcp_header->rtp_len16);

        rtp_packet->rtpHeader.seq = htons(rtp_packet->rtpHeader.seq);
        rtp_packet->rtpHeader.timestamp = htonl(rtp_packet->rtpHeader.timestamp);
        rtp_packet->rtpHeader.ssrc = htonl(rtp_packet->rtpHeader.ssrc);
        memcpy(rtp_packet->payload, frame, frame_size);

        // 发送RTP_OVER_TCP头部
        tcp_header->channel = sig_0;
        ret = send(sd, tcp_header, sizeof(struct rtp_tcp_header), 0);
        if (ret <= 0) {
            return -1;
        }

        // 发送RTP数据包

        ret = send(sd, rtp_packet, RTP_HEADER_SIZE + frame_size, 0);
        if (ret <= 0) {
            return -1;
        }

        // 还原字节序
        tcp_header->rtp_len16 = ntohs(tcp_header->rtp_len16);
        rtp_packet->rtpHeader.seq = ntohs(rtp_packet->rtpHeader.seq);
        rtp_packet->rtpHeader.timestamp = ntohl(rtp_packet->rtpHeader.timestamp);
        rtp_packet->rtpHeader.ssrc = ntohl(rtp_packet->rtpHeader.ssrc);

        rtp_packet->rtpHeader.seq++;
        send_bytes += ret;
        if ((nalu_type & 0x1F) == 7 || (nalu_type & 0x1F) == 8) // 如果是SPS、PPS就不需要加时间戳
            return send_bytes;
        rtp_packet->rtpHeader.timestamp += 90000 / fps;
    } else // nalu长度小于最大包场：分片模式
    {
        /*
         *  0                   1                   2
         *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * | FU indicator  |   FU header   |   FU payload   ...  |
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         */

        /*
         *     FU Indicator
         *    0 1 2 3 4 5 6 7
         *   +-+-+-+-+-+-+-+-+
         *   |F|NRI|  Type   |
         *   +---------------+
         */

        /*
         *      FU Header
         *    0 1 2 3 4 5 6 7
         *   +-+-+-+-+-+-+-+-+
         *   |S|E|R|  Type   |
         *   +---------------+
         */

        int pktNum = frame_size / PTK_RTP_TCP_MAX;        // 有几个完整的包
        int remainPktSize = frame_size % PTK_RTP_TCP_MAX; // 剩余不完整包的大小
        int i, pos = 1;

        /* 发送完整的包 */
        for (i = 0; i < pktNum; i++) {
            memset(rtp_packet->payload, 0, strlen(rtp_packet->payload));
            rtp_packet->rtpHeader.marker = 0;
            rtp_packet->payload[0] = (nalu_type & 0xE0) | 28; // F，NRI保持不变，Type设为28(|28)
            // rtp_packet->payload[0] = (nalu_type & 0x60) | 28;
            rtp_packet->payload[1] = nalu_type & 0x1F; // S E R置0，type与NALU的type相同

            if (i == 0) // 第一包数据
            {

                rtp_packet->payload[1] |= 0x80;               // start
            } else if (remainPktSize == 0 && i == pktNum - 1) // 最后一包数据
            {
                rtp_packet->payload[1] |= 0x40; // end
                rtp_packet->rtpHeader.marker = 1;
            }

            // 初始化，字节序转换
            tcp_header->rtp_len16 = (uint16_t)RTP_HEADER_SIZE + (uint16_t)PTK_RTP_TCP_MAX + 2; // 多两个字节的FUin和FUheader
            tcp_header->rtp_len16 = htons(tcp_header->rtp_len16);

            memcpy(rtp_packet->payload + 2, frame + pos, PTK_RTP_TCP_MAX);
            rtp_packet->rtpHeader.seq = htons(rtp_packet->rtpHeader.seq);
            rtp_packet->rtpHeader.timestamp = htonl(rtp_packet->rtpHeader.timestamp);
            rtp_packet->rtpHeader.ssrc = htonl(rtp_packet->rtpHeader.ssrc);

            // 发送RTP_OVER_TCP头部
            tcp_header->channel = sig_0;
            ret = send(sd, tcp_header, sizeof(struct rtp_tcp_header), 0);
            if (ret <= 0) {
                return -1;
            }

            // 发送RTP数据包

            ret = send(sd, rtp_packet, RTP_HEADER_SIZE + PTK_RTP_TCP_MAX + 2, 0);
            if (ret <= 0) {
                return -1;
            }

            // 还原字节序
            tcp_header->rtp_len16 = ntohs(tcp_header->rtp_len16);
            rtp_packet->rtpHeader.seq = ntohs(rtp_packet->rtpHeader.seq);
            rtp_packet->rtpHeader.timestamp = ntohl(rtp_packet->rtpHeader.timestamp);
            rtp_packet->rtpHeader.ssrc = ntohl(rtp_packet->rtpHeader.ssrc);

            rtp_packet->rtpHeader.seq++;
            send_bytes += ret;
            pos += PTK_RTP_TCP_MAX;
        }

        /* 发送剩余的数据 */
        if (remainPktSize > 0) {
            memset(rtp_packet->payload, 0, strlen(rtp_packet->payload));
            rtp_packet->payload[0] = (nalu_type & 0xE0) | 28;
            rtp_packet->payload[1] = nalu_type & 0x1F;
            rtp_packet->payload[1] |= 0x40; // end
            rtp_packet->rtpHeader.marker = 1;
            // 初始化数据，网络字节序转换
            tcp_header->rtp_len16 = (uint16_t)RTP_HEADER_SIZE + (uint16_t)remainPktSize + 2; // 多两个字节的FUin和FUheader
            tcp_header->rtp_len16 = htons(tcp_header->rtp_len16);

            rtp_packet->rtpHeader.seq = htons(rtp_packet->rtpHeader.seq);
            rtp_packet->rtpHeader.timestamp = htonl(rtp_packet->rtpHeader.timestamp);
            rtp_packet->rtpHeader.ssrc = htonl(rtp_packet->rtpHeader.ssrc);
            memcpy(rtp_packet->payload + 2, frame + pos, remainPktSize);

            // 发送RTP_OVER_TCP头部
            tcp_header->channel = sig_0;
            ret = send(sd, tcp_header, sizeof(struct rtp_tcp_header), 0);
            if (ret <= 0) {
                return -1;
            }

            // 发送RTP数据包

            ret = send(sd, rtp_packet, RTP_HEADER_SIZE + remainPktSize + 2, 0);
            if (ret <= 0) {
                return -1;
            }

            // 还原字节序
            tcp_header->rtp_len16 = ntohs(tcp_header->rtp_len16);
            rtp_packet->rtpHeader.seq = ntohs(rtp_packet->rtpHeader.seq);
            rtp_packet->rtpHeader.timestamp = ntohl(rtp_packet->rtpHeader.timestamp);
            rtp_packet->rtpHeader.ssrc = ntohl(rtp_packet->rtpHeader.ssrc);

            rtp_packet->rtpHeader.seq++;
            send_bytes += ret;
        }
        // 所有分包的时间戳都是一样的，时间戳的原则就是，发送一个完整的NALU时间戳才更改，但是不管什么包，序列号持续+1
        rtp_packet->rtpHeader.timestamp += 90000 / fps;
    }
    return send_bytes;
}
/*判断是否使SEI SPS PPS IDR*/
int isTrueofIDR(uint8_t *frame)
{
    uint8_t nalu_type; // nalu第一个字节

    nalu_type = frame[0];
    if ((nalu_type & 0x1F) == 5 || (nalu_type & 0x1F) == 6 || (nalu_type & 0x1F) == 7 || (nalu_type & 0x1F) == 8) // SEI IDR SPS PPS
    {
        printf("IDR\n");
        return 1;
    }
    return -1;
}
/*
 * 封装一个自定义事件，包括fd，这个fd的回调函数，还有一个额外的参数项
 * 注意：在封装这个事件的时候，为这个事件指明了回调函数，一般来说，一个fd只对一个特定的事件
 * 当感兴趣事件发生的时候，就调用这个回调函数
 */
void eventSet(struct clientinfo_st *ev, int sd, void (*send_call_back)(void *arg), void *arg)
{
    ev->sd = sd;
    ev->send_call_back = send_call_back;
    ev->events = 0;
    ev->arg = arg;
    return;
}
/* 向 epoll监听的红黑树 添加一个文件描述符 */
void eventAdd(int efd, int events, struct clientinfo_st *ev)
{
    if (ev->sd < 0)
        return;
    struct epoll_event epv = {0, {0}};
    int op = EPOLL_CTL_ADD;
    epv.data.ptr = ev;                        // ptr指向一个结构体
    epv.events = ev->events = events;         // EPOLLIN 或 EPOLLOUT
    if (epoll_ctl(efd, op, ev->sd, &epv) < 0) // 添加一个节点
        printf("event add failed [fd=%d]\n", ev->sd);
    else
        printf("event add OK [fd=%d]\n", ev->sd);
    return;
}
/* 从epoll 监听的 红黑树中删除一个文件描述符*/
void eventDel(int epfd, struct clientinfo_st *ev)
{
    if (ev->sd < 0)
        return;
    struct epoll_event epv = {0, {0}};
    epv.data.ptr = NULL;
    epoll_ctl(epfd, EPOLL_CTL_DEL, ev->sd, &epv);
    printf("event del OK [fd=%d]\n", ev->sd);
    return;
}
/*
 * 在客户端收到关键帧之前发送心跳包，防止客户端断开连接
 * 目前的逻辑是到关键帧的位置才给客户端发送数据，所以这里添加了心跳逻辑，也可以客户端连接的时候就直接发送数据给客户端，这样不需要增加心跳了，具体看自己的业务需求。
 */
void sendHeartBeatPkt(int sd, struct rtp_tcp_header *tcp_header, int sig_0)
{
    tcp_header->magic = '$';

    tcp_header->rtp_len16 = 0;
    tcp_header->channel = sig_0;
    tcp_header->rtp_len16 = htons(tcp_header->rtp_len16);
    send(sd, tcp_header, sizeof(struct rtp_tcp_header), 0);
    tcp_header->rtp_len16 = ntohs(tcp_header->rtp_len16);
}
void sendData(void *arg)
{
    struct clientinfo_st *clientinfo = (struct clientinfo_st *)arg;
    /*判断客户端是否收过关键帧*/

    if (clientinfo->IDRflag == -1) {
        if (isTrueofIDR(clientinfo->mp4info->frame->frame + clientinfo->mp4info->frame->start_code) > 0) {
            clientinfo->IDRflag = 1;

        } else // 如果不是关键帧就不给客户端发送该帧
        {
            sendHeartBeatPkt(clientinfo->sd, clientinfo->tcp_header, clientinfo->sig_0); // 发送心跳包

            return;
        }
    }
    int ret = rtpSendH264FrameTcp(clientinfo->sd, clientinfo->tcp_header, clientinfo->rtp_packet, clientinfo->mp4info->frame->frame + clientinfo->mp4info->frame->start_code,
                                  clientinfo->mp4info->frame->frame_size, clientinfo->sig_0, clientinfo->mp4info->fps, clientinfo->mp4info->curtimestamp);
    if (ret <= 0) //&&!(errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN)
    {
        /*从epoll上删除并释放空间*/
        eventDel(clientinfo->mp4info->epfd, clientinfo);
        printf("client:%d offline\n", clientinfo->sd);
        close(clientinfo->sd);
        clientinfo->mp4info->count--;
        printf("thr_mp4:%s client_count:%d\n", clientinfo->mp4info->filename, clientinfo->mp4info->count);
        clientinfo->sd = -1;
        clientinfo->sig_0 = -1;
        clientinfo->playflag = -1;
        clientinfo->IDRflag = -1;

        if (clientinfo->tcp_header != NULL) {
            free(clientinfo->tcp_header);
            clientinfo->tcp_header = NULL;
        }
        if (clientinfo->rtp_packet != NULL) {
            free(clientinfo->rtp_packet);
            clientinfo->rtp_packet = NULL;
        }

        /*更改客户连接总数*/
        pthread_mutex_lock(&mut_clientcount);
        sum_client--;
        printf("sum_client:%d\n", sum_client);
        pthread_mutex_unlock(&mut_clientcount);
    }
}

// 一个MP4文件回放线程处理多个客户端，arg为mp4info_arr数组索引，表示当前线程负责哪个回放任务
void *parseMp4SendDataThd(void *arg)
{
    signal(SIGPIPE, SIG_IGN);
    signal(SIGFPE, SIG_IGN);
    signal(SIGINT, sig_handler);
    signal(SIGQUIT, sig_handler);
    signal(SIGKILL, sig_handler);
    int pos = (int)arg;
    struct mp4info_st *mp4info;
    mp4info = mp4info_arr[pos];
    int findstream = 0;
    if (mp4info == NULL)
        pthread_exit(NULL);
    // struct timeval pre,cur;
    struct epoll_event events[CLIENTMAX];
    int fristrunflag = 0;
    int64_t start_time = av_gettime();
    while (1) {
        if (mp4info == NULL)
            pthread_exit(NULL);
        if (mp4info->stat == ISOVER || mp4info->count == 0) // 如果文件结束或者所有客户端都主动断开了连接，就释放空间并退出线程，这时候就需要rtsp客户端重新发起请求，重新建立连接(客户端需要支持重连机制)
        {
            pthread_mutex_lock(&mut_mp4);
            printf("thr_mp4:%s exit file over\n", mp4info->filename);
            del1Mp4Info(pos);
            pthread_mutex_unlock(&mut_mp4);
            pthread_exit(NULL);
        }
        /*如果buf可写就解析MP4文件*/
        if (mp4info->buffer->stat == WRITE) {
            findstream = 0;
            /*如果对应buf可写，就从对应文件中读取一帧数据并解析*/
            while (av_read_frame(mp4info->context, &mp4info->av_pkt) >= 0) {
                if (mp4info->av_pkt.stream_index == mp4info->video_stream_index) {
                    findstream = 1;
                    /*
                    AVStream *as = mp4info->context->streams[mp4info->video_stream_index];
                    mp4info->curtimestamp =AV_TIME_BASE * mp4info->av_pkt.pts * av_q2d(as->time_base);
                    //printf("timestamp:%ld\n",mp4info->curtimestamp);
                    */
                    // 上面的计算结果和下面的计算结果是一样的，都是把pts转换成真实的时间(这里真实的时间的含义是从第一帧开始计算到当前帧的时间，得到的时间为从第一帧到现在的真正的微妙时间)
                    // 这里得到的是真正的时间，而RTP数据封包里面的90000/25是时间刻度，时间基是90000，即1s包含90000个刻度，而1s有包含fps帧数据，所以RTP中帧间的间隔占用的刻度是90000/fps
                    AVRational time_base = mp4info->context->streams[mp4info->video_stream_index]->time_base;
                    AVRational time_base_q = {1, AV_TIME_BASE};
                    mp4info->pertimestamp = mp4info->curtimestamp;
                    mp4info->curtimestamp = av_rescale_q(mp4info->av_pkt.pts, time_base, time_base_q);
                    break;
                }
            }
            /*文件推流完毕*/
            if (findstream == 0) {
                mp4info->stat = ISOVER;
                /*释放资源，线程退出*/
                // pthread_mutex_unlock(&mp4info->mut);
                pthread_mutex_lock(&mut_mp4);
                printf("thr_mp4:%s exit,file over\n", mp4info->filename);
                av_packet_unref(&mp4info->av_pkt);
                del1Mp4Info(pos);
                pthread_mutex_unlock(&mut_mp4);
                pthread_exit(NULL);
            }
            /*帧数据写道buf中*/
            memset(mp4info->buffer->buf, 0, VIDEO_DATA_MAX_SIZE);
            mp4info->buffer->buf_size = 0;
            mp4info->buffer->pos = 0;
            h264Mp4ToAnnexb(mp4info->context, &mp4info->av_pkt, mp4info->buffer);
            /*设置buf为READ状态*/
            mp4info->buffer->stat = READ;
            av_packet_unref(&mp4info->av_pkt);
        }
        /*如果buf可读就从buf中解析NALU*/
        if (mp4info->buffer->stat == READ) {
            praseFrame(pos); // 如果buf处于可读状态就送去解析NALU
        }
        /*epoll*/
        int64_t now_time = av_gettime() - start_time;
        if (mp4info->curtimestamp > now_time)
            av_usleep(mp4info->curtimestamp - now_time); // 用的是ffmepg的时间所以用ffmpeg的usleep函数来睡眠
        pthread_mutex_lock(&mp4info->mut);
        int nfd = epoll_wait(mp4info->epfd, events, CLIENTMAX, -1);
        pthread_mutex_unlock(&mp4info->mut);
        if (nfd < 0) {
            printf("epoll_wait error, exit\n");
            exit(-1);
        }
        // 向当前回放视频的所有客户端发送数据
        for (int i = 0; i < nfd; i++) {

            struct clientinfo_st *ev = (struct clientinfo_st *)events[i].data.ptr;
            // 如果监听的是读事件，并返回的是读事件
            if ((events[i].events & EPOLLOUT) && (ev->events & EPOLLOUT)) {
                ev->send_call_back(ev->arg);
            }
        }
        mp4info->frame->stat = WRITE;
    }
    return;
}
int get_free_clientinfo(int pos)
{

    for (int i = 0; i < CLIENTMAX; i++) {

        if (mp4info_arr[pos]->clientinfo[i].sd == -1) {

            return i;
        }
    }
    return -1;
}
/*创建一个mp4文件回放任务并添加一个客户端*/
int add1Mp4Info(int pos, char *path_filename, int client_sock_fd, int sig_0)
{
    struct mp4info_st *mp4;
    mp4 = malloc(sizeof(struct mp4info_st));

    /*初始ffmepg标准变量*/
    mp4->filename = malloc(strlen(path_filename) + 1);
    memset(mp4->filename, 0, strlen(path_filename) + 1);

    printf("path_filename len:%d\n", strlen(path_filename));
    // printf("path_filename:%s\n",path_filename);
    memcpy(mp4->filename, path_filename, strlen(path_filename));
    printf("add1Mp4Info:%s\n", mp4->filename);

    mp4->context = NULL;
    mp4->fps = 0;
    mp4->stat = ALIVE;
    av_init_packet(&mp4->av_pkt);
    mp4->av_pkt.data = NULL;
    mp4->av_pkt.size = 0;
    mp4->pertimestamp = 0;
    mp4->curtimestamp = 0;
    /*创建epoll*/
    mp4->epfd = epoll_create(CLIENTMAX);
    if (mp4->epfd <= 0)
        printf("create efd in %s err %s\n", __func__, strerror(errno));

    int ret = avformat_open_input(&mp4->context, mp4->filename, NULL, NULL);
    if (ret < 0) {
        char buf[1024];
        av_strerror(ret, buf, 1024); // 查看报错内容
        printf("avformat_open_input error %d,%s\n", ret, buf);
        if (mp4->filename != NULL)
            free(mp4->filename);
        free(mp4);

        printf("avformat_open_input error,\n");
        return -1;
    }

    /*获取帧率*/
    int video_stream_index;
    AVStream *as;
    video_stream_index = av_find_best_stream(mp4->context, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_stream_index < 0) {
        return -1;
    }
    mp4->video_stream_index = video_stream_index;
    as = mp4->context->streams[video_stream_index];
    mp4->fps = r2d(as->avg_frame_rate);
    printf("%s->fps:%d\n", mp4->filename, mp4->fps);
    pthread_mutex_init(&mp4->mut, NULL);
    mp4->count = 0;

    /*初始buffer*/
    mp4->buffer = malloc(sizeof(struct buf_st));
    mp4->buffer->buf = malloc(VIDEO_DATA_MAX_SIZE);
    memset(mp4->buffer->buf, '\0', VIDEO_DATA_MAX_SIZE);
    mp4->buffer->buf_size = 0;
    mp4->buffer->pos = 0;
    mp4->buffer->stat = WRITE;

    /*初始化frame*/
    mp4->frame = malloc(sizeof(struct frame_st));
    mp4->frame->frame = malloc(VIDEO_DATA_MAX_SIZE);
    mp4->frame->stat = WRITE;

    mp4info_arr[pos] = mp4;
    for (int j = 0; j < CLIENTMAX; j++) {
        mp4info_arr[pos]->clientinfo[j].sd = -1;
        mp4info_arr[pos]->clientinfo[j].sig_0 = -1;
        mp4info_arr[pos]->clientinfo[j].playflag = -1;
        mp4info_arr[pos]->clientinfo[j].rtp_packet = NULL;
        mp4info_arr[pos]->clientinfo[j].tcp_header = NULL;
        mp4info_arr[pos]->clientinfo[j].IDRflag = -1;
        /*epoll*/
        mp4info_arr[pos]->clientinfo[j].send_call_back = NULL;
        mp4info_arr[pos]->clientinfo[j].events = 0;
        mp4info_arr[pos]->clientinfo[j].mp4info = mp4info_arr[pos];
    }
    mp4info_arr[pos]->clientinfo[0].sd = client_sock_fd;
    mp4info_arr[pos]->clientinfo[0].sig_0 = sig_0;
    mp4info_arr[pos]->clientinfo[0].playflag = 1;
    mp4info_arr[pos]->clientinfo[0].IDRflag = -1;
    mp4info_arr[pos]->count++;

    mp4info_arr[pos]->clientinfo[0].rtp_packet = (struct RtpPacket *)malloc(5000000);
    rtpHeaderInit(mp4info_arr[pos]->clientinfo[0].rtp_packet, 0, 0, 0, RTP_VESION, RTP_PAYLOAD_TYPE_H264, 0, 0, 0, 0x88923423);

    mp4info_arr[pos]->clientinfo[0].tcp_header = malloc(sizeof(struct rtp_tcp_header));
    // 添加到epoll中
    // fcntl(mp4info_arr[pos]->clientinfo[0].sd, F_SETFL, O_NONBLOCK);//设为非阻塞如果失败之后不重复发送，会导致数据帧丢失，所以这里暂时不设置非阻塞
    eventSet(&mp4info_arr[pos]->clientinfo[0], mp4info_arr[pos]->clientinfo[0].sd, sendData, &mp4info_arr[pos]->clientinfo[0]);
    eventAdd(mp4info_arr[pos]->epfd, EPOLLOUT, &mp4info_arr[pos]->clientinfo[0]);
    return 0;
}
/*删除mp4回放任务*/
void del1Mp4Info(int pos)
{
    if (mp4info_arr[pos] == NULL) {
        return;
    }
    for (int i = 0; i < CLIENTMAX; i++) {
        if (mp4info_arr[pos]->clientinfo[i].sd >= 0) {
            eventDel(mp4info_arr[pos]->epfd, &mp4info_arr[pos]->clientinfo[i]);
            close(mp4info_arr[pos]->clientinfo[i].sd);
        }

        if (mp4info_arr[pos]->clientinfo[i].rtp_packet != NULL) {
            free(mp4info_arr[pos]->clientinfo[i].rtp_packet);
            mp4info_arr[pos]->clientinfo[i].rtp_packet = NULL;
        }
        if (mp4info_arr[pos]->clientinfo[i].tcp_header != NULL) {
            free(mp4info_arr[pos]->clientinfo[i].tcp_header);
            mp4info_arr[pos]->clientinfo[i].tcp_header = NULL;
        }
    }
    close(mp4info_arr[pos]->epfd);
    avformat_close_input(&mp4info_arr[pos]->context);
    av_packet_free(&mp4info_arr[pos]->av_pkt);
    pthread_mutex_destroy(&mp4info_arr[pos]->mut);
    if (mp4info_arr[pos]->filename != NULL) {
        free(mp4info_arr[pos]->filename);
        mp4info_arr[pos]->filename = NULL;
    }
    if (mp4info_arr[pos]->buffer != NULL) {
        if (mp4info_arr[pos]->buffer->buf != NULL) {
            free(mp4info_arr[pos]->buffer->buf);
            mp4info_arr[pos]->buffer->buf = NULL;
        }
        free(mp4info_arr[pos]->buffer);
        mp4info_arr[pos]->buffer = NULL;
    }
    if (mp4info_arr[pos]->frame != NULL) {
        if (mp4info_arr[pos]->frame->frame != NULL) {
            free(mp4info_arr[pos]->frame->frame);
            mp4info_arr[pos]->frame->frame = NULL;
        }
        free(mp4info_arr[pos]->frame);
    }
    free(mp4info_arr[pos]);
    mp4info_arr[pos] = NULL;
}
/*添加一个客户端*/
int addClient(char *path_filename, int client_sock_fd, int sig_0)
{
    int istrueflag = 0;
    int pos = 0;
    int min_free_pos = FILEMAX;
    int fps;
    /*查看mp4info中是否已经存在该文件*/
    while (pthread_mutex_trylock(&mut_mp4) != 0) // 如果没有加上锁就发送心跳包
    {
        struct rtp_tcp_header tcp_header;
        sendHeartBeatPkt(client_sock_fd, &tcp_header, sig_0);
    }
    for (int i = 0; i < FILEMAX; i++) {
        if (mp4info_arr[i] == NULL) {
            if (i < min_free_pos)
                min_free_pos = i;
            continue;
        }
        while (pthread_mutex_trylock(&mp4info_arr[i]->mut) != 0) // 如果没有加上锁就发送心跳包
        {
            struct rtp_tcp_header tcp_header;
            sendHeartBeatPkt(client_sock_fd, &tcp_header, sig_0);
        }
        if (!strncmp(mp4info_arr[i]->filename, path_filename, strlen(path_filename))) // 客户端请求回放的文件已经在回放任务队列里面
        {
            istrueflag = 1;
            pos = i;
            printf("find file in mp4info[%d]\n", i);
            // 把客户端添加到这个视频文件回放任务中的客户端队列中
            int posofclient = get_free_clientinfo(pos);
            if (posofclient < 0) // 超过一个视频文件回放任务所支持的最大客户端个数，一个回访任务最多支持FILEMAX(1024)个客户端
            {
                printf("over client maxnum\n");
                pthread_mutex_unlock(&mp4info_arr[pos]->mut);
                pthread_mutex_unlock(&mut_mp4);
                return -1;
            }
            mp4info_arr[pos]->clientinfo[posofclient].sd = client_sock_fd;
            mp4info_arr[pos]->clientinfo[posofclient].sig_0 = sig_0;
            mp4info_arr[pos]->clientinfo[posofclient].playflag = 1;
            mp4info_arr[pos]->clientinfo[posofclient].IDRflag = -1;
            // 客户端socket添加到epoll中
            // fcntl(mp4info_arr[pos]->clientinfo[posofclient].sd, F_SETFL, O_NONBLOCK);
            eventSet(&mp4info_arr[pos]->clientinfo[posofclient], mp4info_arr[pos]->clientinfo[posofclient].sd, sendData, &mp4info_arr[pos]->clientinfo[posofclient]);
            eventAdd(mp4info_arr[pos]->epfd, EPOLLOUT, &mp4info_arr[pos]->clientinfo[posofclient]);

            mp4info_arr[pos]->count++;
            printf("thr_mp4:%s client_count:%d clien_pos:%d\n", mp4info_arr[pos]->filename, mp4info_arr[pos]->count, posofclient);
            mp4info_arr[pos]->clientinfo[posofclient].rtp_packet = (struct RtpPacket *)malloc(5000000);

            rtpHeaderInit(mp4info_arr[pos]->clientinfo[posofclient].rtp_packet, 0, 0, 0, RTP_VESION, RTP_PAYLOAD_TYPE_H264, 0, 0, 0, 0x88923423);

            mp4info_arr[pos]->clientinfo[posofclient].tcp_header = malloc(sizeof(struct rtp_tcp_header));

            pthread_mutex_unlock(&mp4info_arr[i]->mut);
            break;
        }
        pthread_mutex_unlock(&mp4info_arr[i]->mut);
    }
    if (istrueflag == 0) // 这个视频文件还没有任务就创建一个线程并初始化客户端信息
    {
        int ret = add1Mp4Info(min_free_pos, path_filename, client_sock_fd, sig_0);
        if (ret < 0) {
            return -1;
        }
        pthread_t tid;
        pthread_create(&tid, NULL, parseMp4SendDataThd, (void *)min_free_pos);
        pthread_detach(tid);
        printf("thr_mp4:%s client_count:%d clien_pos:%d\n", mp4info_arr[min_free_pos]->filename, mp4info_arr[min_free_pos]->count, 0);
    }
    pthread_mutex_unlock(&mut_mp4);
    return 1;
}

/*处理客户端rtsp请求，只支持RTP OVER TCP数据传输模式*/
void *doClientThd(void *arg)
{
    signal(SIGINT, sig_handler);
    signal(SIGQUIT, sig_handler);
    signal(SIGKILL, sig_handler);
    struct thd_arg_st *arg_thd = (struct thd_arg_st *)arg;

    int client_sock_fd = arg_thd->client_sock_fd;
    char *client_ip = arg_thd->client_ip;
    int client_port = arg_thd->client_port;
    char method[40];
    char url[100];
    char url_tmp[100];
    char filename[100];
    char url_setup[100];
    char local_ip[40];
    char version[40];
    int cseq;
    char *buf_ptr;
    char *buf_tmp;
    char *recv_buf = malloc(BUF_MAX_SIZE);
    char *send_buf = malloc(BUF_MAX_SIZE);
    char line[400];
    // rtp_over_tcp变量
    int sig_0;
    int sig_1;
    int ture_of_rtp_tcp = 0;

    char ch = '/';
    int findflag = 0;

    char path[100];
    memcpy(path, mp4Dir, strlen(mp4Dir));
    path[strlen(mp4Dir)] = '\0';

    char path_tmp[100];
    memcpy(path_tmp, mp4Dir, strlen(mp4Dir));
    path_tmp[strlen(mp4Dir)] = '\0';

    int fd;
    while (1) {
        int recv_len;

        recv_len = recv(client_sock_fd, recv_buf, BUF_MAX_SIZE, 0);
        if (recv_len <= 0)
            goto out;

        recv_buf[recv_len] = '\0';
        /*
        printf("---------------C->S--------------\n");
        printf("%s", recv_buf);
        */
        buf_ptr = getLineFromBuf(recv_buf, line);
        buf_tmp = buf_ptr;

        if (sscanf(line, "%s %s %s\r\n", method, url, version) != 3) {
            printf("parse err\n");
            goto out;
        }

        /*解析序列号*/
        while (1) {
            buf_ptr = getLineFromBuf(buf_ptr, line);
            if (!strncmp(line, "CSeq:", strlen("CSeq:"))) {
                if (sscanf(line, "CSeq: %d\r\n", &cseq) != 1) {
                    printf("parse err\n");
                    goto out;
                }
                break;
            }
        }

        /* 如果是SETUP,需要解析是否为RTP_OVER_TCP模式 */

        if (!strcmp(method, "SETUP")) {
            strcpy(url_setup, url);
            while (1) {
                buf_tmp = getLineFromBuf(buf_tmp, line);

                if (!buf_tmp) {
                    break;
                }

                if (!strncmp(line, "Transport: RTP/AVP/TCP", strlen("Transport: RTP/AVP/TCP"))) {

                    sscanf(line, "Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d\r\n",
                           &sig_0, &sig_1);
                    ture_of_rtp_tcp = 1;
                    break;
                }
                if (!strncmp(line, "Transport: RTP/AVP/UDP", strlen("Transport: RTP/AVP/UDP"))) {

                    printf("only rtp over tcp\n");
                    goto out;
                }
                if (!strncmp(line, "Transport: RTP/AVP", strlen("Transport: RTP/AVP"))) {

                    printf("only rtp over tcp\n");
                    goto out;
                }
            }
        }
        if (!strcmp(method, "OPTIONS")) {
            char *p = strrchr(url, ch);
            memcpy(filename, p + 1, strlen(p));

            char *tmp = strcat(path_tmp, filename);
            findflag = 1;
            printf("OPTIONS:%s\n", tmp);
            fd = open(tmp, O_RDONLY);
            if (fd < 0) // 请求的资源不存在返回404并关闭客户端文件描述符
            {
                perror("failed");
                handleCmd_404(send_buf, cseq);
                send(client_sock_fd, send_buf, strlen(send_buf), 0);
                // printf("%s", send_buf);
                goto out;
            } else {
                close(fd);
                if (handleCmd_OPTIONS(send_buf, cseq)) {
                    printf("failed to handle options\n");
                    goto out;
                }
            }

        } else if (!strcmp(method, "DESCRIBE")) {
            if (findflag == 0) {
                char *p = strrchr(url, ch);
                memcpy(filename, p + 1, strlen(p));

                char *tmp = strcat(path_tmp, filename);
                printf("DESCRIBE:%s\n", tmp);
                fd = open(tmp, O_RDONLY);
                if (fd < 0) // 请求的资源不存在返回404并关闭客户端文件描述符
                {
                    perror("failed");
                    handleCmd_404(send_buf, cseq);
                    send(client_sock_fd, send_buf, strlen(send_buf), 0);
                    // printf("%s", send_buf);
                    goto out;
                }
                close(fd);
                findflag = 1;
            }

            if (handleCmd_DESCRIBE(send_buf, cseq, url)) {
                printf("failed to handle describe\n");
                goto out;
            }
        } else if (!strcmp(method, "SETUP") && ture_of_rtp_tcp == 0) // RTP_OVER_UDP
        {
            /*
            sscanf(url, "rtsp://%[^:]:", local_ip);
            if(handleCmd_SETUP(send_buf, cseq, local_ip))
            {
                printf("failed to handle setup\n");
                goto out;
            }
            */
        } else if (!strcmp(method, "SETUP") && ture_of_rtp_tcp == 1) // RTP_OVER_TCP
        {
            sscanf(url, "rtsp://%[^:]:", local_ip);
            if (handleCmd_SETUP_TCP(send_buf, cseq, local_ip, client_ip, sig_0)) {
                printf("failed to handle setup\n");
                goto out;
            }
        } else if (!strcmp(method, "PLAY") && ture_of_rtp_tcp == 0) // RTP_OVER_UDP
        {
            /*
            if(handleCmd_PLAY(send_buf, cseq))
            {
                printf("failed to handle play\n");
                goto out;
            }
            */
        } else if (!strcmp(method, "PLAY") && ture_of_rtp_tcp == 1) // RTP_OVER_TCP
        {
            if (handleCmd_PLAY_TCP(send_buf, cseq, url_setup)) {
                printf("failed to handle play\n");
                goto out;
            }
        } else {
            goto out;
        }
        /*
        printf("---------------S->C--------------\n");
        printf("%s", send_buf);
        */

        send(client_sock_fd, send_buf, strlen(send_buf), 0);

        if (!strcmp(method, "PLAY") && ture_of_rtp_tcp == 1) // RTP_OVER_TCP
        {
            struct timeval time_pre, time_now;
            gettimeofday(&time_pre, NULL);

            char *tmp = strcat(path, filename);
            printf("%s\n", tmp);
            int ret = addClient(tmp, client_sock_fd, sig_0);
            if (ret < 0)
                goto out;
            pthread_mutex_lock(&mut_clientcount);
            sum_client++;
            printf("sum_client:%d\n", sum_client);
            pthread_mutex_unlock(&mut_clientcount);

            gettimeofday(&time_now, NULL);
            int time_handle = 1000 * (time_now.tv_sec - time_pre.tv_sec) + (time_now.tv_usec - time_pre.tv_usec) / 1000;
            printf("timeuse:%dms\n", time_handle);
            goto over;
        }
    }
out:
    close(client_sock_fd);
    free(recv_buf);
    free(send_buf);
    free(arg);
    return;
over:

    free(recv_buf);
    free(send_buf);
    free(arg);
    return;
}

void moduleInit()
{
    // do nothing
}

void moduleDel()
{
    pthread_mutex_lock(&mut_mp4);
    for (int i = 0; i < FILEMAX; i++) {
        del1Mp4Info(i);
    }
    pthread_mutex_unlock(&mut_mp4);
    pthread_mutex_destroy(&mut_mp4);
    pthread_mutex_destroy(&mut_clientcount);
}

int main(int argc, char *argv[])
{
    int server_sock_fd;
    int ret;
    signal(SIGINT, sig_handler);
    signal(SIGQUIT, sig_handler);
    signal(SIGKILL, sig_handler);

    signal(SIGPIPE, SIG_IGN);
    signal(SIGFPE, SIG_IGN);

    server_sock_fd = createTcpSocket();
    if (server_sock_fd < 0) {
        printf("failed to create tcp socket\n");
        return -1;
    }

    ret = bindSocketAddr(server_sock_fd, SERVER_IP, SERVER_PORT);
    if (ret < 0) {
        printf("failed to bind addr\n");
        return -1;
    }

    ret = listen(server_sock_fd, 100);
    if (ret < 0) {
        printf("failed to listen\n");
        return -1;
    }

    moduleInit();

    printf("rtsp://%s:%d/filename\n", SERVER_IP, SERVER_PORT);
    while (1) {
        int client_sock_fd;
        char client_ip[40];
        int client_port;
        pthread_t tid;

        client_sock_fd = acceptClient(server_sock_fd, client_ip, &client_port);
        if (client_sock_fd < 0) {
            printf("failed to accept client\n");
            return -1;
        }
		printf("##############################################\n");
        printf("accept client --> client ip:%s,client port:%d\n", client_ip, client_port);
        struct thd_arg_st *arg;
        arg = malloc(sizeof(struct thd_arg_st));
        memcpy(arg->client_ip, client_ip, strlen(client_ip));
        arg->client_port = client_port;
        arg->client_sock_fd = client_sock_fd;

        ret = pthread_create(&tid, NULL, doClientThd, (void *)arg);
        if (ret < 0) {
            perror("doClientThd pthread_create()");
        }
        pthread_detach(tid);
    }
    moduleDel();
    close(server_sock_fd);
    return 0;
}
