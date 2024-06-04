#include "session.h"
#include "media.h"
#include "aac_rtp.h"
#include "common.h"
#include "h264_rtp.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libavutil/time.h>
#include <malloc.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#define CLIENTMAX 1024
#define FILEMAX 1024
#define VIDEO_DATA_MAX_SIZE 4 * 1024 * 1024
#define RING_BUFFER_MAX 32
int epoll_fd;
pthread_t epoll_thd;
int run_flag = 1;
enum TRANSPORT_e
{
    RTP_OVER_TCP = 1,
    RTP_OVER_UDP,
};
enum MEDIA_e
{
    VIDEO = 1,
    AUDIO,
};
struct MediaPacket_st
{
    char data[2 * 1024 * 1024];
    int64_t size;
    int type; // MEDIA_e
};
/*记录客户端的数据通道及数据包*/
struct clientinfo_st
{
    int sd;          // client tcp socket

    int udp_sd_rtp;  // server rtp udp socket
    int udp_sd_rtcp; // server rtcp udp socket

    int udp_sd_rtp_1;
    int udp_sd_rtcp_1;

    char client_ip[40];
    int client_rtp_port;
    int client_rtp_port_1;

    int transport; // enum TRANSPORT_e

    int sig_0; // RTP_OVER_TCP-->rtp sig
    int sig_2;
    int playflag;

    void *arg;                         // 指向自己结构体指针
    void (*send_call_back)(void *arg); // 回调函数,目前只针对数据发送回调函数，如果需要关注EPOLLIN事件，可以再定义一个回调函数接口，负责接收客户端发送过来的数据
    int events;                        // 对应的监听事件，EPOLLIN和EPLLOUT，目前只关心EPOLLOUT事件
    struct mp4info_st *mp4info;        // 指向上层结构体(视频回放任务)

    struct RtpPacket *rtp_packet;   // video 每一个客户端对应一个rtp 和tcp数据包，否则不同客户端共用数据包会导致序列号不连续
    struct RtpPacket *rtp_packet_1; // audio
    struct rtp_tcp_header *tcp_header;

    // 环形缓冲区
    // video
    pthread_mutex_t mut_list;
    struct MediaPacket_st *packet_list;
    int packet_list_size; // 唤醒缓冲区大小
    int pos_list;         // 下一个要发送数据的位置
    int packet_num;       // 唤醒缓冲区内数据包个数
    int pos_last_packet;  // 环形缓冲区可用的尾部位置

    // audio
    struct MediaPacket_st *packet_list_1;
    int packet_list_size_1; // 唤醒缓冲区大小
    int pos_list_1;         // 下一个要发送数据的位置
    int packet_num_1;       // 唤醒缓冲区内数据包个数
    int pos_last_packet_1;  // 环形缓冲区可用的尾部位置
};
/*视频回放任务结构体*/
struct mp4info_st
{
    void *media;
    char *filename;
    pthread_mutex_t mut;
    struct clientinfo_st clientinfo[CLIENTMAX]; // 请求回放当前视频文件的rtsp客户端
    int count;
    int pos;
};

struct mp4info_st *mp4info_arr[FILEMAX]; // 视频回放任务数组，动态添加删除
pthread_mutex_t mut_mp4 = PTHREAD_MUTEX_INITIALIZER;
/*
 * 注意加锁的层级关系为lock  mut_mp4(读写mp4info_arr) --> lock mp4info_arr[i].mutx(读写mp4info_st) --> lock mp4info_arr[i].clientinfo_st[j].mut_list(读写clientinfo_st的环形缓冲区队列)
 * --> unlock mut_mp4 --> unlock mp4info_st.mutx --> unlock clientinfo_st.mut_list
 * 不是所有操作都需要加上面的三个锁，但是加多个锁的时候要遵循上面的加锁顺序，防止死锁
 */

int sum_client = 0; // 记录一共有多少个客户端正在连接服务器
pthread_mutex_t mut_clientcount = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mut_epoll = PTHREAD_MUTEX_INITIALIZER;
/*
 * mut_clientcount和mut_epoll不要和上面注释提到的三个锁嵌套使用
 */
// 定义文件描述符类型
typedef enum
{
    FD_TYPE_TCP,
    FD_TYPE_UDP_RTP,  // video
    FD_TYPE_UDP_RTP_1 // audio
} fd_type_t;

