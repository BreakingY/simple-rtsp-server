#ifndef _DEVICE_AUDIO_H_
#define _DEVICE_AUDIO_H_
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
#include <libavdevice/avdevice.h>
#if defined(__linux__) || defined(__linux)
// ffmpeg -f alsa -i default -ac 2 -ar 44100 -c:a aac -b:a 64000 output.aac
#define AUDIO_INPUT_FORMAT "alsa"
#define AUDIO_INPUT_DEVICE "audio=default"
#elif defined(_WIN32) || defined(_WIN64)
// ffmpeg -f dshow -i audio="外部麦克风 (Realtek(R) Audio)" -ac 2 -ar 44100 -c:a aac -b:a 64000 output.aac
#define AUDIO_INPUT_FORMAT "dshow"
#define AUDIO_INPUT_DEVICE "audio=外部麦克风 (Realtek(R) Audio)" // ffmpeg -list_devices true -f dshow -i dummy
#endif
// Encode as AAC
#define SAMPLE_RATE 44100
#define CHANNELS 2
#define SAMPLE_FMT AV_SAMPLE_FMT_FLTP
typedef void (*audio_callback_t)(uint8_t *, int, void *); // data, data len, user arg
void setAudioCallback(audio_callback_t cb, void *arg);
int startAudioDeviceLoop();
void stopAudioDevice();
#endif