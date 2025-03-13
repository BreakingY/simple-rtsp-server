#ifndef _DEVICE_VIDEO_H_
#define _DEVICE_VIDEO_H_
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavdevice/avdevice.h>
#if defined(__linux__) || defined(__linux)
// ffmpeg -f v4l2 -i /dev/video0 -pix_fmt yuv420p -c:v libx264 -preset ultrafast output.h264
#define VIDEO_INPUT_FORMAT "v4l2"
#define VIDEO_INPUT_DEVICE "/dev/video0"
#elif defined(_WIN32) || defined(_WIN64)
// ffmpeg -f dshow -i video="Integrated Camera" -pix_fmt yuv420p -c:v libx264 -preset ultrafast output.h264
#define VIDEO_INPUT_FORMAT "dshow"
#define VIDEO_INPUT_DEVICE "video=Integrated Camera" // ffmpeg -list_devices true -f dshow -i dummy
#endif
// Encode as H264
typedef void (*video_callback_t)(uint8_t *, int, void *); // data, data len, user arg
void setVideoCallback(video_callback_t cb, void *arg);
int startVideoDeviceLoop();
void stopVideoDevice();
#endif