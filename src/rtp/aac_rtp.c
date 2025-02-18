#include "aac_rtp.h"
static uint32_t getTimestamp(uint32_t sample_rate)
{
    struct timeval tv = {0};
    gettimeofday(&tv, NULL);
    uint32_t ts = ((tv.tv_sec * 1000) + ((tv.tv_usec + 500) / 1000)) * sample_rate / 1000; // clockRate/1000;
    return ts;
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
    //             profile,
    //             sample_rate,
    //             channels);
    // fwrite(adts_header_buf, 1, 7, aac_fd);
    // fwrite(data, 1, size, aac_fd);
    int ret;
    int send_bytes = 0;
    struct sockaddr_in addr;
    if(tcp_header != NULL && sig != -1){ // tcp
        tcp_header->magic = '$';
        tcp_header->rtp_len16 = (uint16_t)RTP_HEADER_SIZE + (uint16_t)size + 4;

        tcp_header->rtp_len16 = htons(tcp_header->rtp_len16);
        tcp_header->channel = sig;
        ret = send(fd, tcp_header, sizeof(struct rtp_tcp_header), 0);
        if(ret <= 0){
            return -1;
        }
    }
    else if(client_ip != NULL && client_rtp_port != -1){ // udp

        addr.sin_family = AF_INET;
        addr.sin_port = htons(client_rtp_port);
        addr.sin_addr.s_addr = inet_addr(client_ip);
    }
    else{
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
    rtp_packet->payload[2] = (size & 0x1FE0) >> 5; // high 8bits
    rtp_packet->payload[3] = (size & 0x1F) << 3;   // low 5bits
    memcpy(rtp_packet->payload + 4, data, size);
    if(tcp_header != NULL && sig != -1){ // tcp
        ret = send(fd, rtp_packet, RTP_HEADER_SIZE + size + 4, 0);
    }
    else if(client_ip != NULL && client_rtp_port != -1){ // udp
        ret = sendto(fd, (void *)rtp_packet, RTP_HEADER_SIZE + size + 4, 0, (struct sockaddr *)&addr, sizeof(addr));
    }
    if(ret <= 0){
        return -1;
    }
    send_bytes += ret;
    rtp_packet->rtpHeader.seq = ntohs(rtp_packet->rtpHeader.seq);
    rtp_packet->rtpHeader.timestamp = ntohl(rtp_packet->rtpHeader.timestamp);
    rtp_packet->rtpHeader.ssrc = ntohl(rtp_packet->rtpHeader.ssrc);

    rtp_packet->rtpHeader.seq++;
    /**
     *If the sampling frequency is 44100
     *Generally, AAC takes 1024 samples per frame
     *So in one second, there are 44100/1024=43 frames
     *The time increment is 44100/43=1025
     *The time of one frame is 1/43=23ms
     */
    // rtp_packet->rtpHeader.timestamp += 1025;
    return send_bytes;
}
