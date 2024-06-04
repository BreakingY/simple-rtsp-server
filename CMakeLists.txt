cmake_minimum_required(VERSION 3.0)
project (RTSPSERVER)

include_directories(/usr/local/include ./ ./rtp)
aux_source_directory(./ SRC)
aux_source_directory(./rtp RTP)

link_directories(/usr/local/lib /usr/lib)
set(EXECUTABLE_OUTPUT_PATH ./)
add_compile_options(-g)
set(CMAKE_BUILD_TYPE Release)

add_executable(rtsp_server ${SRC} ${RTP})
target_link_libraries(rtsp_server pthread avutil avformat avcodec swscale swresample)

