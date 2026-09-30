# mvsw - built for the coprocessor, which has no usable compiler of its own.
#
# Freestanding on purpose: the appliance runs glibc 2.27, the only cross toolchain to hand targets
# a far newer one, and the only way onto the box is base64 through a 115200 console. With no libc
# this is sixty-odd kilobytes and runs on any aarch64 Linux; linked against the host's glibc it
# would be seven hundred and would not load there anyway.

CROSS?=	aarch64-linux-gnu-
CC=	$(CROSS)gcc
STRIP=	$(CROSS)strip
CFLAGS=	-Os -ffreestanding -fno-builtin -nostdlib -static -fno-stack-protector \
	-fno-asynchronous-unwind-tables -Wall -Wextra
LDFLAGS=-Wl,--build-id=none

mvsw: mvsw.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
	$(STRIP) -s $@

# What goes over the console, because there is no other way in.
mvsw.b64: mvsw
	base64 -w0 $< > $@

clean:
	rm -f mvsw mvsw.b64

.PHONY: clean
