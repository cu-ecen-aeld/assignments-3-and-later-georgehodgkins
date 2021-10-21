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
#include <pthread.h>
	
// client thread parameters
struct client {
	int sock;
	struct in_addr addr;
};

// thread list entry
struct node {
	pthread_t tid;
	struct node* next;
	struct client c;
};

// -----constants-----
#define INITIAL_MAX_PACKET 1024
#define MAX_BACKLOG 8

#ifdef USE_AESD_CHAR_DEVICE
const char* outpath = "/dev/aesdchar";
#else
const char* outpath = "/var/tmp/aesdsocketdata";
#define MAX_TIMELEN 48
#define WRTIME_PERIOD 10
static char timestr[MAX_TIMELEN]; // static buffer for time string
#endif

// -----globals-----
long PAGE_SIZE; // \_/()\_/
int asock = -1; // accepting socket
int ofd = -1; // output file descriptor
size_t of_sz = 0;
#ifndef USE_AESD_CHAR_DEVICE
void* of_mem = NULL; // output file mapping
size_t of_memsz = 0; // size of output file mapping
char* of_pt = NULL; // output cursor
#endif
pthread_mutex_t of_lk = PTHREAD_MUTEX_INITIALIZER; // output file lock
pthread_mutex_t ntoa_lk = PTHREAD_MUTEX_INITIALIZER; // lock for inet_ntoa (uses a static buffer)
struct node* thread_ll = NULL; // linked list of active threads


// ------error handling-----
// exit/error source definitions
typedef enum {SRC_SOCKET, SRC_LISTEN, SRC_INT = SIGINT, // this has to be located at the correct place in the list :/
	SRC_BIND, SRC_OPEN, SRC_MMAP, SRC_MREMAP, SRC_ACCEPT, SRC_SIGACTION,
	SRC_MALLOC, SRC_REALLOC, SRC_FSTAT, SRC_CLOSE, SRC_READ, SRC_WRITE,
	SRC_TERM = SIGTERM, SRC_FTRUNCATE, SRC_EINVAL, SRC_DUP,
	SRC_PTHCR, SRC_PTHJN, SRC_STRFTIME, SRC_PTHATTR, SRC_C_WRITE} cleanup_src;

// error message table
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
	[SRC_DUP] = "error redirecting stream",
	[SRC_PTHCR] = "error creating client thread",
	[SRC_PTHJN] = "error joining client thread",
	[SRC_STRFTIME] = "error formatting time",
	[SRC_PTHATTR] = "error setting thread attributes",
	[SRC_C_WRITE] = "error writing to circular buffer"
};

// main cleanup handler
__attribute__((noreturn))
static void cleanup_errno (cleanup_src src, int e) {
	// cleanup threads
	for (struct node* n = thread_ll; n; n = n->next)
		pthread_cancel(n->tid);
	for (struct node* n = thread_ll; n; n = n->next)
		pthread_join(n->tid, NULL);

	// record exit cause
	const char* msg = errs[src];
	int xstat = 0;
	if (e == 0) {
		syslog(LOG_INFO, "%s\n", msg);
	} else {
		syslog(LOG_ERR, "%s: %s", msg, strerror(e));
		xstat = -1;
	}
	closelog();

	// clean up state
	struct node* p;
	for (struct node* n = thread_ll; n; n = p) {
		p = n->next;
	   	free(n);
	}	
	if (asock != -1) close(asock);
	if (ofd != -1) close (ofd);
#ifndef USE_AESD_CHAR_DEVICE
	if (of_mem) munmap(of_mem, of_memsz);
	unlink(outpath);
#endif

	exit(xstat);
}

// synchronous main thread cleanup function
__attribute__((noreturn))
static void cleanup (cleanup_src src) {
	cleanup_errno(src, errno);
}

// async SIGTERM/SIGINT handler
__attribute__((noreturn))
static void sigcleanup (int sig, siginfo_t* info, void* unused) {
	assert(sig == SIGTERM || sig == SIGINT);
	if (info->si_code == SI_QUEUE) {
		int32_t* unpack = (int32_t*) &info->si_ptr; 
		cleanup_errno((cleanup_src) unpack[0], unpack[1]);
	} else cleanup_errno((cleanup_src) sig, 0);
}

// cleanup handler for threads (sends SIGTERM with error info)
__attribute__((noreturn))
static void cleanup_thr (cleanup_src src) {
	int32_t pack[2] = {src, errno};
	union sigval v = {.sival_ptr = (void*) *(uint64_t*) pack};
	sigqueue(getpid(), SIGTERM, v);
	pthread_exit(NULL);
}

//-----client thread-----

// close socket if cancelled
static void vclose(void* fdpt) {
	close(*(int*) fdpt);
}

