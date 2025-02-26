#include "rtsp_server_handle.h"
#include "mthread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SERVER_IP   "0.0.0.0"
#define SERVER_PORT 8554
#define USER        "admin"
#define PASSWORD    "123456"

#define BUFFER 2 * 1024 * 1024
#define H264_NAL_NIDR           1
#define H264_NAL_PARTITION_A    2
#define H264_NAL_IDR            5
#define H264_NAL_SEI            6
#define H264_NAL_SPS            7
#define H264_NAL_PPS            8
#define H264_NAL_AUD            9
const char *file;
int run_flag = 1;
void *context;
static int start_code3(uint8_t *buffer, int len){
    if(len < 3){
        return 0;
    }
    if (buffer[0] == 0 && buffer[1] == 0 && buffer[2] == 1)
        return 1;
    else
        return 0;
}
static int start_code4(uint8_t *buffer, int len){
    if(len < 4){
        return 0;
    }
    if (buffer[0] == 0 && buffer[1] == 0 && buffer[2] == 0 && buffer[3] == 1)
        return 1;
    else
        return 0;
}
static int get_start_code(uint8_t *buffer, int len){
    if(start_code3(buffer, len)){
        return 3;
    }
    else if(start_code4(buffer, len)){
        return 4;
    }
    return 0;
}
static uint8_t *findNextStartCode(uint8_t *buf, int len)
{
    int i;

    if (len < 3)
        return NULL;

    for (i = 0; i < len - 3; ++i) {
        if (get_start_code(buf, len))
            return buf;

        ++buf;
    }

    if (start_code3(buf, len))
        return buf;

    return NULL;
}
static int getFrameFromH264File(FILE *fp, uint8_t *frame, int size)
{
    int r_size, frame_size;
    uint8_t *next_start_code;

    if (fp == NULL)
        return -1;
    r_size = fread(frame, 1, size, fp);
    if(!get_start_code(frame, r_size)){
        return -1;
    }
    next_start_code = findNextStartCode(frame + 3, r_size - 3);
    if (!next_start_code) {
        fseek(fp, 0, SEEK_SET);
        frame_size = r_size;
    } else {
        frame_size = (next_start_code - frame);
        fseek(fp, frame_size - r_size, SEEK_CUR);
    }
    return frame_size;
}
int mpeg2_h264_new_access_unit(uint8_t *buffer, int len){
    int start_code = get_start_code(buffer, len);
    if(len < (start_code + 2)){
        return 0;
    }
    int nal_type = buffer[start_code] & 0x1f;
    if(H264_NAL_AUD == nal_type || H264_NAL_SPS == nal_type || H264_NAL_PPS == nal_type || H264_NAL_SEI == nal_type || (14 <= nal_type && nal_type <= 18))
        return 1;
    
    if(H264_NAL_NIDR == nal_type || H264_NAL_PARTITION_A == nal_type || H264_NAL_IDR == nal_type){
        return (buffer[start_code + 1] & 0x80) ? 1 : 0; // first_mb_in_slice
    }
    
    return 0;
}
void *sendDataThd(void *arg){
    FILE *fp = fopen(file, "r");
    if(fp == NULL){
        printf("file not exist\n");
        exit(0);
    }
    uint8_t frame[BUFFER];
    int frame_size = 0;
    int fps = 25;
    int start_code;
    int type;
    int ret;
    while (run_flag == 1) {
        start_code = 0;
        frame_size = getFrameFromH264File(fp, frame, BUFFER);
        if (frame_size < 0) {
            printf("read over\n");
            break;
        }
        
        start_code = get_start_code(frame, frame_size);
        type = frame[start_code] & 0x1f;
        if(type == 5 || type == 1){
            if(mpeg2_h264_new_access_unit(frame, frame_size)){
                m_sleep(1000 / fps);
            }
        }
        ret = sessionSendVideoData(context, frame + start_code, frame_size - start_code);
        if(ret < 0){
            printf("sessionSendVideoData error\n");
        }
    }
    return NULL;
}
int main(int argc, char *argv[])
{
    if(argc < 3){
        printf("./rtsp_server_live auth(0-not authentication; 1-authentication) file_path(h264: ../mp4path/test.h264)\n");
        return -1;
    }
    int auth = atoi(argv[1]);
    file = argv[2];
    int ret = rtspModuleInit();
    if(ret < 0){
        printf("rtspModuleInit error\n");
        return -1;
    }
    // add custom session
    context = rtspAddSession("live");
    if(context == NULL){
        printf("rtspAddSession error\n");
        return -1;
    }
    ret = sessionAddVideo(context, VIDEO_H264);
    if(ret < 0){
        printf("sessionAddVideo error\n");
        return -1;
    }
    printf("rtsp://%s:%d/live\n", SERVER_IP, SERVER_PORT);
    mthread_t tid;
    ret = mthread_create(&tid, NULL, sendDataThd, NULL);
    if(ret < 0){
        printf("sendDataThd mthread_create()\n");
        return -1;
    }
    mthread_detach(tid);
    ret = rtspStartServer(auth, SERVER_IP, SERVER_PORT, USER, PASSWORD);
    if(ret < 0){
        printf("rtspStartServer error\n");
    }
    run_flag = 0;
    rtspStopServer();
    rtspDelSession(context);
    rtspModuleDel();
    return 0;
}
