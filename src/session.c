#include "session.h"
#include "media.h"
#include "rtsp_message.h"
#include "io_epoll.h"

#define SESSION_DEBUG
static char mp4Dir[1024];
static int reloop_flag = 1;
static pthread_t event_thd;

static struct session_st *session_arr[FILEMAX]; // Session array, dynamically add and delete
static pthread_mutex_t mut_session = PTHREAD_MUTEX_INITIALIZER;
/*
 * lock  mut_session(session_arr) --> lock session_arr[i].mutx(session_st) --> lock session_arr[i].clientinfo_st[j].mut_list(To read and write to the circular buffer queue of the clientinfo_st)
 *  --> unlock clientinfo_st.mut_list --> unlock session_st.mutx --> unlock mut_session
 * Not all operations require the addition of the above three locks, but when adding more locks, follow the locking sequence above to prevent deadlocks
 */

static int sum_client = 0; // Record how many clients are currently connecting to the server in total
static pthread_mutex_t mut_clientcount = PTHREAD_MUTEX_INITIALIZER;
static int eventAdd(int events, struct clientinfo_st *ev){
    if (ev->sd < 0)
        return -1;
    // media
    if(ev->transport == RTP_OVER_TCP){
        event_data_ptr_t *event_data = (event_data_ptr_t *)malloc(sizeof(event_data_ptr_t));
        event_data->user_data = (void *)ev;
        event_data->fd = ev->sd;
        event_data->fd_type = FD_TYPE_TCP;
        ev->events = events | EVENT_IN; // client heartbeat(rtsp)
        if(addEvent(ev->events, event_data) < 0){
            return -1;
        }
    }
    else{
        // Monitor client TCP connections and handle client shutdown events
        event_data_ptr_t *event_data = (event_data_ptr_t *)malloc(sizeof(event_data_ptr_t));
        event_data->user_data = (void *)ev;
        event_data->fd = ev->sd;
        event_data->fd_type = FD_TYPE_UDP_RTP;
        ev->events = events | EVENT_IN; // client heartbeat(rtsp)
        if(addEvent(ev->events, event_data) < 0){
            return -1;
        }

        if (ev->udp_sd_rtp != -1){ // video
            event_data_ptr_t *event_data = (event_data_ptr_t *)malloc(sizeof(event_data_ptr_t));
            event_data->user_data = (void *)ev;
            event_data->fd = ev->udp_sd_rtp;
            event_data->fd_type = FD_TYPE_UDP_RTP;
            ev->events = events;
            if(addEvent(ev->events, event_data) < 0){
                return -1;
            }
        }
        if(ev->udp_sd_rtp_1 != -1){ // audio
            event_data_ptr_t *event_data = (event_data_ptr_t *)malloc(sizeof(event_data_ptr_t));
            event_data->user_data = (void *)ev;
            event_data->fd = ev->udp_sd_rtp_1;
            event_data->fd_type = FD_TYPE_UDP_RTP;
            ev->events = events;
            if(addEvent(ev->events, event_data) < 0){
                return -1;
            }
        }
    }
    return 0;
}
// handle client heartbeat(rtsp) or TEARDOWN or RTCP(Just care about TCP's RTCP and UDP's direct dropout)
// TCP data must be processed, otherwise it will block the other end. UDP does not have this problem
static int handleClientTcpData(event_data_ptr_t *event_data){
    struct clientinfo_st *clientinfo = (struct clientinfo_st *)event_data->user_data;
    int type = event_data->fd_type;
    int fd = event_data->fd;
    if(clientinfo == NULL){
        return -1;
    }
    if(fd != clientinfo->sd){
        return -1;
    }
    char buffer_send[4096];
    int recv_len = recvWithTimeout(clientinfo->sd, clientinfo->buffer + clientinfo->pos, sizeof(clientinfo->buffer) - clientinfo->pos, 0);
    if(recv_len <= 0){
        return -1;
    }
    clientinfo->len += recv_len;
    clientinfo->pos = clientinfo->len;
    clientinfo->buffer[clientinfo->len] = 0;
    if(clientinfo->buffer[0] == '$'){ // RTCP
        int rtcp_len = 0;
        if(clientinfo->len >= 4){
            rtcp_len = (clientinfo->buffer[2] << 8) | clientinfo->buffer[3];
        }
        if((clientinfo->len - 4) >= rtcp_len){
            // skip rtcp data
            clientinfo->len -= rtcp_len/*RTCP*/ + 4/*rtp tcp header*/;
            memmove(clientinfo->buffer, clientinfo->buffer + rtcp_len + 4, clientinfo->len);
            clientinfo->pos = clientinfo->len;
        }
    }
    else{ // MESSAGE
        struct rtsp_request_message_st request_message;
        memset(&request_message, 0, sizeof(struct rtsp_request_message_st));
        int parse_used = parseRtspRequest(clientinfo->buffer, clientinfo->len, &request_message);
        if(parse_used < 0){
            return -1;
        }
        char *CSeq = findValueByKey(&request_message, "CSeq");
        char *Session = findValueByKey(&request_message, "Session");
        if(CSeq != NULL){
            int cseq = atoi(CSeq);
            handleCmd_General(buffer_send, cseq, Session);
            if(sendWithTimeout(clientinfo->sd, (const char*)buffer_send, strlen(buffer_send), 0) <= 0){
                return -1;
            }
            int i;
            int cnt = clientinfo->len;
            for(i = 0; i < cnt; i++){ // skip RTSP MESSAGE
                if(clientinfo->buffer[i] == '$'){
                    break;
                }
                clientinfo->len--;
            }
            memmove(clientinfo->buffer, clientinfo->buffer + i, clientinfo->len);
        }
        clientinfo->pos = clientinfo->len;
    }
    return 0;
}
static int sendClientMedia(event_data_ptr_t *event_data){
    struct clientinfo_st *clientinfo = (struct clientinfo_st *)event_data->user_data;
    int type = event_data->fd_type;
    int fd = event_data->fd;
    if(clientinfo == NULL){
        return -1;
    }
    // Retrieve data from the circular queue and send it
    pthread_mutex_lock(&clientinfo->session->mut); // Reading the client info in the session, locking is required
    pthread_mutex_lock(&clientinfo->mut_list);
    struct MediaPacket_st node;
    node.size = 0;
    if(fd == clientinfo->sd && ((clientinfo->sig_0 != -1) || (clientinfo->sig_2 != -1)) && (type == FD_TYPE_TCP)){ // rtp over tcp
        // Extract a frame of audio or video
        node = getFrameFromList1(clientinfo);
    }
    else if(fd == clientinfo->udp_sd_rtp && type == FD_TYPE_UDP_RTP){ // video
        // Extract a frame of video
        node = getFrameFromList1(clientinfo);
    }
    else if(fd == clientinfo->udp_sd_rtp_1 && type == FD_TYPE_UDP_RTP){ // audio
        // Extract a frame of audio
        node = getFrameFromList2(clientinfo);
    }
    pthread_mutex_unlock(&clientinfo->mut_list);
    pthread_mutex_unlock(&clientinfo->session->mut);
    if (node.size == 0){ // No data to send
        return 0;
    }
    enum VIDEO_e video_type = getSessionVideoType(clientinfo->session);
    int sample_rate;
    int channels;
    int profile;
    int ret;
    ret = getSessionAudioInfo(clientinfo->session, &sample_rate, &channels, &profile);
    enum AUDIO_e audio_type = getSessionAudioType(clientinfo->session);
    if (fd == clientinfo->sd){ // rtp over tcp
        if(node.type == VIDEO){
            switch(video_type){
                case VIDEO_H264:
                    ret = rtpSendH264Frame(clientinfo->sd, clientinfo->tcp_header, clientinfo->rtp_packet, node.data, node.size, 0, clientinfo->sig_0, NULL, -1);
                    break;
                case VIDEO_H265:
                    ret = rtpSendH265Frame(clientinfo->sd, clientinfo->tcp_header, clientinfo->rtp_packet, node.data, node.size, 0, clientinfo->sig_0, NULL, -1);
                    break;
                default:
                    break;
            }
        }
        else{
            switch(audio_type){
                case AUDIO_AAC:
                    ret = rtpSendAACFrame(clientinfo->sd, clientinfo->tcp_header, clientinfo->rtp_packet_1, node.data, node.size, sample_rate, channels, profile, clientinfo->sig_2, NULL, -1);
                    break;
                case AUDIO_PCMA:
                    ret = rtpSendPCMAFrame(clientinfo->sd, clientinfo->tcp_header, clientinfo->rtp_packet_1, node.data, node.size, sample_rate, channels, profile, clientinfo->sig_2, NULL, -1);
                    break;
                default:
                    break;
            }
        }
    }
    else{ // rtp over udp
        if(node.type == VIDEO){
            switch(video_type){
                case VIDEO_H264:
                    ret = rtpSendH264Frame(clientinfo->udp_sd_rtp, NULL, clientinfo->rtp_packet, node.data, node.size, 0, -1, clientinfo->client_ip, clientinfo->client_rtp_port);
                    break;
                case VIDEO_H265:
                    ret = rtpSendH265Frame(clientinfo->udp_sd_rtp, NULL, clientinfo->rtp_packet, node.data, node.size, 0, -1, clientinfo->client_ip, clientinfo->client_rtp_port);
                    break;
                default:
                    break;
            }
        }
        else{
            switch(audio_type){
                case AUDIO_AAC:
                    ret = rtpSendAACFrame(clientinfo->udp_sd_rtp_1, NULL, clientinfo->rtp_packet_1, node.data, node.size, sample_rate, channels, profile, -1, clientinfo->client_ip, clientinfo->client_rtp_port_1);
                    break;
                case AUDIO_PCMA:
                    ret = rtpSendPCMAFrame(clientinfo->udp_sd_rtp_1, NULL, clientinfo->rtp_packet_1, node.data, node.size, sample_rate, channels, profile, -1, clientinfo->client_ip, clientinfo->client_rtp_port_1);
                    break;
                default:
                    break;
            }
        }
    }
    if(ret <= 0){
        return -1;
    }
    return 0;
}
static int eventDel(struct clientinfo_st *ev)
{
    if(ev->sd < 0)
        return -1;
    event_data_ptr_t *event_data = (event_data_ptr_t *)malloc(sizeof(event_data_ptr_t));
    event_data->user_data = (void *)ev;
    event_data->fd = ev->sd;
    if(delEvent(event_data) < 0){
        return -1;
    }
    if(ev->udp_sd_rtp != -1){
        event_data->fd = ev->udp_sd_rtp;
        if(delEvent(event_data) < 0){
            return -1;
        }
    }
    if(ev->udp_sd_rtp_1 != -1){
        event_data->fd = ev->udp_sd_rtp_1;
        if(delEvent(event_data) < 0){
            return -1;
        }
    }
    free(event_data);
    return 0;
}
static int delClient(event_data_ptr_t *event_data){
    struct clientinfo_st *clientinfo = (struct clientinfo_st *)event_data->user_data;
    int type = event_data->fd_type;
    int fd = event_data->fd;
    if(clientinfo == NULL || clientinfo->sd < 0){
        return -1;
    }
    if(eventDel(clientinfo) < 0){ // Delete all listening sockets(TCP/UDP)
        return -1;
    }
    if(event_data){
        free(event_data);
    }
#ifdef SESSION_DEBUG
    printf("client:%d offline\n", clientinfo->sd);
#endif
    struct session_st *session = clientinfo->session;
    int count = 0;
    pthread_mutex_lock(&clientinfo->session->mut);
    clearClient(clientinfo);
    session->count--;
    count = session->count;
    pthread_mutex_unlock(&clientinfo->session->mut);
    /*Change the total number of customer connections*/
    pthread_mutex_lock(&mut_clientcount);
    sum_client--;
#ifdef SESSION_DEBUG
    printf("sum_client:%d\n", sum_client);
#endif
    pthread_mutex_unlock(&mut_clientcount);
    if(count == 0){
        if(clientinfo->session->is_custom == 0){
#ifdef RTSP_FILE_SERVER
            delFileSession(session_arr[clientinfo->session->pos]);
#endif
        }
    }
    return 0;
}
#ifdef RTSP_FILE_SERVER
/*Audio and video queue operation*/
static void sendData(void *arg)
{
    struct clientinfo_st *clientinfo = (struct clientinfo_st *)arg;
    pthread_mutex_lock(&clientinfo->mut_list);
    if(clientinfo->packet_num >= RING_BUFFER_MAX){
        printf("WARING ring buffer too large\n");
    }
    // The data packet is sent to the circular queue
    if(nowStreamIsVideo(clientinfo->session->media) && (clientinfo->sig_0 != -1 || clientinfo->client_rtp_port != -1)){ // video
        char *ptr = NULL;
        int ptr_len = 0;
        getVideoNALUWithoutStartCode(clientinfo->session->media, &ptr, &ptr_len);
        pushFrameToList1(clientinfo, ptr, ptr_len, VIDEO);
    }
    if(nowStreamIsAudio(clientinfo->session->media) && (clientinfo->sig_2 != -1 || clientinfo->client_rtp_port_1 != -1)){ // audio
        char *ptr = NULL;
        int ptr_len = 0;
        getAudioWithoutADTS(clientinfo->session->media, &ptr, &ptr_len);
        if(clientinfo->client_rtp_port_1 != -1){ // UDP, Use different queues for audio and video
            pushFrameToList2(clientinfo, ptr, ptr_len, AUDIO);
        }
        else if(clientinfo->sig_2 != -1){ // TCP, audio and video use the same queue
            pushFrameToList1(clientinfo, ptr, ptr_len, AUDIO);
        }
    }
    pthread_mutex_unlock(&clientinfo->mut_list);
    return;
}
#endif
static int get_free_clientinfo(int pos)
{
    for(int i = 0; i < CLIENTMAX; i++){
        if(session_arr[pos]->clientinfo[i].sd == -1){
            return i;
        }
    }
    return -1;
}
int configSession(int file_reloop_flag, const char *mp4_file_path) {
    reloop_flag = file_reloop_flag;
    memset(mp4Dir, 0, sizeof(mp4Dir));
    if(mp4_file_path != NULL) {
        if(mp4_file_path[strlen(mp4_file_path) - 1] == '/') {
            strncpy(mp4Dir, mp4_file_path, sizeof(mp4Dir) - 1);
        } 
        else{
            size_t len = strlen(mp4_file_path);
            strncpy(mp4Dir, mp4_file_path, sizeof(mp4Dir) - 2);
            mp4Dir[len] = '/';
        }
    }
    else{
        memcpy(mp4Dir,"./mp4path/", strlen("./mp4path/"));
    }
#ifdef SESSION_DEBUG
    printf("reloop_flag:%d mp4Dir:%s\n", reloop_flag, mp4Dir);
#endif
    return 0;
}
void sig_handler(int s)
{
    printf("catch signal %d,rtsp exit\n", s);
    moduleDel();
    printf("%s\n", __func__);
    exit(1);
}
int moduleInit()
{
    memset(session_arr, 0, sizeof(struct session_st *));
    if(createEvent() < 0){
        return -1;
    }
    setEventCallback(handleClientTcpData, sendClientMedia, delClient);
    int ret = pthread_create(&event_thd, NULL, startEventLoop, NULL);
    if(ret < 0){
        perror("startEventLoop pthread_create()");
        return -1;
    }
    pthread_detach(event_thd);
    return 0;
}

