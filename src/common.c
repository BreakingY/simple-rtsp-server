#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__linux__) || defined(__linux)
#include <sys/time.h>
#elif defined(_WIN32) || defined(_WIN64)
#include <Windows.h>
#endif

#ifdef RTSP_FILE_SERVER
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libavutil/time.h>
#endif

#include "common.h"
#include "md5.h"
void rtpHeaderInit(struct RtpPacket *rtpPacket, uint8_t csrcLen, uint8_t extension,
                   uint8_t padding, uint8_t version, uint8_t payloadType, uint8_t marker,
                   uint16_t seq, uint32_t timestamp, uint32_t ssrc){
    rtpPacket->rtpHeader.csrcLen = csrcLen;
    rtpPacket->rtpHeader.extension = extension;
    rtpPacket->rtpHeader.padding = padding;
    rtpPacket->rtpHeader.version = version;
    rtpPacket->rtpHeader.payloadType = payloadType;
    rtpPacket->rtpHeader.marker = marker;
    rtpPacket->rtpHeader.seq = seq;
    rtpPacket->rtpHeader.timestamp = timestamp;
    rtpPacket->rtpHeader.ssrc = ssrc;
    return;
}

char *getLineFromBuf(char *buf, int len, char *line){
    while(len > 0){
        *line = *buf;
        line++;
        buf++;
        len--;
        if(*buf == '\n'){
            *line = *buf;
            ++line;
            *line = '\0';
            buf++;
            break;
        }
    }
    return buf;
}
static char *extractValue(const char *source, const char *key){
    const char *start = strstr(source, key);
    if (!start) {
        return NULL;
    }
    start += strlen(key) + 2; // skip key="

    const char *end = strchr(start, '"');
    if (!end) {
        return NULL;
    }

    size_t len = end - start;
    char *value = (char *)malloc(len + 1);
    if (!value) {
        return NULL;
    }

    strncpy(value, start, len);
    value[len] = '\0';
    return value;
}

