# Cross-compiles vsdev.sys, from Linux, using mingw-w64's bundled DDK
# headers and ntoskrnl/hal import libraries - not RosBE (RosBE builds
# ReactOS itself; a driver only needs a DDK + import libs for
# ntoskrnl.exe/hal.dll, which mingw-w64 already ships). See
# driver/ntbridge/reactos/README.md for the full correction.
CC       := x86_64-w64-mingw32-gcc
DDK_INC  := /usr/x86_64-w64-mingw32/include/ddk
CFLAGS   := -O2 -Wall -Wextra -I$(DDK_INC)
LDFLAGS  := -Wl,--subsystem,native -Wl,--entry,DriverEntry -nostdlib -shared
LDLIBS   := -lntoskrnl -lhal

.PHONY: all clean
all: vsdev.sys

vsdev.sys: vsdev.o
	$(CC) $(LDFLAGS) -o $@ vsdev.o $(LDLIBS)

vsdev.o: vsdev.c
	$(CC) $(CFLAGS) -c -o $@ vsdev.c

clean:
	rm -f vsdev.o vsdev.sys
