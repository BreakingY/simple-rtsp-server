#include "rtp.h"
int rtpSendH264Frame(socket_t sd, struct rtp_tcp_header *tcp_header, struct RtpPacket *rtp_packet, uint8_t *frame, uint32_t frame_size, int fps, int sig_0, char *client_ip, int client_rtp_port)
{
    uint8_t nalu_type; // nalu第一个字节
    int send_bytes = 0;
    int ret;

    nalu_type = frame[0];
    rtp_packet->rtpHeader.marker = 0;
    memset(rtp_packet->payload, 0, strlen(rtp_packet->payload));

    if(frame == NULL){
        return -1;
    }
    rtp_packet->rtpHeader.timestamp = getTimestamp(90000);
    if(tcp_header != NULL){
        tcp_header->magic = '$';
        tcp_header->rtp_len16 = 0;
    }
    if(frame_size <= PTK_RTP_TCP_MAX){ // nalu长度小于最大包场：单一NALU单元模式
        /*
         *   0 1 2 3 4 5 6 7 8 9
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         *  |F|NRI|  Type   | a single NAL unit ... |--NALU头部+NAL
         *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         */
        /*单一模式m位为1*/
        rtp_packet->rtpHeader.marker = 1;
        if(tcp_header != NULL && sig_0 != -1){ // rtp over tcp
            tcp_header->rtp_len16 = (uint16_t)RTP_HEADER_SIZE + (uint16_t)frame_size;

            tcp_header->rtp_len16 = htons(tcp_header->rtp_len16);
            // 发送RTP_OVER_TCP头部
            tcp_header->channel = sig_0;
            ret = sendWithTimeout(sd, (const char*)tcp_header, sizeof(struct rtp_tcp_header), 0);
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
            ret = sendWithTimeout(sd, (const char*)rtp_packet, RTP_HEADER_SIZE + frame_size, 0);
        }
        else if(client_rtp_port != -1 && client_ip != NULL){ // rtp over udp
            ret = sendUDP(sd, (const char*)rtp_packet, RTP_HEADER_SIZE + frame_size, client_ip, client_rtp_port, 0);
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
    else{ // nalu长度小于最大包场：分片模式
        /*
         *  0                   1                   2
         *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * | FU indicator  |   FU header   |   FU payload   ...  |
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         */

        /*
         *     FU Indicator
         *    0 1 2 3 4 5 6 7
         *   +-+-+-+-+-+-+-+-+
         *   |F|NRI|  Type   |
         *   +---------------+
         */

        /*
         *      FU Header
         *    0 1 2 3 4 5 6 7
         *   +-+-+-+-+-+-+-+-+
         *   |S|E|R|  Type   |
         *   +---------------+
         */

        int pktNum = frame_size / PTK_RTP_TCP_MAX;        // 有几个完整的包
        int remainPktSize = frame_size % PTK_RTP_TCP_MAX; // 剩余不完整包的大小
        int i, pos = 1;

        /* 发送完整的包 */
        for(i = 0; i < pktNum; i++){
            memset(rtp_packet->payload, 0, strlen(rtp_packet->payload));
            rtp_packet->rtpHeader.marker = 0;
            rtp_packet->payload[0] = (nalu_type & 0xE0) | 28; // F，NRI保持不变，Type设为28(|28)
            // rtp_packet->payload[0] = (nalu_type & 0x60) | 28;
            rtp_packet->payload[1] = nalu_type & 0x1F; // S E R置0，type与NALU的type相同

            if(i == 0){ // 第一包数据

                rtp_packet->payload[1] |= 0x80; // start
            }
            else if(remainPktSize == 0 && i == pktNum - 1){ // 最后一包数据
                rtp_packet->payload[1] |= 0x40; // end
                rtp_packet->rtpHeader.marker = 1;
            }

            if(tcp_header != NULL && sig_0 != -1){
                tcp_header->rtp_len16 = (uint16_t)RTP_HEADER_SIZE + (uint16_t)PTK_RTP_TCP_MAX + 2; // 多两个字节的FUin和FUheader
                tcp_header->rtp_len16 = htons(tcp_header->rtp_len16);
                tcp_header->channel = sig_0;
                ret = sendWithTimeout(sd, (const char*)tcp_header, sizeof(struct rtp_tcp_header), 0);
                if(ret <= 0){
                    return -1;
                }
                tcp_header->rtp_len16 = ntohs(tcp_header->rtp_len16);
            }

            memcpy(rtp_packet->payload + 2, frame + pos, PTK_RTP_TCP_MAX);
            rtp_packet->rtpHeader.seq = htons(rtp_packet->rtpHeader.seq);
            rtp_packet->rtpHeader.timestamp = htonl(rtp_packet->rtpHeader.timestamp);
            rtp_packet->rtpHeader.ssrc = htonl(rtp_packet->rtpHeader.ssrc);

            // 发送RTP数据包
            if(tcp_header != NULL && sig_0 != -1){
                ret = sendWithTimeout(sd, (const char*)rtp_packet, RTP_HEADER_SIZE + PTK_RTP_TCP_MAX + 2, 0);
            }
            else if(client_rtp_port != -1 && client_ip != NULL){
                ret = sendUDP(sd, (const char*)rtp_packet, RTP_HEADER_SIZE + PTK_RTP_TCP_MAX + 2, client_ip, client_rtp_port, 0);
            }
            else{
                printf("parameter error\n");
                return -1;
            }
            if(ret <= 0){
                return -1;
            }
            // 还原字节序

            rtp_packet->rtpHeader.seq = ntohs(rtp_packet->rtpHeader.seq);
            rtp_packet->rtpHeader.timestamp = ntohl(rtp_packet->rtpHeader.timestamp);
            rtp_packet->rtpHeader.ssrc = ntohl(rtp_packet->rtpHeader.ssrc);

            rtp_packet->rtpHeader.seq++;
            send_bytes += ret;
            pos += PTK_RTP_TCP_MAX;
        }

        /* 发送剩余的数据 */
        if(remainPktSize > 0){
            memset(rtp_packet->payload, 0, strlen(rtp_packet->payload));
            rtp_packet->payload[0] = (nalu_type & 0xE0) | 28;
            rtp_packet->payload[1] = nalu_type & 0x1F;
            rtp_packet->payload[1] |= 0x40; // end
            rtp_packet->rtpHeader.marker = 1;

            if(tcp_header != NULL && sig_0 != -1){
                tcp_header->rtp_len16 = (uint16_t)RTP_HEADER_SIZE + (uint16_t)remainPktSize + 2; // 多两个字节的FUin和FUheader
                tcp_header->rtp_len16 = htons(tcp_header->rtp_len16);
                // 发送RTP_OVER_TCP头部
                tcp_header->channel = sig_0;
                ret = sendWithTimeout(sd, (const char*)tcp_header, sizeof(struct rtp_tcp_header), 0);
                if(ret <= 0){
                    return -1;
                }
                tcp_header->rtp_len16 = ntohs(tcp_header->rtp_len16);
            }
            rtp_packet->rtpHeader.seq = htons(rtp_packet->rtpHeader.seq);
            rtp_packet->rtpHeader.timestamp = htonl(rtp_packet->rtpHeader.timestamp);
            rtp_packet->rtpHeader.ssrc = htonl(rtp_packet->rtpHeader.ssrc);
            memcpy(rtp_packet->payload + 2, frame + pos, remainPktSize);

            if(tcp_header != NULL && sig_0 != -1){
                ret = sendWithTimeout(sd, (const char*)rtp_packet, RTP_HEADER_SIZE + remainPktSize + 2, 0);
            }
            else if(client_rtp_port != -1 && client_ip != NULL){
                ret = sendUDP(sd, (const char*)rtp_packet, RTP_HEADER_SIZE + remainPktSize + 2, client_ip, client_rtp_port, 0);
            }
            else{
                printf("parameter error\n");
                return -1;
            }
            if(ret <= 0){
                return -1;
            }

            // 还原字节序

            rtp_packet->rtpHeader.seq = ntohs(rtp_packet->rtpHeader.seq);
            rtp_packet->rtpHeader.timestamp = ntohl(rtp_packet->rtpHeader.timestamp);
            rtp_packet->rtpHeader.ssrc = ntohl(rtp_packet->rtpHeader.ssrc);

            rtp_packet->rtpHeader.seq++;
            send_bytes += ret;
        }
        // 所有分包的时间戳都是一样的，时间戳的原则就是，发送一个完整的NALU时间戳才更改，但是不管什么包，序列号持续+1
    }
    return send_bytes;
}