#include <fcntl.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define DEBUG(fmt, ...) do{syslog(LOG_USER|LOG_DEBUG, fmt __VA_OPT__(,) __VA_ARGS__);} while (0)
#define ERROR(fmt, ...) do { \
	syslog(LOG_USER|LOG_ERR, fmt __VA_OPT__(,) __VA_ARGS__); \
	exit(1); \
} while (0)

int main (int argc, char** argv) {
	openlog(NULL, LOG_PERROR, LOG_USER);
	
	if (argc != 3) 
		ERROR("Expected usage: </path/to/file> <string to write>\n");
	
	// we expect first arg to be a path
	// second arg can be any string
	char* base = strrchr(argv[1], '/');
	int dirfd;
	if (base) {
		*base = 0;
		++base;
		dirfd = open(argv[1], O_DIRECTORY);
		if (dirfd < 0)
			ERROR("Could not open directory %s: %s\n", argv[1], strerror(errno));
		*(base-1) = '/';
	} else { // accept all relative paths
		dirfd = AT_FDCWD;
		base = argv[1];
	}

	int fd = openat(dirfd, base, O_WRONLY|O_CREAT|O_TRUNC, 0644);
	if (fd < 0) 
		ERROR("Could not open file %s for writing: %s\n", argv[1], strerror(errno));	
	close(dirfd);

	DEBUG("Writing %s to %s\n", argv[2], argv[1]);

	size_t rlen = strlen(argv[2]);
	char* in = argv[2];
	ssize_t c;
	do {
		c = write(fd, in, rlen);
		rlen -= c;
		in += c;
	} while (c > 0 && rlen > 0);
	
	if (c <= 0) 
		ERROR("Could not write string to file: %s\n", strerror(errno));

	int s = close(fd);
	if (s != 0)
		ERROR("Error writing to file: %s\n", strerror(errno));

	return 0;
}

