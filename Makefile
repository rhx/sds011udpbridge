#
# Makefile for sds011udpbridge
#
.c.o:
	${CC} ${CPPFLAGS} ${CFLAGS} -c -o $*.o $*.c

sds011udpbridge: sds011udpbridge.o
	${CC} -o sds011udpbridge sds011udpbridge.o

sds011udpbridge.o: sds011udpbridge.c sds011udpbridge.h

.PHONY: all
all: sds011udpbridge

.PHONY: clean
clean:
	-rm -f sds011udpbridge.o sds011udpbridge
