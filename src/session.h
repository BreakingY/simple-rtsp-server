#ifndef _SESSION_H_
#define _SESSION_H_
#include "aac_rtp.h"
#include "pcma_rtp.h"
#include "common.h"
#include "h264_rtp.h"
#include "h265_rtp.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#define CLIENTMAX           1024
#define FILEMAX             1024
#define VIDEO_DATA_MAX_SIZE 2 * 1024 * 1024
#define RING_BUFFER_MAX     32
// #define SEND_EPOLL // If using epoll to send audio and video, the memory grows rapidly and needs to be fixed
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
/*Record the data channel and packets of the client*/
struct clientinfo_st
{
    int sd;          // client tcp socket
    // video
    int udp_sd_rtp;  // server rtp udp socket
    int udp_sd_rtcp; // server rtcp udp socket
    // audio
    int udp_sd_rtp_1;
    int udp_sd_rtcp_1;

    char client_ip[40];
    int client_rtp_port;
    int client_rtp_port_1;

    int transport; // enum TRANSPORT_e

    // RTP_OVER_TCP-->rtp sig
    int sig_0; // video
    int sig_2; // audio
    int playflag;

    void (*send_call_back)(void *arg); // Audio and video processing callback function
    int events;                        // EPOLLIN, EPLLOUT, EPOLLERR ,EPOLLRDHUP
    struct session_st *session;        // Point to session structure

    struct RtpPacket *rtp_packet;   // video 
    struct RtpPacket *rtp_packet_1; // audio
    struct rtp_tcp_header *tcp_header;

    // Circular buffer queue
    // video
    pthread_mutex_t mut_list;
    struct MediaPacket_st *packet_list;
    int packet_list_size; // Circular buffer queue size
    int pos_list;         // The next location to send data
    int packet_num;       // Number of data packets in the circular buffer queue
    int pos_last_packet;  // The available tail positions for the circular buffer queue

    // audio
    struct MediaPacket_st *packet_list_1;
    int packet_list_size_1;
    int pos_list_1;
    int packet_num_1;
    int pos_last_packet_1;
};
/*rtsp session*/
struct session_st
{
    void *media;
    char *filename;
    pthread_mutex_t mut;
    struct clientinfo_st clientinfo[CLIENTMAX]; // Client connection queue for session
    int count;
    int pos;

    // Custom session
    int is_custom;
    enum VIDEO_e video_type;
    enum AUDIO_e audio_type;
    int profile;
    int sample_rate;
    int channels;
};
#ifdef RTSP_FILE_SERVER
/**
 * File playback configuration
 * @param[in] file_reloop_flag  Does the file loop back? 0:not 1: is
 * @param[in]  mp4_file_path    The folder path where the file is located, if NULL default:./mp4path
 * @return 0:ok <0:error
 */
int configSession(int file_reloop_flag, const char *mp4_file_path);
#endif
/**
 * Linux signal processing callback function
 */
void sig_handler(int s);
/**
 * initialization, must be called at the beginning of the program
 * @return 0:ok <0:error
 */
int moduleInit();
/**
 * destroy, it can be called or not called. If called, it must be called at the end of the program
 */
void moduleDel();
/**
 * Initialize the client and set default information
 * @return 0:ok <0:error
 */
int initClient(struct session_st *session, struct clientinfo_st *clientinfo);
/**
 * Clear client connection
 * @return 0:ok <0:error
 */
int clearClient(struct clientinfo_st *clientinfo);

void pushFrameToList1(struct clientinfo_st *clientinfo, char *ptr, int ptr_len, int type);
void pushFrameToList2(struct clientinfo_st *clientinfo, char *ptr, int ptr_len, int type);

struct MediaPacket_st getFrameFromList1(struct clientinfo_st *clientinfo);
struct MediaPacket_st getFrameFromList2(struct clientinfo_st *clientinfo);

/**
 * Create client RTP connection
 * @return 0:ok <0:error
 */
int createClient(struct clientinfo_st *clientinfo, 
    int client_sock_fd, int sig_0, int sig_2, int ture_of_tcp, /*tcp*/
    int server_rtp_fd, int server_rtcp_fd, int server_rtp_fd_1, int server_rtcp_fd_1, char *client_ip, int client_rtp_port, int client_rtp_port_1 /*udp*/
    );
#ifdef RTSP_FILE_SERVER
/**
 * add file session
 * @return 0:ok <0:error
 */
int addFileSession(char *path_filename, 
                int client_sock_fd, int sig_0, int sig_2, int ture_of_tcp, /*tcp*/
                int server_rtp_fd, int server_rtcp_fd, int server_rtp_fd_1, int server_rtcp_fd_1, char *client_ip, int client_rtp_port, int client_rtp_port_1 /*udp*/
                );
/**
 * delete file session
 */
void delFileSession(struct session_st *session);
#endif
/**
 * add custom session(live)
 * @return context
 */
void* addCustomSession(const char* session_name);
/**
 * delete custom session
 */
void delCustomSession(void *context);
/**
 * add video(custom session)
 * @return 0:ok <0:error
 */
int addVideo(void *context, enum VIDEO_e type);
/**
 * add audio(custom session), profile for AAC
 * @return 0:ok <0:error
 */
int addAudio(void *context, enum AUDIO_e type, int profile, int sample_rate, int channels);
/**
 * on NALU h26x without startCode(custom session)
 * @return 0:ok <0:error
 */
int sendVideoData(void *context, uint8_t *data, int data_len);
/**
 * AAC without adts or PCMA(custom session)
 * @return 0:ok <0:error
 */
int sendAudioData(void *context, uint8_t *data, int data_len);

int getSessionAudioType(struct session_st *session);
int getSessionAudioInfo(struct session_st *session, int *sample_rate, int *channels, int *profile);
int getSessionVideoType(struct session_st *session);
/**
 * Does the session exist
 * @return 1:exit 0:not exit
 */
int sessionIsExist(char* suffix);
/**
 * generate SDP
 * @return 0:ok <0:error
 */
int sessionGenerateSDP(char *suffix, char *localIp, char *buffer, int buffer_len);

/**
 * add client
 * @return 0:ok <0:error
 */
int addClient(char* suffix, 
            int client_sock_fd, int sig_0, int sig_2, int ture_of_tcp, /*tcp*/
            char *client_ip, int client_rtp_port,int client_rtp_port_1, /*client udp info*/
            int server_udp_socket_rtp, int server_udp_socket_rtcp, int server_udp_socket_rtp_1, int server_udp_socket_rtcp_1 /*udp socket*/
            );
/**
 * otal number of client connections
 * @return client numbers
 */
int getClientNum();

#endif