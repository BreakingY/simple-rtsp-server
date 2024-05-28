#ifndef _SESSION_H_
#define _SESSION_H_
#include "aac_rtp.h"
#include "common.h"
#include "h264_rtp.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libavutil/time.h>
#include <malloc.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

void sig_handler(int s);
void moduleInit();
void moduleDel();
/*添加一个客户端*/
int addClient(char *path_filename, int client_sock_fd, int sig_0, int sig_2, int ture_of_tcp, char *client_ip, int client_rtp_port,
              int client_rtp_port_1, int server_udp_socket_rtp, int server_udp_socket_rtcp, int server_udp_socket_rtp_1, int server_udp_socket_rtcp_1);
int getClientNum();
#endif