// 辅助结构体，用于存储 clientinfo_st 指针和文件描述符类型
typedef struct
{
    struct clientinfo_st *client_info;
    fd_type_t fd_type;
    int fd;
} epoll_data_ptr_t;
void sig_handler(int s)
{
    printf("catch signal %d,rtsp exit\n", s);
    moduleDel();
    printf("%s\n", __func__);
    exit(1);
}
int createEpoll()
{
    epoll_fd = epoll_create(1024);
    if (epoll_fd <= 0)
    {
        printf("create efd in %s err %s\n", __func__, strerror(errno));
        exit(1);
    }
    return 0;
}
int closeEpoll()
{
    close(epoll_fd);
    return 0;
}
/* 向 epoll监听的红黑树 添加一个文件描述符 */
void eventAdd(int events, struct clientinfo_st *ev, void (*send_call_back)(void *arg), void *arg)
{
    if (ev->sd < 0)
        return;
    pthread_mutex_lock(&mut_epoll);

    ev->send_call_back = send_call_back;
    ev->arg = arg;
    int op = EPOLL_CTL_ADD;

    if (ev->transport == RTP_OVER_TCP)
    {
        struct epoll_event epv = {0, {0}};
        epoll_data_ptr_t *epoll_data = (epoll_data_ptr_t *)malloc(sizeof(epoll_data_ptr_t));
        epoll_data->client_info = ev;
        epoll_data->fd = ev->sd;
        epoll_data->fd_type = FD_TYPE_TCP;
        epv.data.ptr = epoll_data;        // ptr指向一个结构体
        epv.events = ev->events = events; // EPOLLIN 或 EPOLLOUT
        if (epoll_ctl(epoll_fd, op, epoll_data->fd, &epv) < 0)
            printf("tcp event add failed [fd=%d]\n", epoll_data->fd);
        else
            printf("tcp event add OK [fd=%d]\n", epoll_data->fd);
    }
    else
    {
        if (ev->udp_sd_rtp != -1)
        { // video
            struct epoll_event epv = {0, {0}};
            epoll_data_ptr_t *epoll_data = (epoll_data_ptr_t *)malloc(sizeof(epoll_data_ptr_t));
            epoll_data->client_info = ev;
            epoll_data->fd = ev->udp_sd_rtp;
            epoll_data->fd_type = FD_TYPE_UDP_RTP;
            epv.data.ptr = epoll_data;
            epv.events = ev->events = events; // EPOLLIN 或 EPOLLOUT
            if (epoll_ctl(epoll_fd, op, epoll_data->fd, &epv) < 0)
                printf("udp event add failed [fd=%d]\n", epoll_data->fd);
            else
                printf("udp event add OK [fd=%d]\n", epoll_data->fd);
        }
        if (ev->udp_sd_rtp_1 != -1)
        { // audio
            struct epoll_event epv = {0, {0}};
            epoll_data_ptr_t *epoll_data = (epoll_data_ptr_t *)malloc(sizeof(epoll_data_ptr_t));
            epoll_data->client_info = ev;
            epoll_data->fd = ev->udp_sd_rtp_1;
            epoll_data->fd_type = FD_TYPE_UDP_RTP;
            epv.data.ptr = epoll_data;
            epv.events = ev->events = events; // EPOLLIN 或 EPOLLOUT
            if (epoll_ctl(epoll_fd, op, epoll_data->fd, &epv) < 0)
                printf("udp event add failed [fd=%d]\n", epoll_data->fd);
            else
                printf("udp event add OK [fd=%d]\n", epoll_data->fd);
        }
    }
    pthread_mutex_unlock(&mut_epoll);
    return;
}
/* 从epoll 监听的 红黑树中删除一个文件描述符*/
void eventDel(struct clientinfo_st *ev)
{
    if (ev->sd < 0)
        return;
    pthread_mutex_lock(&mut_epoll);
    struct epoll_event epv = {0, {0}};
    epv.data.ptr = NULL;
    if (ev->transport == RTP_OVER_TCP)
    {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ev->sd, &epv);
        printf("tcp event del OK [fd=%d]\n", ev->sd);
    }
    else
    {
        if (ev->udp_sd_rtp != -1)
        {
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ev->udp_sd_rtp, &epv);
            printf("udp event del OK [fd=%d]\n", ev->udp_sd_rtp);
        }
        if (ev->udp_sd_rtp_1 != -1)
        {
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ev->udp_sd_rtp_1, &epv);
            printf("udp event del OK [fd=%d]\n", ev->udp_sd_rtp_1);
        }
    }
    pthread_mutex_unlock(&mut_epoll);
    return;
}

