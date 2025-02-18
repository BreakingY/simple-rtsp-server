#include "rtsp_client_handle.h"
#include "common.h"
#include "session.h"
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#define BUF_MAX_SIZE (1024 * 1024)
#define RTSP_DEBUG
static int create_rtp_sockets(int *fd1, int *fd2, int *port1, int *port2)
{
    struct sockaddr_in addr;
    int port = 0;

    *fd1 = socket(AF_INET, SOCK_DGRAM, 0);
    if(*fd1 < 0){
        perror("socket");
        return -1;
    }

    *fd2 = socket(AF_INET, SOCK_DGRAM, 0);
    if (*fd2 < 0){
        perror("socket");
        close(*fd1);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    for (port = 1024; port <= 65535; port += 2){
        addr.sin_port = htons(port);
        if(bind(*fd1, (struct sockaddr *)&addr, sizeof(addr)) == 0){
            addr.sin_port = htons(port + 1);
            if(bind(*fd2, (struct sockaddr *)&addr, sizeof(addr)) == 0){
                *port1 = port;
                *port2 = port + 1;
                return 0;
            }
            close(*fd1);
        }
    }
    close(*fd1);
    close(*fd2);
    return -1;
}
static void generate_session_id(char *session_id, size_t size) {
    if (size < 9) {
        return;
    }
    time_t timestamp = time(NULL);
    srand((unsigned int)timestamp);
    int random_part = rand() % 1000000;
    snprintf(session_id, size, "%02ld%06d", timestamp % 100, random_part);
    return;
}
void *doClientThd(void *arg)
{
    signal(SIGINT, sig_handler);
    signal(SIGQUIT, sig_handler);
    signal(SIGKILL, sig_handler);
    struct thd_arg_st *arg_thd = (struct thd_arg_st *)arg;

    int client_sock_fd = arg_thd->client_sock_fd;
    char *client_ip = arg_thd->client_ip;
    int client_port = arg_thd->client_port;
    char method[40];
    char url[100];
    char url_tmp[100];
    char suffix[100];
    char url_setup[100];
    char track[1024];
    char url_play[1024];
    char local_ip[40];
    char version[40];
    int cseq;
    char *buf_ptr;
    char *buf_tmp;
    char recv_buf[BUF_MAX_SIZE];
    char send_buf[BUF_MAX_SIZE];
    char line[400];
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
    int server_udp_socket_rtp_fd = -1;
    int server_udp_socket_rtcp_fd = -1;
    int server_udp_socket_rtp_1_fd = -1;
    int server_udp_socket_rtcp_1_fd = -1;

    char ch = '/';
    int findflag = 0;

    int ret;
    char *realm = "simple-rtsp-server";
    char nonce[33] = {0};
    generate_nonce(nonce, sizeof(nonce));
    char session_id[512];
    generate_session_id(session_id, sizeof(session_id));
    while(1){
        int recv_len;

        recv_len = recv(client_sock_fd, recv_buf, BUF_MAX_SIZE, 0);
        if (recv_len <= 0)
            goto out;

        recv_buf[recv_len] = '\0';
#ifdef RTSP_DEBUG
        printf("---------------C->S--------------\n");
        printf("%s", recv_buf);
#endif

        buf_ptr = getLineFromBuf(recv_buf, line);
        buf_tmp = buf_ptr;

        if(sscanf(line, "%s %s %s\r\n", method, url, version) != 3){
            printf("parse err\n");
            goto out;
        }

        /*CSeq*/
        while(1){
            buf_ptr = getLineFromBuf(buf_ptr, line);
            if(!strncmp(line, "CSeq:", strlen("CSeq:"))){
                if(sscanf(line, "CSeq: %d\r\n", &cseq) != 1){
                    printf("parse err\n");
                    goto out;
                }
                break;
            }
        }
        if(arg_thd->auth == 1){
            // authorization
            if(!strcmp(method, "SETUP") || !strcmp(method, "DESCRIBE") || !strcmp(method, "PLAY")){
                AuthorizationInfo *auth_info = find_authorization(recv_buf);
                if(auth_info == NULL){
                    handleCmd_Unauthorized(send_buf, cseq, realm, nonce);
#ifdef RTSP_DEBUG
                    printf("---------------S->C--------------\n");
                    printf("%s", send_buf);
#endif
                    send(client_sock_fd, send_buf, strlen(send_buf), 0);
                    continue;
                }
                else{
                    // printf("nonce:%s\n", auth_info->nonce);
                    // printf("realm:%s\n", auth_info->realm);
                    // printf("response:%s\n", auth_info->response);
                    // printf("uri:%s\n", auth_info->uri);
                    // printf("username:%s\n", auth_info->username);
                    int ret = authorization_verify(arg_thd->user_name, arg_thd->password, realm, nonce, auth_info->uri, method, auth_info->response);
                    free_authorization_info(auth_info);
                    if(ret < 0){
                        goto out;
                    }
                }
            }
        }

        /* SETUP:RTP_OVER_TCP or RTP_OVER_UDP */
        if(!strcmp(method, "SETUP")){
            memset(url_setup, 0, sizeof(url_setup));
            memset(track, 0, sizeof(track));
            strcpy(url_setup, url);
            char *p = strrchr(url_setup, ch);
            memcpy(track, p + 1, strlen(p)); // video-track0 audio -track1
            while(1){
                buf_tmp = getLineFromBuf(buf_tmp, line);

                if (!buf_tmp){
                    break;
                }

                if(!strncmp(line, "Transport: RTP/AVP/TCP", strlen("Transport: RTP/AVP/TCP"))){

                    if(memcmp(track, "track0", 6) == 0){
                        sscanf(line, "Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d\r\n", &sig_0, &sig_1);
                    }
                    else{
                        sscanf(line, "Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d\r\n", &sig_2, &sig_3);
                    }

                    ture_of_rtp_tcp = 1;
                    break;
                }
                if(!strncmp(line, "Transport: RTP/AVP/UDP", strlen("Transport: RTP/AVP/UDP"))){
                    if(memcmp(track, "track0", 6) == 0){
                        sscanf(line, "Transport: RTP/AVP/UDP;unicast;client_port=%d-%d\r\n", &client_rtp_port, &client_rtcp_port);
                    }
                    else{
                        sscanf(line, "Transport: RTP/AVP/UDP;unicast;client_port=%d-%d\r\n", &client_rtp_port_1, &client_rtcp_port_1);
                    }
                    break;
                }
                if(!strncmp(line, "Transport: RTP/AVP", strlen("Transport: RTP/AVP"))){

                    if(memcmp(track, "track0", 6) == 0){
                        sscanf(line, "Transport: RTP/AVP;unicast;client_port=%d-%d\r\n", &client_rtp_port, &client_rtcp_port);
                    }
                    else{
                        sscanf(line, "Transport: RTP/AVP;unicast;client_port=%d-%d\r\n", &client_rtp_port_1, &client_rtcp_port_1);
                    }
                    break;
                }
            }
        }
        if(!strcmp(method, "OPTIONS")){
            char *p = strchr(url + strlen("rtsp://"), ch);
            memcpy(suffix, p + 1, strlen(p));
            ret = sessionIsExist(suffix);
            findflag = 1;
            if(ret <= 0){ // The resource does not exist
                printf("The resource does not exist\n");
                handleCmd_404(send_buf, cseq);
                send(client_sock_fd, send_buf, strlen(send_buf), 0);
                goto out;
            }
            else{
                if(handleCmd_OPTIONS(send_buf, cseq)){
                    printf("failed to handle options\n");
                    goto out;
                }
            }
        }
        else if(!strcmp(method, "DESCRIBE")){
            if(findflag == 0){
                char *p = strchr(url + strlen("rtsp://"), ch);
                memcpy(suffix, p + 1, strlen(p));
                ret = sessionIsExist(suffix);
                if (ret <= 0){ // The resource does not exist
                    printf("The resource does not exist\n");
                    handleCmd_404(send_buf, cseq);
                    send(client_sock_fd, send_buf, strlen(send_buf), 0);
                    goto out;
                }
                findflag = 1;
            }
            char sdp[1024];
            char localIp[100];
            sscanf(url, "rtsp://%[^:]:", localIp);
            int ret = sessionGenerateSDP(suffix, localIp, sdp, sizeof(sdp));
            if(ret < 0){ // There is an issue with the mp4 file, or the video is not H264/H265, and the audio is not AAC/PCMA
                handleCmd_500(send_buf, cseq);
                send(client_sock_fd, send_buf, strlen(send_buf), 0);
                goto out;
            }
            if(handleCmd_DESCRIBE(send_buf, cseq, url, sdp)){
                printf("failed to handle describe\n");
                goto out;
            }
        }
        else if(!strcmp(method, "SETUP") && ture_of_rtp_tcp == 0){ // RTP_OVER_UDP
            sscanf(url, "rtsp://%[^:]:", local_ip);
            if(memcmp(track, "track0", 6) == 0){
                create_rtp_sockets(&server_udp_socket_rtp_fd, &server_udp_socket_rtcp_fd, &server_rtp_port, &server_rtp_port);
                handleCmd_SETUP_UDP(send_buf, cseq, client_rtp_port, server_rtp_port, session_id);
            }
            else{
                create_rtp_sockets(&server_udp_socket_rtp_1_fd, &server_udp_socket_rtcp_1_fd, &server_rtp_port_1, &server_rtp_port_1);
                handleCmd_SETUP_UDP(send_buf, cseq, client_rtp_port_1, server_rtp_port_1, session_id);
            }
        }
        else if(!strcmp(method, "SETUP") && ture_of_rtp_tcp == 1){ // RTP_OVER_TCP
            sscanf(url, "rtsp://%[^:]:", local_ip);
            if(memcmp(track, "track0", 6) == 0){
                handleCmd_SETUP_TCP(send_buf, cseq, local_ip, client_ip, sig_0, session_id);
            }
            else{
                handleCmd_SETUP_TCP(send_buf, cseq, local_ip, client_ip, sig_2, session_id);
            }
        }
        else if(!strcmp(method, "PLAY")){
            memset(url_play, 0, sizeof(url_play));
            memset(track, 0, sizeof(track));
            strcpy(url_play, url);
            if (handleCmd_PLAY(send_buf, cseq, url_play, session_id)){
                printf("failed to handle play\n");
                goto out;
            }
        }
        else{
            goto out;
        }
#ifdef RTSP_DEBUG
        printf("---------------S->C--------------\n");
        printf("%s", send_buf);
#endif
        send(client_sock_fd, send_buf, strlen(send_buf), 0);

        if(!strcmp(method, "PLAY")){
            struct timeval time_pre, time_now;
            gettimeofday(&time_pre, NULL);

            int ret = addClient(suffix, client_sock_fd, sig_0, sig_2, ture_of_rtp_tcp, client_ip, client_rtp_port, client_rtp_port_1,
                                server_udp_socket_rtp_fd, server_udp_socket_rtcp_fd, server_udp_socket_rtp_1_fd, server_udp_socket_rtcp_1_fd);
            if (ret < 0)
                goto out;
            int sum = getClientNum();

            gettimeofday(&time_now, NULL);
            int time_handle = 1000 * (time_now.tv_sec - time_pre.tv_sec) + (time_now.tv_usec - time_pre.tv_usec) / 1000;
#ifdef RTSP_DEBUG
            printf("timeuse:%dms sum_client:%d\n\n", time_handle, sum);
#endif
            goto over;
        }
    }
out:
    close(client_sock_fd);
    free(arg);
    return NULL;
over:
    free(arg);
    return NULL;
}