# Cross-compiles ntusb.sys, from Linux, using mingw-w64's bundled DDK
# headers (usb.h/usbioctl.h - real client-facing USBD/URB headers, not
# usbport.h - see this directory's README for why that distinction is
# the whole reason ntusb.c is a bus driver, not a HC miniport) and
# ntoskrnl/hal import libraries - not RosBE, same correction as
# driver/ntbridge/reactos/, driver/vsdev/, driver/net/reactos/.
CC       := x86_64-w64-mingw32-gcc
DDK_INC  := /usr/x86_64-w64-mingw32/include/ddk
CFLAGS   := -O2 -Wall -Wextra -I$(DDK_INC) -I../../ntbridge/protocol
LDFLAGS  := -Wl,--subsystem,native -Wl,--entry,DriverEntry -nostdlib -shared
# -lgcc: NtusbDispatchDeviceControl's on-stack localData[NTUSB_MAX_TRANSFER]
# (4096 bytes) makes its stack frame big enough that mingw's x86_64 codegen
# emits a call to __chkstk_ms (the standard Windows stack-probe helper for
# frames > 1 page) - a real linker error found only by actually building
# this file (undefined reference to ___chkstk_ms), not by inspection.
# __chkstk_ms lives in libgcc, not ntoskrnl/hal, and -nostdlib excludes it
# by default; -lgcc pulls in just that one compiler-support routine, not a
# CRT or anything requiring an OS to run under.
LDLIBS   := -lntoskrnl -lhal -lgcc

.PHONY: all clean
all: ntusb.sys

ntusb.sys: ntusb.o
	$(CC) $(LDFLAGS) -o $@ ntusb.o $(LDLIBS)

ntusb.o: ntusb.c ntusb.h ../../ntbridge/protocol/ntbridge_protocol.h
	$(CC) $(CFLAGS) -c -o $@ ntusb.c

clean:
	rm -f ntusb.o ntusb.sys
