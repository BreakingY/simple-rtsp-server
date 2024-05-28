#include "aac_rtp.h"
static uint32_t getTimestamp(uint32_t sample_rate)
{
    struct timeval tv = {0};
    gettimeofday(&tv, NULL);
    // t(rtsp/rtp时间戳，单位s) =  t(采集时间戳，单位秒)*音视频时钟频率 或者 t(rtsp/rtp时间戳，单位ms)=(t采集时间戳,单位ms)*(时钟频率/1000)
    // 时钟频率是1秒内的频率，比如视频时90000HZ,1ms的话就是90HZ
    // 这种计算方式和ts+=时钟频率/帧率(此时ts需要初始值，一般为0)计算出来的帧之间的时间戳增量是一样 ，但是用系统时间计算rtp的时间能够准确的反应当前帧的采集时间(rtsp/rtp时间基下的时间)
    // clockRate/1000是转换成ms
    uint32_t ts = ((tv.tv_sec * 1000) + ((tv.tv_usec + 500) / 1000)) * sample_rate / 1000; // 90: clockRate/1000;
    return ts;
}
const int sampling_frequencies[] = {
    96000, // 0x0
    88200, // 0x1
    64000, // 0x2
    48000, // 0x3
    44100, // 0x4
    32000, // 0x5
    24000, // 0x6
    22050, // 0x7
    16000, // 0x8
    12000, // 0x9
    11025, // 0xa
    8000   // 0xb
           // 0xc d e f是保留的
};
// arg:profile-编码级别、samplerate-采样率 Hz、channels通道数、没有位深(视频编解码也没有记录位深度)
// 音频采样所得的PCM都含有三个要素：声道(channel)、采样率(sample rate)、样本格式(sample format)。
static int adts_header(char *const p_adts_header, const int data_length,
                       const int profile, const int samplerate,
                       const int channels)
{

    int sampling_frequency_index = 3; // 默认使用48000hz
    int adtsLen = data_length + 7;

    int frequencies_size = sizeof(sampling_frequencies) / sizeof(sampling_frequencies[0]);
    int i = 0;
    for (i = 0; i < frequencies_size; i++) {
        if (sampling_frequencies[i] == samplerate) {
            sampling_frequency_index = i;
            break;
        }
    }
    if (i >= frequencies_size) {
        printf("unsupport samplerate:%d\n", samplerate);
        return -1;
    }

    p_adts_header[0] = 0xff;      // syncword:0xfff                          高8bits
    p_adts_header[1] = 0xf0;      // syncword:0xfff                          低4bits
    p_adts_header[1] |= (0 << 3); // MPEG Version:0 for MPEG-4,1 for MPEG-2  1bit
    p_adts_header[1] |= (0 << 1); // Layer:0                                 2bits
    p_adts_header[1] |= 1;        // protection absent:1                     1bit

    p_adts_header[2] = (profile) << 6; // profile:profile               2bits
    p_adts_header[2] |=
        (sampling_frequency_index & 0x0f) << 2; // sampling frequency index:sampling_frequency_index  4bits
    p_adts_header[2] |= (0 << 1);               // private bit:0                   1bit
    p_adts_header[2] |= (channels & 0x04) >> 2; // channel configuration:channels  高1bit

    p_adts_header[3] = (channels & 0x03) << 6;      // channel configuration:channels 低2bits
    p_adts_header[3] |= (0 << 5);                   // original：0                1bit
    p_adts_header[3] |= (0 << 4);                   // home：0                    1bit
    p_adts_header[3] |= (0 << 3);                   // copyright id bit：0        1bit
    p_adts_header[3] |= (0 << 2);                   // copyright id start：0      1bit
    p_adts_header[3] |= ((adtsLen & 0x1800) >> 11); // frame length：value   高2bits

    p_adts_header[4] = (uint8_t)((adtsLen & 0x7f8) >> 3); // frame length:value    中间8bits
    p_adts_header[5] = (uint8_t)((adtsLen & 0x7) << 5);   // frame length:value    低3bits
    p_adts_header[5] |= 0x1f;                             // buffer fullness:0x7ff 高5bits
    p_adts_header[6] = 0xfc;                              // buffer fullness:0x7ff 低6bits
    // number_of_raw_data_blocks_in_frame：
    //    表示ADTS帧中有number_of_raw_data_blocks_in_frame + 1个AAC原始帧。

    return 0;
}
// char *aac_filename = "test_out.aac";
// FILE *aac_fd = NULL;

