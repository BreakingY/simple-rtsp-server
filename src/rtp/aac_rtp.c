#include "rtp.h"
// char *aac_filename = "test_out.aac";
// FILE *aac_fd = NULL;

int rtpSendAACFrame(socket_t fd, struct rtp_tcp_header *tcp_header, struct RtpPacket *rtp_packet, char *data, int size, uint32_t sample_rate, int channels, int profile, int sig, char *client_ip, int client_rtp_port)
{
    // if(aac_fd==NULL){
    //     aac_fd = fopen(aac_filename, "wb");
    // }

    // char adts_header_buf[7] = {0};
    // adtsHeader(adts_header_buf, size,
    //             profile,
    //             sample_rate,
    //             channels);
    // fwrite(adts_header_buf, 1, 7, aac_fd);
    // fwrite(data, 1, size, aac_fd);
    int ret;
    int send_bytes = 0;
    if(tcp_header != NULL && sig != -1){ // tcp
        tcp_header->magic = '$';
        tcp_header->rtp_len16 = (uint16_t)RTP_HEADER_SIZE + (uint16_t)size + 4;

        tcp_header->rtp_len16 = htons(tcp_header->rtp_len16);
        tcp_header->channel = sig;
        ret = sendWithTimeout(fd, (const char*)tcp_header, sizeof(struct rtp_tcp_header), 0);
        if(ret <= 0){
            return -1;
        }
    }
    else if(client_ip != NULL && client_rtp_port != -1){ // udp
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
        ret = sendWithTimeout(fd, (const char*)rtp_packet, RTP_HEADER_SIZE + size + 4, 0);
    }
    else if(client_ip != NULL && client_rtp_port != -1){ // udp
        ret = sendUDP(fd, (const char*)rtp_packet, RTP_HEADER_SIZE + size + 4, client_ip, client_rtp_port, 0);
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
