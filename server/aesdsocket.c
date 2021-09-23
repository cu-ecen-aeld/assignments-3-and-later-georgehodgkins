#define _GNU_SOURCE
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
	
// -----constants-----
#define INITIAL_MAX_PACKET 1024
#define MAX_BACKLOG 8
const char* outpath = "/var/tmp/aesdsocketdata";

// -----globals-----
int asock = -1; // accepting socket
int csock = -1; // client I/O socket
int ofd = -1; // output file descriptor
void* of_mem = NULL; // output file mapping location
size_t of_memsz = 0; // size of output file mapping
char* packet = NULL; // packet buffer

// exit/error source definitions
typedef enum {SRC_SOCKET, SRC_LISTEN, SRC_INT = SIGINT, // this has to be located at the correct place in the list :/
	SRC_BIND, SRC_OPEN, SRC_MMAP, SRC_MREMAP, SRC_ACCEPT, SRC_SIGACTION,
	SRC_MALLOC, SRC_REALLOC, SRC_FSTAT, SRC_CLOSE, SRC_READ, SRC_WRITE,
	SRC_TERM = SIGTERM, SRC_FTRUNCATE, SRC_EINVAL, SRC_DUP} cleanup_src;

const char* errs[] = {
	[SRC_INT] = "Caught signal SIGINT, exiting",
	[SRC_TERM] = "Caught signal SIGTERM, exiting",
	[SRC_SOCKET] = "error creating server socket",
	[SRC_LISTEN] = "error listening on server socket",
	[SRC_BIND] = "error binding server socket",
	[SRC_OPEN] = "error opening output file",
	[SRC_MMAP] = "error mapping output file",
	[SRC_MREMAP] = "error expanding output file mapping",
	[SRC_ACCEPT] = "error accepting client connection",
	[SRC_SIGACTION] = "error installing signal handler",
	[SRC_MALLOC] = "error allocating packet buffer",
	[SRC_REALLOC] = "error expanding packet buffer",
	[SRC_FSTAT] = "error in fstat",
	[SRC_CLOSE] = "error closing client socket",
	[SRC_READ] = "error reading from client socket",
	[SRC_WRITE] = "error writing to client socket",
	[SRC_FTRUNCATE] = "error extending output file",
	[SRC_EINVAL] = "parsing command line arguments",
	[SRC_DUP] = "error redirecting stream"
};

// cleanup handler
__attribute__((noreturn))
static void cleanup (cleanup_src src) {
	// record exit cause
	const char* msg = errs[src];
	int xstat = 0;
	if (src == SRC_INT || src == SRC_TERM) {
		syslog(LOG_INFO, "%s\n", msg);
	} else {
		syslog(LOG_ERR, "%s: %m", msg);
		xstat = -1;
	}
	closelog();

	// clean up state
	if (csock != -1) close(csock);
	if (asock != -1) close(asock);
	if (of_mem) munmap(of_mem, of_memsz);
	if (ofd != -1) close (ofd);
	if (packet) free(packet);
	unlink(outpath);

	exit(xstat);
}

// wrapper to conform to sigaction struct def
__attribute__((noreturn))
static void sigcleanup (int sig) {
	assert(sig == SIGTERM || sig == SIGINT);
	cleanup((cleanup_src) sig);
}

