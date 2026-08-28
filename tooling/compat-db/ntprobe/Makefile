# Cross-compiles ntprobe.exe for a real Windows machine, from Linux.
# No native Windows toolchain needed to build it - only to run it.
#
# -lntdll links against mingw-w64's own ntdll import stub (confirmed via
# `nm` to export NtQuerySystemInformation/NtQueryInformationProcess/
# NtQueryObject/NtCreateEvent/NtSetEvent/NtWaitForSingleObject/NtClose -
# see ntprobe.c's header comment for why this file hand-declares their
# prototypes/structs instead of #include-ing Musa.Veil's headers directly.
CC       := x86_64-w64-mingw32-gcc
CFLAGS   := -O2 -Wall -Wextra

.PHONY: all clean
all: ntprobe.exe

ntprobe.exe: ntprobe.c
	$(CC) $(CFLAGS) -o $@ ntprobe.c -lntdll

clean:
	rm -f ntprobe.exe
