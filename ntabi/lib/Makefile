# libntabi - the ntabi protocol client library. Static lib for now (Gen2
# prototype scope); a shared library + versioned soname is a reasonable
# follow-up once there's more than one consumer.
CC       ?= cc
CFLAGS   ?= -O2 -Wall -Wextra
AR       ?= ar
INCLUDES = -I../include -I../protocol
PREFIX   ?= /usr

.PHONY: all install uninstall clean

all: libntabi.a

libntabi.o: libntabi.c ../include/ntabi.h ../protocol/ntabi_protocol.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ libntabi.c

libntabi.a: libntabi.o
	$(AR) rcs $@ libntabi.o

install: libntabi.a
	install -D -m 0644 libntabi.a "$(DESTDIR)$(PREFIX)/lib/libntabi.a"
	install -D -m 0644 ../include/ntabi.h "$(DESTDIR)$(PREFIX)/include/ntlinux/ntabi.h"
	install -D -m 0644 ../protocol/ntabi_protocol.h "$(DESTDIR)$(PREFIX)/include/ntlinux/ntabi_protocol.h"

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/lib/libntabi.a" \
	      "$(DESTDIR)$(PREFIX)/include/ntlinux/ntabi.h" \
	      "$(DESTDIR)$(PREFIX)/include/ntlinux/ntabi_protocol.h"

clean:
	rm -f libntabi.o libntabi.a