int initClient(struct clientinfo_st *clientinfo)
{
    clientinfo->sd = -1;
    clientinfo->udp_sd_rtp = -1;
    clientinfo->udp_sd_rtcp = -1;
    clientinfo->udp_sd_rtp_1 = -1;
    clientinfo->udp_sd_rtcp_1 = -1;
    memset(clientinfo->client_ip, 0, sizeof(clientinfo->client_ip));
    clientinfo->client_rtp_port = -1;
    clientinfo->client_rtp_port_1 = -1;
    clientinfo->transport = -1;
    clientinfo->sig_0 = -1;
    clientinfo->sig_2 = -1;
    clientinfo->playflag = -1;

    clientinfo->arg = NULL;
    clientinfo->send_call_back = NULL;
    clientinfo->events = -1;
    clientinfo->mp4info = NULL;

    clientinfo->rtp_packet = NULL;
    clientinfo->rtp_packet_1 = NULL;
    clientinfo->tcp_header = NULL;

    // 环形缓冲区
    // video
    pthread_mutex_init(&clientinfo->mut_list, NULL);
    clientinfo->packet_list = NULL;
    clientinfo->packet_list_size = 0;
    clientinfo->pos_list = 0;
    clientinfo->packet_num = 0;
    clientinfo->pos_last_packet = 0;

    // audio
    clientinfo->packet_list_1 = NULL;
    clientinfo->packet_list_size_1 = 0;
    clientinfo->pos_list_1 = 0;
    clientinfo->packet_num_1 = 0;
    clientinfo->pos_last_packet_1 = 0;
}
int clearClient(struct clientinfo_st *clientinfo)
{

    if (clientinfo == NULL)
    {
        return 0;
    }
    if (clientinfo->sd > 0)
    {
        close(clientinfo->sd);
        clientinfo->sd = -1;
    }
    if (clientinfo->udp_sd_rtp > 0)
    {
        close(clientinfo->udp_sd_rtp);
        clientinfo->udp_sd_rtp = -1;
    }
    if (clientinfo->udp_sd_rtcp > 0)
    {
        close(clientinfo->udp_sd_rtcp);
        clientinfo->udp_sd_rtcp = -1;
    }
    if (clientinfo->udp_sd_rtp_1 > 0)
    {
        close(clientinfo->udp_sd_rtp_1);
        clientinfo->udp_sd_rtp_1 = -1;
    }
    if (clientinfo->udp_sd_rtcp_1 > 0)
    {
        close(clientinfo->udp_sd_rtcp_1);
        clientinfo->udp_sd_rtcp_1 = -1;
    }
    memset(clientinfo->client_ip, 0, sizeof(clientinfo->client_ip));
    clientinfo->client_rtp_port = -1;
    clientinfo->client_rtp_port_1 = -1;
    clientinfo->transport = -1;
    clientinfo->sig_0 = -1;
    clientinfo->sig_2 = -1;
    clientinfo->playflag = -1;

    clientinfo->arg = NULL;
    clientinfo->send_call_back = NULL;
    clientinfo->events = -1;
    // clientinfo->mp4info=NULL;

    if (clientinfo->rtp_packet != NULL)
    {
        free(clientinfo->rtp_packet);
        clientinfo->rtp_packet = NULL;
    }
    if (clientinfo->rtp_packet_1 != NULL)
    {
        free(clientinfo->rtp_packet_1);
        clientinfo->rtp_packet_1 = NULL;
    }
    if (clientinfo->tcp_header != NULL)
    {
        free(clientinfo->tcp_header);
        clientinfo->tcp_header = NULL;
    }

    // 环形缓冲区
    // video
    pthread_mutex_destroy(&clientinfo->mut_list);
    if (clientinfo->packet_list != NULL)
    {
        free(clientinfo->packet_list);
        clientinfo->packet_list = NULL;
    }
    clientinfo->packet_list_size = 0;
    clientinfo->pos_list = 0;
    clientinfo->packet_num = 0;
    clientinfo->pos_last_packet = 0;

    // audio
    if (clientinfo->packet_list_1 != NULL)
    {
        free(clientinfo->packet_list_1);
        clientinfo->packet_list_1 = NULL;
    }
    clientinfo->packet_list_size_1 = 0;
    clientinfo->pos_list_1 = 0;
    clientinfo->packet_num_1 = 0;
    clientinfo->pos_last_packet_1 = 0;

    return 0;
}
void *epollLoop(void *arg)
{
    while (run_flag == 1)
    {
        struct epoll_event events[CLIENTMAX];
        pthread_mutex_lock(&mut_epoll);
        int timeout = 10; // ms
        int nfd = epoll_wait(epoll_fd, events, CLIENTMAX, timeout);
        pthread_mutex_unlock(&mut_epoll);
        if (nfd < 0)
        {
            printf("epoll_wait error, exit\n");
            exit(-1);
        }

        // 向当前回放视频的所有客户端发送数据
        for (int i = 0; i < nfd; i++)
        {
            epoll_data_ptr_t *epoll_data = (struct clientinfo_st *)events[i].data.ptr;
            struct clientinfo_st *clientinfo = epoll_data->client_info;
            int type = epoll_data->fd_type;
            int fd = epoll_data->fd;
            if (clientinfo == NULL)
            {
                continue;
            }
            // 如果监听的是读事件，并返回的是读事件
            if ((events[i].events & EPOLLOUT) && (clientinfo->events & EPOLLOUT))
            {
                // 从环形队列中获取数据并发送
                pthread_mutex_lock(&clientinfo->mp4info->mut); // 正在读取mp4info里面的clientinfo，需要加锁
                pthread_mutex_lock(&clientinfo->mut_list);
                struct MediaPacket_st node;
                node.size = 0;
                if (fd == clientinfo->sd && ((clientinfo->sig_0 != -1) || (clientinfo->sig_2 != -1)))
                { // rtp over tcp
                    // 取出一帧音频或视频
                    if (clientinfo->packet_num > 0 && clientinfo->pos_list < clientinfo->packet_list_size)
                    {
                        memcpy(node.data, clientinfo->packet_list[clientinfo->pos_list].data, clientinfo->packet_list[clientinfo->pos_list].size);
                        node.size = clientinfo->packet_list[clientinfo->pos_list].size;
                        node.type = clientinfo->packet_list[clientinfo->pos_list].type;
                        clientinfo->pos_list++;
                        clientinfo->packet_num--;
                        if (clientinfo->pos_list >= clientinfo->packet_list_size)
                        {
                            clientinfo->pos_list = 0;
                        }
                    }
                }
                else if (fd == clientinfo->udp_sd_rtp)
                { // video
                    // 取出一帧视频
                    if (clientinfo->packet_num > 0 && clientinfo->pos_list < clientinfo->packet_list_size)
                    {
                        memcpy(node.data, clientinfo->packet_list[clientinfo->pos_list].data, clientinfo->packet_list[clientinfo->pos_list].size);
                        node.size = clientinfo->packet_list[clientinfo->pos_list].size;
                        node.type = clientinfo->packet_list[clientinfo->pos_list].type;
                        clientinfo->pos_list++;
                        clientinfo->packet_num--;
                        if (clientinfo->pos_list >= clientinfo->packet_list_size)
                        {
                            clientinfo->pos_list = 0;
                        }
                    }
                }
                else if (fd == clientinfo->udp_sd_rtp_1)
                { // audio
                    // 取出一帧音频
                    if (clientinfo->packet_num_1 > 0 && clientinfo->pos_list_1 < clientinfo->packet_list_size_1)
                    {
                        memcpy(node.data, clientinfo->packet_list_1[clientinfo->pos_list_1].data, clientinfo->packet_list_1[clientinfo->pos_list_1].size);
                        node.size = clientinfo->packet_list_1[clientinfo->pos_list_1].size;
                        node.type = clientinfo->packet_list_1[clientinfo->pos_list_1].type;
                        clientinfo->pos_list_1++;
                        clientinfo->packet_num_1--;
                        if (clientinfo->pos_list_1 >= clientinfo->packet_list_size_1)
                        {
                            clientinfo->pos_list_1 = 0;
                        }
                    }
                }
                if (node.size == 0)
                { // 没有数据要发送
                    pthread_mutex_unlock(&clientinfo->mut_list);
                    pthread_mutex_unlock(&clientinfo->mp4info->mut);
                    continue;
                }
                pthread_mutex_unlock(&clientinfo->mut_list);
                pthread_mutex_unlock(&clientinfo->mp4info->mut);
                enum VIDEO_e video_type = getVideoType(clientinfo->mp4info->media);
                struct audioinfo_st audioinfo = getAudioInfo(clientinfo->mp4info->media);
                int sample_rate = audioinfo.sample_rate;
                int channels = audioinfo.channels;
                int profile = audioinfo.profile;
                enum AUDIO_e audio_type = getAudioType(clientinfo->mp4info->media);
                int ret;
                if (fd == clientinfo->sd)
                { // rtp over tcp
                    if (node.type == VIDEO)
                    {
                        if ( video_type == VIDEO_H264)
                        {
                            ret = rtpSendH264Frame(clientinfo->sd, clientinfo->tcp_header, clientinfo->rtp_packet, node.data, node.size, 0, clientinfo->sig_0, NULL, -1);
                        }
                        else if (video_type == VIDEO_H265)
                        {
                            ret = rtpSendH265Frame(clientinfo->sd, clientinfo->tcp_header, clientinfo->rtp_packet, node.data, node.size, 0, clientinfo->sig_0, NULL, -1);
                        }
                    }
                    else
                    {
                        if (audio_type == AUDIO_AAC)
                        {
                            ret = rtpSendAACFrame(clientinfo->sd, clientinfo->tcp_header, clientinfo->rtp_packet_1, node.data, node.size, sample_rate, channels, profile, clientinfo->sig_2, NULL, -1);
                        }
                        else if (audio_type == AUDIO_PCMA)
                        {
                            ret = rtpSendPCMAFrame(clientinfo->sd, clientinfo->tcp_header, clientinfo->rtp_packet_1, node.data, node.size, sample_rate, channels, profile, clientinfo->sig_2, NULL, -1);
                        }
                    }
                }
                else
                { // rtp over udp
                    if (node.type == VIDEO)
                    {
                        if (video_type == VIDEO_H264)
                        {
                            ret = rtpSendH264Frame(clientinfo->udp_sd_rtp, NULL, clientinfo->rtp_packet, node.data, node.size, 0, -1, clientinfo->client_ip, clientinfo->client_rtp_port);
                        }
                        else if (video_type == VIDEO_H265)
                        {
                            ret = rtpSendH265Frame(clientinfo->udp_sd_rtp, NULL, clientinfo->rtp_packet, node.data, node.size, 0, -1, clientinfo->client_ip, clientinfo->client_rtp_port);
                        }
                    }
                    else
                    {
                        if (audio_type == AUDIO_AAC)
                        {
                            ret = rtpSendAACFrame(clientinfo->udp_sd_rtp_1, NULL, clientinfo->rtp_packet_1, node.data, node.size, sample_rate, channels, profile, -1, clientinfo->client_ip, clientinfo->client_rtp_port_1);
                        }
                        else if (audio_type == AUDIO_PCMA)
                        {
                            ret = rtpSendPCMAFrame(clientinfo->udp_sd_rtp_1, NULL, clientinfo->rtp_packet_1, node.data, node.size, sample_rate, channels, profile, -1, clientinfo->client_ip, clientinfo->client_rtp_port_1);
                        }
                    }
                    // 通过tcp socket判断对端是不是关闭了，udp判断不出来
                    int flags = fcntl(clientinfo->sd, F_GETFL, 0);
                    fcntl(clientinfo->sd, F_SETFL, flags | O_NONBLOCK);
                    char buffer[1024];
                    if (recv(clientinfo->sd, buffer, sizeof(buffer), 0) == 0)
                    {
                        ret = -1;
                    }
                }
                if (ret <= 0) //&&!(errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN)
                {
                    /*从epoll上删除并释放空间*/
                    eventDel(clientinfo);
                    if (epoll_data)
                    {
                        free(epoll_data);
                    }
                    printf("client:%d offline\n", clientinfo->sd);
                    struct mp4info_st *mp4info = clientinfo->mp4info;
                    int count = 0;
                    pthread_mutex_lock(&clientinfo->mp4info->mut);
                    clearClient(clientinfo);
                    mp4info->count--;
                    count = mp4info->count;
                    pthread_mutex_unlock(&clientinfo->mp4info->mut);
                    /*更改客户连接总数*/
                    pthread_mutex_lock(&mut_clientcount);
                    sum_client--;
                    printf("sum_client:%d\n", sum_client);
                    pthread_mutex_unlock(&mut_clientcount);
                    if(count == 0){
                        del1Mp4Info(clientinfo->mp4info->pos);
                    }
                }
            }
        }
        usleep(1000 * 10); // 10ms
    }
    return NULL;
}

