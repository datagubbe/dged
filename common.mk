.POSIX:
.PHONY: default clean check run debug debug-tests install format

default: dged

SOURCES = src/binding.c src/buffer.c src/command.c src/display.c \
	src/keyboard.c src/minibuffer.c src/text.c \
	src/utf8.c src/buffers.c src/window.c src/allocator.c src/undo.c \
	src/settings.c src/lang.c

DGED_SOURCES = $(SOURCES) src/main.c
TEST_SOURCES = test/assert.c test/buffer.c test/text.c test/utf8.c test/main.c \
	test/command.c test/keyboard.c test/fake-reactor.c test/allocator.c \
	test/minibuffer.c test/undo.c test/settings.c

prefix != if [ -n "$$prefix" ]; then echo "$$prefix"; else echo "/usr"; fi

.SUFFIXES:
.SUFFIXES: .c .o .d

UNAME_S != uname -s | tr '[:upper:]' '[:lower:]'

CFLAGS = -Werror -g -std=c99 -I ./src

DEPS = $(DGED_SOURCES:.c=.d) $(TEST_SOURCES:.c=.d)

OBJS = $(SOURCES:.c=.o)
TEST_OBJS = $(TEST_SOURCES:.c=.o)

FILES = $(DEPS) $(DGED_SOURCES:.c=.o) dged libdged.a $(TEST_OBJS)