AuthorizationInfo *findAuthorization(const char *request){
    const char *auth_start = strstr(request, "Authorization: ");
    if (!auth_start) {
        return NULL;
    }

    auth_start += strlen("Authorization: ");
    const char *auth_end = strchr(auth_start, '\r');
    if (!auth_end) {
        auth_end = strchr(auth_start, '\n');
    }
    if (!auth_end) {
        return NULL;
    }

    char *auth_value = (char *)malloc(auth_end - auth_start + 1);
    if (!auth_value) {
        return NULL;
    }
    strncpy(auth_value, auth_start, auth_end - auth_start);
    auth_value[auth_end - auth_start] = '\0';

    AuthorizationInfo *auth_info = (AuthorizationInfo *)malloc(sizeof(AuthorizationInfo));
    if (!auth_info) {
        free(auth_value);
        return NULL;
    }

    auth_info->username = extractValue(auth_value, "username");
    auth_info->realm = extractValue(auth_value, "realm");
    auth_info->nonce = extractValue(auth_value, "nonce");
    auth_info->uri = extractValue(auth_value, "uri");
    auth_info->response = extractValue(auth_value, "response");

    free(auth_value);
    return auth_info;
}
AuthorizationInfo* findAuthorizationByValue(const char *auth_value){
    if(auth_value == NULL){
        return NULL;
    }
    AuthorizationInfo *auth_info = (AuthorizationInfo *)malloc(sizeof(AuthorizationInfo));
    if (!auth_info) {
        return NULL;
    }
    auth_info->username = extractValue(auth_value, "username");
    auth_info->realm = extractValue(auth_value, "realm");
    auth_info->nonce = extractValue(auth_value, "nonce");
    auth_info->uri = extractValue(auth_value, "uri");
    auth_info->response = extractValue(auth_value, "response");
    return auth_info;
}
void freeAuthorizationInfo(AuthorizationInfo *auth_info){
    if (auth_info) {
        free(auth_info->username);
        free(auth_info->realm);
        free(auth_info->nonce);
        free(auth_info->uri);
        free(auth_info->response);
        free(auth_info);
    }
    return;
}
static void generateRandomString(char *buf, int length){
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (size_t i = 0; i < length; i++) {
        buf[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    return;
}
void generateNonce(char *nonce, int length){
    if (length < 1) {
        nonce[0] = '\0';
        return;
    }
    memset(nonce, 0, length);
    srand((unsigned int)time(NULL));

    char random_string[128] = {0};
    generateRandomString(random_string, sizeof(random_string));

    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "%ld", (long)time(NULL));

    char combined[256] = {0};
    snprintf(combined, sizeof(combined), "%s%s", random_string, timestamp);

    MD5_CTX md5;
    unsigned char decrypt[16];
    MD5Init(&md5);
    MD5Update(&md5, combined, strlen(combined));
    MD5Final(&md5, decrypt);
    for(int i = 0; i < 16; i++) {
        snprintf(&(nonce[i * 2]), 3, "%02x", decrypt[i]);
    }
    return;
}
void generateSessionId(char *session_id, size_t size){
    if (size < 9) {
        return;
    }
    time_t timestamp = time(NULL);
    srand((unsigned int)timestamp);
    int random_part = rand() % 1000000;
    snprintf(session_id, size, "%02ld%06d", timestamp % 100, random_part);
    return;
}
int authorizationVerify(char *username, char *password, char *realm, char *nonce, char *uri, char * method, char *response){
    // md5(username:realm:password)
    unsigned char res1[16];
    char res1_hex[33] = {0};
    char buffer1[256] = {0};
    sprintf(buffer1,"%s:%s:%s", username, realm, password);
    MD5_CTX md5_1;
    MD5Init(&md5_1);
    MD5Update(&md5_1, buffer1, strlen(buffer1));
    MD5Final(&md5_1, res1);
    for(int i = 0; i < 16; i++) {
        snprintf(&(res1_hex[i * 2]), 3, "%02x", res1[i]);
    }
    // md5(public_method:url)
    unsigned char res2[16];
    char res2_hex[33] = {0};
    char buffer2[256] = {0};
    sprintf(buffer2,"%s:%s", method, uri);
    MD5_CTX md5_2;
    MD5Init(&md5_2);
    MD5Update(&md5_2, buffer2, strlen(buffer2));
    MD5Final(&md5_2, res2);
    for(int i = 0; i < 16; i++) {
        snprintf(&(res2_hex[i * 2]), 3, "%02x", res2[i]);
    }
    // md5( md5(username:realm:password):nonce:md5(public_method:url) )
    unsigned char res[16];
    char res_hex[33] = {0};
    char buffer[512] = {0};
    sprintf(buffer,"%s:%s:%s", res1_hex, nonce, res2_hex);
    MD5_CTX md5;
    MD5Init(&md5);
    MD5Update(&md5, buffer, strlen(buffer));
    MD5Final(&md5, res);
    for(int i = 0; i < 16; i++) {
        snprintf(&(res_hex[i * 2]), 3, "%02x", res[i]);
    }
    // printf("res:%s response:%s\n", res_hex, response);
    if(strcmp(res_hex, response) == 0){
        return 0;
    }
    return -1;
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
    // AAC HE V2 = AAC LC + SBR + PS
    // AAV HE = AAC LC + SBR
    // So whether it's AAC-HEv2 or AAC-HE, they are both AAC-LC
    switch(aactype){
        case 0:
        case 2:
        case 3:
            return aactype + 1;
        case 1:
        case 4:
        case 28:
            return 2;
        default:
            return 2;

    }
    return 2;
}

static int get_sample_rate_index(int freq, int aactype){

    int i = 0;
    int freq_arr[13] = {
        96000, 88200, 64000, 48000, 44100, 32000,
        24000, 22050, 16000, 12000, 11025, 8000, 7350
    };

    // If it is AAC HEv2 or AAC HE, the frequency is halved
    if(aactype == 28 || aactype == 4){
        freq /= 2;
    }

    for(i = 0; i < 13; i++){
        if(freq == freq_arr[i]){
            return i;
        }
    }
    return 4; // The default is 44100
}

static int get_channel_config(int channels, int aactype){
    // If the number of AAC HEv2 channels is halved
    if(aactype == 28){
        return (channels / 2);
    }
    return channels;
}
#ifdef RTSP_FILE_SERVER
int checkMediaInfo(const char *filename, MediaInfo *info)
{
    AVFormatContext *format_ctx = NULL;
    int ret;

    if((ret = avformat_open_input(&format_ctx, filename, NULL, NULL)) < 0){
        fprintf(stderr, "Could not open source file %s\n", filename);
        return ret;
    }
    if((ret = avformat_find_stream_info(format_ctx, NULL)) < 0){
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

    for(unsigned int i = 0; i < format_ctx->nb_streams; i++){
        AVStream *stream = format_ctx->streams[i];
        AVCodecParameters *codecpar = stream->codecpar;

        if(codecpar->codec_type == AVMEDIA_TYPE_VIDEO){
            info->has_video = 1;
            if(codecpar->codec_id == AV_CODEC_ID_H264 || codecpar->codec_id == AV_CODEC_ID_H265 || codecpar->codec_id == AV_CODEC_ID_HEVC){
                info->is_video_h26x = 1;
                if (codecpar->codec_id == AV_CODEC_ID_H264)
                    info->video_type = VIDEO_H264;
                else
                    info->video_type = VIDEO_H265;
            }
        }
        else if(codecpar->codec_type == AVMEDIA_TYPE_AUDIO){
            info->has_audio = 1;
            if(codecpar->codec_id == AV_CODEC_ID_AAC || codecpar->codec_id == AV_CODEC_ID_PCM_ALAW){
                info->is_audio_aac_pcma = 1;
                info->audio_sample_rate = codecpar->sample_rate;
                info->audio_channels = codecpar->channels;
                info->profile = codecpar->profile;
                if(codecpar->codec_id == AV_CODEC_ID_AAC)
                    info->audio_type = AUDIO_AAC;
                else
                    info->audio_type = AUDIO_PCMA;
            }
        }
    }
    avformat_close_input(&format_ctx);

    return 0;
}
void freeMediaInfo(MediaInfo *info)
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
    return;
}
int generateSDP(char *file, char *localIp, char *buffer, int buffer_len)
{
    memset(buffer, 0, buffer_len);
    MediaInfo info;
    if(checkMediaInfo(file, &info) != 0){
        printf("server error\n");
        freeMediaInfo(&info);
        return -1;
    }
    if(info.has_video && !info.is_video_h26x){
        printf("only support h264 h265\n");
        freeMediaInfo(&info);
        return -1;
    }
    if(info.has_audio && !info.is_audio_aac_pcma){
        printf("only support aac pcma\n");
        freeMediaInfo(&info);
        return -1;
    }
    sprintf(buffer, "v=0\r\n"
                    "o=- 9%ld 1 IN IP4 %s\r\n"
                    "c=IN IP4 %s\r\n"
                    "t=0 0\r\n"
                    "a=control:*\r\n",
            time(NULL), localIp, localIp);
    if(info.has_video){
        sprintf(buffer + strlen(buffer), "m=video 0 RTP/AVP %d\r\n"
                                         "a=rtpmap:%d %s/90000\r\n"
                                         //"a=fmtp:%d profile-level-id=42A01E; packetization-mode=1; sprop-parameter-sets=Z0IACpZTBYmI,aMljiA==\r\n"
                                         "a=fmtp:%d packetization-mode=1\r\n"
                                         "a=control:track0\r\n",
                RTP_PAYLOAD_TYPE_H26X, RTP_PAYLOAD_TYPE_H26X, info.video_type == VIDEO_H264 ? "H264" : "H265", RTP_PAYLOAD_TYPE_H26X);
    }
    if(info.has_audio){
        if (info.audio_type == AUDIO_AAC){
            char config[10] = {0};
            int index = get_sample_rate_index(info.audio_sample_rate, info.profile);
            sprintf(config, "%02x%02x", (uint8_t)((get_audio_obj_type(info.profile)) << 3)|(index >> 1), (uint8_t)((index << 7)|(info.audio_channels << 3)));
            sprintf(buffer + strlen(buffer), "m=audio 0 RTP/AVP %d\r\n"
                                             "a=rtpmap:%d MPEG4-GENERIC/%u/%u\r\n"
                                             "a=fmtp:%d streamtype=5;profile-level-id=1;mode=AAC-hbr;config=%04u;sizelength=13;indexlength=3;indexdeltalength=3\r\n"
                                             "a=control:track1\r\n",
                    RTP_PAYLOAD_TYPE_AAC, RTP_PAYLOAD_TYPE_AAC, info.audio_sample_rate, info.audio_channels, RTP_PAYLOAD_TYPE_AAC, atoi(config));
        }
        else{
            sprintf(buffer + strlen(buffer), "m=audio 0 RTP/AVP %d\r\n"
                                             "a=rtpmap:%d PCMA/%u/%u\r\n"
                                             "a=control:track1\r\n",
                    RTP_PAYLOAD_TYPE_PCMA, RTP_PAYLOAD_TYPE_PCMA, info.audio_sample_rate, info.audio_channels);
        }
    }
    freeMediaInfo(&info);
    return 0;
}
#endif
int generateSDPExt(char *localIp, char *buffer, int buffer_len, int video_type, int audio_type, int sample_rate, int profile, int channels)
{
    memset(buffer, 0, buffer_len);
    sprintf(buffer, "v=0\r\n"
                    "o=- 9%ld 1 IN IP4 %s\r\n"
                    "c=IN IP4 %s\r\n"
                    "t=0 0\r\n"
                    "a=control:*\r\n",
            (long int)time(NULL), localIp, localIp);
    if(video_type != VIDEO_NONE){
        sprintf(buffer + strlen(buffer), "m=video 0 RTP/AVP %d\r\n"
                                         "a=rtpmap:%d %s/90000\r\n"
                                         //"a=fmtp:%d profile-level-id=42A01E; packetization-mode=1; sprop-parameter-sets=Z0IACpZTBYmI,aMljiA==\r\n"
                                         "a=fmtp:%d packetization-mode=1\r\n"
                                         "a=control:track0\r\n",
                RTP_PAYLOAD_TYPE_H26X, RTP_PAYLOAD_TYPE_H26X, video_type == VIDEO_H264 ? "H264" : "H265", RTP_PAYLOAD_TYPE_H26X);
    }
    if(audio_type != AUDIO_NONE){
        if (audio_type == AUDIO_AAC){
            char config[10] = {0};
            int index = get_sample_rate_index(sample_rate, profile);
            sprintf(config, "%02x%02x", (uint8_t)((get_audio_obj_type(profile)) << 3)|(index >> 1), (uint8_t)((index << 7)|(channels << 3)));
            sprintf(buffer + strlen(buffer), "m=audio 0 RTP/AVP %d\r\n"
                                             "a=rtpmap:%d MPEG4-GENERIC/%u/%u\r\n"
                                             "a=fmtp:%d streamtype=5;profile-level-id=1;mode=AAC-hbr;config=%04u;sizelength=13;indexlength=3;indexdeltalength=3\r\n"
                                             "a=control:track1\r\n",
                    RTP_PAYLOAD_TYPE_AAC, RTP_PAYLOAD_TYPE_AAC, sample_rate, channels, RTP_PAYLOAD_TYPE_AAC, atoi(config));
        }
        else{
            sprintf(buffer + strlen(buffer), "m=audio 0 RTP/AVP %d\r\n"
                                             "a=rtpmap:%d PCMA/%u/%u\r\n"
                                             "a=control:track1\r\n",
                    RTP_PAYLOAD_TYPE_PCMA, RTP_PAYLOAD_TYPE_PCMA, sample_rate, channels);
        }
    }
    return 0;
}
// aactype = ffmpeg --> AVCodecParameters *codecpar->profile
void adtsHeader(char *adts_header_buffer, int data_len, int aactype, int frequency, int channels){

    int audio_object_type = get_audio_obj_type(aactype);
    int sampling_frequency_index = get_sample_rate_index(frequency, aactype);
    int channel_config = get_channel_config(channels, aactype);

    int adts_len = data_len + 7;

    adts_header_buffer[0] = 0xff;         //syncword:0xfff                          high 8bits
    adts_header_buffer[1] = 0xf0;         //syncword:0xfff                          low 4bits
    adts_header_buffer[1] |= (0 << 3);    //MPEG Version:0 for MPEG-4,1 for MPEG-2  1bit
    adts_header_buffer[1] |= (0 << 1);    //Layer:0                                 2bits
    adts_header_buffer[1] |= 1;           //protection absent:1                     1bit

    adts_header_buffer[2] = (audio_object_type - 1)<<6;            //profile:audio_object_type - 1                      2bits
    adts_header_buffer[2] |= (sampling_frequency_index & 0x0f)<<2; //sampling frequency index:sampling_frequency_index  4bits
    adts_header_buffer[2] |= (0 << 1);                             //private bit:0                                      1bit
    adts_header_buffer[2] |= (channel_config & 0x04)>>2;           //channel configuration:channel_config               high 1bit

    adts_header_buffer[3] = (channel_config & 0x03)<<6;     //channel configuration:channel_config      low 2bits
    adts_header_buffer[3] |= (0 << 5);                      //original：0                               1bit
    adts_header_buffer[3] |= (0 << 4);                      //home：0                                   1bit
    adts_header_buffer[3] |= (0 << 3);                      //copyright id bit：0                       1bit
    adts_header_buffer[3] |= (0 << 2);                      //copyright id start：0                     1bit
    adts_header_buffer[3] |= ((adts_len & 0x1800) >> 11);           //frame length：value   high 2bits

    adts_header_buffer[4] = (uint8_t)((adts_len & 0x7f8) >> 3);     //frame length:value    middle 8bits
    adts_header_buffer[5] = (uint8_t)((adts_len & 0x7) << 5);       //frame length:value    low 3bits
    adts_header_buffer[5] |= 0x1f;                                 //buffer fullness:0x7ff high 5bits
    adts_header_buffer[6] = 0xfc;
    return;
}
// t (rtsp/rtp timestamp, in seconds)=t (collection timestamp, in seconds) * audio and video clock frequency 
// or t (rtsp/rtp timestamp, in milliseconds)=(collection timestamp, in milliseconds) * (clock frequency/1000)
uint32_t getTimestamp(uint32_t sample_rate){
    uint32_t ts;
#if defined(__linux__) || defined(__linux)
    struct timeval tv = {0};
    gettimeofday(&tv, NULL);
    ts = ((tv.tv_sec * 1000) + ((tv.tv_usec + 500) / 1000)) * sample_rate / 1000; // clockRate/1000;
#elif defined(_WIN32) || defined(_WIN64)
    FILETIME ft;
    SYSTEMTIME st;
    uint64_t epoch_time = 0;
    GetSystemTime(&st);
    SystemTimeToFileTime(&st, &ft);
    epoch_time = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    uint64_t milliseconds = epoch_time / 10000;
    ts = (uint32_t)((milliseconds + 500) * sample_rate / 1000);
#endif
    return ts;
}