int increaseClientList(enum MEDIA_e type, struct clientinfo_st *clientinfo)
{
    // |packet5|packet6|packet7|packet8|packet1(pos)|packet2|packet3|packet4| --> |packet5|packet6|packet7|packet8|空闲1|空闲2|空闲3|空闲4|packet1(pos)|packet2|packet3|packet4|
    if (type == VIDEO)
    {
        if (clientinfo->packet_num >= clientinfo->packet_list_size)
        { // 缓冲区用完了，增大缓冲区
            clientinfo->packet_list = (struct MediaPacket_st *)realloc(clientinfo->packet_list, (clientinfo->packet_list_size + 4) * sizeof(struct MediaPacket_st));
            memmove(clientinfo->packet_list + clientinfo->pos_list + 4, clientinfo->packet_list + clientinfo->pos_list, (clientinfo->packet_list_size - clientinfo->pos_list) * sizeof(struct MediaPacket_st));
            clientinfo->packet_list_size += 4;
            clientinfo->pos_list += 4;
        }
    }
    else if (type == AUDIO)
    {
        if (clientinfo->packet_num_1 >= clientinfo->packet_list_size_1)
        { // 缓冲区用完了，增大缓冲区
            clientinfo->packet_list_1 = (struct MediaPacket_st *)realloc(clientinfo->packet_list_1, (clientinfo->packet_list_size_1 + 4) * sizeof(struct MediaPacket_st));
            memmove(clientinfo->packet_list_1 + clientinfo->pos_list_1 + 4, clientinfo->packet_list_1 + clientinfo->pos_list_1, (clientinfo->packet_list_size_1 - clientinfo->pos_list_1) * sizeof(struct MediaPacket_st));
            clientinfo->packet_list_size_1 += 4;
            clientinfo->pos_list_1 += 4;
        }
    }
    return 0;
}
void sendData(void *arg)
{
    struct clientinfo_st *clientinfo = (struct clientinfo_st *)arg;
    pthread_mutex_lock(&clientinfo->mut_list);
    if (clientinfo->packet_num >= RING_BUFFER_MAX)
    {
        printf("WARING ring buffer too large\n");
    }
    increaseClientList(VIDEO, clientinfo);
    increaseClientList(AUDIO, clientinfo);
    // 数据包送入环形队列中
    
    if (nowStreamIsVideo(clientinfo->mp4info->media) && (clientinfo->sig_0 != -1 || clientinfo->client_rtp_port != -1))
    { // video
        char *ptr = NULL;
        int ptr_len = 0;
        getVideoNALUWithoutStartCode(clientinfo->mp4info->media, &ptr, &ptr_len);
        memcpy(clientinfo->packet_list[clientinfo->pos_last_packet].data, ptr, ptr_len);
        clientinfo->packet_list[clientinfo->pos_last_packet].size = ptr_len;
        clientinfo->packet_list[clientinfo->pos_last_packet].type = VIDEO;
        clientinfo->packet_num++;
        clientinfo->pos_last_packet++;
        if (clientinfo->pos_last_packet >= clientinfo->packet_list_size)
        {
            clientinfo->pos_last_packet = 0;
        }
    }
    if (nowStreamIsAudio(clientinfo->mp4info->media) && (clientinfo->sig_2 != -1 || clientinfo->client_rtp_port_1 != -1))
    { // audio
        char *ptr = NULL;
        int ptr_len = 0;
        getAudioWithoutADTS(clientinfo->mp4info->media, &ptr, &ptr_len);
        if (clientinfo->client_rtp_port_1 != -1)
        { // udp,音视频用不同的队列
            memcpy(clientinfo->packet_list_1[clientinfo->pos_last_packet_1].data, ptr, ptr_len);
            clientinfo->packet_list_1[clientinfo->pos_last_packet_1].size = ptr_len;
            clientinfo->packet_list_1[clientinfo->pos_last_packet_1].type = AUDIO;
            clientinfo->packet_num_1++;
            clientinfo->pos_last_packet_1++;
            if (clientinfo->pos_last_packet_1 >= clientinfo->packet_list_size_1)
            {
                clientinfo->pos_last_packet_1 = 0;
            }
        }
        else if (clientinfo->sig_2 != -1)
        { // tcp 音视频用同一个队列
            memcpy(clientinfo->packet_list[clientinfo->pos_last_packet].data, ptr, ptr_len);
            clientinfo->packet_list[clientinfo->pos_last_packet].size = ptr_len;
            clientinfo->packet_list[clientinfo->pos_last_packet].type = AUDIO;
            clientinfo->packet_num++;
            clientinfo->pos_last_packet++;
            if (clientinfo->pos_last_packet >= clientinfo->packet_list_size)
            {
                clientinfo->pos_last_packet = 0;
            }
        }
    }
    pthread_mutex_unlock(&clientinfo->mut_list);
    return;
}
void mediaCallBack(void *arg){
    struct mp4info_st *mp4info = (struct mp4info_st *)arg;
    if(mp4info == NULL){
        return;
    }
    pthread_mutex_lock(&mp4info->mut);
    for (int i = 0; i < mp4info->count; i++)
    {
        if (mp4info->clientinfo[i].sd != -1 && mp4info->clientinfo[i].send_call_back != NULL && mp4info->clientinfo[i].playflag == 1)
        {
            mp4info->clientinfo[i].send_call_back(mp4info->clientinfo[i].arg);
        }
    }
    pthread_mutex_unlock(&mp4info->mut);
}
int get_free_clientinfo(int pos)
{
    for (int i = 0; i < CLIENTMAX; i++)
    {
        if (mp4info_arr[pos]->clientinfo[i].sd == -1)
        {

            return i;
        }
    }
    return -1;
}
/*创建一个mp4文件回放任务并添加一个客户端*/
int add1Mp4Info(int pos, char *path_filename, int client_sock_fd, int sig_0, int sig_2, int ture_of_tcp, int server_rtp_fd, int server_rtcp_fd, int server_rtp_fd_1, int server_rtcp_fd_1, char *client_ip, int client_rtp_port, int client_rtp_port_1)
{
    pthread_mutex_lock(&mut_mp4);
    struct mp4info_st *mp4;
    mp4 = malloc(sizeof(struct mp4info_st));
    mp4->media = creatMedia(path_filename, mediaCallBack,mp4);

    mp4->filename = malloc(strlen(path_filename) + 1);
    memset(mp4->filename, 0, strlen(path_filename) + 1);
    memcpy(mp4->filename, path_filename, strlen(path_filename));

    printf("add1Mp4Info:%s client_sock_fd:%d\n", mp4->filename, client_sock_fd);
    pthread_mutex_init(&mp4->mut, NULL);
    mp4->count = 0;

    mp4info_arr[pos] = mp4;
    mp4->pos = pos;
    for (int j = 0; j < CLIENTMAX; j++)
    {
        initClient(&mp4info_arr[pos]->clientinfo[j]);
        mp4info_arr[pos]->clientinfo[j].mp4info = mp4info_arr[pos];
    }
    pthread_mutex_lock(&mp4info_arr[pos]->mut);
    mp4info_arr[pos]->clientinfo[0].sd = client_sock_fd;
    mp4info_arr[pos]->count++;
    if (ture_of_tcp == 1)
    {
        mp4info_arr[pos]->clientinfo[0].transport = RTP_OVER_TCP;
        mp4info_arr[pos]->clientinfo[0].sig_0 = sig_0;
        mp4info_arr[pos]->clientinfo[0].sig_2 = sig_2;
    }
    else
    {
        mp4info_arr[pos]->clientinfo[0].transport = RTP_OVER_UDP;
        memset(mp4info_arr[pos]->clientinfo[0].client_ip, 0, sizeof(mp4info_arr[pos]->clientinfo[0].client_ip));
        memcpy(mp4info_arr[pos]->clientinfo[0].client_ip, client_ip, strlen(client_ip));
        mp4info_arr[pos]->clientinfo[0].udp_sd_rtp = server_rtp_fd;
        mp4info_arr[pos]->clientinfo[0].udp_sd_rtcp = server_rtcp_fd;
        mp4info_arr[pos]->clientinfo[0].udp_sd_rtp_1 = server_rtp_fd_1;
        mp4info_arr[pos]->clientinfo[0].udp_sd_rtcp_1 = server_rtcp_fd_1;
        mp4info_arr[pos]->clientinfo[0].client_rtp_port = client_rtp_port;
        mp4info_arr[pos]->clientinfo[0].client_rtp_port_1 = client_rtp_port_1;
    }
    // video
    mp4info_arr[pos]->clientinfo[0].rtp_packet = (struct RtpPacket *)malloc(VIDEO_DATA_MAX_SIZE);
    rtpHeaderInit(mp4info_arr[pos]->clientinfo[0].rtp_packet, 0, 0, 0, RTP_VESION, RTP_PAYLOAD_TYPE_H26X, 0, 0, 0, 0x88923423);
    // audio
    mp4info_arr[pos]->clientinfo[0].rtp_packet_1 = (struct RtpPacket *)malloc(VIDEO_DATA_MAX_SIZE);
    rtpHeaderInit(mp4info_arr[pos]->clientinfo[0].rtp_packet_1, 0, 0, 0, RTP_VESION, getAudioType(mp4info_arr[pos]->media) == AUDIO_AAC ? RTP_PAYLOAD_TYPE_AAC : RTP_PAYLOAD_TYPE_PCMA, 0, 0, 0, 0x88923423);

    mp4info_arr[pos]->clientinfo[0].tcp_header = malloc(sizeof(struct rtp_tcp_header));

    mp4info_arr[pos]->clientinfo[0].packet_list = (struct MediaPacket_st *)malloc((RING_BUFFER_MAX / 4) * sizeof(struct MediaPacket_st));
    mp4info_arr[pos]->clientinfo[0].packet_list_size = RING_BUFFER_MAX / 4;
    mp4info_arr[pos]->clientinfo[0].packet_list_1 = (struct MediaPacket_st *)malloc((RING_BUFFER_MAX / 4) * sizeof(struct MediaPacket_st));
    mp4info_arr[pos]->clientinfo[0].packet_list_size_1 = RING_BUFFER_MAX / 4;

    eventAdd(EPOLLOUT, &mp4info_arr[pos]->clientinfo[0], sendData, &mp4info_arr[pos]->clientinfo[0]);
    mp4info_arr[pos]->clientinfo[0].playflag = 1;
    pthread_mutex_unlock(&mp4info_arr[pos]->mut);
    pthread_mutex_unlock(&mut_mp4);
    return 0;
}
/*删除mp4回放任务*/
void del1Mp4Info(int pos)
{
    destroyMedia(mp4info_arr[pos]->media);// destroyMedia不要加锁
    int client_num = 0;
    pthread_mutex_lock(&mut_mp4);
    if (mp4info_arr[pos] == NULL)
    {
        pthread_mutex_unlock(&mut_mp4);
        return;
    }
    printf("del1Mp4Info:%s\n", mp4info_arr[pos]->filename);
    pthread_mutex_lock(&mp4info_arr[pos]->mut);
    for (int i = 0; i < CLIENTMAX; i++)
    {
        if (mp4info_arr[pos]->clientinfo[i].sd > 0)
        {
            client_num++;
            eventDel(&mp4info_arr[pos]->clientinfo[i]);
        }
        clearClient(&mp4info_arr[pos]->clientinfo[i]);
    }
   
    if(mp4info_arr[pos]->filename){
        free(mp4info_arr[pos]->filename);
        mp4info_arr[pos]->filename = NULL;
    }
    pthread_mutex_unlock(&mp4info_arr[pos]->mut);
    pthread_mutex_destroy(&mp4info_arr[pos]->mut);
    free(mp4info_arr[pos]);
    mp4info_arr[pos] = NULL;
    pthread_mutex_unlock(&mut_mp4);

    pthread_mutex_lock(&mut_clientcount);
    sum_client -= client_num;
    pthread_mutex_unlock(&mut_clientcount);
}
void moduleInit()
{
    createEpoll();
    int ret = pthread_create(&epoll_thd, NULL, epollLoop, NULL);
    if (ret < 0)
    {
        perror("epollLoop pthread_create()");
    }
    pthread_detach(epoll_thd);
}

