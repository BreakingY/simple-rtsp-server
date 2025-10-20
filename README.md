[中文](./README_CN.md)
# simple-rtsp-server
* RTSP 1.0
* H264/H265/AAC/PCMA G711A
* RTP over UDP, RTP over TCP, supports authentication
* Windows/Linux
* epoll/select
* File playback feature: Place video files (MP4, MKV) in a specified folder, and clients can stream them via rtsp://ip:port/filename. Note: The video should not contain B-frames
* Add custom RTSP sessions and audio/video sources
* Device live streaming: Capture video and audio from a camera and microphone for RTSP streaming (rtsp_server_live_device.c), implemented using FFmpeg (Windows: dshow, Linux: v4l2 + alsa)
* MD5: https://github.com/talent518/md5
* RTSP client: https://github.com/BreakingY/simple-rtsp-client
* Setup on Linux: https://sunkx.blog.csdn.net/article/details/139490411
* Setup on Windows: https://sunkx.blog.csdn.net/article/details/146064215
  
# File playback and device live streaming depend on FFmpeg
* FFmpeg version == 4.x, tested versions: 4.0.5 and 4.4.5
* File playback and device live streaming can be configured
  * -DRTSP_FILE_SERVER=ON to enable file playback
  * -DDEVICE_LIVE=ON to enable device live streaming (rtsp_server_live_device.c)
* If both RTSP_FILE_SERVER and DEVICE_LIVE are set to OFF, the project will no longer depend on FFmpeg.

# Compilation
1. Linux
   * mkdir build
   * cd build
   * cmake -DRTSP_FILE_SERVER=ON -DDEVICE_LIVE=ON ..
   * make -j
2. Windows (MinGW + CMake)
   * mkdir build
   * cd build
   * cmake -G "MinGW Makefiles" -DRTSP_FILE_SERVER=ON -DDEVICE_LIVE=ON ..
   * mingw32-make

# Test
1. File playback
* ./rtsp_server_file auth (authentication: 0-not authentication; 1-authentication) loop (file playback loop control: 0-not loop; 1-loop) dir_path (specified folder path, default: ../mp4path)
* Place the MP4/MKV files you want to play back in dir_path. The project includes four test files (MP4/MKV).
* Example: ./rtsp_server_file 1 1 ../mp4path/
* rtsp://admin:123456@ip:8554/filename
2. Custom session and media source (video)
* ./rtsp_server_live auth (authentication: 0-not authentication; 1-authentication) file_path (h264: ../mp4path/test.h264)
* Example: ./rtsp_server_live 1 ../mp4path/test.h264
* rtsp://admin:123456@ip:8554/live
3. Custom session and media source (audio + video, only tested, no A/V synchronization)
* ./rtsp_server_live_av auth (authentication: 0-not authentication; 1-authentication) file_path (h264: ../mp4path/test.h264) file_path (aac: ../mp4path/test.aac)
* Example: ./rtsp_server_live_av 1 ../mp4path/test.h264 ../mp4path/test.aac
* rtsp://admin:123456@ip:8554/live
4. Camera and microphone live streaming
* ./rtsp_server_live_device auth (authentication: 0-not authentication; 1-authentication)
* Example: ./rtsp_server_live_device 1
* rtsp://admin:123456@ip:8554/live

<img width="960" alt="ba2301fb0825b0bab489b9f474fc9cb" src="https://github.com/BreakingY/simple-rtsp-server/assets/99859929/24308b63-235a-4a75-adc7-67c43bde51dd">

# Email
* kxsun617@163.com