void moduleDel()
{
#ifdef RTSP_FILE_SERVER
    for(int i = 0; i < FILEMAX; i++){
        delFileSession(session_arr[i]);
    }
#endif
    pthread_mutex_destroy(&mut_session);
    pthread_mutex_destroy(&mut_clientcount);
    stopEventLoop();
    closeEvent();
    return;
}
int initClient(struct session_st *session, struct clientinfo_st *clientinfo)
{
    if(session == NULL || clientinfo == NULL){
        return -1;
    }
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

    clientinfo->send_call_back = NULL;
    clientinfo->events = -1;
    clientinfo->session = session;

    clientinfo->rtp_packet = NULL;
    clientinfo->rtp_packet_1 = NULL;
    clientinfo->tcp_header = NULL;

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
    return 0;
}
int clearClient(struct clientinfo_st *clientinfo)
{
    if(clientinfo == NULL){
        return -1;
    }
    if(clientinfo->sd > 0){
        closeSocket(clientinfo->sd);
        clientinfo->sd = -1;
    }
    if(clientinfo->udp_sd_rtp > 0){
        closeSocket(clientinfo->udp_sd_rtp);
        clientinfo->udp_sd_rtp = -1;
    }
    if(clientinfo->udp_sd_rtcp > 0){
        closeSocket(clientinfo->udp_sd_rtcp);
        clientinfo->udp_sd_rtcp = -1;
    }
    if(clientinfo->udp_sd_rtp_1 > 0){
        closeSocket(clientinfo->udp_sd_rtp_1);
        clientinfo->udp_sd_rtp_1 = -1;
    }
    if(clientinfo->udp_sd_rtcp_1 > 0){
        closeSocket(clientinfo->udp_sd_rtcp_1);
        clientinfo->udp_sd_rtcp_1 = -1;
    }
    memset(clientinfo->client_ip, 0, sizeof(clientinfo->client_ip));
    clientinfo->client_rtp_port = -1;
    clientinfo->client_rtp_port_1 = -1;
    clientinfo->transport = -1;
    clientinfo->sig_0 = -1;
    clientinfo->sig_2 = -1;
    clientinfo->playflag = -1;

    clientinfo->send_call_back = NULL;
    clientinfo->events = -1;
    // clientinfo->session=NULL;

    if(clientinfo->rtp_packet != NULL){
        free(clientinfo->rtp_packet);
        clientinfo->rtp_packet = NULL;
    }
    if(clientinfo->rtp_packet_1 != NULL){
        free(clientinfo->rtp_packet_1);
        clientinfo->rtp_packet_1 = NULL;
    }
    if(clientinfo->tcp_header != NULL){
        free(clientinfo->tcp_header);
        clientinfo->tcp_header = NULL;
    }

    // video
    pthread_mutex_destroy(&clientinfo->mut_list);
    if(clientinfo->packet_list != NULL){
        free(clientinfo->packet_list);
        clientinfo->packet_list = NULL;
    }
    clientinfo->packet_list_size = 0;
    clientinfo->pos_list = 0;
    clientinfo->packet_num = 0;
    clientinfo->pos_last_packet = 0;

    // audio
    if(clientinfo->packet_list_1 != NULL){
        free(clientinfo->packet_list_1);
        clientinfo->packet_list_1 = NULL;
    }
    clientinfo->packet_list_size_1 = 0;
    clientinfo->pos_list_1 = 0;
    clientinfo->packet_num_1 = 0;
    clientinfo->pos_last_packet_1 = 0;

    return 0;
}
static int increaseClientList(enum MEDIA_e type, struct clientinfo_st *clientinfo)
{
    // |packet5|packet6|packet7|packet8|packet1(pos)|packet2|packet3|packet4| --> |packet5|packet6|packet7|packet8|space1|space2|space3|space4|packet1(pos)|packet2|packet3|packet4|
    if(type == VIDEO){
        if (clientinfo->packet_num >= clientinfo->packet_list_size){ // The buffer is used up, increase the buffer
            clientinfo->packet_list = (struct MediaPacket_st *)realloc(clientinfo->packet_list, (clientinfo->packet_list_size + 4) * sizeof(struct MediaPacket_st));
            memmove(clientinfo->packet_list + clientinfo->pos_list + 4, clientinfo->packet_list + clientinfo->pos_list, (clientinfo->packet_list_size - clientinfo->pos_list) * sizeof(struct MediaPacket_st));
            clientinfo->packet_list_size += 4;
            clientinfo->pos_list += 4;
        }
    }
    else if(type == AUDIO){
        if(clientinfo->packet_num_1 >= clientinfo->packet_list_size_1){ // The buffer is used up, increase the buffer
            clientinfo->packet_list_1 = (struct MediaPacket_st *)realloc(clientinfo->packet_list_1, (clientinfo->packet_list_size_1 + 4) * sizeof(struct MediaPacket_st));
            memmove(clientinfo->packet_list_1 + clientinfo->pos_list_1 + 4, clientinfo->packet_list_1 + clientinfo->pos_list_1, (clientinfo->packet_list_size_1 - clientinfo->pos_list_1) * sizeof(struct MediaPacket_st));
            clientinfo->packet_list_size_1 += 4;
            clientinfo->pos_list_1 += 4;
        }
    }
    return 0;
}
void pushFrameToList1(struct clientinfo_st *clientinfo, char *ptr, int ptr_len, int type){
    increaseClientList(VIDEO, clientinfo);
    memcpy(clientinfo->packet_list[clientinfo->pos_last_packet].data, ptr, ptr_len);
    clientinfo->packet_list[clientinfo->pos_last_packet].size = ptr_len;
    clientinfo->packet_list[clientinfo->pos_last_packet].type = type;
    clientinfo->packet_num++;
    clientinfo->pos_last_packet++;
    if(clientinfo->pos_last_packet >= clientinfo->packet_list_size){
        clientinfo->pos_last_packet = 0;
    }
    return;
}
void pushFrameToList2(struct clientinfo_st *clientinfo, char *ptr, int ptr_len, int type){
    increaseClientList(AUDIO, clientinfo);
    memcpy(clientinfo->packet_list_1[clientinfo->pos_last_packet_1].data, ptr, ptr_len);
    clientinfo->packet_list_1[clientinfo->pos_last_packet_1].size = ptr_len;
    clientinfo->packet_list_1[clientinfo->pos_last_packet_1].type = type;
    clientinfo->packet_num_1++;
    clientinfo->pos_last_packet_1++;
    if(clientinfo->pos_last_packet_1 >= clientinfo->packet_list_size_1){
        clientinfo->pos_last_packet_1 = 0;
    }
    return;
}
struct MediaPacket_st getFrameFromList1(struct clientinfo_st *clientinfo){
    struct MediaPacket_st node;
    node.size = 0;
    if(clientinfo->packet_num > 0 && clientinfo->pos_list < clientinfo->packet_list_size){
        memcpy(node.data, clientinfo->packet_list[clientinfo->pos_list].data, clientinfo->packet_list[clientinfo->pos_list].size);
        node.size = clientinfo->packet_list[clientinfo->pos_list].size;
        node.type = clientinfo->packet_list[clientinfo->pos_list].type;
        clientinfo->pos_list++;
        clientinfo->packet_num--;
        if(clientinfo->pos_list >= clientinfo->packet_list_size){
            clientinfo->pos_list = 0;
        }
    }
    return node;
}
struct MediaPacket_st getFrameFromList2(struct clientinfo_st *clientinfo){
    struct MediaPacket_st node;
    node.size = 0;
    if(clientinfo->packet_num_1 > 0 && clientinfo->pos_list_1 < clientinfo->packet_list_size_1){
        memcpy(node.data, clientinfo->packet_list_1[clientinfo->pos_list_1].data, clientinfo->packet_list_1[clientinfo->pos_list_1].size);
        node.size = clientinfo->packet_list_1[clientinfo->pos_list_1].size;
        node.type = clientinfo->packet_list_1[clientinfo->pos_list_1].type;
        clientinfo->pos_list_1++;
        clientinfo->packet_num_1--;
        if(clientinfo->pos_list_1 >= clientinfo->packet_list_size_1){
            clientinfo->pos_list_1 = 0;
        }
    }
    return node;
}
int createClient(struct clientinfo_st *clientinfo, 
    int client_sock_fd, int sig_0, int sig_2, int ture_of_tcp, /*tcp*/
    int server_rtp_fd, int server_rtcp_fd, int server_rtp_fd_1, int server_rtcp_fd_1, char *client_ip, int client_rtp_port, int client_rtp_port_1 /*udp*/
    )
{
    if((clientinfo == NULL) || (client_sock_fd < 0)){
        return -1;
    }
    clientinfo->sd = client_sock_fd;
#ifdef RTSP_FILE_SERVER
    clientinfo->send_call_back = sendData; // only file session
#endif
    if(ture_of_tcp == 1){
        clientinfo->transport = RTP_OVER_TCP;
        clientinfo->sig_0 = sig_0;
        clientinfo->sig_2 = sig_2;
    }
    else{
        clientinfo->transport = RTP_OVER_UDP;
        memset(clientinfo->client_ip, 0, sizeof(clientinfo->client_ip));
        memcpy(clientinfo->client_ip, client_ip, strlen(client_ip));
        clientinfo->udp_sd_rtp = server_rtp_fd;
        clientinfo->udp_sd_rtcp = server_rtcp_fd;
        clientinfo->udp_sd_rtp_1 = server_rtp_fd_1;
        clientinfo->udp_sd_rtcp_1 = server_rtcp_fd_1;
        clientinfo->client_rtp_port = client_rtp_port;
        clientinfo->client_rtp_port_1 = client_rtp_port_1;
    }
    // video
    clientinfo->rtp_packet = (struct RtpPacket *)malloc(VIDEO_DATA_MAX_SIZE);
    rtpHeaderInit(clientinfo->rtp_packet, 0, 0, 0, RTP_VESION, RTP_PAYLOAD_TYPE_H26X, 0, 0, 0, 0x88923423);
    // audio
    clientinfo->rtp_packet_1 = (struct RtpPacket *)malloc(VIDEO_DATA_MAX_SIZE);
    rtpHeaderInit(clientinfo->rtp_packet_1, 0, 0, 0, RTP_VESION, getSessionAudioType(clientinfo->session) == AUDIO_AAC ? RTP_PAYLOAD_TYPE_AAC : RTP_PAYLOAD_TYPE_PCMA, 0, 0, 0, 0x88923423);

    clientinfo->tcp_header = malloc(sizeof(struct rtp_tcp_header));

    clientinfo->packet_list = (struct MediaPacket_st *)malloc((RING_BUFFER_MAX / 4) * sizeof(struct MediaPacket_st));
    clientinfo->packet_list_size = RING_BUFFER_MAX / 4;
    clientinfo->packet_list_1 = (struct MediaPacket_st *)malloc((RING_BUFFER_MAX / 4) * sizeof(struct MediaPacket_st));
    clientinfo->packet_list_size_1 = RING_BUFFER_MAX / 4;

    clientinfo->playflag = 1;
    return 0;
}
static int sendDataToClient(struct clientinfo_st *clientinfo, char *ptr, int ptr_len, int type){
    int ret = 0;
    enum VIDEO_e video_type = getSessionVideoType(clientinfo->session);
    int sample_rate;
    int channels;
    int profile;
    ret = getSessionAudioInfo(clientinfo->session, &sample_rate, &channels, &profile);
    enum AUDIO_e audio_type = getSessionAudioType(clientinfo->session);
    if(type == VIDEO){
        if(clientinfo->udp_sd_rtp != -1){ // udp
            switch(video_type){
                case VIDEO_H264:
                    ret = rtpSendH264Frame(clientinfo->udp_sd_rtp, NULL, clientinfo->rtp_packet, ptr, ptr_len, 0, -1, clientinfo->client_ip, clientinfo->client_rtp_port);
                    break;
                case VIDEO_H265:
                    ret = rtpSendH265Frame(clientinfo->udp_sd_rtp, NULL, clientinfo->rtp_packet, ptr, ptr_len, 0, -1, clientinfo->client_ip, clientinfo->client_rtp_port);
                    break;
                default:
                    break;
            }
        }
        else{ // tcp
            switch(video_type){
                case VIDEO_H264:
                    ret = rtpSendH264Frame(clientinfo->sd, clientinfo->tcp_header, clientinfo->rtp_packet, ptr, ptr_len, 0, clientinfo->sig_0, NULL, -1);
                    break;
                case VIDEO_H265:
                    ret = rtpSendH265Frame(clientinfo->sd, clientinfo->tcp_header, clientinfo->rtp_packet, ptr, ptr_len, 0, clientinfo->sig_0, NULL, -1);
                    break;
                default:
                    break;
            }
        }
    }
    else if(type == AUDIO){
        if(clientinfo->udp_sd_rtp_1 != -1){ // udp
            switch(audio_type){
                case AUDIO_AAC:
                    ret = rtpSendAACFrame(clientinfo->udp_sd_rtp_1, NULL, clientinfo->rtp_packet_1, ptr, ptr_len, sample_rate, channels, profile, -1, clientinfo->client_ip, clientinfo->client_rtp_port_1);
                    break;
                case AUDIO_PCMA:
                    ret = rtpSendPCMAFrame(clientinfo->udp_sd_rtp_1, NULL, clientinfo->rtp_packet_1, ptr, ptr_len, sample_rate, channels, profile, -1, clientinfo->client_ip, clientinfo->client_rtp_port_1);
                    break;
                default:
                    break;
            }
        }
        else{ // tcp
            switch(audio_type){
                case AUDIO_AAC:
                    ret = rtpSendAACFrame(clientinfo->sd, clientinfo->tcp_header, clientinfo->rtp_packet_1, ptr, ptr_len, sample_rate, channels, profile, clientinfo->sig_2, NULL, -1);
                    break;
                case AUDIO_PCMA:
                    ret = rtpSendPCMAFrame(clientinfo->sd, clientinfo->tcp_header, clientinfo->rtp_packet_1, ptr, ptr_len, sample_rate, channels, profile, clientinfo->sig_2, NULL, -1);
                    break;
                default:
                    break;
            }
        }
    }
    else{
        return -1;
    }
    return ret;
}
#ifdef RTSP_FILE_SERVER
/*Audio and video callback function*/
static void mediaCallBack(void *arg){
    struct session_st *session = (struct session_st *)arg;
    if(session == NULL){
        return;
    }
    int ret = 0;
    pthread_mutex_lock(&session->mut);
    for(int i = 0; i < CLIENTMAX; i++){
        if(session->clientinfo[i].sd != -1 && session->clientinfo[i].send_call_back != NULL && session->clientinfo[i].playflag == 1){
#ifdef SEND_DATA_EVENT
            session->clientinfo[i].send_call_back(&session->clientinfo[i]);
#else
            // Directly sending audio and video data to the client, not applicable to epoll
            struct clientinfo_st *clientinfo = &session->clientinfo[i];
            if(nowStreamIsVideo(clientinfo->session->media) && (clientinfo->sig_0 != -1 || clientinfo->client_rtp_port != -1)){ // video
                char *ptr = NULL;
                int ptr_len = 0;
                getVideoNALUWithoutStartCode(clientinfo->session->media, &ptr, &ptr_len);
                ret = sendDataToClient(clientinfo, ptr, ptr_len, VIDEO);
                if(ret <= 0){
                    // do nothing, epoll_loop will delete client
                }
            }
            if(nowStreamIsAudio(clientinfo->session->media) && (clientinfo->sig_2 != -1 || clientinfo->client_rtp_port_1 != -1)){ // audio
                char *ptr = NULL;
                int ptr_len = 0;
                getAudioWithoutADTS(clientinfo->session->media, &ptr, &ptr_len);
                ret = sendDataToClient(clientinfo, ptr, ptr_len, AUDIO);
                if(ret <= 0){
                    // do nothing, epoll_loop will delete client
                }
            }
#endif
        }
    }
    pthread_mutex_unlock(&session->mut);
    return;
}
static void reloopCallBack(void *arg){
    struct session_st *session = (struct session_st *)arg;
    if(session == NULL){
        return;
    }
    if(reloop_flag == 1){
        return;
    }
    delFileSession(session_arr[session->pos]);
    return;
}
/*Create a file session and add one client*/
int addFileSession(char *path_filename, 
    int client_sock_fd, int sig_0, int sig_2, int ture_of_tcp, /*tcp*/
    int server_rtp_fd, int server_rtcp_fd, int server_rtp_fd_1, int server_rtcp_fd_1, char *client_ip, int client_rtp_port, int client_rtp_port_1 /*udp*/
    )
{
    if(path_filename == NULL){
        return -1;
    }
    pthread_mutex_lock(&mut_session);
    int pos = FILEMAX;
    for (int i = 0; i < FILEMAX; i++){
        if (session_arr[i] == NULL){
            if (i < pos){
                pos = i;
            }
            continue;
        }
    }
    struct session_st *session;
    session = malloc(sizeof(struct session_st));
    memset(session, 0, sizeof(struct session_st));
    session->media = creatMedia(path_filename, mediaCallBack, reloopCallBack, session);

    session->filename = malloc(strlen(path_filename) + 1);
    memset(session->filename, 0, strlen(path_filename) + 1);
    memcpy(session->filename, path_filename, strlen(path_filename));
#ifdef SESSION_DEBUG
    printf("addFileSession:%s client_sock_fd:%d\n", session->filename, client_sock_fd);
#endif
    pthread_mutex_init(&session->mut, NULL);
    session->count = 0;

    session_arr[pos] = session;
    session->pos = pos;
    for(int j = 0; j < CLIENTMAX; j++){
        initClient(session_arr[pos], &session_arr[pos]->clientinfo[j]);
    }
    pthread_mutex_lock(&session_arr[pos]->mut);
    session_arr[pos]->count++;
    // add one client
    createClient(&session_arr[pos]->clientinfo[0], 
        client_sock_fd, sig_0, sig_2, ture_of_tcp, /*tcp*/
        server_rtp_fd, server_rtcp_fd,server_rtp_fd_1, server_rtcp_fd_1, client_ip, client_rtp_port, client_rtp_port_1 /*udp*/
        );
    int events = EVENT_ERR|EVENT_RDHUP|EVENT_HUP;
#ifdef SEND_DATA_EVENT
    events |= EVENT_OUT;
#endif
    eventAdd(events, &session_arr[pos]->clientinfo[0]);
    pthread_mutex_unlock(&session_arr[pos]->mut);
    pthread_mutex_unlock(&mut_session);
    return 0;
}
#endif
void* addCustomSession(const char* session_name){
    if(session_name == NULL){
        return NULL;
    }
    int min_free_pos = FILEMAX;
    char path_filename[1024] = {0};
    memcpy(path_filename, mp4Dir, strlen(mp4Dir));
    memcpy(path_filename + strlen(mp4Dir), session_name, strlen(session_name));
    /*Check if the session already exists*/
    pthread_mutex_lock(&mut_session);
    for(int i = 0; i < FILEMAX; i++){
        if(session_arr[i] == NULL){
            if(i < min_free_pos)
                min_free_pos = i;
            continue;
        }
        if(!strncmp(session_arr[i]->filename, path_filename, strlen(path_filename))){
            pthread_mutex_unlock(&mut_session);
            return session_arr[i];
        }
    }
    struct session_st *session;
    session = malloc(sizeof(struct session_st));
    memset(session, 0, sizeof(struct session_st));
    session->is_custom = 1;
    session->audio_type = AUDIO_NONE;
    session->video_type = VIDEO_NONE;

    session->filename = malloc(strlen(path_filename) + 1);
    memset(session->filename, 0, strlen(path_filename) + 1);
    memcpy(session->filename, path_filename, strlen(path_filename));
#ifdef SESSION_DEBUG
    printf("addCustomSession:%s\n", session->filename);
#endif
    pthread_mutex_init(&session->mut, NULL);
    session->count = 0;

    session_arr[min_free_pos] = session;
    session->pos = min_free_pos;
    for(int j = 0; j < CLIENTMAX; j++){
        initClient(session_arr[min_free_pos], &session_arr[min_free_pos]->clientinfo[j]);
    }
    pthread_mutex_unlock(&mut_session);
    return session;
}
static int delAndFreeSession(struct session_st *session){
    if(session == NULL){
        return -1;
    }
    int client_num = 0;
    pthread_mutex_lock(&mut_session);
#ifdef SESSION_DEBUG
    printf("delAndFreeSession:%s\n", session->filename);
#endif
    pthread_mutex_lock(&session->mut);
    for(int i = 0; i < CLIENTMAX; i++){
        if(session->clientinfo[i].sd > 0){
            client_num++;
            eventDel(&session->clientinfo[i]);
        }
        clearClient(&session->clientinfo[i]);
    }
   
    if(session->filename){
        free(session->filename);
        session->filename = NULL;
    }
    pthread_mutex_unlock(&session->mut);
    pthread_mutex_destroy(&session->mut);
    session_arr[session->pos] = NULL;
    free(session);
    pthread_mutex_unlock(&mut_session);

    pthread_mutex_lock(&mut_clientcount);
    sum_client -= client_num;
    int cnt = sum_client;
    pthread_mutex_unlock(&mut_clientcount);
#ifdef SESSION_DEBUG
    printf("sum_client:%d\n",cnt);
#endif
    return 0;
}
#ifdef RTSP_FILE_SERVER
/*delete file session*/
void delFileSession(struct session_st *session)
{
    if(session == NULL || session->is_custom == 1){
        return;
    }
    destroyMedia(session->media); // DestroyMedia, do not lock
    delAndFreeSession(session);
    return;
}
#endif
void delCustomSession(void *context){
    if(context == NULL){
        return;
    }
    struct session_st *session = (struct session_st *)context;
    delAndFreeSession(session);
    return;
}
int addVideo(void *context, enum VIDEO_e type){
    if(context == NULL){
        return -1;
    }
    struct session_st *session = (struct session_st *)context;
    session->video_type = type;
    return 0;
}

