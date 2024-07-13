#ifndef __PTHREAD__POOL__H__
#define __PTHREAD__POOL__H__ 1
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>

typedef struct task{
    void * (*do_task) (void * arg);
    void * arg;
    struct task * next;
}Task;

typedef struct pthread_pool{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    Task * taskList;
    pthread_t * pids;
    int maxTasks;
    int active_pthreads;
    int cur_waiting_task;
    int shutdown;
}Pthread_pool;

int initThreadPool(int maxTasks);
void * addTask(void * (*do_task) (void *),void * arg);
void * routine();
void destroyThreadPool();
int taskNULL();
void changeShowMessage();
#endif