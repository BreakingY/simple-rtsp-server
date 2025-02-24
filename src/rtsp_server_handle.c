#include "rtsp_server_handle.h"
static int rtsp_run_flag = 1;
int rtspModuleInit(){
    return moduleInit();
}
void rtspModuleDel(){
    return moduleDel();
}
int rtspConfigSession(int file_reloop_flag, const char *mp4_file_path){
#ifdef RTSP_FILE_SERVER
    return configSession(file_reloop_flag, mp4_file_path);
#endif
    return 0;
}
void* rtspAddSession(const char* session_name){
    return addCustomSession(session_name);
}

void rtspDelSession(void *context){
    return delCustomSession(context);
}
int rtspStartServer(int auth, const char *server_ip, int server_port, const char *user, const char *password){
    int server_sock_fd;
    int ret;
    signal(SIGINT, sig_handler);
    signal(SIGQUIT, sig_handler);
    signal(SIGKILL, sig_handler);

    signal(SIGPIPE, SIG_IGN);
    signal(SIGFPE, SIG_IGN);

    server_sock_fd = createTcpSocket();
    if(server_sock_fd < 0){
        printf("failed to create tcp socket\n");
        return -1;
    }

    ret = bindSocketAddr(server_sock_fd, server_ip, server_port);
    if(ret < 0){
        printf("failed to bind addr\n");
        return -1;
    }

    ret = serverListen(server_sock_fd, 100);
    if(ret < 0){
        printf("failed to listen\n");
        return -1;
    }
    while(rtsp_run_flag == 1){
        int client_sock_fd;
        char client_ip[40];
        int client_port;
        pthread_t tid;

        client_sock_fd = acceptClient(server_sock_fd, client_ip, &client_port, 2000);
        if(client_sock_fd <= 0){
            // printf("failed to accept client or timeout\n");
            continue;
        }
        printf("###########accept client --> client_sock_fd:%d client ip:%s,client port:%d###########\n", client_sock_fd, client_ip, client_port);
        struct thd_arg_st *arg;
        arg = malloc(sizeof(struct thd_arg_st));
        memset(arg, 0 , sizeof(struct thd_arg_st));
        memcpy(arg->client_ip, client_ip, strlen(client_ip));
        memcpy(arg->user_name, user, strlen(user));
        memcpy(arg->password, password, strlen(password));
        arg->client_port = client_port;
        arg->client_sock_fd = client_sock_fd;
        arg->auth = auth;

        ret = pthread_create(&tid, NULL, doClientThd, (void *)arg);
        if(ret < 0){
            perror("doClientThd pthread_create()");
        }
        pthread_detach(tid);
    }
    closeSocket(server_sock_fd);
    return 0;
}
void rtspStopServer(){
    rtsp_run_flag = 0;
    return;
}
int sessionAddVideo(void *context, enum VIDEO_e type){
    return addVideo(context, type);
}
int sessionAddAudio(void *context, enum AUDIO_e type, int profile, int sample_rate, int channels){
    return addAudio(context, type, profile, sample_rate, channels);
}
int sessionSendVideoData(void *context, uint8_t *data, int data_len){
    return sendVideoData(context, data, data_len);
}
int sessionSendAudioData(void *context, uint8_t *data, int data_len){
    return sendAudioData(context, data, data_len);
}