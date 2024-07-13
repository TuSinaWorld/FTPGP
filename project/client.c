#include "server_client.h"

int socketFd = 0;
//_Atomic确保原子性
_Atomic bool isConnect = false;
_Atomic bool allowConnect = true;
volatile bool showMessageClient = false;
pthread_mutex_t mutex; //锁,保证发送的有序性(绝不能同时发送两个包体等等)
// pthread_mutex_t mutex2; //锁,保证连接状态变量的正确(被CAS替代)

pthread_attr_t attr; //方便设置为分离状态,问就是懒得pthread_join,反正不用返回值~
// CAS无锁编程,最大程度优化性能~~~
bool compare_and_swap_bool(_Atomic bool *ptr, bool old_val, bool new_val) {
    return __atomic_compare_exchange_n(ptr, &old_val, new_val, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}


bool initSocket(int *socketFd){
    // 创建套接字
    *socketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (*socketFd < 0) {
        perror("Socket creation failed");
        return false;
    }

    // 配置服务器地址
    struct sockaddr_in remoteAddr;
    memset(&remoteAddr, 0, sizeof(remoteAddr));
    remoteAddr.sin_family = AF_INET;
    remoteAddr.sin_port = htons(ServerPort);
    remoteAddr.sin_addr.s_addr = inet_addr(RemoteIp);

    // 连接服务器
    if (connect(*socketFd, (struct sockaddr *)&remoteAddr, sizeof(remoteAddr)) < 0) {
        perror("Connect failed");
        close(*socketFd);
        return false;
    }
    return true;
}

int sendResourceHead(int socketFd, ResourceHead resourceHead) { //-1:已经断开,0:需要断开,1:正常
    ssize_t bytesSend = 0;
    int eintrTryNum = 0;
    while (bytesSend < sizeof(resourceHead)) {
        ssize_t bytes = send(socketFd, ((char *)&resourceHead) + bytesSend, sizeof(resourceHead) - bytesSend, 0);
        if (bytes <= 0) {
            if (bytes == -1 && errno == EINTR && ++eintrTryNum < 16) {
                continue;
            } else {
                if(bytes == 0 || errno == EBADF){
                    printf("Server actively disconnects(sending resource head)\n");
                    return -1;
                }else{
                    perror("Error sending resource head");
                    return 0;
                }
            }
        }
        bytesSend += bytes;
        eintrTryNum = 0;
    }
    return 1;
}

int sendData(int acceptFd, char * buffData, int length) {//-1:已经断开,0:需要断开,1:正常
    ssize_t bytesSend = 0;
    int eintrTryNum = 0;
    while (bytesSend < length) {
        ssize_t bytes = send(acceptFd, buffData + bytesSend, length - bytesSend, 0);
        if (bytes <= 0) {
            if (bytes == -1 && errno == EINTR && ++eintrTryNum < 16) {
                continue;
            } else {
                if(bytes == 0 || errno == EBADF){
                    printf("Client actively disconnects(sending data)\n");
                    return -1;
                }else{
                    perror("Error sending data");
                    return 0;
                }
            }
        }
        bytesSend += bytes;
        eintrTryNum = 0;
    }
    return 1;
}

int recvResourceHead(int acceptFd, ResourceHead *resourceHead) {
    ssize_t bytesRead = 0;
    int eintrTryNum = 0;
    while (bytesRead < sizeof(*resourceHead)) {
        ssize_t bytes = recv(acceptFd, ((char *)resourceHead) + bytesRead, sizeof(*resourceHead) - bytesRead,0);
        if (bytes <= 0) {
            if (bytes == -1 && errno == EINTR && ++eintrTryNum < 16) {
                continue;
            } else {
                if(bytes == 0 || errno == EBADF){
                    printf("Client actively disconnects(recving resource head)\n");
                    return -1;
                }else{
                    perror("Error recving resource head");
                    return 0;
                }
            }
        }
        bytesRead += bytes;
        eintrTryNum = 0;
    }
    return 1;
}

bool recvData(int acceptFd, char * buffData, ResourceHead resourceHead) {
    ssize_t bytesRead = 0;
    int eintrTryNum = 0;
    while (bytesRead < resourceHead.dataSize) {
        ssize_t bytes = recv(acceptFd, buffData + bytesRead, resourceHead.dataSize - bytesRead,0);
        if (bytes <= 0) {
            if (bytes == -1 && errno == EINTR && ++eintrTryNum < 16) {
                continue;
            } else {
                if(bytes == 0 || errno == EBADF){
                    printf("Client actively disconnects(recving data)\n");
                    return -1;
                }else{
                    perror("Error recving data");
                    return 0;
                }
            }
        }
        bytesRead += bytes;
        eintrTryNum = 0;
    }
    return 1;
}

bool doEstablish(){
    bool flag = initSocket(&socketFd);
    compare_and_swap_bool(&isConnect,false,flag);
    return isConnect;
}

bool doQuit(int socketFd){
    ResourceHead resourceHead;
    memset(&resourceHead, 0, sizeof(resourceHead));
    resourceHead.dataSize = 0;
    resourceHead.instruction = QUIT;
    resourceHead.isEnd = 1;
    resourceHead.isStart = true;
    pthread_mutex_lock(&mutex);//有序性,有序性.......
    int sendResult = sendResourceHead(socketFd,resourceHead);
    pthread_mutex_unlock(&mutex);
    if(sendResult == -1){
        perror("server disconnect");
        compare_and_swap_bool(&isConnect,true,false);
        return false;
    }else if(sendResult == 0){
        close(socketFd);
        compare_and_swap_bool(&isConnect,true,false);
        return false;
    }
    compare_and_swap_bool(&isConnect,true,false);
    return true;
}

void * doQuitT(void * args){
    doQuit(socketFd);
    printf("quit success.\n");
}

bool doList(int socketFd){
    ResourceHead resourceHead;
    memset(&resourceHead, 0, sizeof(resourceHead));
    resourceHead.dataSize = 0;
    resourceHead.instruction = LIST;
    resourceHead.isEnd = 1;
    resourceHead.isStart = true;
    pthread_mutex_lock(&mutex);//有序性,有序性.......
    int sendResult = sendResourceHead(socketFd,resourceHead);
    pthread_mutex_unlock(&mutex);
    if(sendResult == -1){
        perror("server disconnect");
        compare_and_swap_bool(&isConnect,true,false);
        return false;
    }else if(sendResult == 0){
        close(socketFd);
        compare_and_swap_bool(&isConnect,true,false);
        return false;
    }
    return true;
}

bool doDel(int socketFd,char * filename){
    ResourceHead resourceHead;
    memset(&resourceHead, 0, sizeof(resourceHead));
    resourceHead.dataSize = 0;
    resourceHead.instruction = DEL;
    resourceHead.isEnd = 1;
    resourceHead.isStart = true;
    strcpy(resourceHead.filename,filename);
    pthread_mutex_lock(&mutex);//有序性,有序性.......
    int sendResult = sendResourceHead(socketFd,resourceHead);
    pthread_mutex_unlock(&mutex);
    if(sendResult == -1){
        perror("server disconnect");
        compare_and_swap_bool(&isConnect,true,false);
        return false;
    }else if(sendResult == 0){
        close(socketFd);
        compare_and_swap_bool(&isConnect,true,false);
        return false;
    }
    return true;
}

bool doPut(int socketFd,char * path){
    char * filename = strrchr(path, '/');
    if (filename != NULL) {
        // 跳过 '/' 本身
        filename++;
    } else {
        // 如果没有找到 '/', 整个字符串就是文件名
        filename = path;
    }

    if(showMessageClient){
        // 打印路径以调试
        printf("File path: %s\n", path);
    }

    FILE *file = fopen(path, "rb");
    if (!file) {
        perror("File open failed");
        return false;
    }
    char buffStr[BuffSize];//发送包体大小(64KB)
    int length;
    bool isStart = true;
    while ((length = fread(buffStr, sizeof(char), BuffSize, file)) > 0) {//从本地读取,无需处理粘包
        // 填充资源头信息
        ResourceHead resourceHead;
        memset(&resourceHead, 0, sizeof(resourceHead));
        resourceHead.dataSize = length;
        strcpy(resourceHead.filename, filename);
        resourceHead.instruction = PUT;
        resourceHead.isEnd = 0;
        resourceHead.isStart = isStart;
        if(isStart){
            isStart = false;
        }
        // 发送资源头
        pthread_mutex_lock(&mutex);
        int sendResourceResult = sendResourceHead(socketFd,resourceHead);
        if(sendResourceResult == -1){
            pthread_mutex_unlock(&mutex);
            perror("server disconnect");
            compare_and_swap_bool(&isConnect,true,false);
            return false;
        }else if(sendResourceResult == 0){
            pthread_mutex_unlock(&mutex);
            close(socketFd);
            compare_and_swap_bool(&isConnect,true,false);
            return false;
        }
        
        // 发送数据
        int sendResourceHeadResult = sendData(socketFd,buffStr,length);
        pthread_mutex_unlock(&mutex);
        if(sendResourceHeadResult == -1){
            perror("server disconnect");
            compare_and_swap_bool(&isConnect,true,false);
            return false;
        }else if(sendResourceHeadResult == 0){
            close(socketFd);
            compare_and_swap_bool(&isConnect,true,false);
            return false;
        }
        // sleep(3);//多线程测试用
    }
    // 发送结束标志
    ResourceHead resourceHeadOfEnd;
    memset(&resourceHeadOfEnd, 0, sizeof(resourceHeadOfEnd));
    resourceHeadOfEnd.instruction = PUT;
    resourceHeadOfEnd.isEnd = 1;
    resourceHeadOfEnd.isStart = false;
    strcpy(resourceHeadOfEnd.filename, filename);
    resourceHeadOfEnd.dataSize = 0;
    pthread_mutex_lock(&mutex);
    int resourceHeadOfEndResult = sendResourceHead(socketFd,resourceHeadOfEnd);
    pthread_mutex_unlock(&mutex);
    if(resourceHeadOfEndResult == -1){
        perror("server disconnect");
        compare_and_swap_bool(&isConnect,true,false);
        return false;
    }else if(resourceHeadOfEndResult == 0){
        close(socketFd);
        compare_and_swap_bool(&isConnect,true,false);
        return false;
    }
    fclose(file);
    return true;
}

bool doGet(int socketFd,char * filename,char * getPath,int currentSize){
    ResourceHead resourceHead;
    memset(&resourceHead, 0, sizeof(resourceHead));
    resourceHead.instruction = GET;
    resourceHead.isEnd = 1;
    resourceHead.isStart = true;
    strcpy(resourceHead.filename, filename);
    resourceHead.dataSize = 0;
    strcpy(resourceHead.getPath, getPath);
    resourceHead.currentSize = currentSize;
    pthread_mutex_lock(&mutex);//有序性,有序性.......
    int sendResult = sendResourceHead(socketFd,resourceHead);
    pthread_mutex_unlock(&mutex);
    if(sendResult == -1){
        perror("server disconnect");
        compare_and_swap_bool(&isConnect,true,false);
        return false;
    }else if(sendResult == 0){
        close(socketFd);
        compare_and_swap_bool(&isConnect,true,false);
        return false;
    }
    return true;
}



void * doGetT(void * args) {
    GetArgs *getArgs = (GetArgs *)args;
    ResourceHead resourceHead = getArgs->getResourceHead;
    char *buffStr = getArgs->buffStr;
    free(args);
    if(showMessageClient){
        printf("Path: %s\n", resourceHead.getPath);
        printf("Expected data size: %d\n", resourceHead.dataSize);
    }
    FILE *file = fopen(resourceHead.getPath, "ab"); // 修改：以写入模式打开文件
    if (!file) {
        perror("Error opening file");
        free(buffStr);
        return NULL;
    }

    int written = fwrite(buffStr, 1, resourceHead.dataSize, file);
    if (written != resourceHead.dataSize) {
        if (ferror(file)) {
            perror("Error writing to file");
        } else {
            fprintf(stderr, "Unexpected end of file while writing\n");
        }
        fclose(file);
        free(buffStr);
        return NULL;
    }
    fclose(file);
    free(buffStr);
    if(showMessageClient){
        printf("Write done: %d\n", written);
    }
    // sleep(3);//多线程调试使用
    doGet(socketFd, resourceHead.filename, resourceHead.getPath, resourceHead.currentSize);
}

bool doRecord(int socketFd,int recordNum){
    ResourceHead resourceHead;
    memset(&resourceHead, 0, sizeof(resourceHead));
    resourceHead.instruction = RECORD;
    resourceHead.isEnd = 1;
    resourceHead.isStart = true;
    resourceHead.dataSize = 0;
    resourceHead.recordNum = recordNum;
    strcpy(resourceHead.filename,"");
    pthread_mutex_lock(&mutex);
    int sendResult = sendResourceHead(socketFd,resourceHead);
    pthread_mutex_unlock(&mutex);
    if(sendResult == -1){
        perror("server disconnect");
        compare_and_swap_bool(&isConnect,true,false);
        return false;
    }else if(sendResult == 0){
        close(socketFd);
        compare_and_swap_bool(&isConnect,true,false);
        return false;
    }
    return true;
}


void * recvResource(void * args) {
    while (1) {
        ResourceHead resourceHead;
        memset(&resourceHead, 0, sizeof(resourceHead));
        
        int recvResourceHeadResult = recvResourceHead(socketFd, &resourceHead);
        if (recvResourceHeadResult == -1) {
            compare_and_swap_bool(&isConnect, true, false);
            pthread_exit(NULL);
        } else if (recvResourceHeadResult == 0) {
            close(socketFd);
            compare_and_swap_bool(&isConnect, true, false);
            pthread_exit(NULL);
        }
        time_t timeNow;
        time(&timeNow);
        if(showMessageClient){
            if(strcmp(resourceHead.filename,"")){
                printf("Resource head: %s---%d---%s---%d(%ld)\n",requestInstructionNames[resourceHead.instruction],resourceHead.dataSize,resourceHead.filename,resourceHead.isEnd,timeNow);
            }else{
                printf("Resource head: %s---%d---%d(%ld)\n",requestInstructionNames[resourceHead.instruction],resourceHead.dataSize,resourceHead.isEnd,timeNow);
            }
        }
        if (resourceHead.instruction == LIST) {
            char * array = (char *)malloc(resourceHead.dataSize);
            if (!array) {
                perror("Error allocating memory for array");
                pthread_exit(NULL);
            }

            int recvResourceDataResult = recvData(socketFd, array, resourceHead);
            if (recvResourceDataResult == -1) {
                free(array);
                compare_and_swap_bool(&isConnect, true, false);
                pthread_exit(NULL);
            } else if (recvResourceDataResult == 0) {
                free(array);
                close(socketFd);
                compare_and_swap_bool(&isConnect, true, false);
                pthread_exit(NULL);
            }

            printf("filelist is: %s\n", array);
            free(array);
        } else if (resourceHead.instruction == GET) {
            if (resourceHead.isEnd == true) {
                printf("%s file is download success!\n", resourceHead.filename);
            } else {
                char * buffStr = (char *)malloc(resourceHead.dataSize);
                if (!buffStr) {
                    perror("Error allocating memory for buffStr");
                    pthread_exit(NULL);
                }
            
                int recvResourceDataResult = recvData(socketFd, buffStr, resourceHead);
                if (recvResourceDataResult == -1) {
                    free(buffStr);
                    compare_and_swap_bool(&isConnect, true, false);
                    pthread_exit(NULL);
                } else if (recvResourceDataResult == 0) {
                    free(buffStr);
                    close(socketFd);
                    compare_and_swap_bool(&isConnect, true, false);
                    pthread_exit(NULL);
                }

                GetArgs * getArgs = (GetArgs *)malloc(sizeof(GetArgs));
                if (!getArgs) {
                    perror("Error allocating memory for getArgs");
                    free(buffStr);
                    pthread_exit(NULL);
                }

                getArgs->buffStr = buffStr;
                getArgs->getResourceHead = resourceHead;
                addTask(doGetT, getArgs);
            }
        } else if (resourceHead.instruction == QUIT) {
            printf("Timeout...Please reconnect...\n");
            addTask(doQuitT, NULL);
            // close(socketFd);理论上应该直接客户端关闭,但天知道为啥需求是要服务端关闭...
        } else if (resourceHead.instruction == FILE_DELETE_SUCCESS) {
            printf("file delete success!\n");
        } else if (resourceHead.instruction == ERROR_FILE_NOT_EXIT_OR_NO_PERMISSION) {
            printf("file delete error: no this file or no permission...\n");
        } else if (resourceHead.instruction == UNKNOWN_ERROR) {
            printf("server has a unknown error...\n");
        } else if (resourceHead.instruction == RECORD){
            char * array = (char *)malloc(resourceHead.dataSize);
            if (!array) {
                perror("Error allocating memory for array");
                pthread_exit(NULL);
            }
            int recvResourceDataResult = recvData(socketFd, array, resourceHead);
            if (recvResourceDataResult == -1) {
                free(array);
                compare_and_swap_bool(&isConnect, true, false);
                pthread_exit(NULL);
            } else if (recvResourceDataResult == 0) {
                free(array);
                close(socketFd);
                compare_and_swap_bool(&isConnect, true, false);
                pthread_exit(NULL);
            }
            printf("%s\n", array);
            free(array);
        }
    }
    return NULL; // 确保线程函数有返回值
}



bool clearFile(char * getPath){
    FILE *file = fopen(getPath, "wb");
    if (file == NULL) {
        perror("Error clear file");
        return false;
    }
    fclose(file);
    return true;
}

void * threadMethod(void * args){
    char inputChar = ((ClientArgs *)args) -> inputChar;
    char inputArgs[200] = {0};
    char inputArgs2[200] = {0};
    strcpy(inputArgs,((ClientArgs *)args) -> args); 
    strcpy(inputArgs2,((ClientArgs *)args) -> args2); 
    int recordNum = ((ClientArgs *)args) -> recordNum;
    free(((ClientArgs *)args) -> args);
    free(((ClientArgs *)args) -> args2);
    free(args);
    switch (inputChar){
        case 'e':
            if(isConnect){
                printf("you has already connected...\n");
            }else{
                if(doEstablish()){
                    pthread_t pid;
                    pthread_create(&pid,&attr,recvResource,NULL);
                    printf("connect success.\n");
                }
            }
            break;
        case 'g':
            if(isConnect){
                if(clearFile(inputArgs2) && doGet(socketFd,inputArgs,inputArgs2,0)){
                    printf("get request send.\n");
                }
            }else{
                printf("Please establish a connection first!\n");
            }
            break;
        case 'p':
            if(isConnect){
                if(showMessageClient){
                    printf("path: %s\n",inputArgs);
                }
                if(doPut(socketFd,inputArgs)){
                    printf("get request send.\n");
                    printf("%s put success.\n",inputArgs);
                }
            }else{
                printf("Please establish a connection first!\n");
            }
            break;
        case 'l':
            if(isConnect){
                if(doList(socketFd)){
                    printf("list success.\n");
                }
            }else{
                printf("Please establish a connection first!\n");
            }
            break;
        case 'd':
            if(isConnect){
                if(doDel(socketFd,inputArgs)){
                    printf("%s del success.\n",inputArgs);
                }
            }else{
                printf("Please establish a connection first!\n");
            }
            break;
        case 'q':
            if(isConnect){
                if(doQuit(socketFd)){
                    printf("quit success.\n");
                }
            }else{
                printf("you has not connect.\n");
            }
            break;
        case 'm':
            if(showMessageClient){
                showMessageClient = false;
                changeShowMessage();
                printf("You close the test message show!\n");
            }else{
                showMessageClient = true;
                changeShowMessage();
                printf("You open the test message show!\n");
            }
            break;
        case 'r':
            if(isConnect){
                if(doRecord(socketFd,recordNum)){
                    printf("send record request success.\n");
                }
            }else{
                printf("you has not connect.\n");
            }
            break;
        default:
            break;
    }
}

void start(){
    while(1){//主界面
        char inputChar = fgetc(stdin);
        char args[200] = {0};
        char args2[200] = {0};
        while(inputChar == '\n'){
            inputChar = fgetc(stdin);
        }
        fgetc(stdin);
        ClientArgs * clientArgs = (ClientArgs *)malloc(sizeof(clientArgs));
        clientArgs -> args = malloc(sizeof(char) * 200);
        clientArgs -> args2 = malloc(sizeof(char) * 200);
        if(inputChar == 'p'){
            printf("Please input filePath:");
            fgets(args,200,stdin);
            if(strlen(args) > 0 && args[strlen(args) - 1] == '\n'){
                args[strlen(args) - 1] = '\0';
            }
            fflush(stdin);
            if(showMessageClient){
                printf("path: %s\n",args);
            }
        }else if(inputChar == 'd'){
            printf("Please input filename:");
            fgets(args,200,stdin);
            if(strlen(args) > 0 && args[strlen(args) - 1] == '\n'){
                args[strlen(args) - 1] = '\0';
            }
        }else if(inputChar == 'g'){
            printf("Please input filename:");
            fgets(args,200,stdin);
            if(strlen(args) > 0 && args[strlen(args) - 1] == '\n'){
                args[strlen(args) - 1] = '\0';
            }
            printf("Please input filePath:");
            fgets(args2,200,stdin);
            if(strlen(args2) > 0 && args2[strlen(args2) - 1] == '\n'){
                args2[strlen(args2) - 1] = '\0';
            }
            // printf("%s\n",args2);
        }else if(inputChar == 'r'){
            printf("Please input recordNum(1-100):");
            scanf("%d",&(clientArgs -> recordNum));
        }
        clientArgs -> inputChar = inputChar;
        strcpy(clientArgs -> args,args);
        strcpy(clientArgs -> args2,args2);
        addTask(threadMethod,clientArgs);
    }
}

int main() {
    initThreadPool(5);
    changeShowMessage();
    pthread_mutex_init(&mutex,NULL);
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    printf("please input key to do next:\n");
    printf("e:establish a connection...\n");
    printf("l:list file from server...\n");
    printf("g:get file from server...\n");
    printf("p:put file to server...\n");
    printf("d:del server file...\n");
    printf("q:disconnect from server...\n");
    printf("m:show/hide message for test...\n");
    printf("r:show server records...\n");
    start();
    close(socketFd);
    isConnect = false;
    return 0;
}
