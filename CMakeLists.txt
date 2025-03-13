cmake_minimum_required(VERSION 3.10)
project (RTSPSERVER)

option(RTSP_FILE_SERVER "Enable RTSP file server support" OFF)
option(DEVICE_LIVE "Enable device live support" OFF)

include_directories(./src ./src/rtp ./src/io_event ./3rdparty)
aux_source_directory(./src SRC)
aux_source_directory(./src/rtp RTP)
aux_source_directory(./src/io_event IO_EVENT)
aux_source_directory(./3rdparty 3RD)

if(RTSP_FILE_SERVER)
    message(STATUS "RTSP_FILE_SERVER OK")
    add_definitions(-DRTSP_FILE_SERVER)
    # for ffmpeg lib
    if(WIN32)
        include_directories("D:/msys64/mingw64/include")
        include_directories("D:/msys64/home/sunkx1/ff-build/include")
        link_directories("D:/msys64/mingw64/lib")
        link_directories("D:/msys64/home/sunkx1/ff-build/lib")
    elseif(UNIX)
        include_directories(/usr/local/include)
        link_directories(/usr/local/lib /usr/lib)
    endif()
endif()

set(EXECUTABLE_OUTPUT_PATH ./)
add_compile_options(-g)
set(CMAKE_BUILD_TYPE Release)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ./)
add_library(rtsp_server SHARED ${SRC} ${RTP} ${IO_EVENT} ${3RD})
if(WIN32)
    # Set the function stack size to 6MB
    add_compile_options(-Wl,--stack,6000000)
    target_link_libraries(rtsp_server ws2_32)
elseif(UNIX)
    target_link_libraries(rtsp_server pthread)
endif()

if(RTSP_FILE_SERVER)
    # rtsp file server, ffmpeg lib
    target_link_libraries(rtsp_server avutil avformat avcodec swscale swresample)
endif()

add_executable(rtsp_server_file ./example/rtsp_server_file.c)
target_link_libraries(rtsp_server_file rtsp_server)

add_executable(rtsp_server_live ./example/rtsp_server_live.c)
target_link_libraries(rtsp_server_live rtsp_server)

add_executable(rtsp_server_live_av ./example/rtsp_server_live_av.c)
target_link_libraries(rtsp_server_live_av rtsp_server)

if(DEVICE_LIVE)
    message(STATUS "DEVICE_LIVE OK")
    # for ffmpeg lib
    if(WIN32)
        include_directories("D:/msys64/mingw64/include")
        include_directories("D:/msys64/home/sunkx1/ff-build/include")
        link_directories("D:/msys64/mingw64/lib")
        link_directories("D:/msys64/home/sunkx1/ff-build/lib")
    elseif(UNIX)
        include_directories(/usr/local/include)
        link_directories(/usr/local/lib /usr/lib)
    endif()
    include_directories(./example/device)
    aux_source_directory(./example/device DEVICE)
    add_executable(rtsp_server_live_device ./example/rtsp_server_live_device.c ${DEVICE})
    target_link_libraries(rtsp_server_live_device rtsp_server avutil avformat avcodec swscale swresample avdevice)
endif()