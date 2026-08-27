# Cross-compiles ntexports.exe for a real Windows machine, from Linux.
# No native Windows toolchain needed to build it - only to run it.
CC       := x86_64-w64-mingw32-gcc
CFLAGS   := -O2 -Wall -Wextra -municode
LDLIBS   := -lversion

.PHONY: all clean

all: ntexports.exe

ntexports.exe: ntexports.c
	$(CC) $(CFLAGS) -o $@ ntexports.c $(LDLIBS)

clean:
	rm -f ntexports.exe
