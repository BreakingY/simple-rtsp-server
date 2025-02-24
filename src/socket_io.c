#include "socket_io.h"
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
int closeSocket(int sockfd){
    if(sockfd < 0){
        return -1;
    }
    close(sockfd);
    return 0;
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
#if 0
int acceptClient(int sockfd, char *ip, int *port, int timeout/*ms*/)
{
    int clientfd;
    socklen_t len = 0;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    len = sizeof(addr);
    clientfd = accept(sockfd, (struct sockaddr *)&addr, &len);
    if (clientfd < 0){
        printf("accept err:%s\n", strerror(errno));
        return -1;
    }
    strcpy(ip, inet_ntoa(addr.sin_addr));
    *port = ntohs(addr.sin_port);

    return clientfd;
}
#else
int acceptClient(int sockfd, char *ip, int *port, int timeout/*ms*/)
{
    int clientfd;
    socklen_t len = 0;
    struct sockaddr_in addr;
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
    if (ret < 0) {
        printf("select err: %s\n", strerror(errno));
        return -1;
    } else if (ret == 0) {
        // printf("accept timeout\n");
        return 0;
    } else {
        clientfd = accept(sockfd, (struct sockaddr *)&addr, &len);
        if (clientfd < 0) {
            printf("accept err: %s\n", strerror(errno));
            return -1;
        }
        strcpy(ip, inet_ntoa(addr.sin_addr));
        *port = ntohs(addr.sin_port);
        return clientfd;
    }
    return -1;
}
#endif
int serverListen(int sockfd, int num){
    return listen(sockfd, num);
}
int create_rtp_sockets(int *fd1, int *fd2, int *port1, int *port2)
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
int recvWithTimeout(int sockfd, char *buffer, size_t len, int timeout/*ms*/){
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
        printf("select err: %s\n", strerror(errno));
        return -1;
    }
    else if(ret == 0){
        return 0;
    }
    else{
        return recv(sockfd, buffer, len, 0);
    }
}
int sendWithTimeout(int sockfd, const char *buffer, size_t len, int timeout/*ms*/){
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
        printf("select err: %s\n", strerror(errno));
        return -1;
    }
    else if(ret == 0){
        return 0;
    }
    else{
        return send(sockfd, buffer, len, 0);
    }
}

int sendUDP(int sockfd, const char *message, size_t length, const char *ip, int port){
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    if(inet_pton(AF_INET, ip, &dest_addr.sin_addr) <= 0){
        perror("inet_pton error");
        return -1;
    }
    ssize_t sent_bytes = sendto(sockfd, message, length, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    return sent_bytes;
}

int recvUDP(int sockfd, char *buffer, size_t buffer_len, char *ip, int *port){
    struct sockaddr_in src_addr;
    socklen_t addr_len = sizeof(src_addr);

    ssize_t recv_bytes = recvfrom(sockfd, buffer, buffer_len, 0, (struct sockaddr *)&src_addr, &addr_len);
    if(recv_bytes < 0){
        perror("recvfrom error");
        return recv_bytes;
    }
    if(ip){
        inet_ntop(AF_INET, &src_addr.sin_addr, ip, INET_ADDRSTRLEN);
    }
    if(port){
        *port = ntohs(src_addr.sin_port);
    }
    return recv_bytes;
}
