#include "rtsp_server_handle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SERVER_IP   "0.0.0.0"
#define SERVER_PORT 8554
#define USER        "admin"
#define PASSWORD    "123456"

int main(int argc, char *argv[])
{
#ifdef RTSP_FILE_SERVER
    if(argc < 3){
        printf("./rtsp_server_file auth(0-not authentication; 1-authentication) loop(0-not loop 1-loop) dir_path(default:./mp4path)\n");
        return -1;
    }
    int auth = atoi(argv[1]);
    int file_reloop_flag = atoi(argv[2]);
    char *dir_path = NULL;
    if(argc > 3){
        dir_path = argv[3];
    }
    int ret = rtspModuleInit();
    if(ret < 0){
        printf("rtspModuleInit error\n");
        return -1;
    }
    // Must be configured first, only for file playback 
    ret = rtspConfigSession(file_reloop_flag, dir_path);
    if(ret < 0){
        printf("rtspConfigSession error\n");
        return -1;
    }
    printf("rtsp://%s:%d/filename\n", SERVER_IP, SERVER_PORT);
    ret = rtspStartServer(auth, SERVER_IP, SERVER_PORT, USER, PASSWORD);
    if(ret < 0){
        printf("rtspStartServer error\n");
        return -1;
    }
    rtspStopServer();
    rtspModuleDel();
#else
    printf("RTSP_FILE SERVER not defined\n");
#endif
    return 0;
}
