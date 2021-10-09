CFLAGS=-O0 -g
CC=$(CROSS_COMPILE)gcc

all: clean aesdsocket

default: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -o $@ $^ -pthread

clean:
	rm -f aesdsocket