int addAudio(void *context, enum AUDIO_e type, int profile, int sample_rate, int channels){
    if(context == NULL){
        return -1;
    }
    struct session_st *session = (struct session_st *)context;
    session->audio_type = type;
    session->profile = profile;
    session->sample_rate = sample_rate;
    session->channels = channels;
    return 0;
}
int sendVideoData(void *context, uint8_t *data, int data_len){
    if(context == NULL){
        return -1;
    }
    struct session_st *session = (struct session_st *)context;
    int ret = 0;
    pthread_mutex_lock(&session->mut);
    for (int i = 0; i < CLIENTMAX; i++){
        if (session->clientinfo[i].sd != -1 && session->clientinfo[i].playflag == 1){
            struct clientinfo_st *clientinfo = &session->clientinfo[i];
#ifdef SEND_DATA_EVENT
            pthread_mutex_lock(&clientinfo->mut_list);
            if(clientinfo->packet_num >= RING_BUFFER_MAX){
                printf("WARING ring buffer too large\n");
            }
            increaseClientList(VIDEO, clientinfo);
            
            if(clientinfo->sig_0 != -1 || clientinfo->client_rtp_port != -1){
                char *ptr = data;
                int ptr_len = data_len;
                memcpy(clientinfo->packet_list[clientinfo->pos_last_packet].data, ptr, ptr_len);
                clientinfo->packet_list[clientinfo->pos_last_packet].size = ptr_len;
                clientinfo->packet_list[clientinfo->pos_last_packet].type = VIDEO;
                clientinfo->packet_num++;
                clientinfo->pos_last_packet++;
                if(clientinfo->pos_last_packet >= clientinfo->packet_list_size){
                    clientinfo->pos_last_packet = 0;
                }
            }
            pthread_mutex_unlock(&clientinfo->mut_list);
#else
            ret = sendDataToClient(clientinfo, data, data_len, VIDEO);
            if(ret <= 0){
                // do nothing, epoll_loop will delete client
            }
#endif
        }
    }
    pthread_mutex_unlock(&session->mut);
    return 0;
}

