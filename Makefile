CC=gcc
#需要调用的链接库
LIBS+=-pthread -lavutil -lavformat -lavcodec
#库路径
LDFLAGS+= -L /usr/local/lib

#编译选项
CFLAGS+=-g -I /usr/local/include -pthread


OBJS   := rtsp_server.o common.o
TARGET := rtsp_server

$(TARGET):$(OBJS)
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS) $(LIBS)
	
clean:
	rm -rf *.o $(TARGET) 
