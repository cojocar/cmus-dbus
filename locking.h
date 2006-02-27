#ifndef _LOCKING_H
#define _LOCKING_H

#include <pthread.h>

#define CMUS_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER

void cmus_mutex_init(pthread_mutex_t *mutex);
void cmus_mutex_lock(pthread_mutex_t *mutex);
void cmus_mutex_unlock(pthread_mutex_t *mutex);

#endif
