#include "h264_rtp.h"
int startCode3(char *buf)
{
    if (buf[0] == 0 && buf[1] == 0 && buf[2] == 1)
        return 1;
    else
        return 0;
}

int startCode4(char *buf)
{
    if (buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] == 1)
        return 1;
    else
        return 0;
}
/* 获取SPS与PPS 并添加起始码*/
static int h264_extradata_to_annexb(const unsigned char *pCodecExtraData, const int codecExtraDataSize, AVPacket *pOutExtradata, int padding)
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
static uint32_t getTimestamp()
{
    struct timeval tv = {0};
    gettimeofday(&tv, NULL);
    // t(rtsp/rtp时间戳，单位s) =  t(采集时间戳，单位秒)*音视频时钟频率 或者 t(rtsp/rtp时间戳，单位ms)=(t采集时间戳,单位ms)*(时钟频率/1000)
    // 时钟频率是1秒内的频率，比如视频时90000HZ,1ms的话就是90HZ
    // 这种计算方式和ts+=时钟频率/帧率(此时ts需要初始值，一般为0)计算出来的帧之间的时间戳增量是一样 ，但是用系统时间计算rtp的时间能够准确的反应当前帧的采集时间(rtsp/rtp时间基下的时间)
    // clockRate/1000是转换成ms
    uint32_t ts = ((tv.tv_sec * 1000) + ((tv.tv_usec + 500) / 1000)) * 90; // 90: clockRate/1000;
    return ts;
}
int rtpSendH264Frame(int sd, struct rtp_tcp_header *tcp_header, struct RtpPacket *rtp_packet, uint8_t *frame, uint32_t frame_size, int fps, int sig_0, char *client_ip, int client_rtp_port)
{
    uint8_t nalu_type; // nalu第一个字节
    int send_bytes = 0;
    int ret;

    nalu_type = frame[0];
    rtp_packet->rtpHeader.marker = 0;
    memset(rtp_packet->payload, 0, strlen(rtp_packet->payload));

    if (frame == NULL) {
        return -1;
    }
    rtp_packet->rtpHeader.timestamp = getTimestamp();
    if (tcp_header != NULL) {
        tcp_header->magic = '$';
        tcp_header->rtp_len16 = 0;
    }
    struct sockaddr_in addr;
    if (client_rtp_port != -1 && client_ip != NULL) {
        addr.sin_family = AF_INET;
        addr.sin_port = htons(client_rtp_port);
        addr.sin_addr.s_addr = inet_addr(client_ip);
    }
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
        if (tcp_header != NULL && sig_0 != -1) { // rtp over tcp
            tcp_header->rtp_len16 = (uint16_t)RTP_HEADER_SIZE + (uint16_t)frame_size;

            tcp_header->rtp_len16 = htons(tcp_header->rtp_len16);
            // 发送RTP_OVER_TCP头部
            tcp_header->channel = sig_0;
            ret = send(sd, tcp_header, sizeof(struct rtp_tcp_header), 0);
            if (ret <= 0) {
                return -1;
            }
            tcp_header->rtp_len16 = ntohs(tcp_header->rtp_len16);
        }

        rtp_packet->rtpHeader.seq = htons(rtp_packet->rtpHeader.seq);
        rtp_packet->rtpHeader.timestamp = htonl(rtp_packet->rtpHeader.timestamp);
        rtp_packet->rtpHeader.ssrc = htonl(rtp_packet->rtpHeader.ssrc);
        memcpy(rtp_packet->payload, frame, frame_size);

        if (tcp_header != NULL && sig_0 != -1) { // rtp over tcp
            ret = send(sd, rtp_packet, RTP_HEADER_SIZE + frame_size, 0);
        } else if (client_rtp_port != -1 && client_ip != NULL) { // rtp over udp
            ret = sendto(sd, (void *)rtp_packet, RTP_HEADER_SIZE + frame_size, 0, (struct sockaddr *)&addr, sizeof(addr));
        } else {
            printf("parameter error\n");
            return -1;
        }
        if (ret <= 0) {
            return -1;
        }
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

            if (tcp_header != NULL && sig_0 != -1) {
                tcp_header->rtp_len16 = (uint16_t)RTP_HEADER_SIZE + (uint16_t)PTK_RTP_TCP_MAX + 2; // 多两个字节的FUin和FUheader
                tcp_header->rtp_len16 = htons(tcp_header->rtp_len16);
                tcp_header->channel = sig_0;
                ret = send(sd, tcp_header, sizeof(struct rtp_tcp_header), 0);
                if (ret <= 0) {
                    return -1;
                }
                tcp_header->rtp_len16 = ntohs(tcp_header->rtp_len16);
            }

            memcpy(rtp_packet->payload + 2, frame + pos, PTK_RTP_TCP_MAX);
            rtp_packet->rtpHeader.seq = htons(rtp_packet->rtpHeader.seq);
            rtp_packet->rtpHeader.timestamp = htonl(rtp_packet->rtpHeader.timestamp);
            rtp_packet->rtpHeader.ssrc = htonl(rtp_packet->rtpHeader.ssrc);

            // 发送RTP数据包
            if (tcp_header != NULL && sig_0 != -1) {
                ret = send(sd, rtp_packet, RTP_HEADER_SIZE + PTK_RTP_TCP_MAX + 2, 0);

            } else if (client_rtp_port != -1 && client_ip != NULL) {
                ret = sendto(sd, (void *)rtp_packet, RTP_HEADER_SIZE + PTK_RTP_TCP_MAX + 2, 0, (struct sockaddr *)&addr, sizeof(addr));
            } else {
                printf("parameter error\n");
                return -1;
            }
            if (ret <= 0) {
                return -1;
            }
            // 还原字节序

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

            if (tcp_header != NULL && sig_0 != -1) {
                tcp_header->rtp_len16 = (uint16_t)RTP_HEADER_SIZE + (uint16_t)remainPktSize + 2; // 多两个字节的FUin和FUheader
                tcp_header->rtp_len16 = htons(tcp_header->rtp_len16);
                // 发送RTP_OVER_TCP头部
                tcp_header->channel = sig_0;
                ret = send(sd, tcp_header, sizeof(struct rtp_tcp_header), 0);
                if (ret <= 0) {
                    return -1;
                }
                tcp_header->rtp_len16 = ntohs(tcp_header->rtp_len16);
            }
            rtp_packet->rtpHeader.seq = htons(rtp_packet->rtpHeader.seq);
            rtp_packet->rtpHeader.timestamp = htonl(rtp_packet->rtpHeader.timestamp);
            rtp_packet->rtpHeader.ssrc = htonl(rtp_packet->rtpHeader.ssrc);
            memcpy(rtp_packet->payload + 2, frame + pos, remainPktSize);

            if (tcp_header != NULL && sig_0 != -1) {
                ret = send(sd, rtp_packet, RTP_HEADER_SIZE + remainPktSize + 2, 0);
            } else if (client_rtp_port != -1 && client_ip != NULL) {
                ret = sendto(sd, (void *)rtp_packet, RTP_HEADER_SIZE + remainPktSize + 2, 0, (struct sockaddr *)&addr, sizeof(addr));
            } else {
                printf("parameter error\n");
                return -1;
            }
            if (ret <= 0) {
                return -1;
            }

            // 还原字节序

            rtp_packet->rtpHeader.seq = ntohs(rtp_packet->rtpHeader.seq);
            rtp_packet->rtpHeader.timestamp = ntohl(rtp_packet->rtpHeader.timestamp);
            rtp_packet->rtpHeader.ssrc = ntohl(rtp_packet->rtpHeader.ssrc);

            rtp_packet->rtpHeader.seq++;
            send_bytes += ret;
        }
        // 所有分包的时间戳都是一样的，时间戳的原则就是，发送一个完整的NALU时间戳才更改，但是不管什么包，序列号持续+1
        // rtp_packet->rtpHeader.timestamp += 90000 / fps;
    }
    return send_bytes;
}