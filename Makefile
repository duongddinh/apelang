CC = cc
CFLAGS = -O2 -Wall -Wextra -Wno-unused-parameter -std=c99 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -Isrc
LDFLAGS = -lm
SANITIZERS ?= address,undefined
SRCS = src/main.c src/lexer/lexer.c src/compiler/compiler.c src/vm/vm.c src/debug/debug.c
OBJS = $(SRCS:.c=.o)
all: apeslang
apeslang: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
-include $(OBJS:.o=.d)
test: apeslang
	python3 tests/test_language.py
sanitize:
	$(CC) $(CFLAGS) -g -O1 -fsanitize=$(SANITIZERS) -fno-omit-frame-pointer $(SRCS) $(LDFLAGS) -o apeslang-sanitize
	UBSAN_OPTIONS=halt_on_error=1 APELANG_EXE="$(CURDIR)/apeslang-sanitize" python3 tests/test_language.py
clean:
	rm -f $(OBJS) $(OBJS:.o=.d) apeslang apeslang-sanitize
.PHONY: all test sanitize clean
