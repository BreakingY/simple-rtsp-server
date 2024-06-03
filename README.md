# simple-rtsp-server
* 从文件中读取音视频发送给客户端，文件格式支持MP4、MKV；音视频支持H264、H265、AAC。
* 支持rtp over udp、rtp over tcp，多个rtsp客户端请求同一个视频时，不同客户端收到的视频是同步的(模拟真实摄像头)。文件结束时将断开客户端连接，需要客户端加上重连机制。
* 使用epoll发送数据。
* ffmpeg版本 >= 4.x。
![rtsp测试服务器设计](https://github.com/BreakingY/rtsp-over-tcp-server/assets/99859929/33526b42-d5ea-4c58-8cbe-fb5dc0fd8a35)




# 编译运行
* mkdir build
* cd build
* cmake ..
* make -j
* cp -r ../mp4path .
* ./rtsp_server

# RTSP拉流
* 把要回放的视频放到mp4path中，通过rtsp://ip:8554/mp4文件名字即可回放
<img width="960" alt="e2f2bd57a083eb64558eef44dd55d85" src="https://github.com/BreakingY/rtsp-over-tcp-server/assets/99859929/8c810989-529b-479f-be15-b89fd49e7870">


# 技术交流
* kxsun617@163.com
