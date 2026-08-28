# Cross-compiles ntnet.sys, from Linux, using mingw-w64's bundled DDK
# headers (ReactOS's own ndis.h/wdm.h, repackaged) - not RosBE, same
# correction as driver/ntbridge/reactos/ and driver/vsdev/.
#
# Three of those headers' own bugs are patched into a local ./build/
# copy first (prepare-ndis-header.sh; does not touch the system
# install) - found only by actually building an NDIS 5.1 miniport
# against them, documented in that script and in ntnet.c's own header
# comment.
CC       := x86_64-w64-mingw32-gcc
DDK_INC  := /usr/x86_64-w64-mingw32/include/ddk
BUILD    := build
CFLAGS   := -O2 -Wall -Wextra -I$(BUILD) -I$(DDK_INC) -I../../ntbridge/protocol
LDFLAGS  := -Wl,--subsystem,native -Wl,--entry,DriverEntry -nostdlib -shared
LDLIBS   := -lndis -lntoskrnl -lhal

.PHONY: all clean
all: ntnet.sys

$(BUILD)/ndis.h $(BUILD)/wdm.h: prepare-ndis-header.sh
	./prepare-ndis-header.sh

ntnet.sys: ntnet.o
	$(CC) $(LDFLAGS) -o $@ ntnet.o $(LDLIBS)

ntnet.o: ntnet.c ../../ntbridge/protocol/ntbridge_protocol.h $(BUILD)/ndis.h $(BUILD)/wdm.h
	$(CC) $(CFLAGS) -c -o $@ ntnet.c

clean:
	rm -f ntnet.o ntnet.sys
	rm -rf $(BUILD)
