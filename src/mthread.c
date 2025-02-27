#include "mthread.h"
#if defined(__linux__) || defined(__linux)
int mthread_mutex_init(mthread_mutex_t *mthread_mutex, void *attr){
    int ret = pthread_mutex_init(mthread_mutex, (const pthread_mutexattr_t *)attr);
    return ret;
}
int mthread_mutex_lock(mthread_mutex_t *mthread_mutex){
    int ret = pthread_mutex_lock(mthread_mutex);
    return ret;
}
int mthread_mutex_unlock(mthread_mutex_t *mthread_mutex){
    int ret = pthread_mutex_unlock(mthread_mutex);
    return ret;
}
int mthread_mutex_destroy(mthread_mutex_t *mthread_mutex){
    int ret = pthread_mutex_destroy(mthread_mutex);
    return ret;
}
int mthread_create(mthread_t *mthread, void *attr, mthread_func thd_func, void *arg){
    int ret = pthread_create(mthread, (const pthread_attr_t *)attr, thd_func, arg);
    return ret;
}
int mthread_detach(mthread_t mthread){
    int ret = pthread_detach(mthread);
    return ret;
}
int mthread_join(mthread_t mthread, void **retval){
    int ret = pthread_join(mthread, retval);
    return ret;
}
void mthread_exit(void *retval){
    pthread_exit(retval);
    return;
}
void m_sleep(int millisecond){
    usleep(1000 * millisecond);
}
#elif defined(_WIN32) || defined(_WIN64)

int mthread_mutex_init(mthread_mutex_t *mthread_mutex, void *attr){
    InitializeCriticalSection(mthread_mutex);
    return 0;
}

int mthread_mutex_lock(mthread_mutex_t *mthread_mutex){
    EnterCriticalSection(mthread_mutex);
    return 0;
}

int mthread_mutex_unlock(mthread_mutex_t *mthread_mutex){
    LeaveCriticalSection(mthread_mutex);
    return 0;
}

int mthread_mutex_destroy(mthread_mutex_t *mthread_mutex){
    DeleteCriticalSection(mthread_mutex);
    return 0;
}

int mthread_create(mthread_t *mthread, void *attr, mthread_func thd_func, void *arg){
    *mthread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)thd_func, arg, 0, NULL);
    if (*mthread == NULL) {
        return -1;
    }
    return 0;
}

int mthread_detach(mthread_t mthread){
    if(mthread){
        CloseHandle(mthread);
    }
    return 0;
}

int mthread_join(mthread_t mthread, void **retval){
    DWORD dwResult = WaitForSingleObject(mthread, INFINITE);
    if (dwResult == WAIT_OBJECT_0) {
        if (retval != NULL) {
            // In Windows, threads don't return a pointer like POSIX threads.
            // You would need to set up a way to handle return values if needed.
            *retval = NULL;
        }
        CloseHandle(mthread);
        return 0;
    }
    return -1;
}

void mthread_exit(void *retval) {
    ExitThread((DWORD)(uintptr_t)retval);
}
void m_sleep(int millisecond){
    Sleep(millisecond);
}
#endif



