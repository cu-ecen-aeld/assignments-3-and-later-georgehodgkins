CFLAGS=-O0 -g
CC=$(CROSS_COMPILE)gcc

all: aesdsocket

default: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS}

clean:
	rm -f aesdsocket
