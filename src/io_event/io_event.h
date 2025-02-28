#ifndef _IO_EVENT_H_
#define _IO_EVENT_H_
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <stdint.h>
#if defined(__linux__) || defined(__linux)
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/epoll.h>
#elif defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <windows.h>
#endif

#include "mthread.h"
#include "socket_io.h"
typedef enum
{
    FD_TYPE_TCP,
    FD_TYPE_UDP_RTP,
} fd_type_t;
typedef struct
{
    void *user_data;
    fd_type_t fd_type;
    socket_t fd;
    int events;
} event_data_ptr_t;
enum event_type
{
#if defined(__linux__) || defined(__linux)
    EVENT_NONE   = 0,
	EVENT_IN     = EPOLLIN,
	EVENT_PRI    = EPOLLPRI,		
	EVENT_OUT    = EPOLLOUT,
	EVENT_ERR    = EPOLLERR,
	EVENT_HUP    = EPOLLHUP,
	EVENT_RDHUP  = EPOLLRDHUP
#elif defined(_WIN32) || defined(_WIN64)
    EVENT_NONE   = 0,
    EVENT_IN     = 1,
    EVENT_PRI    = 2,
    EVENT_OUT    = 4,
    EVENT_ERR    = 8,
    EVENT_HUP    = 16,
    EVENT_RDHUP  = 8192

#endif
};
typedef int (*event_callback_t)(event_data_ptr_t *);

typedef struct {
    event_callback_t event_in;
    event_callback_t event_out;
    event_callback_t event_close;
} event_callbacks_t;

int createEvent();
void setEventCallback(event_callback_t event_in, event_callback_t event_out, event_callback_t event_close);
int closeEvent();
int addEvent(int events, event_data_ptr_t *event_data);
int delEvent(event_data_ptr_t *event_data);
void *startEventLoop(void *arg);
void stopEventLoop();

#endif