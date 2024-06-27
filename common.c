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
    info->is_audio_aac_pcma = 0;
    info->audio_sample_rate = 0;
    info->audio_channels = 0;
    // info->vps = NULL;
    // info->sps = NULL;
    // info->pps = NULL;
    // info->vps_size = 0;
    // info->sps_size = 0;
    // info->pps_size = 0;

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
                    info->video_type = H264;
                else
                    info->video_type = H265;
            }
        }
        else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            info->has_audio = 1;
            if (codecpar->codec_id == AV_CODEC_ID_AAC || codecpar->codec_id == AV_CODEC_ID_PCM_ALAW)
            {
                info->is_audio_aac_pcma = 1;
                info->audio_sample_rate = codecpar->sample_rate;
                info->audio_channels = codecpar->channels;
                info->profile = codecpar->profile;
                if(codecpar->codec_id == AV_CODEC_ID_AAC)
                    info->audio_type = AAC;
                else
                    info->audio_type = PCMA;
            }
        }
    }
    avformat_close_input(&format_ctx);

    return 0;
}
void free_media_info(MediaInfo *info)
{
    // if (info->vps)
    // {
    //     free(info->vps);
    // }
    // if (info->sps)
    // {
    //     free(info->sps);
    // }
    // if (info->pps)
    // {
    //     free(info->pps);
    // }
}
/*
#define FF_PROFILE_AAC_MAIN 0
#define FF_PROFILE_AAC_LOW  1
#define FF_PROFILE_AAC_SSR  2
#define FF_PROFILE_AAC_LTP  3
#define FF_PROFILE_AAC_HE   4
#define FF_PROFILE_AAC_HE_V2 28
#define FF_PROFILE_AAC_LD   22
#define FF_PROFILE_AAC_ELD  38
#define FF_PROFILE_MPEG2_AAC_LOW 128
#define FF_PROFILE_MPEG2_AAC_HE  131
*/
static int get_audio_obj_type(int aactype){
    //AAC HE V2 = AAC LC + SBR + PS
    //AAV HE = AAC LC + SBR
    //所以无论是 AAC_HEv2 还是 AAC_HE 都是 AAC_LC
    switch(aactype){
        case 0:
        case 2:
        case 3:
            return aactype+1;
        case 1:
        case 4:
        case 28:
            return 2;
        default:
            return 2;

    }
}

static int get_sample_rate_index(int freq, int aactype){

    int i = 0;
    int freq_arr[13] = {
        96000, 88200, 64000, 48000, 44100, 32000,
        24000, 22050, 16000, 12000, 11025, 8000, 7350
    };

    //如果是 AAC HEv2 或 AAC HE, 则频率减半
    if(aactype == 28 || aactype == 4){
        freq /= 2;
    }

    for(i=0; i< 13; i++){
        if(freq == freq_arr[i]){
            return i;
        }
    }
    return 4;//默认是44100
}

