#include "io_event.h"
#if defined(_WIN32) || defined(_WIN64)
#define EVENT_DEBUG
#define SELECT_MAX   1024
static int run_flag = 1;
static mthread_mutex_t mut_select;
static event_data_ptr_t *event_listen[SELECT_MAX];
static int event_listen_cnt;
event_callbacks_t event_callbacks = {NULL, NULL, NULL};

void setEventCallback(event_callback_t event_in, event_callback_t event_out, event_callback_t event_close){
    event_callbacks.event_in = event_in;
    event_callbacks.event_out = event_out;
    event_callbacks.event_close = event_close;
    return;
}

int createEvent(){
    for(int i = 0; i < SELECT_MAX; i++){
        event_listen[i] = NULL;
    }
    event_listen_cnt = 0;
    mthread_mutex_init(&mut_select, NULL);
    return 0;
}

int closeEvent(){
    mthread_mutex_destroy(&mut_select);
    return 0;
}

int addEvent(int events, event_data_ptr_t *event_data){
    event_data->events = events;
    mthread_mutex_lock(&mut_select);
    for(int i = 0; i < SELECT_MAX; i++){
        if(event_listen[i] == NULL){
            event_listen[i] = event_data;
            event_listen_cnt++;
            mthread_mutex_unlock(&mut_select);
#ifdef EVENT_DEBUG
            printf("addEvent OK [fd=%d]\n", event_data->fd);
#endif
            return 0;
        }
    }
    mthread_mutex_unlock(&mut_select);
    printf("addEvent failed [fd=%d]\n", event_data->fd);
    return -1;
}

int delEvent(event_data_ptr_t *event_data){
    mthread_mutex_lock(&mut_select);

    for(int i = 0; i < SELECT_MAX; i++){
        if(event_listen[i] == event_data){
            event_listen[i] = NULL;
            event_listen_cnt--;
            mthread_mutex_unlock(&mut_select);
#ifdef EVENT_DEBUG
            printf("delEvent OK [fd=%d]\n", event_data->fd);
#endif
            return 0;
        }
    }
    mthread_mutex_unlock(&mut_select);
    printf("eventDel failed [fd=%d]\n", event_data->fd);
    return -1;
}

void *startEventLoop(void *arg){
    fd_set read_fds;
    fd_set write_fds;
    fd_set except_fds;
    int max_fd;
    int read_cnt = 0;
    int write_cnt = 0;
    int except_cnt = 0;
    while(run_flag == 1){
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        FD_ZERO(&except_fds);
        read_cnt = 0;
        write_cnt = 0;
        except_cnt = 0;
        max_fd = 0;
        mthread_mutex_lock(&mut_select);
        event_data_ptr_t *event_listen_copy[SELECT_MAX];
        memcpy(event_listen_copy, event_listen, sizeof(event_listen_copy));
        for(int i = 0; i < SELECT_MAX; i++){
            event_data_ptr_t *event_data = event_listen_copy[i];
            if(event_data != NULL){
                if(event_data->events & EVENT_IN){
                    FD_SET(event_data->fd, &read_fds);
                    read_cnt++;
                }
                if(event_data->events & EVENT_OUT){
                    FD_SET(event_data->fd, &write_fds);
                    write_cnt++;
                }
                if((event_data->events & EVENT_ERR) || (event_data->events & EVENT_HUP) || (event_data->events & EVENT_RDHUP)){
                    FD_SET(event_data->fd, &except_fds);
                    except_cnt++;
                }
                if(event_data->fd > max_fd){
                    max_fd = event_data->fd;
                }
            }
        }
        if(read_cnt == 0 && write_cnt == 0 && except_cnt == 0){
            Sleep(10);
            mthread_mutex_unlock(&mut_select);
            continue;
        }
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 20000; // 20ms
        int nfd = select(max_fd + 1, &read_fds, &write_fds, &except_fds, &timeout);
        mthread_mutex_unlock(&mut_select);
        if(nfd < 0){
            printf("select error, exit\n");
            exit(-1);
        }
        if(nfd != 0){
            int close_flag = 0;
            for(int i = 0; i < SELECT_MAX; i++){
                event_data_ptr_t *event_data = event_listen_copy[i];
                if(event_data == NULL){
                    continue;
                }
                if(FD_ISSET(event_data->fd, &read_fds)){
                    if(event_callbacks.event_in){
                        if(event_callbacks.event_in(event_data) < 0){
                            close_flag = 1;
                        }
                    }
                }
                if(FD_ISSET(event_data->fd, &write_fds)){
                    if(event_callbacks.event_out){
                        if(event_callbacks.event_out(event_data) < 0){
                            close_flag = 1;
                        }
                    }
                }
                if(FD_ISSET(event_data->fd, &except_fds)){
                    close_flag = 1;
                }
                if(close_flag == 1){
                    if(event_callbacks.event_close){
                        event_callbacks.event_close(event_data);
                    }
                }

            }
        }
        Sleep(10); // Sleep 10ms
    }
    return NULL;
}

void stopEventLoop(){
    run_flag = 0;
    return;
}
#endif