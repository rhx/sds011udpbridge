#
# Makefile for tty2udp
#
.c.o:
	${CC} ${CPPFLAGS} ${CFLAGS} -c -o $*.o $*.c

tty2udp: tty2udp.o
	${CC} -o tty2udp tty2udp.o

tty2udp.o: tty2udp.c

.PHONY: all
all: tty2udp

.PHONY: clean
clean:
	-rm -f tty2udp.o tty2udp
