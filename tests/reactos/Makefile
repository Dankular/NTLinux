CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -std=gnu11 -static
INCLUDES = -I../../driver/ntbridge/protocol

.PHONY: all clean
all: ntbridge-guest-test

ntbridge-guest-test: ntbridge-guest-test.c ../../driver/ntbridge/protocol/ntbridge_protocol.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ ntbridge-guest-test.c

clean:
	rm -f ntbridge-guest-test
	rm -rf initramfs.cpio.gz initramfs-root
