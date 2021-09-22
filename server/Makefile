CFLAGS=-O1 -g
CC=$(CROSS_COMPILE)gcc

all: aesdsocket

default: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f aesdsocket