void moduleDel()
{
    for (int i = 0; i < FILEMAX; i++)
    {
        del1Mp4Info(i);
    }
    pthread_mutex_destroy(&mut_mp4);
    pthread_mutex_destroy(&mut_clientcount);
    run_flag = 0;
    closeEpoll();
}
/*添加一个客户端*/
int addClient(char *path_filename, int client_sock_fd, int sig_0, int sig_2, int ture_of_tcp, char *client_ip, int client_rtp_port,
              int client_rtp_port_1, int server_udp_socket_rtp, int server_udp_socket_rtcp, int server_udp_socket_rtp_1, int server_udp_socket_rtcp_1)
{
    printf("sig_0:%d, sig_2:%d, ture_of_tcp:%d, client_ip:%s, client_rtp_port:%d, client_rtp_port_1:%d, server_udp_socket_rtp:%d server_udp_socket_rtcp:%d server_udp_socket_rtp_1:%d,server_udp_socket_rtcp_1:%d\n",
           sig_0, sig_2, ture_of_tcp, client_ip, client_rtp_port, client_rtp_port_1, server_udp_socket_rtp, server_udp_socket_rtcp, server_udp_socket_rtp_1, server_udp_socket_rtcp_1);
    int istrueflag = 0;
    int pos = 0;
    int min_free_pos = FILEMAX;
    int fps;
    /*查看mp4info中是否已经存在该文件*/
    pthread_mutex_lock(&mut_mp4);
    for (int i = 0; i < FILEMAX; i++)
    {
        if (mp4info_arr[i] == NULL)
        {
            if (i < min_free_pos)
                min_free_pos = i;
            continue;
        }
        if (!strncmp(mp4info_arr[i]->filename, path_filename, strlen(path_filename))) // 客户端请求回放的文件已经在回放任务队列里面
        {
            pthread_mutex_lock(&mp4info_arr[i]->mut);
            istrueflag = 1;
            pos = i;
            // 把客户端添加到这个视频文件回放任务中的客户端队列中
            int posofclient = get_free_clientinfo(pos);
            if (posofclient < 0) // 超过一个视频文件回放任务所支持的最大客户端个数，一个回访任务最多支持FILEMAX(1024)个客户端
            {
                printf("over client maxnum\n");
                pthread_mutex_unlock(&mp4info_arr[pos]->mut);
                pthread_mutex_unlock(&mut_mp4);
                return -1;
            }
            mp4info_arr[pos]->clientinfo[posofclient].sd = client_sock_fd;

            if (ture_of_tcp == 1)
            {
                mp4info_arr[pos]->clientinfo[posofclient].transport = RTP_OVER_TCP;
                mp4info_arr[pos]->clientinfo[posofclient].sig_0 = sig_0; // video
                mp4info_arr[pos]->clientinfo[posofclient].sig_2 = sig_2; // audio
            }
            else
            {
                mp4info_arr[pos]->clientinfo[posofclient].transport = RTP_OVER_UDP;
                memset(mp4info_arr[pos]->clientinfo[posofclient].client_ip, 0, sizeof(mp4info_arr[pos]->clientinfo[posofclient].client_ip));
                memcpy(mp4info_arr[pos]->clientinfo[posofclient].client_ip, client_ip, strlen(client_ip));
                // video
                mp4info_arr[pos]->clientinfo[posofclient].udp_sd_rtp = server_udp_socket_rtp;
                mp4info_arr[pos]->clientinfo[posofclient].udp_sd_rtcp = server_udp_socket_rtcp;
                mp4info_arr[pos]->clientinfo[posofclient].client_rtp_port = client_rtp_port;
                // audio
                mp4info_arr[pos]->clientinfo[posofclient].udp_sd_rtp_1 = server_udp_socket_rtp_1;
                mp4info_arr[pos]->clientinfo[posofclient].udp_sd_rtcp_1 = server_udp_socket_rtcp_1;
                mp4info_arr[pos]->clientinfo[posofclient].client_rtp_port_1 = client_rtp_port_1;
            }

            mp4info_arr[pos]->count++;
            // video
            mp4info_arr[pos]->clientinfo[posofclient].rtp_packet = (struct RtpPacket *)malloc(VIDEO_DATA_MAX_SIZE);
            rtpHeaderInit(mp4info_arr[pos]->clientinfo[posofclient].rtp_packet, 0, 0, 0, RTP_VESION, RTP_PAYLOAD_TYPE_H26X, 0, 0, 0, 0x88923423);
            // audio
            mp4info_arr[pos]->clientinfo[posofclient].rtp_packet_1 = (struct RtpPacket *)malloc(VIDEO_DATA_MAX_SIZE);
            rtpHeaderInit(mp4info_arr[pos]->clientinfo[posofclient].rtp_packet_1, 0, 0, 0, RTP_VESION, getAudioType(mp4info_arr[pos]->media) == AUDIO_AAC ? RTP_PAYLOAD_TYPE_AAC : RTP_PAYLOAD_TYPE_PCMA, 0, 0, 0, 0x88923423);

            mp4info_arr[pos]->clientinfo[posofclient].tcp_header = malloc(sizeof(struct rtp_tcp_header));

            mp4info_arr[pos]->clientinfo[posofclient].packet_list = (struct MediaPacket_st *)malloc((RING_BUFFER_MAX / 4) * sizeof(struct MediaPacket_st));
            mp4info_arr[pos]->clientinfo[posofclient].packet_list_size = RING_BUFFER_MAX / 4;
            mp4info_arr[pos]->clientinfo[posofclient].packet_list_1 = (struct MediaPacket_st *)malloc((RING_BUFFER_MAX / 4) * sizeof(struct MediaPacket_st));
            mp4info_arr[pos]->clientinfo[posofclient].packet_list_size_1 = RING_BUFFER_MAX / 4;

            eventAdd(EPOLLOUT, &mp4info_arr[pos]->clientinfo[posofclient], sendData, &mp4info_arr[pos]->clientinfo[posofclient]);
            mp4info_arr[pos]->clientinfo[posofclient].playflag = 1;
            printf("append client ok fd:%d\n", mp4info_arr[pos]->clientinfo[posofclient].sd);
            pthread_mutex_unlock(&mp4info_arr[i]->mut);
            break;
        }
    }
    pthread_mutex_unlock(&mut_mp4);
    if (istrueflag == 0)
    {
        int ret = add1Mp4Info(min_free_pos, path_filename, client_sock_fd, sig_0, sig_2, ture_of_tcp, server_udp_socket_rtp, server_udp_socket_rtcp, server_udp_socket_rtp_1, server_udp_socket_rtcp_1, client_ip, client_rtp_port, client_rtp_port_1);
        if (ret < 0)
        {

            return -1;
        }
    }
    pthread_mutex_lock(&mut_clientcount);
    sum_client++;
    pthread_mutex_unlock(&mut_clientcount);
    return 1;
}
int getClientNum()
{
    int sum;
    pthread_mutex_lock(&mut_clientcount);
    sum = sum_client;
    pthread_mutex_unlock(&mut_clientcount);
    return sum;
}