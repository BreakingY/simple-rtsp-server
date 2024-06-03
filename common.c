#include "common.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libavutil/time.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
int createTcpSocket()
{
    int sockfd;
    int on = 1;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        return -1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));
    return sockfd;
}
int createUdpSocket()
{
    int sockfd;
    int on = 1;
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        return -1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));
    return sockfd;
}

int bindSocketAddr(int sockfd, const char *ip, int port)
{
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr)) < 0)
        return -1;
    return 0;
}
int acceptClient(int sockfd, char *ip, int *port)
{
    int clientfd;
    socklen_t len = 0;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    len = sizeof(addr);
    clientfd = accept(sockfd, (struct sockaddr *)&addr, &len);
    if (clientfd < 0)
    {
        printf("accept err:%s\n", strerror(errno));
        return -1;
    }
    strcpy(ip, inet_ntoa(addr.sin_addr));
    *port = ntohs(addr.sin_port);

    return clientfd;
}
void rtpHeaderInit(struct RtpPacket *rtpPacket, uint8_t csrcLen, uint8_t extension,
                   uint8_t padding, uint8_t version, uint8_t payloadType, uint8_t marker,
                   uint16_t seq, uint32_t timestamp, uint32_t ssrc)
{
    rtpPacket->rtpHeader.csrcLen = csrcLen;
    rtpPacket->rtpHeader.extension = extension;
    rtpPacket->rtpHeader.padding = padding;
    rtpPacket->rtpHeader.version = version;
    rtpPacket->rtpHeader.payloadType = payloadType;
    rtpPacket->rtpHeader.marker = marker;
    rtpPacket->rtpHeader.seq = seq;
    rtpPacket->rtpHeader.timestamp = timestamp;
    rtpPacket->rtpHeader.ssrc = ssrc;
}

