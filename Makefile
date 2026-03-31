CC = gcc
CFLAGS = -Wall -Wextra -g $(shell pkg-config --cflags fuse3)
LDFLAGS = $(shell pkg-config --libs fuse3)

TARGET = mini_unionfs

SRCS = member1/member1.c \
       member2/member2.c \
       member3/member3.c \
       member4/member4.c

OBJS = $(SRCS:.c=.o)

.PHONY: all clean test

all: $(TARGET)

# Link all object files into the final binary
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Compile each member's .c into a .o
%.o: %.c shared/common.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)

test: $(TARGET)
	bash test_unionfs.sh
