.PHONY: default clean check run debug debug-tests

default: dged

headers != find src/ -type f -name '*.h'
srcs != find src/ -type f -name '*.c' ! -name 'main.c'

test_headers != find test/ -type f -name '*.h'
test_srcs != find test/ -type f -name '*.c'

objs-path = objs
objs = $(patsubst %.c,$(objs-path)/%.o, $(srcs))
test_objs = $(patsubst %.c,$(objs-path)/test/%.o, $(test_srcs))

UNAME_S != uname -s

CFLAGS = -Werror -g -std=c99

ifeq ($(UNAME_S),Linux)
	DEFINES += -DLINUX -D_XOPEN_SOURCE=700
endif

$(objs-path)/test/%.o: %.c $(headers)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEFINES) -I ./src -I ./test -c $< -o $@

$(objs-path)/%.o: %.c $(headers)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEFINES) -I ./src -c $< -o $@

dged: $(objs) $(objs-path)/src/main.o
	$(CC) $(LDFLAGS) $(objs) $(objs-path)/src/main.o -o dged

run-tests: $(test_objs) $(objs)
	$(CC) $(LDFLAGS) $(test_objs) $(objs) -o run-tests

check: run-tests
	./run-tests

run: dged
	./dged

debug: dged
	gdb ./dged

debug-tests: run-tests
	gdb ./run-tests

clean:
	rm -rf $(objs-path) dged run-tests