int rtpSendAACFrame(int fd, struct rtp_tcp_header *tcp_header, struct RtpPacket *rtp_packet, char *data, int size, uint32_t sample_rate, int channels, int profile, int sig, char *client_ip, int client_rtp_port)
{
    // if(aac_fd==NULL){
    //     aac_fd = fopen(aac_filename, "wb");
    // }

    // char adts_header_buf[7] = {0};
    // adts_header(adts_header_buf, size,
    //             profile,//AAC编码级别
    //             sample_rate,//采样率 Hz
    //             channels);
    // fwrite(adts_header_buf, 1, 7, aac_fd);  // 写adts header , ts流不适用，ts流分离出来的packet带了adts header
    // fwrite(data, 1, size, aac_fd);   // 写adts data
    int ret;
    int send_bytes = 0;
    struct sockaddr_in addr;
    if (tcp_header != NULL && sig != -1) { // tcp
        tcp_header->magic = '$';
        tcp_header->rtp_len16 = (uint16_t)RTP_HEADER_SIZE + (uint16_t)size + 4;

        tcp_header->rtp_len16 = htons(tcp_header->rtp_len16);
        tcp_header->channel = sig;
        ret = send(fd, tcp_header, sizeof(struct rtp_tcp_header), 0);
        if (ret <= 0) {
            return -1;
        }
    } else if (client_ip != NULL && client_rtp_port != -1) { // udp

        addr.sin_family = AF_INET;
        addr.sin_port = htons(client_rtp_port);
        addr.sin_addr.s_addr = inet_addr(client_ip);
    } else {
        printf("parameter error\n");
        return -1;
    }
    rtp_packet->rtpHeader.timestamp = getTimestamp(sample_rate);
    send_bytes += ret;
    rtp_packet->rtpHeader.seq = htons(rtp_packet->rtpHeader.seq);
    rtp_packet->rtpHeader.timestamp = htonl(rtp_packet->rtpHeader.timestamp);
    rtp_packet->rtpHeader.ssrc = htonl(rtp_packet->rtpHeader.ssrc);
    rtp_packet->rtpHeader.marker = 1;
    rtp_packet->payload[0] = 0x00;
    rtp_packet->payload[1] = 0x10;
    rtp_packet->payload[2] = (size & 0x1FE0) >> 5; // 高8位
    rtp_packet->payload[3] = (size & 0x1F) << 3;   // 低5位
    memcpy(rtp_packet->payload + 4, data, size);
    if (tcp_header != NULL && sig != -1) { // tcp
        ret = send(fd, rtp_packet, RTP_HEADER_SIZE + size + 4, 0);
    } else if (client_ip != NULL && client_rtp_port != -1) { // udp
        ret = sendto(fd, (void *)rtp_packet, RTP_HEADER_SIZE + size + 4, 0, (struct sockaddr *)&addr, sizeof(addr));
    }
    if (ret <= 0) {
        return -1;
    }
    send_bytes += ret;
    rtp_packet->rtpHeader.seq = ntohs(rtp_packet->rtpHeader.seq);
    rtp_packet->rtpHeader.timestamp = ntohl(rtp_packet->rtpHeader.timestamp);
    rtp_packet->rtpHeader.ssrc = ntohl(rtp_packet->rtpHeader.ssrc);

    rtp_packet->rtpHeader.seq++;
    /*
     * 如果采样频率是44100
     * 一般AAC每个1024个采样为一帧
     * 所以一秒就有 44100 / 1024 = 43帧
     * 时间增量就是 44100 / 43 = 1025
     * 一帧的时间为 1 / 43 = 23ms
     */
    // rtp_packet->rtpHeader.timestamp += 1025;
    return send_bytes;
}
