# libntabi/ntd test suite. Depends on ../lib/libntabi.a and ../../ntd/ntd
# both being built first.
CC       ?= cc
CFLAGS   ?= -O2 -Wall -Wextra
INCLUDES = -I../include -I../protocol
LDLIBS   = -lpthread -lrt

.PHONY: all check clean

all: test_ntabi

../lib/libntabi.a:
	$(MAKE) -C ../lib

../../ntd/ntd:
	$(MAKE) -C ../../ntd

test_ntabi: test_ntabi.c ../lib/libntabi.a ../include/ntabi.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_ntabi.c ../lib/libntabi.a $(LDLIBS)

check: test_ntabi ../../ntd/ntd
	./test_ntabi ../../ntd/ntd

clean:
	rm -f test_ntabi
