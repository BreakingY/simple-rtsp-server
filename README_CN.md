# simple-rtsp-server
* RTSP1.0
* H264/H265/AAC/PCMA(G711A)
* 支持rtp over udp、rtp over tcp, 支持鉴权
* epoll
* 内置文件回放功能，把视频文件(MP4、MKV)放到指定文件夹下面，客户端通过"rtsp://ip:port/文件名"即可拉流
* 可添加自定义rtsp会话并添加音视频源
* 注意视频不要包含B帧
* MD5: https://github.com/talent518/md5
* 博客：https://sunkx.blog.csdn.net/article/details/139490411
* rtsp client: https://github.com/BreakingY/simple-rtsp-client
  
# 文件回放功能依赖ffmpeg
* ffmpeg版本 >= 4.x，测试版本为4.0.5和4.4.5
* 文件回放功能是可配置的，通过CMakeLists.txt中的"set(RTSP_FILE_SERVER FORCE)"实现开启和关闭文件回放功能(默认是打开的)，关闭文件回放功能，项目将不再依赖ffmpeg

# 编译
1. Linux
   * mkdir build
   * cd build
   * cmake ..
   * make -j
2. Windows(MinGW + cmake)
   * mkdir build
   * cd build
   * cmake -G "MinGW Makefiles" ..
   * mingw32-make

# 测试
1. 文件回放
* ./rtsp_server_file auth(鉴权 0-not authentication; 1-authentication) loop(文件回放循环控制 0-not loop 1-loop) dir_path(指定文件夹路径 default:./mp4path)
* 把要回放的MP4/MKV文件放到dir_path中即可，项目自带了四个测试文件(MP4/MKV)
  * 不加鉴权：rtsp://ip:8554/文件名字
  * 鉴权：rtsp://admin:123456@ip:8554/文件名字
* eg: ./rtsp_server_file 1 1 ../mp4path/
2. 自定义会话、媒体源(视频)
* ./rtsp_server_live auth(鉴权 0-not authentication; 1-authentication) file_path(h264: ../mp4path/test.h264)
  * 不加鉴权：rtsp://ip:8554/live
  * 鉴权：rtsp://admin:123456@ip:8554/live
* eg: ./rtsp_server_live 1 ../mp4path/test.h264
3. 自定义会话、媒体源(音频 + 视频，仅测试，未进行音视频同步)
* ./rtsp_server_live_av auth(鉴权 0-not authentication; 1-authentication) file_path(h264: ../mp4path/test.h264) file_path(aac: ../mp4path/test.aac)
  * 不加鉴权：rtsp://ip:8554/live
  * 鉴权：rtsp://admin:123456@ip:8554/live
* eg: ./rtsp_server_live_av 1 ../mp4path/test.h264 ../mp4path/test.aac

<img width="960" alt="ba2301fb0825b0bab489b9f474fc9cb" src="https://github.com/BreakingY/simple-rtsp-server/assets/99859929/24308b63-235a-4a75-adc7-67c43bde51dd">

# 技术交流
* kxsun617@163.com
