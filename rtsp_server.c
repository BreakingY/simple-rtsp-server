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
char *mp4Dir = "mp4path/\0"; // MP4文件存放位置
/*doClientThd线程参数*/
struct thd_arg_st
{
    int client_sock_fd;
    char client_ip[30];
    int client_port;
};

int create_rtp_sockets(int *fd1, int *fd2, int *port1, int *port2)
{
    struct sockaddr_in addr;
    int port = 0;

    *fd1 = socket(AF_INET, SOCK_DGRAM, 0);
    if (*fd1 < 0)
    {
        perror("socket");
        return -1;
    }

    *fd2 = socket(AF_INET, SOCK_DGRAM, 0);
    if (*fd2 < 0)
    {
        perror("socket");
        close(*fd1);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    for (port = 1024; port <= 65535; port += 2)
    {
        addr.sin_port = htons(port);
        if (bind(*fd1, (struct sockaddr *)&addr, sizeof(addr)) == 0)
        {
            addr.sin_port = htons(port + 1);
            if (bind(*fd2, (struct sockaddr *)&addr, sizeof(addr)) == 0)
            {
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

/*处理客户端rtsp请求*/
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
    char filename[100];
    char url_setup[100];
    char track[1024];
    char url_play[1024];
    char local_ip[40];
    char version[40];
    int cseq;
    char *buf_ptr;
    char *buf_tmp;
    char *recv_buf = malloc(BUF_MAX_SIZE);
    char *send_buf = malloc(BUF_MAX_SIZE);
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

    char path[100];
    memcpy(path, mp4Dir, strlen(mp4Dir));
    path[strlen(mp4Dir)] = '\0';

    char path_tmp[100];
    memcpy(path_tmp, mp4Dir, strlen(mp4Dir));
    path_tmp[strlen(mp4Dir)] = '\0';

    int fd;
    while (1)
    {
        int recv_len;

        recv_len = recv(client_sock_fd, recv_buf, BUF_MAX_SIZE, 0);
        if (recv_len <= 0)
            goto out;

        recv_buf[recv_len] = '\0';

        printf("---------------C->S--------------\n");
        printf("%s", recv_buf);

        buf_ptr = getLineFromBuf(recv_buf, line);
        buf_tmp = buf_ptr;

        if (sscanf(line, "%s %s %s\r\n", method, url, version) != 3)
        {
            printf("parse err\n");
            goto out;
        }

        /*解析序列号*/
        while (1)
        {
            buf_ptr = getLineFromBuf(buf_ptr, line);
            if (!strncmp(line, "CSeq:", strlen("CSeq:")))
            {
                if (sscanf(line, "CSeq: %d\r\n", &cseq) != 1)
                {
                    printf("parse err\n");
                    goto out;
                }
                break;
            }
        }

        /* 如果是SETUP,需要解析是RTP_OVER_TCP还是RTP_OVER_UDP模式 */
        if (!strcmp(method, "SETUP"))
        {
            memset(url_setup, 0, sizeof(url_setup));
            memset(track, 0, sizeof(track));
            strcpy(url_setup, url);
            char *p = strrchr(url_setup, ch);
            memcpy(track, p + 1, strlen(p)); // video-track0 audio -track1
            while (1)
            {
                buf_tmp = getLineFromBuf(buf_tmp, line);

                if (!buf_tmp)
                {
                    break;
                }

                if (!strncmp(line, "Transport: RTP/AVP/TCP", strlen("Transport: RTP/AVP/TCP")))
                {

                    if (memcmp(track, "track0", 6) == 0)
                    {
                        sscanf(line, "Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d\r\n", &sig_0, &sig_1);
                    }
                    else
                    {
                        sscanf(line, "Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d\r\n", &sig_2, &sig_3);
                    }

                    ture_of_rtp_tcp = 1;
                    break;
                }
                if (!strncmp(line, "Transport: RTP/AVP/UDP", strlen("Transport: RTP/AVP/UDP")))
                {
                    if (memcmp(track, "track0", 6) == 0)
                    {
                        sscanf(line, "Transport: RTP/AVP/UDP;unicast;client_port=%d-%d\r\n", &client_rtp_port, &client_rtcp_port);
                    }
                    else
                    {
                        sscanf(line, "Transport: RTP/AVP/UDP;unicast;client_port=%d-%d\r\n", &client_rtp_port_1, &client_rtcp_port_1);
                    }
                    break;
                }
                if (!strncmp(line, "Transport: RTP/AVP", strlen("Transport: RTP/AVP")))
                {

                    if (memcmp(track, "track0", 6) == 0)
                    {
                        sscanf(line, "Transport: RTP/AVP;unicast;client_port=%d-%d\r\n", &client_rtp_port, &client_rtcp_port);
                    }
                    else
                    {
                        sscanf(line, "Transport: RTP/AVP;unicast;client_port=%d-%d\r\n", &client_rtp_port_1, &client_rtcp_port_1);
                    }
                    break;
                }
            }
        }
        if (!strcmp(method, "OPTIONS"))
        {
            char *p = strrchr(url, ch);
            memcpy(filename, p + 1, strlen(p));

            char *tmp = strcat(path_tmp, filename);
            findflag = 1;
            fd = open(tmp, O_RDONLY);
            if (fd < 0) // 请求的资源不存在返回404并关闭客户端文件描述符
            {
                perror("failed");
                handleCmd_404(send_buf, cseq);
                send(client_sock_fd, send_buf, strlen(send_buf), 0);
                goto out;
            }
            else
            {
                close(fd);
                if (handleCmd_OPTIONS(send_buf, cseq))
                {
                    printf("failed to handle options\n");
                    goto out;
                }
            }
        }
        else if (!strcmp(method, "DESCRIBE"))
        {
            if (findflag == 0)
            {
                char *p = strrchr(url, ch);
                memcpy(filename, p + 1, strlen(p));

                char *tmp = strcat(path_tmp, filename);
                fd = open(tmp, O_RDONLY);
                if (fd < 0) // 请求的资源不存在返回404并关闭客户端文件描述符
                {
                    perror("failed");
                    handleCmd_404(send_buf, cseq);
                    send(client_sock_fd, send_buf, strlen(send_buf), 0);
                    goto out;
                }
                close(fd);
                findflag = 1;
            }
            char sdp[1024];
            char localIp[100];
            sscanf(url, "rtsp://%[^:]:", localIp);
            int ret = generateSDP(path_tmp, localIp, sdp, sizeof(sdp));
            if (ret < 0)
            { // mp4文件有问题，或者视频不是H264/H265,音频不是AAC
                handleCmd_500(send_buf, cseq);
                send(client_sock_fd, send_buf, strlen(send_buf), 0);
                goto out;
            }
            if (handleCmd_DESCRIBE(send_buf, cseq, url, sdp))
            {
                printf("failed to handle describe\n");
                goto out;
            }
        }
        else if (!strcmp(method, "SETUP") && ture_of_rtp_tcp == 0) // RTP_OVER_UDP
        {
            sscanf(url, "rtsp://%[^:]:", local_ip);
            if (memcmp(track, "track0", 6) == 0)
            {
                create_rtp_sockets(&server_udp_socket_rtp_fd, &server_udp_socket_rtcp_fd, &server_rtp_port, &server_rtp_port);
                handleCmd_SETUP_UDP(send_buf, cseq, client_rtp_port, server_rtp_port);
            }
            else
            {
                create_rtp_sockets(&server_udp_socket_rtp_1_fd, &server_udp_socket_rtcp_1_fd, &server_rtp_port_1, &server_rtp_port_1);
                handleCmd_SETUP_UDP(send_buf, cseq, client_rtp_port_1, server_rtp_port_1);
            }
        }
        else if (!strcmp(method, "SETUP") && ture_of_rtp_tcp == 1) // RTP_OVER_TCP
        {
            sscanf(url, "rtsp://%[^:]:", local_ip);
            if (memcmp(track, "track0", 6) == 0)
            {
                handleCmd_SETUP_TCP(send_buf, cseq, local_ip, client_ip, sig_0);
            }
            else
            {
                handleCmd_SETUP_TCP(send_buf, cseq, local_ip, client_ip, sig_2);
            }
        }
        else if (!strcmp(method, "PLAY"))
        {
            memset(url_play, 0, sizeof(url_play));
            memset(track, 0, sizeof(track));
            strcpy(url_play, url);
            if (handleCmd_PLAY(send_buf, cseq, url_play))
            {
                printf("failed to handle play\n");
                goto out;
            }
        }
        else
        {
            goto out;
        }

        printf("---------------S->C--------------\n");
        printf("%s", send_buf);

        send(client_sock_fd, send_buf, strlen(send_buf), 0);

        if (!strcmp(method, "PLAY"))
        {
            struct timeval time_pre, time_now;
            gettimeofday(&time_pre, NULL);

            char *tmp = strcat(path, filename);
            int ret = addClient(tmp, client_sock_fd, sig_0, sig_2, ture_of_rtp_tcp, client_ip, client_rtp_port, client_rtp_port_1,
                                server_udp_socket_rtp_fd, server_udp_socket_rtcp_fd, server_udp_socket_rtp_1_fd, server_udp_socket_rtcp_1_fd);
            if (ret < 0)
                goto out;
            int sum = getClientNum();

            gettimeofday(&time_now, NULL);
            int time_handle = 1000 * (time_now.tv_sec - time_pre.tv_sec) + (time_now.tv_usec - time_pre.tv_usec) / 1000;
            printf("timeuse:%dms sum_client:%d\n\n", time_handle, sum);
            goto over;
        }
    }
out:
    close(client_sock_fd);
    free(recv_buf);
    free(send_buf);
    free(arg);
    return;
over:

    free(recv_buf);
    free(send_buf);
    free(arg);
    return;
}

int main(int argc, char *argv[])
{
    int server_sock_fd;
    int ret;
    signal(SIGINT, sig_handler);
    signal(SIGQUIT, sig_handler);
    signal(SIGKILL, sig_handler);

    signal(SIGPIPE, SIG_IGN);
    signal(SIGFPE, SIG_IGN);

    server_sock_fd = createTcpSocket();
    if (server_sock_fd < 0)
    {
        printf("failed to create tcp socket\n");
        return -1;
    }

    ret = bindSocketAddr(server_sock_fd, SERVER_IP, SERVER_PORT);
    if (ret < 0)
    {
        printf("failed to bind addr\n");
        return -1;
    }

    ret = listen(server_sock_fd, 100);
    if (ret < 0)
    {
        printf("failed to listen\n");
        return -1;
    }

    moduleInit();

    printf("rtsp://%s:%d/filename\n", SERVER_IP, SERVER_PORT);
    while (1)
    {
        int client_sock_fd;
        char client_ip[40];
        int client_port;
        pthread_t tid;

        client_sock_fd = acceptClient(server_sock_fd, client_ip, &client_port);
        if (client_sock_fd < 0)
        {
            printf("failed to accept client\n");
            return -1;
        }
        printf("###########accept client --> client_sock_fd:%d client ip:%s,client port:%d###########\n", client_sock_fd, client_ip, client_port);
        struct thd_arg_st *arg;
        arg = malloc(sizeof(struct thd_arg_st));
        memcpy(arg->client_ip, client_ip, strlen(client_ip));
        arg->client_port = client_port;
        arg->client_sock_fd = client_sock_fd;

        ret = pthread_create(&tid, NULL, doClientThd, (void *)arg);
        if (ret < 0)
        {
            perror("doClientThd pthread_create()");
        }
        pthread_detach(tid);
    }
    moduleDel();
    close(server_sock_fd);
    return 0;
}
