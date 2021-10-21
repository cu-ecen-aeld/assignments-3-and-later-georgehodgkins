CFLAGS=-O0 -g
LDFLAGS=-pthread
CC=$(CROSS_COMPILE)gcc

all: aesdsocket

default: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

aesdsocket-chrdev: aesdsocket.c
	$(CC) $(CFLAGS) -DUSE_AESD_CHAR_DEVICE -o $@ $^ $(LDFLAGS)

clean:
	rm -f aesdsocket*
