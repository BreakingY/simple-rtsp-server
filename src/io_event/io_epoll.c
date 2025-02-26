#include "io_event.h"
#if defined(__linux__) || defined(__linux)
#define EVENT_DEBUG
#define EPOLL_MAX   1024
static int epoll_fd;
static int run_flag = 1;
static mthread_mutex_t mut_epoll;
event_callbacks_t event_callbacks = {NULL, NULL, NULL};
void setEventCallback(event_callback_t event_in, event_callback_t event_out, event_callback_t event_close){
    event_callbacks.event_in = event_in;
    event_callbacks.event_out = event_out;
    event_callbacks.event_close = event_close;
    return;
}
int createEvent(){
    epoll_fd = epoll_create(EPOLL_MAX);
    if(epoll_fd < 0){
        printf("create efd in %s err %d\n", __func__, epoll_fd);
        return -1;
    }
    mthread_mutex_init(&mut_epoll, NULL);
    return 0;
}
int closeEvent(){
    if(epoll_fd >= 0){
        close(epoll_fd);
    }
    mthread_mutex_destroy(&mut_epoll);
    return 0;
}
int addEvent(int events, event_data_ptr_t *event_data){
    mthread_mutex_lock(&mut_epoll);
    struct epoll_event epv = {0, {0}};
    epv.data.ptr = event_data;
    event_data->events = events;
    epv.events = events;
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event_data->fd, &epv) < 0){
        printf("addEvent failed [fd=%d]\n", event_data->fd);
        mthread_mutex_unlock(&mut_epoll);
        return -1;
    }
    else{
#ifdef EVENT_DEBUG
        printf("addEvent OK [fd=%d]\n", event_data->fd);
#endif
    }
    mthread_mutex_unlock(&mut_epoll);
    return 0;
}
int delEvent(event_data_ptr_t *event_data){
    mthread_mutex_lock(&mut_epoll);
    struct epoll_event epv = {0, {0}};
    epv.data.ptr = NULL;
    if(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event_data->fd, &epv) < 0){
        printf("eventDel failed [fd=%d]\n", event_data->fd);
        mthread_mutex_unlock(&mut_epoll);
        return -1;
    }
    else{
#ifdef EVENT_DEBUG
        printf("delEvent OK [fd=%d]\n", event_data->fd);
#endif
    }
    mthread_mutex_unlock(&mut_epoll);
    return 0;
}
void *startEventLoop(void *arg){
    while (run_flag == 1){
        struct epoll_event events[EPOLL_MAX];
        mthread_mutex_lock(&mut_epoll);
        int timeout = 10; // ms
        int nfd = epoll_wait(epoll_fd, events, EPOLL_MAX, timeout);
        mthread_mutex_unlock(&mut_epoll);
        if(nfd < 0){
            printf("epoll_wait error, exit\n");
            exit(-1);
        }
        for(int i = 0; i < nfd; i++){
            event_data_ptr_t *event_data = (event_data_ptr_t *)events[i].data.ptr;
            int close_flag = 0;
            if((events[i].events & EPOLLIN)){
                if(event_callbacks.event_in){
                    if(event_callbacks.event_in(event_data) < 0){
                        close_flag = 1;
                    }
                }
            }
            if((events[i].events & EPOLLERR) || (events[i].events & EPOLLRDHUP) || (events[i].events & EPOLLHUP)){
                close_flag = 1;
            }
            else if ((events[i].events & EPOLLOUT)){
                if(event_callbacks.event_out){
                    if(event_callbacks.event_out(event_data) < 0){
                        close_flag = 1;
                    }
                }
            }
            if(close_flag == 1){
                if(event_callbacks.event_close){
                    event_callbacks.event_close(event_data);
                }
            }
        }
        usleep(1000 * 10); // 10ms
    }
    return NULL;
}
void stopEventLoop(){
    run_flag = 0;
    return;
}
#endif