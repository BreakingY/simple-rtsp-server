#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "common.h"
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
    if (clientfd < 0) {
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
    while (*buf != '\n') {
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
int handleCmd_DESCRIBE(char *result, int cseq, char *url)
{
    char sdp[500];
    char localIp[100];

    sscanf(url, "rtsp://%[^:]:", localIp);

    sprintf(sdp, "v=0\r\n"
                 "o=- 9%ld 1 IN IP4 %s\r\n"
                 "t=0 0\r\n"
                 "a=control:*\r\n"
                 "m=video 0 RTP/AVP 96\r\n"
                 "a=rtpmap:96 H264/90000\r\n"
                 //"a=fmtp:96 profile-level-id=42A01E; packetization-mode=1; sprop-parameter-sets=Z0IACpZTBYmI,aMljiA==\r\n"
                 "a=fmtp:96 packetization-mode=1\r\n"
                 "a=control:track0\r\n",
            time(NULL), localIp);

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
// RTP-Info: url=rtsp://192.168.178.128/track0;seq=61622;rtptime=1335382752
int handleCmd_PLAY_TCP(char *result, int cseq, char *url_setup)
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
