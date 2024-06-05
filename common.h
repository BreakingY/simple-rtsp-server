#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdint.h>

#define RTP_VESION 2 // 版本

#define RTP_PAYLOAD_TYPE_H26X 96 // 媒体类型-视频
#define RTP_PAYLOAD_TYPE_AAC 97  // 媒体类型-音频
#define RTP_PAYLOAD_TYPE_PCMA 8  // 媒体类型-音频

#define RTP_HEADER_SIZE 12 // 头部大小

#define SERVER_IP "0.0.0.0"
#define SERVER_PORT 8554

#define PTK_RTP_TCP_MAX 1400 // rtp over tcp分包

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
/*
*版本号（V）：2比特，用来标志使用的RTP版本。

*填充位（P）：1比特，如果该位置位，则该RTP包的尾部就包含附加的填充字节。

*扩展位（X）：1比特，如果该位置位的话，RTP固定头部后面就跟有一个扩展头部。

*CSRC计数器（CC）：4比特，含有固定头部后面跟着的CSRC的数目。

*标记位（M）：1比特,该位的解释由配置文档（Profile）来承担.

*载荷类型（PT）：7比特，标识了RTP载荷的类型。

*序列号（SN）：16比特，发送方在每发送完一个RTP包后就将该域的值增加1，接收方可以由该域检测包的丢失及恢复包序列。序列号的初始值是随机的。

*时间戳：32比特，记录了该包中数据的第一个字节的采样时刻。在一次会话开始时，时间戳初始化成一个初始值。即使在没有信号发送时，时间戳的数值也要随时间而不断地增加（时间在流逝嘛）。时间戳是去除抖动和实现同步不可缺少的。

*同步源标识符(SSRC)：32比特，同步源就是指RTP包流的来源。在同一个RTP会话中不能有两个相同的SSRC值。该标识符是随机选取的 RFC1889推荐了MD5随机算法。

*贡献源列表（CSRC List）：0～15项，每项32比特，用来标志对一个RTP混合器产生的新包有贡献的所有RTP包的源。由混合器将这些有贡献的SSRC标识符插入表中。SSRC标识符都被列出来，以便接收端能正确指出交谈双方的身份
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
enum VIDEOTYPE
{
    H264 = 1,
    H265,
};
enum AUDIOTYPE
{
    AAC = 1,
    PCMA,
};
typedef struct
{
    int has_audio;
    int has_video;
    int is_video_h26x;
    enum VIDEOTYPE video_type;
    enum AUDIOTYPE audio_type;
    int is_audio_aac_pcma;
    int audio_sample_rate;
    int audio_channels;
    // uint8_t *vps;
    // int vps_size;
    // uint8_t *sps;
    // int sps_size;
    // uint8_t *pps;
    // int pps_size;
} MediaInfo;
int createTcpSocket();
int createUdpSocket();
int bindSocketAddr(int sockfd, const char *ip, int port);
int acceptClient(int sockfd, char *ip, int *port);
void rtpHeaderInit(struct RtpPacket *rtpPacket, uint8_t csrcLen, uint8_t extension,
                   uint8_t padding, uint8_t version, uint8_t payloadType, uint8_t marker,
                   uint16_t seq, uint32_t timestamp, uint32_t ssrc);
char *getLineFromBuf(char *buf, char *line);

int handleCmd_OPTIONS(char *result, int cseq);
int handleCmd_DESCRIBE(char *result, int cseq, char *url, char *sdp);
int handleCmd_SETUP_TCP(char *result, int cseq, char *localIp, char *clientip, int sig_0);
int handleCmd_SETUP_UDP(char *result, int cseq, int clientRtpPort, int serverRtpPort);
int handleCmd_PLAY(char *result, int cseq, char *url);
int handleCmd_404(char *result, int cseq);
int handleCmd_500(char *result, int cseq);

int check_media_info(const char *filename, MediaInfo *info);
void free_media_info(MediaInfo *info);
int generateSDP(char *file, char *localIp, char *buffer, int buffer_len);

#endif
