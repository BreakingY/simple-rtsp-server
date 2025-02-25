#ifndef _IO_EPOLL_H_
#define _IO_EPOLL_H_
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/epoll.h>
typedef enum
{
    FD_TYPE_TCP,
    FD_TYPE_UDP_RTP,
} fd_type_t;
typedef struct
{
    void *user_data;
    fd_type_t fd_type;
    int fd;
} event_data_ptr_t;
enum EventType
{
	EVENT_IN     = EPOLLIN,
	EVENT_PRI    = EPOLLPRI,		
	EVENT_OUT    = EPOLLOUT,
	EVENT_ERR    = EPOLLERR,
	EVENT_HUP    = EPOLLHUP,
	EVENT_RDHUP  = EPOLLRDHUP
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