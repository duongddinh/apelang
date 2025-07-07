CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g
LDFLAGS = -lm

SRCS = main.c lexer.c compiler.c vm.c debug.c

OBJS = $(SRCS:.c=.o)

TARGET = apeslang

all: $(TARGET)

# Link the math library (-lm) when creating the executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) *.apb

.PHONY: all clean
