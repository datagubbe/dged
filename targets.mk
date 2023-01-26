# dependency generation
.c.d:
	$(CC) -MM $(CFLAGS) -MT $*.o $< > $@
	@sed -i 's,\($*\)\.o[ :]*,\1.o $@ : ,g' $@

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@


dged: src/main.o libdged.a
	$(CC) $(LDFLAGS) src/main.o libdged.a -o dged

libdged.a: $(OBJS) $(PLATFORM_OBJS)
	$(AR) -rc libdged.a $(OBJS) $(PLATFORM_OBJS)

run-tests: $(TEST_OBJS) $(OBJS)
	$(CC) $(LDFLAGS) $(TEST_OBJS) $(OBJS) -o run-tests

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
	rm -rf docs

install: dged
	install -d $(prefix)/bin
	install -m 755 dged $(prefix)/bin/dged

	install -d $(prefix)/share/man/man1
	install -m 644 dged.1 $(prefix)/share/man/man1/dged.1

docs:
	doxygen Doxyfile
