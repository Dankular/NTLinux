# Compiles wddm-probe.c against the real Microsoft WDK/SDK WDDM headers
# (fetch-wdk-headers.sh) using this project's existing mingw-w64 DDK
# toolchain - the real, live check behind ROADMAP.md Phase 11's
# corrected account of the WDDM/DXGKRNL toolchain gap.
#
# build/ is fetched fresh each time (ADR-0002/Rule 17, and the WDK
# EULA's own no-redistribution clause) - never committed, see
# fetch-wdk-headers.sh's own header comment.
CC       := x86_64-w64-mingw32-gcc
DDK_INC  := /usr/x86_64-w64-mingw32/include/ddk
BUILD    := build
CFLAGS   := -Wall -Wextra -I$(BUILD) -I$(DDK_INC)

.PHONY: all clean fetch
all: wddm-probe.o

fetch: $(BUILD)/dispmprt.h

$(BUILD)/dispmprt.h $(BUILD)/d3dkmddi.h $(BUILD)/d3dkmdt.h $(BUILD)/d3dukmdt.h: fetch-wdk-headers.sh
	./fetch-wdk-headers.sh

wddm-probe.o: wddm-probe.c $(BUILD)/dispmprt.h $(BUILD)/d3dkmddi.h $(BUILD)/d3dkmdt.h $(BUILD)/d3dukmdt.h
	$(CC) $(CFLAGS) -c -o $@ wddm-probe.c

clean:
	rm -f wddm-probe.o
	rm -rf $(BUILD)
