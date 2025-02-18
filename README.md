# simple-rtsp-server
* RTSP1.0
* H264/H265/AAC/PCMA(G711a)
* Support rtp over udp、rtp over tcp, support authentication
* epoll
* Built in file playback function, place video files (MP4, MKV) in a designated folder, and the client can access them through“ rtsp://ip:port/filename "can pull the stream
* Can add custom RTSP sessions and add audio and video sources
* Be careful not to include B-frames in the video
* MD5: https://github.com/talent518/md5
  
# The file playback function relies on ffmpeg
* FFmpeg version>=4. x, test versions are 4.0.5 and 4.4.5.
* The file playback function is configurable, and can be turned on and off through the "set(RTSP_FILE_SERVER FORCE)" in CMakeLists.txt (default is turned on). Turning off the file playback function means that the project will no longer rely on ffmpeg

# Compile
* mkdir build
* cd build
* cmake ..
* make -j

# Test
1. File playback
* ./rtsp_server_file auth(0-not authentication; 1-authentication) loop(File playback loop control 0-not loop 1-loop) dir_path(Specify folder path default:./mp4path)
* Simply place the MP4/MKV files to be replayed into dir_death. The project comes with four test files (MP4/MKV)
  * Without authentication：rtsp://ip:8554/filename
  * Authentication：rtsp://admin:123456@ip:8554/filename
* eg: ./rtsp_server_file 1 1 ../mp4path/
2. Customize sessions and media sources
* ./rtsp_server_live auth(0-not authentication; 1-authentication) file_path(Test files h264 , can use../mp4path/test.h264)
  * Without authentication：rtsp://ip:8554/live
  * Authentication：rtsp://admin:123456@ip:8554/live
* eg: ./rtsp_server_live 1 ../mp4path/test.h264

<img width="960" alt="ba2301fb0825b0bab489b9f474fc9cb" src="https://github.com/BreakingY/simple-rtsp-server/assets/99859929/24308b63-235a-4a75-adc7-67c43bde51dd">

# Email
* kxsun617@163.com
