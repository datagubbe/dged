.POSIX:
.PHONY: default clean check run debug debug-tests

default: dged

SOURCES = src/binding.c src/buffer.c src/command.c src/display.c \
	src/keyboard.c src/minibuffer.c src/reactor.c src/text.c \
	src/utf8.c

DGED_SOURCES = $(SOURCES) src/main.c
TEST_SOURCES = test/assert.c test/buffer.c test/text.c test/utf8.c test/main.c

.SUFFIXES:
.SUFFIXES: .c .o .d

UNAME_S != uname -s | tr '[:upper:]' '[:lower:]'

CFLAGS = -Werror -g -std=c99 -I ./src

# dependency generation
.c.d:
	$(CC) -MM $(CFLAGS) -MT $*.o $< > $@
	@sed -i 's,\($*\)\.o[ :]*,\1.o $@ : ,g' $@

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

DEPS = $(DGED_SOURCES:.c=.d) $(TEST_SOURCES:.c=.d)
FILES += $(DEPS)

OBJS = $(SOURCES:.c=.o)
FILES += $(DGED_SOURCES:.c=.o)

dged: src/main.o libdged.a
	$(CC) $(LDFLAGS) src/main.o libdged.a -o dged

FILES += dged

libdged.a: $(OBJS)
	$(AR) -rc libdged.a $(OBJS)

FILES += libdged.a

TEST_OBJS = $(TEST_SOURCES:.c=.o)

run-tests: $(TEST_OBJS) libdged.a
	$(CC) $(LDFLAGS) $(TEST_OBJS) libdged.a -o run-tests

FILES += $(TEST_OBJS)

check: run-tests
	./run-tests

run: dged
	./dged

debug: dged
	gdb ./dged

debug-tests: run-tests
	gdb ./run-tests

clean:
	rm -f $(FILES)
