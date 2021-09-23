#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <string.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* tdat_v)
{
	struct thread_data* tdat = (struct thread_data*) tdat_v;
	tdat->thread_complete_success = true;
	struct timespec ts;
	ts.tv_sec = tdat->pre_wait/1000; 
	ts.tv_nsec = (tdat->pre_wait % 1000) * 1000000; // ms to ns
	int s;
	do {
		s = nanosleep(&ts, &ts);
	} while (s == -1 && errno == EINTR);
	if (s == -1) goto fail;
		
	s = pthread_mutex_lock(tdat->lock);
	if (s != 0) goto fail;
	ts.tv_sec = tdat->post_wait/1000;
	ts.tv_nsec = (tdat->post_wait % 1000) * 1000000;
	do {
		s = nanosleep(&ts, &ts);
	} while (s == -1 && errno == EINTR);
	pthread_mutex_unlock(tdat->lock);
	if (s == -1) goto fail;
	
    return tdat_v;

fail:
	tdat->thread_complete_success = false;
	return tdat_v;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
	if (wait_to_obtain_ms < 0 || wait_to_release_ms < 0) return false;

	struct thread_data* tdat = malloc(sizeof(struct thread_data));
	tdat->lock = mutex;
	tdat->pre_wait = wait_to_obtain_ms;
	tdat->post_wait = wait_to_release_ms;
	int s = pthread_create(thread, NULL, threadfunc, tdat);
	if (s) {
		ERROR_LOG("Failed to create thread: %s", strerror(s));
    	return false;
	} else return true;
}

