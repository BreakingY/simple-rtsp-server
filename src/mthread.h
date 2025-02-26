#ifndef _MTHREAD_H_
#define _MTHREAD_H_
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#if defined(__linux__) || defined(__linux)
#include <unistd.h>
#include <pthread.h>
typedef pthread_t mthread_t;
typedef pthread_mutex_t mthread_mutex_t;
#elif defined(_WIN32) || defined(_WIN64)
#include <windows.h>
typedef HANDLE mthread_t;
typedef CRITICAL_SECTION mthread_mutex_t;
#endif

typedef void* (*mthread_func)(void *);

int mthread_mutex_init(mthread_mutex_t *mthread_mutex, void *attr);
int mthread_mutex_lock(mthread_mutex_t *mthread_mutex);
int mthread_mutex_unlock(mthread_mutex_t *mthread_mutex);
int mthread_mutex_destroy(mthread_mutex_t *mthread_mutex);
int mthread_create(mthread_t *mthread, void *attr, mthread_func thd_func, void *arg);
int mthread_detach(mthread_t mthread);
int mthread_join(mthread_t mthread, void **retval);
void mthread_exit(void *retval);
void m_sleep(int millisecond);

#endif
