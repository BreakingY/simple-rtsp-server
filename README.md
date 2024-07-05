# simple-rtsp-server
* 从文件中读取音视频发送给客户端，文件格式支持MP4、MKV；音视频支持H264、H265、AAC、PCMA。注意：MP4不支持PCMA，RTP传输PCMA时请使用MKV文件。
* 支持rtp over udp、rtp over tcp，多个rtsp客户端请求同一个视频时，不同客户端收到的视频是同步的(模拟真实摄像头)。
* 使用epoll发送数据。
* ffmpeg版本 >= 4.x。
* 文件结束后会自动循环。
* 支持鉴权，MD5依赖 https://github.com/talent518/md5
![rtsp测试服务器设计](https://github.com/BreakingY/simple-rtsp-server/assets/99859929/f63e22aa-0d42-49c4-b72c-e4b0e0bf65c3)







# 编译运行
* mkdir build
* cd build
* cmake ..
* make -j
* cp -r ../mp4path .
* ./rtsp_server 0 (0-不鉴权；1-鉴权)

# RTSP拉流
* 把要回放的视频放到mp4path中。
* rtsp地址：
  * 不加鉴权：rtsp://ip:8554/mp4文件名字
  * 鉴权：rtsp://admin:123456@ip:8554/mp4文件名字。
* 注意视频不要包含B帧。
<img width="960" alt="ba2301fb0825b0bab489b9f474fc9cb" src="https://github.com/BreakingY/simple-rtsp-server/assets/99859929/24308b63-235a-4a75-adc7-67c43bde51dd">



# 技术交流
* kxsun617@163.com
