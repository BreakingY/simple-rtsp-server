#include "h265_rtp.h"

static uint32_t getTimestamp()
{
    struct timeval tv = {0};
    gettimeofday(&tv, NULL);
    uint32_t ts = ((tv.tv_sec * 1000) + ((tv.tv_usec + 500) / 1000)) * 90; // clockRate/1000;
    return ts;
}
int rtpSendH265Frame(int sd, struct rtp_tcp_header *tcp_header, struct RtpPacket *rtp_packet, uint8_t *frame, uint32_t frame_size, int fps, int sig_0, char *client_ip, int client_rtp_port)
{
    uint8_t nalu_type;
    int send_bytes = 0;
    int ret;

    if(frame == NULL){
        return -1;
    }

    nalu_type = (frame[0] & 0x7E) >> 1;
    rtp_packet->rtpHeader.marker = 0;
    memset(rtp_packet->payload, 0, strlen(rtp_packet->payload));

    rtp_packet->rtpHeader.timestamp = getTimestamp();
    if(tcp_header != NULL){
        tcp_header->magic = '$';
        tcp_header->rtp_len16 = 0;
    }

    struct sockaddr_in addr;
    if(client_rtp_port != -1 && client_ip != NULL){
        addr.sin_family = AF_INET;
        addr.sin_port = htons(client_rtp_port);
        addr.sin_addr.s_addr = inet_addr(client_ip);
    }

    if(frame_size <= PTK_RTP_TCP_MAX){ // nalu长度小于最大包场：单一NALU单元模式
        rtp_packet->rtpHeader.marker = 1;
        if(tcp_header != NULL && sig_0 != -1){ // rtp over tcp
            tcp_header->rtp_len16 = (uint16_t)RTP_HEADER_SIZE + (uint16_t)frame_size;
            tcp_header->rtp_len16 = htons(tcp_header->rtp_len16);
            tcp_header->channel = sig_0;
            ret = send(sd, tcp_header, sizeof(struct rtp_tcp_header), 0);
            if(ret <= 0){
                return -1;
            }
            tcp_header->rtp_len16 = ntohs(tcp_header->rtp_len16);
        }

        rtp_packet->rtpHeader.seq = htons(rtp_packet->rtpHeader.seq);
        rtp_packet->rtpHeader.timestamp = htonl(rtp_packet->rtpHeader.timestamp);
        rtp_packet->rtpHeader.ssrc = htonl(rtp_packet->rtpHeader.ssrc);
        memcpy(rtp_packet->payload, frame, frame_size);

        if(tcp_header != NULL && sig_0 != -1){ // rtp over tcp
            ret = send(sd, rtp_packet, RTP_HEADER_SIZE + frame_size, 0);
        }
        else if(client_rtp_port != -1 && client_ip != NULL){ // rtp over udp
            ret = sendto(sd, (void *)rtp_packet, RTP_HEADER_SIZE + frame_size, 0, (struct sockaddr *)&addr, sizeof(addr));
        }
        else{
            printf("parameter error\n");
            return -1;
        }
        if(ret <= 0){
            return -1;
        }
        rtp_packet->rtpHeader.seq = ntohs(rtp_packet->rtpHeader.seq);
        rtp_packet->rtpHeader.timestamp = ntohl(rtp_packet->rtpHeader.timestamp);
        rtp_packet->rtpHeader.ssrc = ntohl(rtp_packet->rtpHeader.ssrc);

        rtp_packet->rtpHeader.seq++;
        send_bytes += ret;
    }
    else{ // nalu长度大于最大包长：分片模式
        int pktNum = frame_size / PTK_RTP_TCP_MAX;        // 有几个完整的包
        int remainPktSize = frame_size % PTK_RTP_TCP_MAX; // 剩余不完整包的大小
        int i, pos = 2;

        for(i = 0; i < pktNum; i++){
            memset(rtp_packet->payload, 0, strlen(rtp_packet->payload));
            rtp_packet->rtpHeader.marker = 0;
            rtp_packet->payload[0] = (frame[0] & 0x81) | (49 << 1);
            rtp_packet->payload[1] = frame[1];
            rtp_packet->payload[2] = nalu_type;

            if(i == 0){ // 第一包数据
                rtp_packet->payload[2] |= 0x80; // start
            }
            else if(remainPktSize == 0 && i == pktNum - 1){ // 最后一包数据
                rtp_packet->payload[2] |= 0x40; // end
                rtp_packet->rtpHeader.marker = 1;
            }

            if(tcp_header != NULL && sig_0 != -1){
                tcp_header->rtp_len16 = (uint16_t)RTP_HEADER_SIZE + (uint16_t)PTK_RTP_TCP_MAX + 3; // 多三个字节的FU indicator、FU header和FU payload
                tcp_header->rtp_len16 = htons(tcp_header->rtp_len16);
                tcp_header->channel = sig_0;
                ret = send(sd, tcp_header, sizeof(struct rtp_tcp_header), 0);
                if(ret <= 0){
                    return -1;
                }
                tcp_header->rtp_len16 = ntohs(tcp_header->rtp_len16);
            }

            memcpy(rtp_packet->payload + 3, frame + pos, PTK_RTP_TCP_MAX);
            rtp_packet->rtpHeader.seq = htons(rtp_packet->rtpHeader.seq);
            rtp_packet->rtpHeader.timestamp = htonl(rtp_packet->rtpHeader.timestamp);
            rtp_packet->rtpHeader.ssrc = htonl(rtp_packet->rtpHeader.ssrc);

            if(tcp_header != NULL && sig_0 != -1){
                ret = send(sd, rtp_packet, RTP_HEADER_SIZE + PTK_RTP_TCP_MAX + 3, 0);
            }
            else if(client_rtp_port != -1 && client_ip != NULL){
                ret = sendto(sd, (void *)rtp_packet, RTP_HEADER_SIZE + PTK_RTP_TCP_MAX + 3, 0, (struct sockaddr *)&addr, sizeof(addr));
            }
            else{
                printf("parameter error\n");
                return -1;
            }
            if(ret <= 0){
                return -1;
            }

            rtp_packet->rtpHeader.seq = ntohs(rtp_packet->rtpHeader.seq);
            rtp_packet->rtpHeader.timestamp = ntohl(rtp_packet->rtpHeader.timestamp);
            rtp_packet->rtpHeader.ssrc = ntohl(rtp_packet->rtpHeader.ssrc);

            rtp_packet->rtpHeader.seq++;
            send_bytes += ret;
            pos += PTK_RTP_TCP_MAX;
        }

        if(remainPktSize > 0){
            memset(rtp_packet->payload, 0, strlen(rtp_packet->payload));
            rtp_packet->payload[0] = (frame[0] & 0x81) | (49 << 1);
            rtp_packet->payload[1] = frame[1];
            rtp_packet->payload[2] = nalu_type;
            rtp_packet->payload[2] |= 0x40; // end
            rtp_packet->rtpHeader.marker = 1;

            if(tcp_header != NULL && sig_0 != -1){
                tcp_header->rtp_len16 = (uint16_t)RTP_HEADER_SIZE + (uint16_t)remainPktSize + 3;
                tcp_header->rtp_len16 = htons(tcp_header->rtp_len16);
                tcp_header->channel = sig_0;
                ret = send(sd, tcp_header, sizeof(struct rtp_tcp_header), 0);
                if(ret <= 0){
                    return -1;
                }
                tcp_header->rtp_len16 = ntohs(tcp_header->rtp_len16);
            }
            rtp_packet->rtpHeader.seq = htons(rtp_packet->rtpHeader.seq);
            rtp_packet->rtpHeader.timestamp = htonl(rtp_packet->rtpHeader.timestamp);
            rtp_packet->rtpHeader.ssrc = htonl(rtp_packet->rtpHeader.ssrc);
            memcpy(rtp_packet->payload + 3, frame + pos, remainPktSize);

            if(tcp_header != NULL && sig_0 != -1){
                ret = send(sd, rtp_packet, RTP_HEADER_SIZE + remainPktSize + 3, 0);
            }
            else if(client_rtp_port != -1 && client_ip != NULL){
                ret = sendto(sd, (void *)rtp_packet, RTP_HEADER_SIZE + remainPktSize + 3, 0, (struct sockaddr *)&addr, sizeof(addr));
            }
            else{
                printf("parameter error\n");
                return -1;
            }
            if(ret <= 0){
                return -1;
            }

            rtp_packet->rtpHeader.seq = ntohs(rtp_packet->rtpHeader.seq);
            rtp_packet->rtpHeader.timestamp = ntohl(rtp_packet->rtpHeader.timestamp);
            rtp_packet->rtpHeader.ssrc = ntohl(rtp_packet->rtpHeader.ssrc);

            rtp_packet->rtpHeader.seq++;
            send_bytes += ret;
        }
    }
    return send_bytes;
}
