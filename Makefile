# Makefile for bmake/openbsd make

default: dged

.PHONY: default clean check run debug debug-tests install format
.OBJDIR: ./build

build:
	mkdir -p build

.if ! exists(${.CURDIR}/config.mk)
.  error "Please run ./configure first"
.endif

.include "config.mk"
SYNTAX_ENABLE ?= true

HEADERS = src/dged/settings.h src/dged/minibuffer.h src/dged/keyboard.h src/dged/binding.h \
	src/dged/buffers.h src/dged/text.h src/dged/display.h src/dged/hashmap.h src/dged/path.h \
	src/dged/buffer.h src/dged/btree.h src/dged/command.h src/dged/allocator.h src/dged/reactor.h \
	src/dged/vec.h src/dged/window.h src/dged/hash.h src/dged/undo.h src/dged/lang.h \
	src/dged/settings-parse.h src/dged/utf8.h src/main/cmds.h src/main/bindings.h \
	src/main/search-replace.h src/dged/location.h src/dged/buffer_view.h src/main/completion.h \
	src/dged/timers.h src/dged/s8.h src/main/version.h src/config.h

SOURCES = src/dged/binding.c src/dged/buffer.c src/dged/command.c src/dged/display.c \
	src/dged/keyboard.c src/dged/minibuffer.c src/dged/text.c \
	src/dged/utf8.c src/dged/buffers.c src/dged/window.c src/dged/allocator.c src/dged/undo.c \
	src/dged/settings.c src/dged/lang.c src/dged/settings-parse.c src/dged/location.c \
	src/dged/buffer_view.c src/dged/timers.c src/dged/s8.c

MAIN_SOURCES = src/main/main.c src/main/cmds.c src/main/bindings.c src/main/search-replace.c src/main/completion.c

.if "$(HAS_EPOLL)" == true
  MAIN_SOURCES += src/dged/reactor-epoll.c
.elif "$(HAS_KQUEUE)" == true
  MAIN_SOURCES += src/dged/reactor-kqueue.c
.endif


TEST_SOURCES = test/assert.c test/buffer.c test/text.c test/utf8.c test/main.c \
	test/command.c test/keyboard.c test/fake-reactor.c test/allocator.c \
	test/minibuffer.c test/undo.c test/settings.c test/container.c

prefix ?= /usr/local
DESTDIR ?= $(prefix)
datadir = share/dged

.SUFFIXES:
.SUFFIXES: .c .o .d

CFLAGS += -Werror -g -O2 -std=c99 \
	-I $(.CURDIR)/src \
	-I $(.CURDIR)/src/main \
	-DDATADIR="$(prefix)/$(datadir)"

ASAN ?= false

.if $(ASAN) == true
  CFLAGS += -fsanitize=address -fno-omit-frame-pointer
  LDFLAGS += -fsanitize=address
.endif

.if $(SYNTAX_ENABLE) == true
  HEADERS += src/dged/syntax.h
  SOURCES += src/dged/syntax.c

  treesitterflags != pkg-config tree-sitter --cflags
  CFLAGS += ${treesitterflags} -DSYNTAX_ENABLE

  treesitterld != pkg-config tree-sitter --libs
  LDFLAGS += ${treesitterld}
.endif

UNAME_S != uname -s | tr '[:upper:]' '[:lower:]'
.if exists(${.CURDIR}/${UNAME_S}.mk)
.  include "$(.CURDIR)/$(UNAME_S).mk"
.endif

# add a define like LINUX/OPENBSD
UNAME_UPPER != uname -s | tr '[:lower:]' '[:upper:]'
CFLAGS += -D$(UNAME_UPPER)

FORMAT_TOOL ?= clang-format

DEPS = $(SOURCES:.c=.d) $(MAIN_SOURCES:.c=.d) $(TEST_SOURCES:.c=.d)

OBJS = $(SOURCES:.c=.o)
MAIN_OBJS = $(MAIN_SOURCES:.c=.o)
TEST_OBJS = $(TEST_SOURCES:.c=.o)

FILES = $(DEPS) \
		$(MAIN_OBJS) \
		$(OBJS) \
		$(TEST_OBJS) \
		dged \
		libdged.a

# dependency generation
.c.d: config.mk
	@mkdir -p $(@D)
	$(CC) -MM $(CFLAGS) -MT $*.o $< > $@
	@sed -i 's,\($*\)\.o[ :]*,\1.o $@ : ,g' $@

.c.o: config.mk
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

grammars:
	@if [ -n "$$TREESITTER_GRAMMARS" ]; then \
		IFS=":"; for p in "$$TREESITTER_GRAMMARS"; do \
			cp -rL --no-preserve=mode "$$p"/ grammars; \
		done \
	else \
		echo "TODO: download and build default set of grammars"; \
		mkdir -p ./grammars; \
	fi

dged: $(MAIN_OBJS) libdged.a grammars
	$(CC) $(LDFLAGS) $(MAIN_OBJS) libdged.a -o dged -lm

libdged.a: $(OBJS)
	$(AR) -rc libdged.a $(OBJS)

run-tests: $(TEST_OBJS) $(OBJS)
	$(CC) $(LDFLAGS) $(TEST_OBJS) $(OBJS) -lm -o run-tests

check: run-tests
	@echo "Running $(FORMAT_TOOL) (--dry-run --Werror)..."
	@$(FORMAT_TOOL) --dry-run --Werror \
		$(SOURCES:%.c=$(.CURDIR)/%.c) \
		$(MAIN_SOURCES:%.c=$(.CURDIR)/%.c) \
		$(TEST_SOURCES:%c=$(.CURDIR)/%c) \
		$(HEADERS:%.h=$(.CURDIR)/%.h)
	./run-tests

run: dged
	./dged

debug: dged
	gdb ./dged

debug-tests: run-tests
	gdb ./run-tests

format:
	@echo "Formatting with '$(FORMAT_TOOL) -i'..."
	@$(FORMAT_TOOL) -i \
		$(SOURCES:%.c=$(.CURDIR)/%.c) \
		$(MAIN_SOURCES:%.c=$(.CURDIR)/%.c) \
		$(TEST_SOURCES:%c=$(.CURDIR)/%c) \
		$(HEADERS:%.h=$(.CURDIR)/%.h)

clean:
	rm -f $(FILES)
	rm -rf $(.CURDIR)/docs
	rm -rf $(.OBJDIR)/grammars

install: dged
	install -d $(DESTDIR)/bin
	install -m 755 $(.OBJDIR)/dged $(DESTDIR)/bin/dged

	install -d $(DESTDIR)/share/man/man1
	install -m 644 $(.CURDIR)/dged.1 $(DESTDIR)/share/man/man1/dged.1

	install -d $(DESTDIR)/$(datadir)/grammars
	cp -RL $(.OBJDIR)/grammars "$(DESTDIR)/$(datadir)/"

docs:
	CURDIR=$(.CURDIR) doxygen $(.CURDIR)/Doxyfile

depend: $(DEPS)
	@:

.if !(make(clean))
.  for o in ${DEPS}
.    if exists(${o})
.      include "$o"
.    endif
.  endfor
.endif
