# Compiles kmdf-probe.c against the real Microsoft KMDF headers
# (fetch-kmdf-headers.sh) using this project's existing mingw-w64 DDK
# toolchain - the real, live check behind ROADMAP.md Phase 9's "found,
# not yet compile-verified" KMDF finding.
#
# build/ is fetched fresh each time (ADR-0002/Rule 17, and the WDK
# EULA's own no-redistribution clause) - never committed, see
# fetch-kmdf-headers.sh's own header comment.
CC       := x86_64-w64-mingw32-gcc
DDK_INC  := /usr/x86_64-w64-mingw32/include/ddk
BUILD    := build
CFLAGS   := -Wall -Wextra -I$(BUILD)/wdf/kmdf-1.11 -I$(DDK_INC)

.PHONY: all clean fetch
all: kmdf-probe.o

fetch: $(BUILD)/wdf/kmdf-1.11/wdf.h

$(BUILD)/wdf/kmdf-1.11/wdf.h: fetch-kmdf-headers.sh
	./fetch-kmdf-headers.sh

kmdf-probe.o: kmdf-probe.c $(BUILD)/wdf/kmdf-1.11/wdf.h
	$(CC) $(CFLAGS) -c -o $@ kmdf-probe.c

clean:
	rm -f kmdf-probe.o
	rm -rf $(BUILD)
