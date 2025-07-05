CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g

SRCS = main.c lexer.c compiler.c vm.c

OBJS = $(SRCS:.c=.o)

TARGET = apeslang

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) *.apb

.PHONY: all clean
