#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdint.h>
#include <string.h>
// #define RTSP_FILE_SERVER
#define RTP_VESION              2

#define RTP_PAYLOAD_TYPE_H26X   96
#define RTP_PAYLOAD_TYPE_AAC    97
#define RTP_PAYLOAD_TYPE_PCMA   8

#define RTP_HEADER_SIZE         12

#define PTK_RTP_TCP_MAX         1400 // rtp over tcp

/*
 *
 *    0                   1                   2                   3
 *    0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |V=2|P|X|  CC   |M|     PT      |       sequence number         |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |                           timestamp                           |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |           synchronization source (SSRC) identifier            |
 *   +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
 *   |            contributing source (CSRC) identifiers             |
 *   :                             ....                              :
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 */
struct RtpHeader
{
    /* byte 0 */
    uint8_t csrcLen : 4;
    uint8_t extension : 1;
    uint8_t padding : 1;
    uint8_t version : 2;

    /* byte 1 */
    uint8_t payloadType : 7;
    uint8_t marker : 1;

    /* bytes 2,3 */
    uint16_t seq;

    /* bytes 4-7 */
    uint32_t timestamp;

    /* bytes 8-11 */
    uint32_t ssrc;
};

struct RtpPacket
{
    struct RtpHeader rtpHeader;
    uint8_t payload[0];
};

struct rtp_tcp_header
{
    uint8_t magic;   // $
    uint8_t channel; // 0-1
    uint16_t rtp_len16;
};
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
typedef struct
{
    int has_audio;
    int has_video;
    int is_video_h26x;
    enum VIDEO_e video_type;
    enum AUDIO_e audio_type;
    int is_audio_aac_pcma;
    int audio_sample_rate;
    int audio_channels;
    int profile;
    // uint8_t *vps;
    // int vps_size;
    // uint8_t *sps;
    // int sps_size;
    // uint8_t *pps;
    // int pps_size;
} MediaInfo;
typedef struct {
    char *username;
    char *realm;
    char *nonce;
    char *uri;
    char *response;
} AuthorizationInfo;
void rtpHeaderInit(struct RtpPacket *rtpPacket, uint8_t csrcLen, uint8_t extension,
                   uint8_t padding, uint8_t version, uint8_t payloadType, uint8_t marker,
                   uint16_t seq, uint32_t timestamp, uint32_t ssrc);
char *getLineFromBuf(char *buf, int len, char *line);

AuthorizationInfo* findAuthorization(const char *request);
AuthorizationInfo* findAuthorizationByValue(const char *auth_value);
void freeAuthorizationInfo(AuthorizationInfo *auth_info);
void generateNonce(char *nonce, int length);
void generateSessionId(char *session_id, size_t size);
int authorizationVerify(char *username, char *password, char *realm, char *nonce, char *uri, char * method, char *response);

#ifdef RTSP_FILE_SERVER
int checkMediaInfo(const char *filename, MediaInfo *info);
void freeMediaInfo(MediaInfo *info);
int generateSDP(char *file, char *localIp, char *buffer, int buffer_len);
#endif
int generateSDPExt(char *localIp, char *buffer, int buffer_len, int video_type, int audio_type, int sample_rate, int profile, int channels);
void adtsHeader(char *adts_header_buffer, int data_len, int aactype, int frequency, int channels);
uint32_t getTimestamp(uint32_t sample_rate);
#endif
