#include "rtp.h"
int rtpSendPCMAFrame(socket_t fd, struct rtp_tcp_header *tcp_header, struct RtpPacket *rtp_packet, char *data, int size, uint32_t sample_rate, int channels, int profile, int sig, char *client_ip, int client_rtp_port)
{
    int ret;
    int send_bytes = 0;
    if(tcp_header != NULL && sig != -1){ // tcp
        tcp_header->magic = '$';
        tcp_header->rtp_len16 = (uint16_t)RTP_HEADER_SIZE + (uint16_t)size;

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
    memcpy(rtp_packet->payload, data, size);
    if(tcp_header != NULL && sig != -1){ // tcp
        ret = sendWithTimeout(fd, (const char*)rtp_packet, RTP_HEADER_SIZE + size, 0);
    }
    else if(client_ip != NULL && client_rtp_port != -1){ // udp
        ret = sendUDP(fd, (const char*)rtp_packet, RTP_HEADER_SIZE + size, client_ip, client_rtp_port);
    }
    if(ret <= 0){
        return -1;
    }
    send_bytes += ret;
    rtp_packet->rtpHeader.seq = ntohs(rtp_packet->rtpHeader.seq);
    rtp_packet->rtpHeader.timestamp = ntohl(rtp_packet->rtpHeader.timestamp);
    rtp_packet->rtpHeader.ssrc = ntohl(rtp_packet->rtpHeader.ssrc);

    rtp_packet->rtpHeader.seq++;
    return send_bytes;
}
