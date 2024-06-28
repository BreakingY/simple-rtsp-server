#include "media.h"
static double r2d(AVRational r)
{
    return r.den == 0 ? 0 : (double)r.num / (double)r.den;
}
static int mp4ToAnnexb(struct mediainfo_st *mp4info, AVPacket *v_packet)
{
    uint8_t *out_data = NULL;
    int out_size = 0;
    int ret = 0;

    av_bitstream_filter_filter(mp4info->h26xbsfc, mp4info->context->streams[mp4info->video_stream_index]->codec, NULL, &out_data, &out_size, v_packet->data, v_packet->size, v_packet->flags & AV_PKT_FLAG_KEY);

    AVPacket tmp_pkt;
    av_init_packet(&tmp_pkt);
    av_packet_copy_props(&tmp_pkt, v_packet);
    av_packet_from_data(&tmp_pkt, out_data, out_size);
    tmp_pkt.size = out_size;
    av_packet_unref(v_packet);

    av_copy_packet(v_packet, &tmp_pkt);
    av_packet_unref(&tmp_pkt);

    return 0;
}
static int startCode3(char *buf)
{
    if (buf[0] == 0 && buf[1] == 0 && buf[2] == 1)
        return 1;
    else
        return 0;
}

static int startCode4(char *buf)
{
    if (buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] == 1)
        return 1;
    else
        return 0;
}
/*从buf中读取一个NALU数据到frame中*/
static int getNALUFromBuf(unsigned char **frame, struct buf_st *buf)
{
    int startCode;
    char *pstart;
    char *tmp;
    int frame_size;
    int bufoverflag = 1;

    if (buf->pos >= buf->buf_size)
    {
        buf->stat = WRITE;
        printf("h264buf empty\n");
        return -1;
    }

    if (!startCode3(buf->buf + buf->pos) && !startCode4(buf->buf + buf->pos))
    {
        printf("statrcode err\n");
        return -1;
    }

    if (startCode3(buf->buf + buf->pos))
    {
        startCode = 3;
    }
    else
        startCode = 4;

    pstart = buf->buf + buf->pos; // pstart跳过之前已经读取的数据指向本次NALU的起始码

    tmp = pstart + startCode; // 指向NALU数据

    for (int i = 0; i < buf->buf_size - buf->pos - 3; i++) // pos表示起始码的位置，所以已经读取的数据长度也应该是pos个字节
    {
        if (startCode3(tmp) || startCode4(tmp)) // 此时tmp指向下一个起始码位置
        {
            frame_size = tmp - pstart; // 包含起始码的NALU长度
            bufoverflag = 0;
            break;
        }
        tmp++;
    }
    if (bufoverflag == 1)
    {
        frame_size = buf->buf_size - buf->pos;
    }

    *frame = buf->buf + buf->pos;

    buf->pos += frame_size;
    /*buf中的数据全部读取完毕，设置buf状态为WREIT*/
    if (buf->pos >= buf->buf_size)
    {
        buf->stat = WRITE;
    }
    return frame_size;
}
/*从buf中解析NALU数据*/
void praseFrame(struct mediainfo_st *mp4info)
{
    if (mp4info == NULL)
    {
        return;
    }
    /*frame==READ,buffer==WRITE状态不可访问*/
    if (mp4info->frame->stat == READ || mp4info->buffer->stat == WRITE)
    {
        return;
    }

    /*buf处于可读状态并且frame处于可写状态*/
    mp4info->frame->frame_size = getNALUFromBuf(&mp4info->frame->frame, mp4info->buffer);

    if (mp4info->frame->frame_size < 0)
    {
        return;
    }

    if (startCode3(mp4info->frame->frame))
        mp4info->frame->start_code = 3;
    else
        mp4info->frame->start_code = 4;
    mp4info->frame->stat = READ;

    return;
}
static void sig_handler_media(int s)
{
    exit(1);
}
static void *parseMp4SendDataThd(void *arg)
{
    signal(SIGINT, sig_handler_media);
    signal(SIGQUIT, sig_handler_media);
    signal(SIGKILL, sig_handler_media);
    struct mediainfo_st *mp4info = (struct mediainfo_st *)arg;
    int findstream = 0;
    if (mp4info == NULL)
        pthread_exit(NULL);
    int64_t start_time = av_gettime();
    while (mp4info->run_flag == 1)
    {
        if (mp4info == NULL){
            pthread_exit(NULL);
        }
        findstream = 0;
        while (av_read_frame(mp4info->context, &mp4info->av_pkt) >= 0)
        {
            AVRational time_base = mp4info->context->streams[mp4info->av_pkt.stream_index]->time_base;
            AVRational time_base_q = {1, AV_TIME_BASE};
            mp4info->curtimestamp = av_rescale_q(mp4info->av_pkt.dts, time_base, time_base_q); // 微妙
            mp4info->now_stream_index = mp4info->av_pkt.stream_index;
            if (mp4info->av_pkt.stream_index == mp4info->video_stream_index)
            {
                findstream = 1;
                /*帧数据写道buf中*/
                mp4info->buffer->buf_size = 0;
                mp4info->buffer->pos = 0;
                mp4ToAnnexb(mp4info, &mp4info->av_pkt);
                mp4info->buffer->buf = mp4info->av_pkt.data;
                mp4info->buffer->buf_size = mp4info->av_pkt.size;
                /*设置buf为READ状态*/
                mp4info->buffer->stat = READ;
                break;
            }
            if (mp4info->av_pkt.stream_index == mp4info->audio_stream_index)
            {
                findstream = 1;
                mp4info->buffer_audio = mp4info->av_pkt.data;
                mp4info->buffer_audio_size = mp4info->av_pkt.size;
                break;
            }
        }
        /*文件推流完毕*/
        if (findstream == 0)
        {
            printf("thr_mp4:%s reloop\n", mp4info->filename);
            av_packet_unref(&mp4info->av_pkt);
            av_seek_frame(mp4info->context, -1, 0, AVSEEK_FLAG_BACKWARD);
            start_time = av_gettime();
            continue;
        }
        
        int64_t now_time = av_gettime() - start_time;
        if (mp4info->curtimestamp > now_time)
            av_usleep(mp4info->curtimestamp - now_time);
        while ((mp4info->buffer->stat == READ) || (mp4info->now_stream_index == mp4info->audio_stream_index))
        {                    // video audio
            praseFrame(mp4info); // 如果buf处于可读状态就送去解析NALU
            mp4info->data_call_back(mp4info->arg); // 传递音视频
            mp4info->frame->stat = WRITE;
            if ((mp4info->now_stream_index == mp4info->audio_stream_index) || (mp4info->run_flag == 0))
            {
                break;
            }
        }
        av_packet_unref(&mp4info->av_pkt);
    }
    printf("parseMp4SendDataThd exit\n");
    return NULL;
}
void *creatMedia(char *path_filename,void *call_back, void *arg){
    struct mediainfo_st *mp4;
    mp4 = malloc(sizeof(struct mediainfo_st));
    mp4->data_call_back = call_back;
    mp4->arg = arg;

    mp4->filename = malloc(strlen(path_filename) + 1);
    memset(mp4->filename, 0, strlen(path_filename) + 1);

    memcpy(mp4->filename, path_filename, strlen(path_filename));

    mp4->context = NULL;
    mp4->fps = 0;
    av_init_packet(&mp4->av_pkt);
    mp4->av_pkt.data = NULL;
    mp4->av_pkt.size = 0;
    mp4->curtimestamp = 0;
    mp4->h26xbsfc = NULL;
    mp4->video_type = VIDEO_NONE;
    mp4->audio_type = AUDIO_NONE;

    int ret = avformat_open_input(&mp4->context, mp4->filename, NULL, NULL);
    if (ret < 0)
    {
        char buf[1024];
        av_strerror(ret, buf, 1024); // 查看报错内容
        printf("avformat_open_input error %d,%s\n", ret, buf);
        if (mp4->filename != NULL)
            free(mp4->filename);
        free(mp4);

        printf("avformat_open_input error,\n");
        return NULL;
    }
    avformat_find_stream_info(mp4->context, NULL);
    mp4->video_stream_index = av_find_best_stream(mp4->context, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    mp4->audio_stream_index = av_find_best_stream(mp4->context, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (mp4->video_stream_index >= 0)
    {
        AVStream *as = mp4->context->streams[mp4->video_stream_index];
        mp4->fps = r2d(as->avg_frame_rate);
        AVCodecParameters *codecpar = as->codecpar;
        if (codecpar->codec_id == AV_CODEC_ID_H264)
        {
            mp4->h26xbsfc = av_bitstream_filter_init("h264_mp4toannexb");
            mp4->video_type = VIDEO_H264;
        }
        else if (codecpar->codec_id == AV_CODEC_ID_H265 || codecpar->codec_id == AV_CODEC_ID_HEVC)
        {
            mp4->h26xbsfc = av_bitstream_filter_init("hevc_mp4toannexb");
            mp4->video_type = VIDEO_H265;
        }
    }
    if (mp4->audio_stream_index >= 0)
    {
        AVStream *as = mp4->context->streams[mp4->audio_stream_index];
        AVCodecParameters *codecpar = as->codecpar;
        if (codecpar->codec_id == AV_CODEC_ID_AAC)
        {
            mp4->audio_type = AUDIO_AAC;
        }
        else if(codecpar->codec_id == AV_CODEC_ID_PCM_ALAW)
        {
            mp4->audio_type = AUDIO_PCMA;
        }
    }
    
    /*初始buffer*/
    mp4->buffer = malloc(sizeof(struct buf_st));
    mp4->buffer->buf = NULL;
    mp4->buffer->buf_size = 0;
    mp4->buffer->pos = 0;
    mp4->buffer->stat = WRITE;

    /*初始化frame*/
    mp4->frame = malloc(sizeof(struct frame_st));
    mp4->frame->frame = NULL;
    mp4->frame->stat = WRITE;
    mp4->run_flag = 1;
    pthread_create(&mp4->tid, NULL, parseMp4SendDataThd, (void *)mp4);
    printf("creatMedia:%s file fps:%d\n", mp4->filename, mp4->fps);
    return mp4;
}
enum VIDEO_e getVideoType(void *context){
    struct mediainfo_st *mp4 = (struct mediainfo_st *)context;
    if(mp4 == NULL){
        return VIDEO_NONE;
    }
    return mp4->video_type;
}
int nowStreamIsVideo(void *context){
    struct mediainfo_st *mp4 = (struct mediainfo_st *)context;
    if(mp4 == NULL){
        return 0;
    }
    if(mp4->now_stream_index == mp4->video_stream_index){
        return 1;
    }
    return 0;
}
enum AUDIO_e getAudioType(void *context){
    struct mediainfo_st *mp4 = (struct mediainfo_st *)context;
    if(mp4 == NULL){
        return AUDIO_NONE;
    }
    return mp4->audio_type;
}
struct audioinfo_st getAudioInfo(void *context){
    struct audioinfo_st info;
    struct mediainfo_st *mp4 = (struct mediainfo_st *)context;
    if(mp4 == NULL){
        return info;
    }
    if(mp4->audio_stream_index < 0){
        return info;
    }
    info.sample_rate = mp4->context->streams[mp4->audio_stream_index]->codecpar->sample_rate;
    info.channels = mp4->context->streams[mp4->audio_stream_index]->codecpar->channels;
    info.profile = mp4->context->streams[mp4->audio_stream_index]->codecpar->profile;
    return info;
}
int nowStreamIsAudio(void *context){
    struct mediainfo_st *mp4 = (struct mediainfo_st *)context;
    if(mp4 == NULL){
        return 0;
    }
    if(mp4->now_stream_index == mp4->audio_stream_index){
        return 1;
    }
    return 0;
}

int getVideoNALUWithoutStartCode(void *context, char **ptr, int *ptr_len){
    
    struct mediainfo_st *mp4 = (struct mediainfo_st *)context;
    if(mp4 == NULL || mp4->now_stream_index == mp4->audio_stream_index){
        return -1;
    }
    if(mp4->frame->stat = READ){
        
        *ptr = mp4->frame->frame + mp4->frame->start_code;
        *ptr_len = mp4->frame->frame_size - mp4->frame->start_code;
    }
    else{
        *ptr_len = 0;
    }
    return 0;
}
int getAudioWithoutADTS(void *context, char **ptr, int *ptr_len)
{
    struct mediainfo_st *mp4 = (struct mediainfo_st *)context;
    if (mp4 == NULL || mp4->now_stream_index == mp4->video_stream_index)
    {
        return -1;
    }
    *ptr = mp4->buffer_audio;
    *ptr_len = mp4->buffer_audio_size;
    return 0;
}
void destroyMedia(void *context){
    struct mediainfo_st *mp4 = (struct mediainfo_st *)context;
    if(mp4 == NULL){
        return;
    }
    mp4->run_flag = 0;
    pthread_join(mp4->tid, NULL);
    avformat_close_input(&mp4->context);
    av_packet_free(&mp4->av_pkt);
    if (mp4->h26xbsfc != NULL)
    {
        av_bitstream_filter_close(mp4->h26xbsfc);
    }
    if (mp4->filename != NULL)
    {
        free(mp4->filename);
        mp4->filename = NULL;
    }
    if (mp4->buffer != NULL)
    {
        free(mp4->buffer);
        mp4->buffer = NULL;
    }
    if (mp4->frame != NULL)
    {
        free(mp4->frame);
        mp4->frame = NULL;
    }
    free(mp4);
    context = NULL;
    printf("destroyMedia\n");
    return ;
}
