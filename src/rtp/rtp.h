#ifndef _RTP_H_
#define _RTP_H_
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "socket_io.h"

int rtpSendH264Frame(int sd, struct rtp_tcp_header *tcp_header, struct RtpPacket *rtp_packet, 
                    uint8_t *frame, uint32_t frame_size, int fps, int sig_0, char *client_ip, int client_rtp_port);
int rtpSendH265Frame(int sd, struct rtp_tcp_header *tcp_header, struct RtpPacket *rtp_packet, 
                    uint8_t *frame, uint32_t frame_size, int fps, int sig_0, char *client_ip, int client_rtp_port);

int rtpSendAACFrame(int fd, struct rtp_tcp_header *tcp_header, struct RtpPacket *rtp_packet, 
                    char *data, int size, uint32_t sample_rate, int channels, int profile, int sig, char *client_ip, int client_rtp_port);
int rtpSendPCMAFrame(int fd, struct rtp_tcp_header *tcp_header, struct RtpPacket *rtp_packet, 
                    char *data, int size, uint32_t sample_rate, int channels, int profile, int sig, char *client_ip, int client_rtp_port);

#endif