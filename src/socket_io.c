#include "socket_io.h"
int socketInit(){
#if defined(__linux__) || defined(__linux)
    return 0;
#elif defined(_WIN32) || defined(_WIN64)
    WSADATA wsadata;
    return WSAStartup(MAKEWORD(2, 2), &wsadata);
#endif
}
int socketDestroy(){
#if defined(__linux__) || defined(__linux)
    return 0;
#elif defined(_WIN32) || defined(_WIN64)
    return WSACleanup();
#endif
}
socket_t createTcpSocket()
{
    socket_t sockfd;
    int on = 1;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if ((sockfd == INVALID_SOCKET) || (sockfd < 0))
        return INVALID_SOCKET;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));
    return sockfd;
}
socket_t createUdpSocket()
{
    socket_t sockfd;
    int on = 1;
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if ((sockfd == INVALID_SOCKET) || (sockfd < 0))
        return INVALID_SOCKET;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));
    return sockfd;
}
int closeSocket(socket_t sockfd){
    if((sockfd == INVALID_SOCKET) || (sockfd < 0)){
        return -1;
    }
#if defined(__linux__) || defined(__linux)
    return close(sockfd);
#elif defined(_WIN32) || defined(_WIN64)
    return closesocket(sockfd);
#endif
    return -1;
}
void setNonBlock(socket_t fd){
#if defined(__linux) || defined(__linux__)
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#elif defined(_WIN32) || defined(_WIN64)
    unsigned long on = 1;
    ioctlsocket(fd, FIONBIO, &on);
#endif
}

void setBlock(socket_t fd){
#if defined(__linux) || defined(__linux__)
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags&(~O_NONBLOCK));
#elif defined(_WIN32) || defined(_WIN64)
    unsigned long on = 0;
    ioctlsocket(fd, FIONBIO, &on);
#endif
}
int bindSocketAddr(socket_t sockfd, const char *ip, int port)
{
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
#if defined(__linux__) || defined(__linux)
    addr.sin_addr.s_addr = inet_addr(ip);
#elif defined(_WIN32) || defined(_WIN64)
    addr.sin_addr.S_un.S_addr = inet_addr(ip);
#endif
    return bind(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr));
}
int serverListen(socket_t sockfd, int num){
    return listen(sockfd, num);
}

int connectToServer(socket_t sockfd, const char *ip, int port, int timeout/*ms*/){
    int is_connected = 1;
    if (timeout > 0) {
        setNonBlock(sockfd);
    }
    struct sockaddr_in addr = { 0 };
    socklen_t addrlen = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);

    if(connect(sockfd, (struct sockaddr *)&addr, addrlen) < 0){
        if(timeout > 0){
            is_connected = 0;
            fd_set fd_write;
            FD_ZERO(&fd_write);
            FD_SET(sockfd, &fd_write);
            struct timeval tv = {timeout / 1000, timeout % 1000 * 1000};
            select((int)sockfd + 1, NULL, &fd_write, NULL, &tv);
            if(FD_ISSET(sockfd, &fd_write)){
                is_connected = 1;
            }
            setBlock(sockfd);
        }
        else{
            is_connected = 0;
        }
    }
    if(is_connected == 1){
        return 0;
    }
    return -1;
}
socket_t acceptClient(socket_t sockfd, char *ip, int *port, int timeout/*ms*/)
{
    socket_t clientfd;
    struct sockaddr_in addr;
#if defined(__linux__) || defined(__linux)
    socklen_t len = 0;
#elif defined(_WIN32) || defined(_WIN64)
    int len = 0;
#endif
    fd_set read_fds;
    struct timeval timeout_convert;
    int ret;

    memset(&addr, 0, sizeof(addr));
    len = sizeof(addr);

    timeout_convert.tv_sec = timeout / 1000;
    timeout_convert.tv_usec = (timeout % 1000) * 1000;

    FD_ZERO(&read_fds);
    FD_SET(sockfd, &read_fds);

    ret = select(sockfd + 1, &read_fds, NULL, NULL, &timeout_convert);
    if(ret < 0){
        printf("select err: %d\n", ret);
        return INVALID_SOCKET;
    }
    else if(ret == 0){
        // printf("accept timeout\n");
        return INVALID_SOCKET;
    } 
    else{
        clientfd = accept(sockfd, (struct sockaddr *)&addr, &len);
        if((clientfd == INVALID_SOCKET) || (clientfd < 0)){
            printf("accept err: %d\n", clientfd);
            return INVALID_SOCKET;
        }
        strcpy(ip, inet_ntoa(addr.sin_addr));
        *port = ntohs(addr.sin_port);
        return clientfd;
    }
    return INVALID_SOCKET;
}
int createRtpSockets(socket_t *fd1, socket_t *fd2, int *port1, int *port2)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
#if defined(__linux__) || defined(__linux)
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
#elif defined(_WIN32) || defined(_WIN64)
    addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