char *getLineFromBuf(char *buf, char *line)
{
    while (*buf != '\n')
    {
        *line = *buf;
        line++;
        buf++;
    }

    *line = '\n';
    ++line;
    *line = '\0';

    ++buf;
    return buf;
}
int handleCmd_OPTIONS(char *result, int cseq)
{
    sprintf(result, "RTSP/1.0 200 OK\r\n"
                    "CSeq: %d\r\n"
                    "Public: OPTIONS, DESCRIBE, SETUP, PLAY\r\n"
                    "\r\n",
            cseq);

    return 0;
}
int handleCmd_DESCRIBE(char *result, int cseq, char *url, char *sdp)
{
    sprintf(result, "RTSP/1.0 200 OK\r\nCSeq: %d\r\n"
                    "Content-Base: %s\r\n"
                    "Content-type: application/sdp\r\n"
                    "Content-length: %d\r\n\r\n"
                    "%s",
            cseq,
            url,
            strlen(sdp),
            sdp);

    return 0;
}
int handleCmd_SETUP_TCP(char *result, int cseq, char *localIp, char *clientIp, int sig_0)
{
    sprintf(result, "RTSP/1.0 200 OK\r\n"
                    "CSeq: %d\r\n"
                    "Transport: RTP/AVP/TCP;unicast;destination=%s;source=%s;interleaved=%d-%d\r\n"
                    "Session: 66334873\r\n"
                    "\r\n",
            cseq,
            clientIp,
            localIp,
            sig_0,
            sig_0 + 1);

    return 0;
}
int handleCmd_SETUP_UDP(char *result, int cseq, int clientRtpPort, int serverRtpPort)
{
    sprintf(result, "RTSP/1.0 200 OK\r\n"
                    "CSeq: %d\r\n"
                    "Transport: RTP/AVP;unicast;client_port=%d-%d;server_port=%d-%d\r\n"
                    "Session: 66334873\r\n"
                    "\r\n",
            cseq,
            clientRtpPort,
            clientRtpPort + 1,
            serverRtpPort,
            serverRtpPort + 1);

    return 0;
}
int handleCmd_PLAY(char *result, int cseq, char *url_setup)
{
    sprintf(result, "RTSP/1.0 200 OK\r\n"
                    "CSeq: %d\r\n"
                    "Range: npt=0.000-\r\n"
                    "Session: 66334873; timeout=60\r\n"
                    "RTP-Info: url=%s;seq=0;rtptime=0\r\n\r\n",
            cseq, url_setup);

    return 0;
}
int handleCmd_404(char *result, int cseq)
{
    sprintf(result, "RTSP/1.0 404 NOT FOUND\r\n"
                    "CSeq: %d\r\n"
                    "\r\n",
            cseq);

    return 0;
}
int handleCmd_500(char *result, int cseq)
{
    sprintf(result, "RTSP/1.0 500 SERVER ERROR\r\n"
                    "CSeq: %d\r\n"
                    "\r\n",
            cseq);

    return 0;
}
int check_media_info(const char *filename, MediaInfo *info)
{
    AVFormatContext *format_ctx = NULL;
    int ret;

    if ((ret = avformat_open_input(&format_ctx, filename, NULL, NULL)) < 0)
    {
        fprintf(stderr, "Could not open source file %s\n", filename);
        return ret;
    }
    if ((ret = avformat_find_stream_info(format_ctx, NULL)) < 0)
    {
        fprintf(stderr, "Could not find stream information\n");
        avformat_close_input(&format_ctx);
        return ret;
    }

    info->has_audio = 0;
    info->has_video = 0;
    info->is_video_h26x = 0;
    info->is_audio_aac = 0;
    info->audio_sample_rate = 0;
    info->audio_channels = 0;
    info->sps = NULL;
    info->pps = NULL;
    info->sps_size = 0;
    info->pps_size = 0;

    for (unsigned int i = 0; i < format_ctx->nb_streams; i++)
    {
        AVStream *stream = format_ctx->streams[i];
        AVCodecParameters *codecpar = stream->codecpar;

        if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            info->has_video = 1;
            if (codecpar->codec_id == AV_CODEC_ID_H264 || codecpar->codec_id == AV_CODEC_ID_H265 || codecpar->codec_id == AV_CODEC_ID_HEVC)
            {
                info->is_video_h26x = 1;
                if (codecpar->codec_id == AV_CODEC_ID_H264)
                    info->type = H264;
                else
                    info->type = H265;
            }
        }
        else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            info->has_audio = 1;
            if (codecpar->codec_id == AV_CODEC_ID_AAC)
            {
                info->is_audio_aac = 1;
                info->audio_sample_rate = codecpar->sample_rate;
                info->audio_channels = codecpar->channels;
            }
        }
    }
    avformat_close_input(&format_ctx);

    return 0;
}
void free_media_info(MediaInfo *info)
{
    if (info->sps)
    {
        free(info->sps);
    }
    if (info->pps)
    {
        free(info->pps);
    }
}
int generateSDP(char *file, char *localIp, char *buffer, int buffer_len)
{
    memset(buffer, 0, buffer_len);
    MediaInfo info;
    if (check_media_info(file, &info) != 0)
    {
        printf("server error\n");
        return -1;
    }
    if (info.has_video && !info.is_video_h26x)
    {
        printf("only support h264\n");
        return -1;
    }
    if (info.has_audio && !info.is_audio_aac)
    {
        printf("only support aac\n");
        return -1;
    }
    sprintf(buffer, "v=0\r\n"
                    "o=- 9%ld 1 IN IP4 %s\r\n"
                    "c=IN IP4 %s\r\n"
                    "t=0 0\r\n"
                    "a=control:*\r\n",
            time(NULL), localIp, localIp);
    if (info.has_video)
    {
        sprintf(buffer + strlen(buffer), "m=video 0 RTP/AVP %d\r\n"
                                         "a=rtpmap:%d %s/90000\r\n"
                                         //"a=fmtp:%d profile-level-id=42A01E; packetization-mode=1; sprop-parameter-sets=Z0IACpZTBYmI,aMljiA==\r\n"
                                         "a=fmtp:%d packetization-mode=1\r\n"
                                         "a=control:track0\r\n",
                RTP_PAYLOAD_TYPE_H26X, RTP_PAYLOAD_TYPE_H26X, info.type == H264 ? "H264" : "H265", RTP_PAYLOAD_TYPE_H26X);
    }
    if (info.has_audio)
    {
        sprintf(buffer + strlen(buffer), "m=audio 0 RTP/AVP %d\r\n"
                                         "a=rtpmap:%d MPEG4-GENERIC/%u/%u\r\n"
                                         "a=fmtp:%d profile-level-id=1;mode=AAC-hbr;sizelength=13;indexlength=3;indexdeltalength=3\r\n"
                                         "a=control:track1\r\n",
                RTP_PAYLOAD_TYPE_AAC, RTP_PAYLOAD_TYPE_AAC, info.audio_sample_rate, info.audio_channels, RTP_PAYLOAD_TYPE_AAC);
    }
    free_media_info(&info);
    return 0;
}