int sendAudioData(void *context, uint8_t *data, int data_len){
    if(context == NULL){
        return -1;
    }
    struct session_st *session = (struct session_st *)context;
    int ret = 0;
    pthread_mutex_lock(&session->mut);
    for (int i = 0; i < CLIENTMAX; i++){
        if (session->clientinfo[i].sd != -1 && session->clientinfo[i].playflag == 1){
            struct clientinfo_st *clientinfo = &session->clientinfo[i];
#ifdef SEND_DATA_EVENT
            pthread_mutex_lock(&clientinfo->mut_list);
            if(clientinfo->packet_num >= RING_BUFFER_MAX){
                printf("WARING ring buffer too large\n");
            }
            increaseClientList(AUDIO, clientinfo);
            
            if(clientinfo->sig_2 != -1 || clientinfo->client_rtp_port_1 != -1){ 
                char *ptr = data;
                int ptr_len = data_len;
                if(clientinfo->client_rtp_port_1 != -1){ // UDP, Use different queues for audio and video
                    memcpy(clientinfo->packet_list_1[clientinfo->pos_last_packet_1].data, ptr, ptr_len);
                    clientinfo->packet_list_1[clientinfo->pos_last_packet_1].size = ptr_len;
                    clientinfo->packet_list_1[clientinfo->pos_last_packet_1].type = AUDIO;
                    clientinfo->packet_num_1++;
                    clientinfo->pos_last_packet_1++;
                    if(clientinfo->pos_last_packet_1 >= clientinfo->packet_list_size_1){
                        clientinfo->pos_last_packet_1 = 0;
                    }
                }
                else if(clientinfo->sig_2 != -1){ // TCP, audio and video use the same queue
                    memcpy(clientinfo->packet_list[clientinfo->pos_last_packet].data, ptr, ptr_len);
                    clientinfo->packet_list[clientinfo->pos_last_packet].size = ptr_len;
                    clientinfo->packet_list[clientinfo->pos_last_packet].type = AUDIO;
                    clientinfo->packet_num++;
                    clientinfo->pos_last_packet++;
                    if(clientinfo->pos_last_packet >= clientinfo->packet_list_size){
                        clientinfo->pos_last_packet = 0;
                    }
                }
            }
            pthread_mutex_unlock(&clientinfo->mut_list);
#else
            ret = sendDataToClient(clientinfo, data, data_len, AUDIO);
            if(ret <= 0){
                // do nothing, epoll_loop will delete client
            }
#endif
        }
    }
    pthread_mutex_unlock(&session->mut);
    return 0;
}
int getSessionAudioType(struct session_st *session){
    if(session == NULL){
        return AUDIO_NONE;
    }
    if(session->is_custom == 0){
#ifdef RTSP_FILE_SERVER
        return getAudioType(session->media);
#endif
    }
    else{
        return session->audio_type;
    }
    return AUDIO_NONE;
}
int getSessionAudioInfo(struct session_st *session, int *sample_rate, int *channels, int *profile){
    if(session == NULL){
        return -1;
    }
    if(session->is_custom == 0){
#ifdef RTSP_FILE_SERVER
        struct audioinfo_st audioinfo = getAudioInfo(session->media);
        *sample_rate = audioinfo.sample_rate;
        *channels = audioinfo.channels;
        *profile = audioinfo.profile;
#endif
    }
    else{
        *sample_rate = session->sample_rate;
        *channels = session->channels;
        *profile = session->profile;
    }
    return 0;
}