static void* client_thread (void* param_v) {
	// these must be declared before pushing the cleanup handler
	// because it creates a scope...
	int s;
	char* cip;
	struct client* param = (struct client*) param_v;
	pthread_cleanup_push(vclose, &param->sock);

	// log accepted connection
	cip = alloca(INET_ADDRSTRLEN);
	pthread_mutex_lock(&ntoa_lk);
	const char* ipstr = inet_ntoa(param->addr);
	strncpy(cip, ipstr, INET_ADDRSTRLEN);
	pthread_mutex_unlock(&ntoa_lk);
	syslog(LOG_INFO, "Accepted connection from %s\n", cip);
	
	// alloc packet buffer
	size_t max_packet = INITIAL_MAX_PACKET;
	char* packet = malloc(max_packet);
	if (!packet) cleanup_thr(SRC_MALLOC);
	pthread_cleanup_push(free, packet);

	// get packet
	char* delim = NULL;
	size_t packet_sz = 0;
	size_t max_read = max_packet;
	while (!delim) {
		assert(packet_sz <= max_packet);
		ssize_t read_sz = read(param->sock, &packet[packet_sz], max_read);	
		if (read_sz == -1)
			cleanup_thr(SRC_READ);
		
		if (read_sz == max_read) { // need to expand buffer
			max_read = max_packet;
			max_packet *= 2;
			packet = realloc(packet, max_packet);
			if (!packet) cleanup_thr(SRC_REALLOC);
		}

		delim = memchr(&packet[packet_sz], '\n', read_sz);
		packet_sz += read_sz;
		max_read -= read_sz;
	}

#ifdef USE_AESD_CHAR_DEVICE
	// write packet to buffer
	pthread_mutex_lock(&of_lk);
	size_t wrsz = packet_sz;
	char* packpt = packet;
	ssize_t wrc;
	do {
		wrc = write(ofd, packpt, wrsz);
		wrsz -= wrc;
		packpt += wrc;
	} while (wrsz > 0 && wrc != -1);
	if (wrc == -1) cleanup_thr(SRC_C_WRITE);
	of_sz += packet_sz;

	// send buffer contents to client
	off64_t sploff = 0;
	wrsz = of_sz;
	do {
		wrc = splice(ofd, &sploff, param->sock, NULL, wrsz, 0);
		wrsz -= wrc;
	} while (wrsz > 0 && wrc != -1);
	if (wrc == -1) cleanup_thr(SRC_WRITE);
	pthread_mutex_unlock(&of_lk);
#else 
	// write packet
	pthread_mutex_lock(&of_lk);
	if (of_sz + packet_sz > of_memsz) { // need to expand mapping
		void* prev_mem = of_mem;
		size_t new_memsz = ((of_sz + packet_sz)/PAGE_SIZE + 1)*PAGE_SIZE;
		s = ftruncate(ofd, new_memsz);
		if (s == -1) cleanup_thr(SRC_FTRUNCATE);
		of_mem = mremap(of_mem, of_memsz, new_memsz, MREMAP_MAYMOVE);
		if (of_mem == MAP_FAILED) cleanup_thr(SRC_MREMAP);
		if (of_mem != prev_mem) of_pt = (char*) of_mem + of_sz;
		of_memsz = new_memsz;
	}
	memcpy(of_pt, packet, packet_sz);
	of_sz += packet_sz;
	of_pt += packet_sz;
	size_t write_sz = of_sz;
	char* src_pt = (char*) of_mem;
	pthread_mutex_unlock(&of_lk);

	// send output file to client, up to its own packet
	ssize_t wsz = 0;
	do {
		write_sz -= wsz;
		src_pt += wsz;
		wsz = write(param->sock, of_mem, write_sz);
	} while (wsz != -1 && write_sz > wsz);
	if (wsz == -1) cleanup_thr(SRC_WRITE);
#endif

	// close & cleanup connection
	pthread_cleanup_pop(0);
	free(packet);
	pthread_cleanup_pop(0);
	s = close(param->sock);
	if (s == -1) cleanup(SRC_CLOSE);
	syslog(LOG_INFO, "Closed connection from %s", cip);
	return NULL;
}

