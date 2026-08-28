# Compiles (and, unlike driver/gpu/wddm-probe.c and driver/kmdf/
# kmdf-probe.c, actually links) ndis6-probe.c against the real
# Microsoft NDIS 6.x headers (fetch-ndis6-headers.sh) using this
# project's existing mingw-w64 DDK toolchain - the real, live check
# behind tooling/compat-db/ddkgap/'s "abi-only (header gap)" NDIS 6.x
# finding (ROADMAP.md Phase 9).
#
# build/ is fetched fresh each time (ADR-0002/Rule 17, and the WDK
# EULA's own no-redistribution clause) - never committed, see
# fetch-ndis6-headers.sh's own header comment.
CC       := x86_64-w64-mingw32-gcc
DDK_INC  := /usr/x86_64-w64-mingw32/include/ddk
DDK_LIB  := /usr/x86_64-w64-mingw32/lib
BUILD    := build
CFLAGS   := -Wall -Wextra -I$(BUILD)/shared -I$(BUILD)/km -I$(DDK_INC)
LDFLAGS  := -Wl,--subsystem,native -Wl,--entry,DriverEntry -nostdlib -shared -L$(DDK_LIB)
LDLIBS   := -lndis -lntoskrnl -lhal

.PHONY: all link clean fetch
all: ndis6-probe.o

# The real, further-than-KMDF/WDDM check: this DOES link, against the
# real libndis.a already in this toolchain (no separate import library
# needed, unlike KMDF's Wdf01000/WDDM's dxgkrnl.sys, both absent) -
# same LDFLAGS/LDLIBS shape driver/net/reactos/Makefile already uses
# to link ntnet.sys.
link: ndis6-probe.sys

fetch: $(BUILD)/km/ndis.h

$(BUILD)/km/ndis.h: fetch-ndis6-headers.sh
	./fetch-ndis6-headers.sh

ndis6-probe.o: ndis6-probe.c $(BUILD)/km/ndis.h
	$(CC) $(CFLAGS) -c -o $@ ndis6-probe.c

ndis6-probe.sys: ndis6-probe.o
	$(CC) $(LDFLAGS) -o $@ ndis6-probe.o $(LDLIBS)

clean:
	rm -f ndis6-probe.o ndis6-probe.sys
	rm -rf $(BUILD)
