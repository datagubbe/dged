CFLAGS += -DLINUX -D_XOPEN_SOURCE=700

PLATFORM_SOURCES += src/reactor-epoll.c
PLATFORM_OBJS = $(PLATFORM_SOURCES:.c=.o)