#ifndef USE_AESD_CHAR_DEVICE
// periodic time logger (SIGALRM handler)
static void wrtime (int sig) {
	assert(sig == SIGALRM);
	time_t t = time(NULL);
	size_t s = strftime((char*) &timestr, MAX_TIMELEN, "timestamp:%a, %d %b %Y %T %z\n", localtime(&t));
	if (s == 0) cleanup(SRC_STRFTIME);
	pthread_mutex_lock(&of_lk);
	if (of_sz + s > of_memsz) { // need to expand mapping
		void* prev_mem = of_mem;
		size_t new_memsz = (of_memsz/PAGE_SIZE + 1)*PAGE_SIZE;
		s = ftruncate(ofd, new_memsz);
		if (s == -1) cleanup(SRC_FTRUNCATE);
		of_mem = mremap(of_mem, of_memsz, new_memsz, MREMAP_MAYMOVE);
		if (of_mem == MAP_FAILED) cleanup(SRC_MREMAP);
		if (of_mem != prev_mem) of_pt = (char*) of_mem + of_sz;
		of_memsz = new_memsz;
	}
	memcpy(of_pt, timestr, s);
	of_sz += s;
	of_pt += s;
	pthread_mutex_unlock(&of_lk);
	alarm(WRTIME_PERIOD);
}
#endif // USE_AESD_CHAR_DEVICE

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
	sigset_t fullmask;
	sigfillset(&fullmask); // no alarms during termination
	struct sigaction act = {
		.sa_sigaction = sigcleanup,
		.sa_flags = SA_SIGINFO,
		.sa_mask = fullmask
	};
	int s = sigaction(SIGINT, &act, NULL);
	if (s == -1) cleanup(SRC_SIGACTION);
	s = sigaction(SIGTERM, &act, NULL);
	if (s == -1) cleanup(SRC_SIGACTION);

#ifdef USE_AESD_CHAR_DEVICE
	ofd = open(outpath, O_RDWR, 0644);
	if (ofd == -1) cleanup(SRC_OPEN);
#else
	// open+mmap output file
	PAGE_SIZE = sysconf(_SC_PAGESIZE);
	ofd = open(outpath, O_RDWR | O_CREAT | O_APPEND, 0644);
	struct stat stat;
	s = fstat(ofd, &stat);
	if (s == -1) cleanup(SRC_FSTAT);
	of_sz = (size_t) stat.st_size;
	of_memsz = (of_sz/PAGE_SIZE + 1)*PAGE_SIZE;
	s = ftruncate(ofd, of_memsz);
	if (s == -1) cleanup(SRC_FTRUNCATE);
	of_mem = mmap(NULL, of_memsz, PROT_READ | PROT_WRITE, MAP_SHARED, ofd, 0);
	if (of_mem == MAP_FAILED) cleanup(SRC_MMAP);
	of_pt = (char*) of_mem + of_sz;
#endif

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
		.sin_addr = htonl(INADDR_ANY)
	};
	s = bind(asock, &serv_addr, sizeof(struct sockaddr_in));
	if (s == -1) cleanup(SRC_BIND);

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
			
			syslog(LOG_INFO, "daemonised with pid %d", getpid()); 
		} else {
			printf("Daemon pid %d\n", cpid);
		   	exit(0);
		}
	}	

	// listen on socket
	s = listen(asock, MAX_BACKLOG);
	if (s == -1) cleanup(SRC_LISTEN);

#ifndef USE_AESD_CHAR_DEVICE
	// install alarm handler and start alarm (must be after fork)
	act.sa_sigaction = NULL; // in case it's a union
	act.sa_handler = wrtime;
	act.sa_flags = 0;
	s = sigaction(SIGALRM, &act, NULL);
	if (s == -1) cleanup(SRC_SIGACTION);
	alarm(WRTIME_PERIOD);
#endif

	// this thread handles all signals
	/* only in glibc >= 2.32 though :(
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setsigmask_np(&attr, &fullmask);
	s = pthread_setattr_default_np(&attr);
	if (s != 0) cleanup(SRC_PTHATTR);
	pthread_attr_destroy(&attr); */
	
	// main server loop (exited via interrupt)
	while (1) {
		// wait for connection
		struct sockaddr_in cli_addr = {0};
		socklen_t addrlen = sizeof(struct sockaddr_in);
		int csock = -1;
		do {
			csock = accept4(asock, &cli_addr, &addrlen, 0);
		} while (csock == -1 && errno == EINTR);
		if (csock == -1) cleanup(SRC_ACCEPT);
		assert(addrlen <= sizeof(struct sockaddr_in));

		// give connection to thread
		struct node* newnode = malloc(sizeof(struct node));		
		newnode->next = thread_ll;
		thread_ll = newnode;
		newnode->c.sock = csock;
		newnode->c.addr = cli_addr.sin_addr;
		pthread_sigmask(SIG_BLOCK, &fullmask, NULL); // this thread handles all signals
		s = pthread_create(&newnode->tid, NULL, client_thread, &newnode->c);
		pthread_sigmask(SIG_UNBLOCK, &fullmask, NULL);
		if (s != 0) cleanup_errno(SRC_PTHCR, s);

		// check for completed threads
		struct node* p = NULL;
		for (struct node* n = thread_ll; n; n = (p) ? p->next : thread_ll) {
			s = pthread_tryjoin_np(n->tid, NULL);
			if (s == 0) {
				if (p) p->next = n->next;
				else thread_ll = n->next;
				free(n);
			} else if (s != EBUSY) {
				cleanup_errno(SRC_PTHJN, s);
			} else p = n;
		}
	}
}
				