int getSessionVideoType(struct session_st *session){
    if(session == NULL){
        return VIDEO_NONE;
    }
    if(session->is_custom == 0){
#ifdef RTSP_FILE_SERVER
        return getVideoType(session->media);
#endif
    }
    else{
        return session->video_type;
    }
    return VIDEO_NONE;
}
int sessionIsExist(char* suffix){
    char path_filename[1024] = {0};
    memcpy(path_filename, mp4Dir, strlen(mp4Dir));
    memcpy(path_filename + strlen(mp4Dir), suffix, strlen(suffix));
    pthread_mutex_lock(&mut_session);
    for (int i = 0; i < FILEMAX; i++){
        if ((session_arr[i] == NULL) || (session_arr[i]->filename == NULL)){
            continue;
        }
        if (!strncmp(session_arr[i]->filename, path_filename, strlen(path_filename))){
            pthread_mutex_unlock(&mut_session);
            return 1;
        }
    }
    pthread_mutex_unlock(&mut_session);
#ifdef RTSP_FILE_SERVER
    FILE *file = fopen(path_filename, "r");
    if (file) {
        fclose(file);
        return 1;
    } else {
        return 0;
    }
#endif
    return 0;
}
int sessionGenerateSDP(char *suffix, char *localIp, char *buffer, int buffer_len){
    if(suffix == NULL){
        return -1;
    }
    char path_filename[1024] = {0};
    memcpy(path_filename, mp4Dir, strlen(mp4Dir));
    memcpy(path_filename + strlen(mp4Dir), suffix, strlen(suffix));
    pthread_mutex_lock(&mut_session);
    for (int i = 0; i < FILEMAX; i++){   
        if((session_arr[i] == NULL) || (session_arr[i]->filename == NULL)){
            continue;
        }
        if(!strncmp(session_arr[i]->filename, path_filename, strlen(path_filename))){
            if(session_arr[i]->is_custom == 0){
#ifdef RTSP_FILE_SERVER
                pthread_mutex_unlock(&mut_session);
                return generateSDP(path_filename, localIp, buffer, buffer_len);
#endif
            }
            else{
                pthread_mutex_unlock(&mut_session);
                return generateSDPExt(localIp, buffer, buffer_len, session_arr[i]->video_type, 
                                    session_arr[i]->audio_type, session_arr[i]->sample_rate, session_arr[i]->profile, session_arr[i]->channels);
            }
        }
    }
    pthread_mutex_unlock(&mut_session);
#ifdef RTSP_FILE_SERVER
    return generateSDP(path_filename, localIp, buffer, buffer_len);
#endif
    return -1;
}

