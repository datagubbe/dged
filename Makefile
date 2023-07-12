# Makefile for bmake
.OBJDIR: ./build
.PHONY: default clean check run debug debug-tests install format

default: dged

build:
	mkdir -p build

HEADERS = src/dged/settings.h src/dged/minibuffer.h src/dged/keyboard.h src/dged/binding.h \
	src/dged/buffers.h src/dged/text.h src/dged/display.h src/dged/hashmap.h src/dged/path.h \
	src/dged/buffer.h src/dged/btree.h src/dged/command.h src/dged/allocator.h src/dged/reactor.h \
	src/dged/vec.h src/dged/window.h src/dged/hash.h src/dged/undo.h src/dged/lang.h \
	src/dged/settings-parse.h src/dged/utf8.h src/main/cmds.h src/main/bindings.h \
	src/main/search-replace.h src/dged/location.h src/dged/buffer_view.h

SOURCES = src/dged/binding.c src/dged/buffer.c src/dged/command.c src/dged/display.c \
	src/dged/keyboard.c src/dged/minibuffer.c src/dged/text.c \
	src/dged/utf8.c src/dged/buffers.c src/dged/window.c src/dged/allocator.c src/dged/undo.c \
	src/dged/settings.c src/dged/lang.c src/dged/settings-parse.c src/dged/location.c \
	src/dged/buffer_view.c

MAIN_SOURCES = src/main/main.c src/main/cmds.c src/main/bindings.c src/main/search-replace.c

TEST_SOURCES = test/assert.c test/buffer.c test/text.c test/utf8.c test/main.c \
	test/command.c test/keyboard.c test/fake-reactor.c test/allocator.c \
	test/minibuffer.c test/undo.c test/settings.c test/container.c

prefix ?= "/usr"

.SUFFIXES:
.SUFFIXES: .c .o .d

CFLAGS += -Werror -g -std=c99 -I $(.CURDIR)/src -I $(.CURDIR)/src/main

UNAME_S != uname -s | tr '[:upper:]' '[:lower:]'
.sinclude "$(UNAME_S).mk"

DEPS = $(DGED_SOURCES:.c=.d) $(TEST_SOURCES:.c=.d)

OBJS = $(SOURCES:.c=.o)
PLATFORM_OBJS = $(PLATFORM_SOURCES:.c=.o)
MAIN_OBJS = $(MAIN_SOURCES:.c=.o)
TEST_OBJS = $(TEST_SOURCES:.c=.o)

FILES = $(DEPS) $(MAIN_OBJS) $(OBJS) dged libdged.a $(TEST_OBJS) $(PLATFORM_OBJS)

# dependency generation
.c.d:
	@mkdir -p $(@D)
	$(CC) -MM $(CFLAGS) -MT $*.o $< > $@
	@sed -i 's,\($*\)\.o[ :]*,\1.o $@ : ,g' $@

.c.o:
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

dged: $(MAIN_OBJS) libdged.a
	$(CC) $(LDFLAGS) $(MAIN_OBJS) libdged.a -o dged -lm

libdged.a: $(OBJS) $(PLATFORM_OBJS)
	$(AR) -rc libdged.a $(OBJS) $(PLATFORM_OBJS)

run-tests: $(TEST_OBJS) $(OBJS)
	$(CC) $(LDFLAGS) $(TEST_OBJS) $(OBJS) -lm -o run-tests

check: run-tests
	clang-format --dry-run --Werror $(SOURCES:%.c=../%.c) $(MAIN_SOURCES:%.c=../%.c) $(TEST_SOURCES:%c=../%c) $(HEADERS:%.h=../%.h)
	./run-tests

run: dged
	./dged

debug: dged
	gdb ./dged

debug-tests: run-tests
	gdb ./run-tests

format:
	clang-format -i $(SOURCES:%.c=../%.c) $(MAIN_SOURCES:%.c=../%.c) $(TEST_SOURCES:%c=../%c) $(HEADERS:%.h=../%.h)

clean:
	rm -f $(FILES)
	rm -rf $(.CURDIR)/docs

install: dged
	install -d $(prefix)/bin
	install -m 755 $(.OBJDIR)/dged $(prefix)/bin/dged

	install -d $(prefix)/share/man/man1
	install -m 644 $(.CURDIR)/dged.1 $(prefix)/share/man/man1/dged.1

docs:
	doxygen $(.CURDIR)/Doxyfile

# in this case we need a separate depend target
depend: $(DEPS)
	@:

.if !(make(clean))
.  for o in ${DEPS}
.    sinclude "$o"
.  endfor
.endif
