#ifndef _SOCKET_IO_H_
#define _SOCKET_IO_H_
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/select.h>

int createTcpSocket();
int createUdpSocket();
int closeSocket(int sockfd);
int bindSocketAddr(int sockfd, const char *ip, int port);
int serverListen(int sockfd, int num);
// return 0:timeout <0:error >0:client socket
int acceptClient(int sockfd, char *ip, int *port, int timeout/*ms*/);
int create_rtp_sockets(int *fd1, int *fd2, int *port1, int *port2);
int recvWithTimeout(int sockfd, char *buffer, size_t len, int timeout/*ms*/);
int sendWithTimeout(int sockfd, const char *buffer, size_t len, int timeout/*ms*/);
int sendUDP(int sockfd, const char *message, size_t length, const char *ip, int port);
int recvUDP(int sockfd, char *buffer, size_t buffer_len, char *ip, int *port);
#endif