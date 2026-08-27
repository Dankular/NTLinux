# ntloader build. No external dependencies beyond a C toolchain and libc -
# it deliberately doesn't link against Wine or anything else; it only needs
# to parse a PE header, manage a directory tree, and execve() into `wine`
# (found via $PATH at run time, per CLAUDE.md Rule 1: consume, don't link
# against, Wine's internals).

CC       ?= cc
CFLAGS   ?= -O2 -Wall -Wextra
PREFIX   ?= /usr
LIBEXEC  ?= $(PREFIX)/libexec

.PHONY: all install uninstall clean check

all: ntloader

ntloader: ntloader.c
	$(CC) $(CFLAGS) -o $@ $<

install: ntloader
	install -D -m 0755 ntloader "$(DESTDIR)$(LIBEXEC)/ntloader"

uninstall:
	rm -f "$(DESTDIR)$(LIBEXEC)/ntloader"

check: ntloader
	./ntloader --version

clean:
	rm -f ntloader
