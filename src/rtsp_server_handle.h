#ifndef _RTSP_SERVER_HANDLE_H_
#define _RTSP_SERVER_HANDLE_H_
#include "common.h"
#include "session.h"
#include "rtsp_client_handle.h"
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
/*****rtsp*****/
/**
 * initialization, must be called at the beginning of the program
 * @return 0:ok <0:error
 */
int rtspModuleInit();
/**
 * destroy, it can be called or not called. If called, it must be called at the end of the program
 */
void rtspModuleDel();
/**
 * File playback configuration
 * @param[in] file_reloop_flag  Does the file loop back? 0:not 1: is
 * @param[in]  mp4_file_path    The folder path where the file is located, if NULL default:./mp4path
 * @return 0:ok <0:error
 */
int rtspConfigSession(int file_reloop_flag, const char *mp4_file_path);
/**
 * add one session(live)
 * @return context
 */
void* rtspAddSession(const char* session_name);
/**
 * delete session
 */
void rtspDelSession(void *context);
/**
 * start one rtsp server loop
 * @param[in] auth  1: Enable authentication 0: Do not enable authentication
 * @return 0:ok <0:error
 */
int rtspStartServer(int auth, const char *server_ip, int server_port, const char *user, const char *password);

/*****session*****/
/**
 * add video(live session)
 * @return 0:ok <0:error
 */
int sessionAddVideo(void *context, enum VIDEO_e type);
/**
 * add audio(live session), profile for AAC
 * @return 0:ok <0:error
 */
int sessionAddAudio(void *context, enum AUDIO_e type, int profile, int sample_rate, int channels);
/**
 * on NALU h26x without startCode(custom session)
 * @return 0:ok <0:error
 */
int sessionSendVideoData(void *context, uint8_t *data, int data_len);
/**
 * AAC without adts or PCMA(custom session)
 * @return 0:ok <0:error
 */
int sessionSendAudioData(void *context, uint8_t *data, int data_len);
#endif