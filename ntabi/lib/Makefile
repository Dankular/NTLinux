# libntabi - the ntabi protocol client library.
CC       ?= cc
CFLAGS   ?= -O2 -Wall -Wextra
AR       ?= ar
INCLUDES = -I../include -I../protocol
PREFIX   ?= /usr

.PHONY: all install uninstall clean

# Real second consumer now exists (wine/ntabi-sync-integration/ dlopens
# libntabi.so at runtime, gated behind WINENTABI=1 - see that
# directory's README) - the "follow-up once there's more than one
# consumer" this file used to defer is done. No versioned soname yet
# (still a single, same-tree consumer, not an installed system library
# other packages link against) - real, separate follow-up if that
# changes.
all: libntabi.a libntabi.so

libntabi.o: libntabi.c ../include/ntabi.h ../protocol/ntabi_protocol.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ libntabi.c

libntabi.a: libntabi.o
	$(AR) rcs $@ libntabi.o

libntabi_pic.o: libntabi.c ../include/ntabi.h ../protocol/ntabi_protocol.h
	$(CC) $(CFLAGS) $(INCLUDES) -fPIC -c -o $@ libntabi.c

libntabi.so: libntabi_pic.o
	$(CC) -shared -o $@ libntabi_pic.o -lpthread -lrt

install: libntabi.a libntabi.so
	install -D -m 0644 libntabi.a "$(DESTDIR)$(PREFIX)/lib/libntabi.a"
	install -D -m 0755 libntabi.so "$(DESTDIR)$(PREFIX)/lib/libntabi.so"
	install -D -m 0644 ../include/ntabi.h "$(DESTDIR)$(PREFIX)/include/ntlinux/ntabi.h"
	install -D -m 0644 ../protocol/ntabi_protocol.h "$(DESTDIR)$(PREFIX)/include/ntlinux/ntabi_protocol.h"

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/lib/libntabi.a" "$(DESTDIR)$(PREFIX)/lib/libntabi.so" \
	      "$(DESTDIR)$(PREFIX)/include/ntlinux/ntabi.h" \
	      "$(DESTDIR)$(PREFIX)/include/ntlinux/ntabi_protocol.h"

clean:
	rm -f libntabi.o libntabi_pic.o libntabi.a libntabi.so