/*add one client*/
int addClient(char* suffix, 
    int client_sock_fd, int sig_0, int sig_2, int ture_of_tcp, /*tcp*/
    char *client_ip, int client_rtp_port,int client_rtp_port_1, /*client udp info*/
    int server_udp_socket_rtp, int server_udp_socket_rtcp, int server_udp_socket_rtp_1, int server_udp_socket_rtcp_1 /*udp socket*/
    )
{
#ifdef SESSION_DEBUG
    printf("sig_0:%d, sig_2:%d, ture_of_tcp:%d, client_ip:%s, client_rtp_port:%d, client_rtp_port_1:%d, server_udp_socket_rtp:%d server_udp_socket_rtcp:%d server_udp_socket_rtp_1:%d,server_udp_socket_rtcp_1:%d\n",
           sig_0, sig_2, ture_of_tcp, client_ip, client_rtp_port, client_rtp_port_1, server_udp_socket_rtp, server_udp_socket_rtcp, server_udp_socket_rtp_1, server_udp_socket_rtcp_1);
#endif
    int istrueflag = 0;
    int pos = 0;
    int fps;
    char path_filename[1024] = {0};
    memcpy(path_filename, mp4Dir, strlen(mp4Dir));
    memcpy(path_filename + strlen(mp4Dir), suffix, strlen(suffix));
    /*Check if the session already exists*/
    pthread_mutex_lock(&mut_session);
    for(int i = 0; i < FILEMAX; i++){
        if(session_arr[i] == NULL){
            continue;
        }
        if(!strncmp(session_arr[i]->filename, path_filename, strlen(path_filename))){ // The session exists, add the client to the client queue of the session
            pthread_mutex_lock(&session_arr[i]->mut);
            istrueflag = 1;
            pos = i;

            int posofclient = get_free_clientinfo(pos);
            if(posofclient < 0){ // Exceeding the maximum number of clients supported by a session, a session can support a maximum of FileMAX (1024) clients
                printf("over client maxnum\n");
                pthread_mutex_unlock(&session_arr[pos]->mut);
                pthread_mutex_unlock(&mut_session);
                return -1;
            }
            createClient(&session_arr[pos]->clientinfo[posofclient], 
                client_sock_fd, sig_0, sig_2, ture_of_tcp, /*tcp*/
                server_udp_socket_rtp, server_udp_socket_rtcp,server_udp_socket_rtp_1, server_udp_socket_rtcp_1, client_ip, client_rtp_port, client_rtp_port_1 /*udp*/
                );
            int events = EVENT_ERR|EVENT_RDHUP|EVENT_HUP;
#ifdef SEND_DATA_EVENT
            events |= EVENT_OUT;
#endif
            eventAdd(events, &session_arr[pos]->clientinfo[posofclient]);
            session_arr[pos]->count++;
#ifdef SESSION_DEBUG
            printf("append client ok fd:%d\n", session_arr[pos]->clientinfo[posofclient].sd);
#endif
            pthread_mutex_unlock(&session_arr[i]->mut);
            break;
        }
    }
    pthread_mutex_unlock(&mut_session);
    if(istrueflag == 0){ // create a new file session, not custom session
#ifdef RTSP_FILE_SERVER
        int ret = addFileSession(path_filename, client_sock_fd, sig_0, sig_2, ture_of_tcp, server_udp_socket_rtp, server_udp_socket_rtcp, server_udp_socket_rtp_1, server_udp_socket_rtcp_1, client_ip, client_rtp_port, client_rtp_port_1);
        if (ret < 0)
        {

            return -1;
        }
#else
        return -1;
#endif
    }
    pthread_mutex_lock(&mut_clientcount);
    sum_client++;
    pthread_mutex_unlock(&mut_clientcount);
    return 0;
}
int getClientNum()
{
    int sum;
    pthread_mutex_lock(&mut_clientcount);
    sum = sum_client;
    pthread_mutex_unlock(&mut_clientcount);
    return sum;
}