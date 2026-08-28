# Cross-compiles ntbridge.sys, from Linux, using mingw-w64's bundled DDK
# headers and ntoskrnl/hal import libraries - NOT RosBE. See this
# directory's README.md and ROADMAP.md Phase 4 for the correction: RosBE
# is only needed to build ReactOS *itself* from source; a driver that
# merely targets its (Windows-compatible) kernel ABI needs only a DDK
# header set and ntoskrnl.exe/hal.dll import stubs, which mingw-w64
# already ships.
CC       := x86_64-w64-mingw32-gcc
DDK_INC  := /usr/x86_64-w64-mingw32/include/ddk
CFLAGS   := -O2 -Wall -Wextra -I$(DDK_INC) -I../protocol
LDFLAGS  := -Wl,--subsystem,native -Wl,--entry,DriverEntry -nostdlib -shared
LDLIBS   := -lntoskrnl -lhal

.PHONY: all clean
all: ntbridge.sys

ntbridge.sys: ntbridge_pnp.o
	$(CC) $(LDFLAGS) -o $@ ntbridge_pnp.o $(LDLIBS)

ntbridge_pnp.o: ntbridge_pnp.c ntbridge_pnp.h ../protocol/ntbridge_protocol.h
	$(CC) $(CFLAGS) -c -o $@ ntbridge_pnp.c

clean:
	rm -f ntbridge_pnp.o ntbridge.sys
