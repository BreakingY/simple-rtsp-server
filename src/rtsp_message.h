#ifndef _RTSP_MESSAGE_H_
#define _RTSP_MESSAGE_H_
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct k_v_st{
    char k[512];
    int k_len;
    char v[512];
    int v_len;
};
struct rtsp_request_message_st{
    char method[512];
    char url[512];
    char rtsp_version[512]; // RTSP/1.0
    struct k_v_st kv[1024];
    int kv_nums;
};
void dumpRequestMessage(struct rtsp_request_message_st *request_message);
// return used bytes
int parseRtspRequest(const char *buffer, int len, struct rtsp_request_message_st *request_message);
char *findValueByKey(struct rtsp_request_message_st *request_message, const char *Key);
int handleCmd_General(char *result, int cseq, char *session);
int handleCmd_Unauthorized(char *result, int cseq, char *realm, char *nonce);
int handleCmd_OPTIONS(char *result, int cseq);
int handleCmd_DESCRIBE(char *result, int cseq, char *url, char *sdp);
int handleCmd_SETUP_TCP(char *result, int cseq, char *localIp, char *clientip, int sig_0, char *session);
int handleCmd_SETUP_UDP(char *result, int cseq, int clientRtpPort, int serverRtpPort, char *session);
int handleCmd_PLAY(char *result, int cseq, char *url, char *session);
int handleCmd_404(char *result, int cseq);
int handleCmd_500(char *result, int cseq);
#endif