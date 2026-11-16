CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -std=c11
# Uncomment the line below during debugging to catch memory leaks & buffer overflows:
# CFLAGS += -fsanitize=address,undefined

SRCS = src/main.c src/hashtable.c src/heap.c src/aof.c src/protocol.c
OBJS = $(SRCS:.c=.o)
TARGET = mini-redis

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) appendonly.aof