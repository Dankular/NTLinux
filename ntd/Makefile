# ntd - the NT object-manager daemon (Phase 2 scope: Event/Mutant/
# Semaphore/handles/waits only - see ntd.c's header comment).
CC       ?= cc
CFLAGS   ?= -O2 -Wall -Wextra
INCLUDES = -I../ntabi/protocol
LDLIBS   = -lpthread -lrt
PREFIX   ?= /usr

.PHONY: all install uninstall clean

all: ntd

ntd: ntd.c ../ntabi/protocol/ntabi_protocol.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ ntd.c $(LDLIBS)

install: ntd
	install -D -m 0755 ntd "$(DESTDIR)$(PREFIX)/bin/ntd"

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/bin/ntd"

clean:
	rm -f ntd
