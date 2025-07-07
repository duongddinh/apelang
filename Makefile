CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -Isrc
LDFLAGS = -lm

# Source directories
SRC_DIR := src
LEXER_DIR := $(SRC_DIR)/lexer
COMPILER_DIR := $(SRC_DIR)/compiler
VM_DIR := $(SRC_DIR)/vm
DEBUG_DIR := $(SRC_DIR)/debug

# Source files
SRCS := \
	$(SRC_DIR)/main.c \
	$(LEXER_DIR)/lexer.c \
	$(COMPILER_DIR)/compiler.c \
	$(VM_DIR)/vm.c \
	$(DEBUG_DIR)/debug.c

# Object files
OBJS := $(SRCS:.c=.o)

# Executable
TARGET := apeslang

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) build/*.apb

.PHONY: all clean