static int get_channel_config(int channels, int aactype){
    //如果是 AAC HEv2 通道数减半
    if(aactype == 28){
        return (channels / 2);
    }
    return channels;
}
int generateSDP(char *file, char *localIp, char *buffer, int buffer_len)
{
    memset(buffer, 0, buffer_len);
    MediaInfo info;
    if (check_media_info(file, &info) != 0)
    {
        printf("server error\n");
        free_media_info(&info);
        return -1;
    }
    if (info.has_video && !info.is_video_h26x)
    {
        printf("only support h264 h265\n");
        free_media_info(&info);
        return -1;
    }
    if (info.has_audio && !info.is_audio_aac_pcma)
    {
        printf("only support aac pcma\n");
        free_media_info(&info);
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
                RTP_PAYLOAD_TYPE_H26X, RTP_PAYLOAD_TYPE_H26X, info.video_type == H264 ? "H264" : "H265", RTP_PAYLOAD_TYPE_H26X);
    }
    if (info.has_audio)
    {
        if (info.audio_type == AAC)
        {
            char config[10] = {0};
            int index = get_sample_rate_index(info.audio_sample_rate, info.profile);
            sprintf(config, "%02x%02x", (uint8_t)((get_audio_obj_type(info.profile)) << 3)|(index >> 1), (uint8_t)((index << 7)|(info.audio_channels << 3)));
            sprintf(buffer + strlen(buffer), "m=audio 0 RTP/AVP %d\r\n"
                                             "a=rtpmap:%d MPEG4-GENERIC/%u/%u\r\n"
                                             "a=fmtp:%d streamtype=5;profile-level-id=1;mode=AAC-hbr;config=%04u;sizelength=13;indexlength=3;indexdeltalength=3\r\n"
                                             "a=control:track1\r\n",
                    RTP_PAYLOAD_TYPE_AAC, RTP_PAYLOAD_TYPE_AAC, info.audio_sample_rate, info.audio_channels, RTP_PAYLOAD_TYPE_AAC, atoi(config));
        }
        else
        {
            sprintf(buffer + strlen(buffer), "m=audio 0 RTP/AVP %d\r\n"
                                             "a=rtpmap:%d PCMA/%u/%u\r\n"
                                             "a=control:track1\r\n",
                    RTP_PAYLOAD_TYPE_PCMA, RTP_PAYLOAD_TYPE_PCMA, info.audio_sample_rate, info.audio_channels);
        }
    }
    free_media_info(&info);
    return 0;
}
// aactype = ffmpeg --> AVCodecParameters *codecpar->profile
void adts_header(char *adts_header_buffer, int data_len, int aactype, int frequency, int channels){

    int audio_object_type = get_audio_obj_type(aactype);
    int sampling_frequency_index = get_sample_rate_index(frequency, aactype);
    int channel_config = get_channel_config(channels, aactype);

    int adts_len = data_len + 7;

    adts_header_buffer[0] = 0xff;         //syncword:0xfff                          高8bits
    adts_header_buffer[1] = 0xf0;         //syncword:0xfff                          低4bits
    adts_header_buffer[1] |= (0 << 3);    //MPEG Version:0 for MPEG-4,1 for MPEG-2  1bit
    adts_header_buffer[1] |= (0 << 1);    //Layer:0                                 2bits
    adts_header_buffer[1] |= 1;           //protection absent:1                     1bit

    adts_header_buffer[2] = (audio_object_type - 1)<<6;            //profile:audio_object_type - 1                      2bits
    adts_header_buffer[2] |= (sampling_frequency_index & 0x0f)<<2; //sampling frequency index:sampling_frequency_index  4bits
    adts_header_buffer[2] |= (0 << 1);                             //private bit:0                                      1bit
    adts_header_buffer[2] |= (channel_config & 0x04)>>2;           //channel configuration:channel_config               高1bit

    adts_header_buffer[3] = (channel_config & 0x03)<<6;     //channel configuration:channel_config      低2bits
    adts_header_buffer[3] |= (0 << 5);                      //original：0                               1bit
    adts_header_buffer[3] |= (0 << 4);                      //home：0                                   1bit
    adts_header_buffer[3] |= (0 << 3);                      //copyright id bit：0                       1bit
    adts_header_buffer[3] |= (0 << 2);                      //copyright id start：0                     1bit
    adts_header_buffer[3] |= ((adts_len & 0x1800) >> 11);           //frame length：value   高2bits

    adts_header_buffer[4] = (uint8_t)((adts_len & 0x7f8) >> 3);     //frame length:value    中间8bits
    adts_header_buffer[5] = (uint8_t)((adts_len & 0x7) << 5);       //frame length:value    低3bits
    adts_header_buffer[5] |= 0x1f;                                 //buffer fullness:0x7ff 高5bits
    adts_header_buffer[6] = 0xfc;
}