int main (int argc, char** argv) {
	// open syslog
	openlog(NULL, LOG_PERROR, LOG_USER);

	// check for daemon arg
	bool daemon = false;
	if (argc == 2 && strncmp("-d", argv[1], 3) == 0) daemon = true;
	else if (argc >= 2) {
		errno = EINVAL;   
		cleanup(SRC_EINVAL);
	}
	
	// install signal handler
	struct sigaction act = {
		.sa_handler = sigcleanup,
		.sa_flags = 0
	};
	int s = sigaction(SIGINT, &act, NULL);
	if (s == -1) cleanup(SRC_SIGACTION);
	s = sigaction(SIGTERM, &act, NULL);
	if (s == -1) cleanup(SRC_SIGACTION);

	// open+mmap output file
	const long PAGE_SIZE = sysconf(_SC_PAGESIZE);
	ofd = open(outpath, O_RDWR | O_CREAT | O_APPEND, 0644);
	struct stat stat;
	s = fstat(ofd, &stat);
	if (s == -1) cleanup(SRC_FSTAT);
	size_t of_sz = (size_t) stat.st_size;
	of_memsz = (of_sz/PAGE_SIZE + 1)*PAGE_SIZE;
	s = ftruncate(ofd, of_memsz);
	if (s == -1) cleanup(SRC_FTRUNCATE);
	void* of_mem = mmap(NULL, of_memsz, PROT_READ | PROT_WRITE, MAP_SHARED, ofd, 0);
	if (of_mem == MAP_FAILED) cleanup(SRC_MMAP);
	char* of_pt = (char*) of_mem + of_sz;

	// create socket
	asock = socket(AF_INET, SOCK_STREAM, 0);
	if (asock == -1) cleanup(SRC_SOCKET);
	int opt = 1;
	s = setsockopt(asock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int)); // skip waiting for address to be freed
	if (s == -1) cleanup(SRC_SOCKET);

	// bind socket
	struct sockaddr_in serv_addr = {
		.sin_family = AF_INET,
		.sin_port = htons(9000),
	//	.sin_addr = htonl(INADDR_LOOPBACK)
		.sin_addr = htonl(INADDR_ANY)
	};
	s = bind(asock, &serv_addr, sizeof(struct sockaddr_in));
	if (s == -1) cleanup(SRC_BIND);

	// alloc packet buffer
	size_t max_packet = INITIAL_MAX_PACKET;
	packet = malloc(max_packet);
	if (!packet) cleanup(SRC_MALLOC);

	// daemonise if requested
	if (daemon) {
		pid_t cpid = fork();
		if (cpid == 0) { // in daemon
			int nullfd = open("/dev/null", O_RDWR);
			if (nullfd == -1) cleanup(SRC_DUP);
			int fd = dup2(nullfd, fileno(stdout));
			if (fd == -1) cleanup(SRC_DUP);
			fd = dup2(nullfd, fileno(stderr));
			if (fd == -1) cleanup(SRC_DUP);
			fd = dup2(nullfd, fileno(stdin));
			if (fd == -1) cleanup(SRC_DUP);
			
			syslog(LOG_INFO, "daemonised with pid %d, sid %d", getpid(), setsid()); 
		} else {
			printf("Daemon pid %d\n", cpid);
		   	exit(0);
		}
	}	

	// listen on socket
	s = listen(asock, MAX_BACKLOG);
	if (s == -1) cleanup(SRC_LISTEN);

	// main server loop (exited via interrupt)
	while (1) {
		// wait for connection
		struct sockaddr_in cli_addr = {0};
		socklen_t addrlen = sizeof(struct sockaddr_in);
		csock = accept4(asock, &cli_addr, &addrlen, 0);
		if (csock == -1) cleanup(SRC_ACCEPT);
		assert(addrlen <= sizeof(struct sockaddr_in));

		// log accepted connection
		const char* cip = inet_ntoa(cli_addr.sin_addr);
		syslog(LOG_INFO, "Accepted connection from %s\n", cip);

		// get packet
		char* delim = NULL;
		size_t packet_sz = 0;
		size_t max_read = max_packet;
		while (!delim) {
			assert(packet_sz <= max_packet);
			ssize_t read_sz = read(csock, &packet[packet_sz], max_read);	
			if (read_sz == -1)
				cleanup(SRC_READ);
			
			if (read_sz == max_read) { // need to expand buffer
				max_read = max_packet;
				max_packet *= 2;
				packet = realloc(packet, max_packet);
				if (!packet) cleanup(SRC_REALLOC);
			}

			delim = memchr(&packet[packet_sz], '\n', read_sz);
			packet_sz += read_sz;
			max_read -= read_sz;
		}

		// log packet
		if (of_sz + packet_sz > of_memsz) { // need to expand mapping
			void* prev_mem = of_mem;
			size_t new_memsz = ((of_sz + packet_sz)/PAGE_SIZE + 1)*PAGE_SIZE;
			s = ftruncate(ofd, new_memsz);
			if (s == -1) cleanup(SRC_FTRUNCATE);
			of_mem = mremap(of_mem, of_memsz, new_memsz, MREMAP_MAYMOVE);
			if (of_mem == MAP_FAILED) cleanup(SRC_MREMAP);
			if (of_mem != prev_mem) of_pt = (char*) of_mem + of_sz;
			of_memsz = new_memsz;
		}
		memcpy(of_pt, packet, packet_sz);
		of_sz += packet_sz;
		of_pt += packet_sz;

		// send whole output file to client
		size_t write_sz = of_sz;
		char* src_pt = (char*) of_mem;
		ssize_t wsz = 0;
		do {
			write_sz -= wsz;
			src_pt += wsz;
			wsz = write(csock, of_mem, write_sz);
		} while (wsz != -1 && errno != EAGAIN && errno != EWOULDBLOCK && write_sz > wsz);
		if (wsz == -1) cleanup(SRC_WRITE);

		// close & cleanup connection
		s = close(csock);
		if (s == -1) cleanup(SRC_CLOSE);
		syslog(LOG_INFO, "Closed connection from %s", cip);
		csock = -1;
		memset(packet, 0, max_packet);
	}
}
				
