#ifndef _SOCKET_IO_H_
#define _SOCKET_IO_H_
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__) || defined(__linux)
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
#elif defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif

#if defined(__linux__) || defined(__linux)
typedef int socket_t;
#define INVALID_SOCKET -1
#elif defined(_WIN32) || defined(_WIN64)
/*Note that socket_t is unsigned in Windows, so do not compare its size with 0 to determine if it is valid*/
typedef SOCKET socket_t;
#endif

int socketInit();
int socketDestroy();
socket_t createTcpSocket();
socket_t createUdpSocket();
int closeSocket(socket_t sockfd);
void setNonBlock(socket_t fd);
void setBlock(socket_t fd);
int bindSocketAddr(socket_t sockfd, const char *ip, int port);
int serverListen(socket_t sockfd, int num);
int connectToServer(socket_t sockfd, const char *ip, int port, int timeout/*ms*/);
socket_t acceptClient(socket_t sockfd, char *ip, int *port, int timeout/*ms*/);
int createRtpSockets(socket_t *fd1, socket_t *fd2, int *port1, int *port2);
int recvWithTimeout(socket_t sockfd, char *buffer, size_t len, int timeout/*ms*/);
int sendWithTimeout(socket_t sockfd, const char *buffer, size_t len, int timeout/*ms*/);
int sendUDP(socket_t sockfd, const char *message, size_t length, const char *ip, int port, int timeout/*ms*/);
int recvUDP(socket_t sockfd, char *buffer, size_t buffer_len, char *ip, int *port, int timeout/*ms*/);

#endif // _SOCKET_IO_H_
