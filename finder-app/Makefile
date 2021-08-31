
CC=$(CROSS_COMPILE)gcc
all: writer

writer: writer.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f writer *.o

