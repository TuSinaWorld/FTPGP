#include "pthread_pool.h"

Pthread_pool * pthreadPool;

volatile bool showMessage = true;

int initThreadPool(int maxTasks){
    pthreadPool = (Pthread_pool*)malloc(sizeof(Pthread_pool));
    pthreadPool -> maxTasks = maxTasks;
    pthreadPool -> cur_waiting_task = 0;
    pthreadPool -> active_pthreads = 0;
    pthreadPool -> shutdown = 1;
    pthreadPool -> pids = (pthread_t*)malloc(sizeof(pthread_t) * maxTasks);
    pthreadPool -> taskList = NULL;
    pthread_mutex_init(&(pthreadPool -> mutex),NULL);
    pthread_cond_init(&pthreadPool -> cond,NULL);
    for(int i = 0;i < maxTasks;i++){
        pthread_create(&((pthreadPool -> pids)[i]),NULL,routine,NULL);
    }
    showMessage = true;
    return 1;
}

void * addTask(void * (*do_task) (void *),void * arg){
    // printf("ttt\n");
    Task * newTask = (Task*)malloc(sizeof(Task));
    newTask -> do_task = do_task;
    newTask -> arg = arg;
    newTask -> next = NULL;
    pthread_mutex_lock(&(pthreadPool -> mutex));
    if(pthreadPool -> taskList == NULL){
        pthreadPool -> taskList = newTask;
    }else{
        Task * task = pthreadPool -> taskList;
        while(task -> next != NULL){
            task = task -> next;
        }
        task -> next = newTask;
    }
    pthread_mutex_unlock(&(pthreadPool -> mutex));
    pthread_cond_broadcast(&(pthreadPool -> cond));
    return NULL;
}

void * routine(){
    while(1){
        pthread_mutex_lock(&(pthreadPool -> mutex));
        while(pthreadPool -> taskList == NULL){
            if(pthreadPool -> shutdown == 0){
                pthread_mutex_unlock(&(pthreadPool -> mutex));
                pthread_exit(NULL);
            }
            pthread_cond_wait(&(pthreadPool -> cond),&(pthreadPool -> mutex));
        }
        Task * task = pthreadPool -> taskList;
        pthreadPool -> taskList = pthreadPool -> taskList -> next;
        pthreadPool -> active_pthreads += 1;
        pthread_mutex_unlock(&(pthreadPool -> mutex));
        if(showMessage){
            printf("start task(%ld);\n",pthread_self());
        }
        (task -> do_task)(task -> arg);
        free(task);
        if(showMessage){
            printf("end task(%ld);\n",pthread_self());
        }
        pthread_mutex_lock(&(pthreadPool -> mutex));
        pthreadPool -> active_pthreads -= 1;
        pthread_mutex_unlock(&(pthreadPool -> mutex));
        if(pthreadPool -> shutdown == 0){
            pthread_exit(NULL);
        }
    }
}

void destroyThreadPool(){
    pthreadPool -> shutdown = 0;
    pthread_cond_broadcast(&(pthreadPool -> cond));
    for(int i = 0;i < pthreadPool -> maxTasks;i++){
        pthread_join(pthreadPool -> pids[i],NULL);
    }
    free(pthreadPool);
    pthreadPool = NULL;
}

int taskNULL(){
    return ((pthreadPool -> taskList) == NULL);
}

void changeShowMessage(){
    if(showMessage){
        showMessage = false;
    }else{
        showMessage = true;
    }
}