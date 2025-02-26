#include "rtsp_message.h"
#include "common.h"

void dumpRequestMessage(struct rtsp_request_message_st *request_message) {
    if(request_message == NULL){
        return;
    }
    printf("method:%s\n", request_message->method);
    printf("url:%s\n", request_message->url);
    printf("rtsp_version:%s\n", request_message->rtsp_version);
    for(int i = 0; i < request_message->kv_nums; i++){
        printf("Key: %.*s\n", request_message->kv[i].k_len, request_message->kv[i].k);
        printf("Value: %.*s\n", request_message->kv[i].v_len, request_message->kv[i].v);
    }
    return;
}

static int parseHttpLine(const char *line, int len, struct k_v_st *kv){
    if(line == NULL || len <= 0 || kv == NULL){
        return -1;
    }

    const char *colon_pos = strchr(line, ':');
    if(colon_pos == NULL){
        return -1;
    }

    int key_len = colon_pos - line;
    const char *value_start = colon_pos + 1;
    while(*value_start == ' '){
        value_start++;
    }
    int value_len = len - (value_start - line);

    if(value_len >= 2 && value_start[value_len - 2] == '\r' && value_start[value_len - 1] == '\n'){
        value_len -= 2;
    }
    if(value_len >= 1 && value_start[value_len - 1] == '\r'){
        value_len -= 1;
    }

    memcpy(kv->k, line, key_len);
    kv->k_len = key_len;
    memcpy(kv->v, value_start, value_len);
    kv->v_len = value_len;

    return 0;
}

int parseRtspRequest(const char *buffer, int len, struct rtsp_request_message_st *request_message){
    if(buffer == NULL || request_message == NULL || len <= 0){
        return -1;
    }
    int used_bytes = 0;
    char *buf_ptr = (char *)buffer;
    char line[1024] = {0};
    int buf_ptr_len = len;
    int ret = 0;
    while(buf_ptr_len > 0){ // Skip the previous message 
        buf_ptr = getLineFromBuf(buf_ptr, buf_ptr_len, line);
        buf_ptr_len -= strlen(line);
        used_bytes += strlen(line);
        if(sscanf(line, "%s %s %s\r\n", request_message->method, request_message->url, request_message->rtsp_version) == 3){ // Skip the previous message 
            break;;
        }
    }
    while(buf_ptr_len > 0){ // Headers
        buf_ptr = getLineFromBuf(buf_ptr, buf_ptr_len, line);
        buf_ptr_len -= strlen(line);
        used_bytes += strlen(line);
        if(request_message->kv_nums >= (sizeof(request_message->kv) / sizeof(request_message->kv[0]))){
            return -1;
        }
        if(parseHttpLine((const char*)line, strlen(line), &request_message->kv[request_message->kv_nums++]) < 0){
            request_message->kv_nums--;
        }
    }

    return used_bytes;
}
char *findValueByKey(struct rtsp_request_message_st *request_message, const char *Key){
    for(int i = 0; i < request_message->kv_nums; i++){
        if(request_message->kv[i].k_len != strlen(Key)){
            continue;
        }
        if(memcmp(request_message->kv[i].k, Key, strlen(Key)) == 0){
            return request_message->kv[i].v;
        }
    }
    return NULL;
}
int handleCmd_General(char *result, int cseq, char *session){
    if(session == NULL){
        sprintf(result, "RTSP/1.0 200 OK\r\n"
            "CSeq: %d\r\n"
            "\r\n",
        cseq);
        return 0;
    }
    sprintf(result, "RTSP/1.0 200 OK\r\n"
        "CSeq: %d\r\n"
        "Session: %s\r\n"
        "\r\n",
    cseq,
    session);
    return 0;
}
int handleCmd_Unauthorized(char *result, int cseq, char *realm, char *nonce){
	sprintf(result, "RTSP/1.0 401 Unauthorized\r\n"
			        "CSeq: %d\r\n"
			        "WWW-Authenticate: Digest realm=\"%s\", nonce=\"%s\"\r\n"
			        "\r\n",
                cseq,
                realm,
                nonce);

	return 0;
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
            (int)strlen(sdp),
            sdp);

    return 0;
}
int handleCmd_SETUP_TCP(char *result, int cseq, char *localIp, char *clientIp, int sig_0, char *session)
{
    sprintf(result, "RTSP/1.0 200 OK\r\n"
                    "CSeq: %d\r\n"
                    "Transport: RTP/AVP/TCP;unicast;destination=%s;source=%s;interleaved=%d-%d\r\n"
                    "Session: %s\r\n"
                    "\r\n",
            cseq,
            clientIp,
            localIp,
            sig_0,
            sig_0 + 1,
            session);

    return 0;
}
int handleCmd_SETUP_UDP(char *result, int cseq, int clientRtpPort, int serverRtpPort, char *session)
{
    sprintf(result, "RTSP/1.0 200 OK\r\n"
                    "CSeq: %d\r\n"
                    "Transport: RTP/AVP;unicast;client_port=%d-%d;server_port=%d-%d\r\n"
                    "Session: %s\r\n"
                    "\r\n",
            cseq,
            clientRtpPort,
            clientRtpPort + 1,
            serverRtpPort,
            serverRtpPort + 1,
            session);

    return 0;
}
int handleCmd_PLAY(char *result, int cseq, char *url_setup, char *session)
{
    sprintf(result, "RTSP/1.0 200 OK\r\n"
                    "CSeq: %d\r\n"
                    "Range: npt=0.000-\r\n"
                    "Session: %s; timeout=60\r\n\r\n",
            cseq,
            session);

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