#endif
    int port = 0;

    *fd1 = socket(AF_INET, SOCK_DGRAM, 0);
    if((*fd1 < 0) || (*fd1 == INVALID_SOCKET)){
        return -1;
    }

    *fd2 = socket(AF_INET, SOCK_DGRAM, 0);
    if ((*fd2 < 0) || (*fd2 == INVALID_SOCKET)){
        closeSocket(*fd1);
        return -1;
    }
    for (port = 1024; port <= 65535; port += 2){
        addr.sin_port = htons(port);
        if(bind(*fd1, (struct sockaddr *)&addr, sizeof(addr)) == 0){
            addr.sin_port = htons(port + 1);
            if(bind(*fd2, (struct sockaddr *)&addr, sizeof(addr)) == 0){
                *port1 = port;
                *port2 = port + 1;
                return 0;
            }
        }
    }
    closeSocket(*fd1);
    closeSocket(*fd2);
    return -1;
}
int recvWithTimeout(socket_t sockfd, char *buffer, size_t len, int timeout/*ms*/){
    fd_set read_fds;
    struct timeval timeout_convert;
    int ret;
    if(timeout == 0){
        return recv(sockfd, buffer, len, 0);
    }
    timeout_convert.tv_sec = timeout / 1000;
    timeout_convert.tv_usec = (timeout % 1000) * 1000;

    FD_ZERO(&read_fds);
    FD_SET(sockfd, &read_fds);

    ret = select(sockfd + 1, &read_fds, NULL, NULL, &timeout_convert);
    if(ret < 0){
        printf("select err: %d\n", ret);
        return -1;
    }
    else if(ret == 0){
        return 0;
    }
    else{
        return recv(sockfd, buffer, len, 0);
    }
}
int sendWithTimeout(socket_t sockfd, const char *buffer, size_t len, int timeout/*ms*/){
    fd_set write_fds;
    struct timeval timeout_convert;
    int ret;
    if(timeout == 0){
        return send(sockfd, buffer, len, 0);
    }

    timeout_convert.tv_sec = timeout / 1000;
    timeout_convert.tv_usec = (timeout % 1000) * 1000;

    FD_ZERO(&write_fds);
    FD_SET(sockfd, &write_fds);

    ret = select(sockfd + 1, NULL, &write_fds, NULL, &timeout_convert);
    if(ret < 0){
        printf("select err: %d\n", ret);
        return -1;
    }
    else if(ret == 0){
        return 0;
    }
    else{
        return send(sockfd, buffer, len, 0);
    }
}
int sendUDP(socket_t sockfd, const char *message, size_t length, const char *ip, int port, int timeout/*ms*/){
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
#if defined(__linux__) || defined(__linux)
    dest_addr.sin_addr.s_addr = inet_addr(ip);
#elif defined(_WIN32) || defined(_WIN64)
    dest_addr.sin_addr.S_un.S_addr = inet_addr(ip);
#endif
    if(timeout != 0){
        fd_set write_fds;
        struct timeval tv;
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout % 1000) * 1000;

        FD_ZERO(&write_fds);
        FD_SET(sockfd, &write_fds);

        int ret = select(sockfd + 1, NULL, &write_fds, NULL, &tv);
        if(ret <= 0){ 
            return -1;
        }
    }
    ssize_t sent_bytes = sendto(sockfd, message, length, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    return sent_bytes;
}
int recvUDP(socket_t sockfd, char *buffer, size_t buffer_len, char *ip, int *port, int timeout/*ms*/){
    struct sockaddr_in src_addr;
#if defined(__linux__) || defined(__linux)
    socklen_t addr_len = sizeof(src_addr);
#elif defined(_WIN32) || defined(_WIN64)
    int addr_len = sizeof(src_addr);
#endif
    if(timeout != 0){
        fd_set read_fds;
        struct timeval tv;
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout % 1000) * 1000;

        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);

        int ret = select(sockfd + 1, &read_fds, NULL, NULL, &tv);
        if(ret <= 0){
            return -1;
        }
    }
    ssize_t recv_bytes = recvfrom(sockfd, buffer, buffer_len, 0, (struct sockaddr *)&src_addr, &addr_len);
    if(recv_bytes < 0){
        return recv_bytes;
    }
    if(ip){
        const char *ip_str = inet_ntoa(src_addr.sin_addr);
        if(ip_str){
            strncpy(ip, ip_str, INET_ADDRSTRLEN);
        }
    }
    if(port){
        *port = ntohs(src_addr.sin_port);
    }
    return recv_bytes;
}

    
