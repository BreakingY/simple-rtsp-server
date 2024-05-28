#ifndef _AACRTP_H_
#define _AACRTP_H_
#include "common.h"
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

int rtpSendAACFrame(int fd, struct rtp_tcp_header *tcp_header, struct RtpPacket *rtp_packet, char *data, int size, uint32_t sample_rate, int channels, int profile, int sig, char *client_ip, int client_rtp_port);

#endif