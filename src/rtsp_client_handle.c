#include "rtsp_client_handle.h"
#define BUF_MAX_SIZE (1024 * 1024)
#define RTSP_DEBUG
void *doClientThd(void *arg)
{
#if defined(__linux__) || defined(__linux)
    signal(SIGINT, sig_handler);
    signal(SIGQUIT, sig_handler);
    signal(SIGKILL, sig_handler);
#endif
    struct thd_arg_st *arg_thd = (struct thd_arg_st *)arg;
    socket_t client_sock_fd = arg_thd->client_sock_fd;
    char *client_ip = arg_thd->client_ip;
    int client_port = arg_thd->client_port;
    char suffix[100] = {0};
    char url_setup[100] = {0};
    char track[1024] = {0};
    char url_play[1024] = {0};
    char local_ip[40] = {0};
    int cseq = 0;
    char recv_buf[BUF_MAX_SIZE] = {0};
    char send_buf[BUF_MAX_SIZE] = {0};
    // rtp_over_tcp
    int sig_0 = -1;
    int sig_1 = -1;
    int sig_2 = -1;
    int sig_3 = -1;
    int ture_of_rtp_tcp = 0;
    // rtp_over_udp
    int client_rtp_port = -1;
    int client_rtcp_port = -1;
    int client_rtp_port_1 = -1;
    int client_rtcp_port_1 = -1;
    int server_rtp_port = -1;
    int server_rtcp_port = -1;
    int server_rtp_port_1 = -1;
    int server_rtcp_port_1 = -1;
    socket_t server_udp_socket_rtp_fd = -1;
    socket_t server_udp_socket_rtcp_fd = -1;
    socket_t server_udp_socket_rtp_1_fd = -1;
    socket_t server_udp_socket_rtcp_1_fd = -1;

    char ch = '/';
    int findflag = 0;

    char *realm = "simple-rtsp-server";
    char nonce[33] = {0};
    char session_id[512] = {0};

    int used_bytes = 0;
    int pos = 0;
    int total_len = 0;
    int ret = 0;
    generate_nonce(nonce, sizeof(nonce));
    generate_session_id(session_id, sizeof(session_id));
    while(1){
        int recv_len = recvWithTimeout(client_sock_fd, recv_buf + pos, BUF_MAX_SIZE - pos, 0);
        if (recv_len <= 0)
            goto out;
        total_len += recv_len;
        recv_buf[total_len] = '\0';
#ifdef RTSP_DEBUG
        printf("---------------C->S--------------\n");
        printf("%s", recv_buf);
#endif
        struct rtsp_request_message_st request_message;
        memset(&request_message, 0, sizeof(struct rtsp_request_message_st));
        int parse_used = parseRtspRequest(recv_buf, total_len, &request_message);
        if(parse_used < 0){
            goto out;
        }
        used_bytes += parse_used;
        // dumpRequestMessage(&request_message);
        char *CSeq = findValueByKey(&request_message, "CSeq");
        if(CSeq == NULL){
            used_bytes -= parse_used;
            goto need_more_data;
        }
        cseq = atoi(CSeq);
        if(arg_thd->auth == 1){
            // authorization
            if(!strcmp(request_message.method, "SETUP") || !strcmp(request_message.method, "DESCRIBE") || !strcmp(request_message.method, "PLAY")){
                char *Authorization = findValueByKey(&request_message, "Authorization");
                if(Authorization == NULL){
                    handleCmd_Unauthorized(send_buf, cseq, realm, nonce);
                    goto need_more_data;
                }
                else{
                    AuthorizationInfo *auth_info = find_authorization_by_value((const char *)Authorization);
                    // printf("nonce:%s\n", auth_info->nonce);
                    // printf("realm:%s\n", auth_info->realm);
                    // printf("response:%s\n", auth_info->response);
                    // printf("uri:%s\n", auth_info->uri);
                    // printf("username:%s\n", auth_info->username);
                    ret = authorization_verify(arg_thd->user_name, arg_thd->password, realm, nonce, auth_info->uri, request_message.method, auth_info->response);
                    free_authorization_info(auth_info);
                    if(ret < 0){
                        handleCmd_Unauthorized(send_buf, cseq, realm, nonce);
                        goto out;
                    }
                }
            }
        }
        /* SETUP:RTP_OVER_TCP or RTP_OVER_UDP */
        if(!strcmp(request_message.method, "SETUP")){
            memset(url_setup, 0, sizeof(url_setup));
            memset(track, 0, sizeof(track));
            strcpy(url_setup, request_message.url);
            char *p = strrchr(url_setup, ch);
            memcpy(track, p + 1, strlen(p)); // video-track0 audio -track1
            char *Transport = findValueByKey(&request_message, "Transport");
            if(Transport == NULL){
                used_bytes -= parse_used;
                goto need_more_data;
            }

            if(!strncmp(Transport, "RTP/AVP/TCP", strlen("RTP/AVP/TCP"))){

                if(memcmp(track, "track0", 6) == 0){
                    sscanf(Transport, "RTP/AVP/TCP;unicast;interleaved=%d-%d\r\n", &sig_0, &sig_1);
                }
                else{
                    sscanf(Transport, "RTP/AVP/TCP;unicast;interleaved=%d-%d\r\n", &sig_2, &sig_3);
                }
                ture_of_rtp_tcp = 1;
            }
            else if(!strncmp(Transport, "RTP/AVP/UDP", strlen("RTP/AVP/UDP"))){
                if(memcmp(track, "track0", 6) == 0){
                    sscanf(Transport, "RTP/AVP/UDP;unicast;client_port=%d-%d\r\n", &client_rtp_port, &client_rtcp_port);
                }
                else{
                    sscanf(Transport, "RTP/AVP/UDP;unicast;client_port=%d-%d\r\n", &client_rtp_port_1, &client_rtcp_port_1);
                }
            }
            else if(!strncmp(Transport, "RTP/AVP", strlen("RTP/AVP"))){

                if(memcmp(track, "track0", 6) == 0){
                    sscanf(Transport, "RTP/AVP;unicast;client_port=%d-%d\r\n", &client_rtp_port, &client_rtcp_port);
                }
                else{
                    sscanf(Transport, "RTP/AVP;unicast;client_port=%d-%d\r\n", &client_rtp_port_1, &client_rtcp_port_1);
                }
            }
        
        }
        if(!strcmp(request_message.method, "OPTIONS")){
            char *p = strchr(request_message.url + strlen("rtsp://"), ch);
            memcpy(suffix, p + 1, strlen(p));
            ret = sessionIsExist(suffix);
            findflag = 1;
            if(ret <= 0){ // The resource does not exist
                printf("The resource does not exist\n");
                handleCmd_404(send_buf, cseq);
                goto out;
            }
            else{
                handleCmd_OPTIONS(send_buf, cseq);
            }
        }
        else if(!strcmp(request_message.method, "DESCRIBE")){
            if(findflag == 0){
                char *p = strchr(request_message.url + strlen("rtsp://"), ch);
                memcpy(suffix, p + 1, strlen(p));
                ret = sessionIsExist(suffix);
                if (ret <= 0){ // The resource does not exist
                    printf("The resource does not exist\n");
                    handleCmd_404(send_buf, cseq);
                    goto out;
                }
                findflag = 1;
            }
            char sdp[1024];
            char localIp[100];
            sscanf(request_message.url, "rtsp://%[^:]:", localIp);
            ret = sessionGenerateSDP(suffix, localIp, sdp, sizeof(sdp));
            if(ret < 0){
                handleCmd_500(send_buf, cseq);
                goto out;
            }
            handleCmd_DESCRIBE(send_buf, cseq, request_message.url, sdp);
        }
        else if(!strcmp(request_message.method, "SETUP") && ture_of_rtp_tcp == 0){ // RTP_OVER_UDP
            sscanf(request_message.url, "rtsp://%[^:]:", local_ip);
            if(memcmp(track, "track0", 6) == 0){
                create_rtp_sockets(&server_udp_socket_rtp_fd, &server_udp_socket_rtcp_fd, &server_rtp_port, &server_rtp_port);
                handleCmd_SETUP_UDP(send_buf, cseq, client_rtp_port, server_rtp_port, session_id);
            }
            else{
                create_rtp_sockets(&server_udp_socket_rtp_1_fd, &server_udp_socket_rtcp_1_fd, &server_rtp_port_1, &server_rtp_port_1);
                handleCmd_SETUP_UDP(send_buf, cseq, client_rtp_port_1, server_rtp_port_1, session_id);
            }
        }
        else if(!strcmp(request_message.method, "SETUP") && ture_of_rtp_tcp == 1){ // RTP_OVER_TCP
            sscanf(request_message.url, "rtsp://%[^:]:", local_ip);
            if(memcmp(track, "track0", 6) == 0){
                handleCmd_SETUP_TCP(send_buf, cseq, local_ip, client_ip, sig_0, session_id);
            }
            else{
                handleCmd_SETUP_TCP(send_buf, cseq, local_ip, client_ip, sig_2, session_id);
            }
        }
        else if(!strcmp(request_message.method, "PLAY")){
            memset(url_play, 0, sizeof(url_play));
            memset(track, 0, sizeof(track));
            strcpy(url_play, request_message.url);
            handleCmd_PLAY(send_buf, cseq, url_play, session_id);
        }
        else{
            goto out;
        }
need_more_data:
        if(strlen(send_buf) > 0){
#ifdef RTSP_DEBUG
            printf("---------------S->C--------------\n");
            printf("%s", send_buf);
#endif
            sendWithTimeout(client_sock_fd, (const char*)send_buf, strlen(send_buf), 0);
            memset(send_buf, 0, sizeof(send_buf));
        }
        memmove(recv_buf, recv_buf + used_bytes, total_len - used_bytes);
        total_len -= used_bytes;
        pos = total_len;
        used_bytes = 0;
        if(!strcmp(request_message.method, "PLAY")){
            ret = addClient(suffix, client_sock_fd, sig_0, sig_2, ture_of_rtp_tcp, client_ip, client_rtp_port, client_rtp_port_1,
                                server_udp_socket_rtp_fd, server_udp_socket_rtcp_fd, server_udp_socket_rtp_1_fd, server_udp_socket_rtcp_1_fd);
            if (ret < 0)
                goto out;
            int sum = getClientNum();
#ifdef RTSP_DEBUG
            printf("sum_client:%d\n\n", sum);
#endif
            goto over;
        }
    }
out:
    if(strlen(send_buf) > 0){
#ifdef RTSP_DEBUG
        printf("---------------S->C--------------\n");
        printf("%s", send_buf);
#endif
        sendWithTimeout(client_sock_fd, (const char*)send_buf, strlen(send_buf), 0);
    }
    closeSocket(client_sock_fd);
    free(arg);
    return NULL;
over:
    free(arg);
    return NULL;
}