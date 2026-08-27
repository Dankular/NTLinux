CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -std=gnu11
INCLUDES = -I../protocol

.PHONY: all clean
all: ntbridge-host

ntbridge-host: ntbridge-host.c ../protocol/ntbridge_protocol.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ ntbridge-host.c

clean:
	rm -f ntbridge-host
