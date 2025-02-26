#ifndef _RTSP_CLIENT_HANDLE_H_
#define _RTSP_CLIENT_HANDLE_H_
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__) || defined(__linux)
#include <signal.h>
#endif
#include "socket_io.h"
#include "session.h"
#include "rtsp_message.h"
struct thd_arg_st
{
    socket_t client_sock_fd;
    int client_port;
    int auth; // 0 1
    char client_ip[30];
    char user_name[30];
    char password[30];
};
// thread, Handling RTSP client connections
// arg:struct thd_arg_st
void *doClientThd(void *arg);
#endif