#include "pcma_rtp.h"
static uint32_t getTimestamp(uint32_t sample_rate)
{
    struct timeval tv = {0};
    gettimeofday(&tv, NULL);
    // t(rtsp/rtp时间戳，单位s) =  t(采集时间戳，单位秒)*音视频时钟频率 或者 t(rtsp/rtp时间戳，单位ms)=(t采集时间戳,单位ms)*(时钟频率/1000)
    // 时钟频率是1秒内的频率，比如视频时90000HZ,1ms的话就是90HZ
    // 这种计算方式和ts+=时钟频率/帧率(此时ts需要初始值，一般为0)计算出来的帧之间的时间戳增量是一样 ，但是用系统时间计算rtp的时间能够准确的反应当前帧的采集时间(rtsp/rtp时间基下的时间)
    // clockRate/1000是转换成ms
    uint32_t ts = ((tv.tv_sec * 1000) + ((tv.tv_usec + 500) / 1000)) * sample_rate / 1000; // clockRate/1000;
    return ts;
}
int rtpSendPCMAFrame(int fd, struct rtp_tcp_header *tcp_header, struct RtpPacket *rtp_packet, char *data, int size, uint32_t sample_rate, int channels, int profile, int sig, char *client_ip, int client_rtp_port)
{
    int ret;
    int send_bytes = 0;
    struct sockaddr_in addr;
    if (tcp_header != NULL && sig != -1)
    { // tcp
        tcp_header->magic = '$';
        tcp_header->rtp_len16 = (uint16_t)RTP_HEADER_SIZE + (uint16_t)size;

        tcp_header->rtp_len16 = htons(tcp_header->rtp_len16);
        tcp_header->channel = sig;
        ret = send(fd, tcp_header, sizeof(struct rtp_tcp_header), 0);
        if (ret <= 0)
        {
            return -1;
        }
    }
    else if (client_ip != NULL && client_rtp_port != -1)
    { // udp

        addr.sin_family = AF_INET;
        addr.sin_port = htons(client_rtp_port);
        addr.sin_addr.s_addr = inet_addr(client_ip);
    }
    else
    {
        printf("parameter error\n");
        return -1;
    }
    rtp_packet->rtpHeader.timestamp = getTimestamp(sample_rate);
    send_bytes += ret;
    rtp_packet->rtpHeader.seq = htons(rtp_packet->rtpHeader.seq);
    rtp_packet->rtpHeader.timestamp = htonl(rtp_packet->rtpHeader.timestamp);
    rtp_packet->rtpHeader.ssrc = htonl(rtp_packet->rtpHeader.ssrc);
    rtp_packet->rtpHeader.marker = 1;
    memcpy(rtp_packet->payload, data, size);
    if (tcp_header != NULL && sig != -1)
    { // tcp
        ret = send(fd, rtp_packet, RTP_HEADER_SIZE + size, 0);
    }
    else if (client_ip != NULL && client_rtp_port != -1)
    { // udp
        ret = sendto(fd, (void *)rtp_packet, RTP_HEADER_SIZE + size, 0, (struct sockaddr *)&addr, sizeof(addr));
    }
    if (ret <= 0)
    {
        return -1;
    }
    send_bytes += ret;
    rtp_packet->rtpHeader.seq = ntohs(rtp_packet->rtpHeader.seq);
    rtp_packet->rtpHeader.timestamp = ntohl(rtp_packet->rtpHeader.timestamp);
    rtp_packet->rtpHeader.ssrc = ntohl(rtp_packet->rtpHeader.ssrc);

    rtp_packet->rtpHeader.seq++;
    /*
     * 如果采样频率是44100
     * 一般AAC每个1024个采样为一帧
     * 所以一秒就有 44100 / 1024 = 43帧
     * 时间增量就是 44100 / 43 = 1025
     * 一帧的时间为 1 / 43 = 23ms
     */
    // rtp_packet->rtpHeader.timestamp += 1025;
    return send_bytes;
}
