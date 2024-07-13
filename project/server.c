#include "server_client.h"
#include <sqlite3.h>
struct InsertParams {
    const char* type;
    const char* ip;
    int datasize;
    const char* filename;
    int direction;
    sqlite3 *db;
};

pthread_attr_t attr; //方便设置为分离状态,问就是懒得pthread_join,反正不用返回值~

sqlite3 * dbCon;

// CAS无锁编程,最大程度优化性能~~~
bool compare_and_swap_bool(_Atomic bool *ptr, bool old_val, bool new_val) {
    return __atomic_compare_exchange_n(ptr, &old_val, new_val, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

// 初始化套接字
bool initSocket(int *socketFd) {
    // 创建套接字
    *socketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (*socketFd < 0) {
        perror("Socket creation failed");
        return false;
    }

    struct linger so_linger;
    so_linger.l_onoff = 1;  // 打开SO_LINGER选项
    so_linger.l_linger = 5; // 设置等待时间为0
    int result;
    result = setsockopt(*socketFd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));
    if (result == -1) {
        perror("setsockopt");
        close(*socketFd);
        exit(EXIT_FAILURE);
    }

    // 配置地址结构体
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ServerPort);
    addr.sin_addr.s_addr = inet_addr(LocalIp);

    // 绑定套接字
    if (bind(*socketFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        close(*socketFd); // 关闭套接字
        return false;
    }

    // 监听连接
    if (listen(*socketFd, 8) < 0) {
        perror("Listen failed");
        close(*socketFd); // 关闭套接字
        return false;
    }

    return true;
}

sqlite3* create_connection(const char *db_name) {
    sqlite3 *db;
    int rc = sqlite3_open(db_name, &db);

    if (rc) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        exit(-1);
    } else {
        fprintf(stderr, "Opened database successfully\n");
    }
    return db;
}

int callback(void *data, int argc, char **argv, char **azColName) {
    QueryResult * qr = (QueryResult*)data;
    for (int i = 0; i < argc; i++) {
        long additional_length = argv[i] ? (strlen(argv[i]) + 1) : 5; // 1是‘|’的长度, 5是"NULL"的长度
        *(qr -> length) += additional_length;
        if (*(qr -> length) >= *(qr -> size)) {
            *(qr -> size) = *(qr -> size) * 2;
            *(qr -> result) = realloc(*(qr -> result), *(qr -> size));
            if (*(qr -> result) == NULL) {
                fprintf(stderr, "Memory reallocation failed\n");
                return 1; // 终止回调函数并返回错误
            }
        }
        strcat(*(qr -> result), argv[i] ? argv[i] : "NULL");
        if (i < argc - 1) {
            strcat(*(qr -> result), "|");
        }
    }
    strcat(*(qr -> result), "\n");
    return 0;
}

bool doCreateTable(sqlite3 *db, const char *sql) {
    char *err_msg = NULL;
    int rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        perror("SQL error");
        fprintf(stderr, "Error message: %s\n", err_msg);
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

bool createTable(){
    char createSql[300] = {0};
    time_t currentTime = time(NULL);
    // 将时间转换为当地时间
    struct tm *localTime = localtime(&currentTime);
    // 获取年月日
    int year = localTime->tm_year + 1900;  // tm_year 是自 1900 年以来的年份，需要加上 1900
    int month = localTime->tm_mon + 1;     // tm_mon 是从 0 到 11，需要加上 1
    int day = localTime->tm_mday;
    sprintf(createSql,CreateTableSQL,year,month,day);
    if(!doCreateTable(dbCon,createSql)){
        return false;
    }
    return true;
}

char* query_concat(sqlite3 *db, const char *sql) {
    createTable();
    char *err_msg = 0;
    QueryResult qr;
    long *size = malloc(sizeof(long));
    long *length = malloc(sizeof(long));
    *length = 1;
    *size = 256;
    char *result = malloc(*size);
    qr.length = length;
    qr.result = &result;
    qr.size = size;
    if (result == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        return NULL;
    }
    result[0] = '\0';  // 确保结果字符串为空
    int rc = sqlite3_exec(db, sql, callback, &qr, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        free(result);
        return NULL;
    }
    free(size);
    free(length);
    return result;
}

void* doInsertStatement(void* arg) {
    char filename[100];
    struct InsertParams* params = (struct InsertParams*)arg;
    strcpy(filename,params -> filename);
    createTable();
    char insertStatement[1000] = {0}; // 定义一个插入语句字符串
    time_t currentTime = time(NULL);
    // 将时间转换为当地时间
    struct tm *localTime = localtime(&currentTime);
    // 获取年月日
    int year = localTime->tm_year + 1900;  // tm_year 是自 1900 年以来的年份，需要加上 1900
    int month = localTime->tm_mon + 1;     // tm_mon 是从 0 到 11，需要加上 1
    int day = localTime->tm_mday;
    sprintf(insertStatement, InsertSQL, year, month, day, params -> type, params -> ip, params -> datasize, filename, params -> direction);
    createTable();
    // printf("%s\n",insertStatement);
    char *err_msg = 0;
    int rc = sqlite3_exec(params -> db, insertStatement, 0, 0, &err_msg); // 执行插入语句
    if (rc != SQLITE_OK) { // 检查执行结果
        fprintf(stderr, "SQL error: %s\n", err_msg);
    }
    sqlite3_free(err_msg);
    free(params);
}

void addInsertThreadPool(const char* type, const char* ip, int datasize, const char* filename, int direction, sqlite3 *db) {
    struct InsertParams* params = malloc(sizeof(struct InsertParams));
    if (params == NULL) {
        perror("Memory allocation failed");
        return;
    }
    params->type = type;
    params->ip = ip;
    params->datasize = datasize;
    params->filename = filename;
    params->direction = direction;
    params->db = db;
    addTask(doInsertStatement,params);
}

void handleError(const char *msg, int acceptFd, DIR **dir, char **buffData, FILE ** fileFd,struct sockaddr_in * remoteAddr,bool * quitflag) {
    if(remoteAddr != NULL){
        char msg2[256];
        sprintf(msg2, "%s-(%s:%d)", msg, inet_ntoa((*remoteAddr).sin_addr), ntohs((*remoteAddr).sin_port));
        perror(msg2);
    }else{
        perror(msg);
    }
    if(quitflag != NULL){
        *quitflag = true;
    }
    if (dir && *dir) closedir(*dir);
    if (buffData && *buffData) free(*buffData);
    if (fileFd && *fileFd) fclose(*fileFd);
    if (acceptFd >= 0) {
        close(acceptFd);
        addInsertThreadPool("QUIT",inet_ntoa(remoteAddr -> sin_addr),0,"",1,dbCon);
    }
}

void handleDisconnect(const char *msg, int acceptFd, DIR **dir, char **buffData, FILE ** fileFd,struct sockaddr_in * remoteAddr,bool * quitflag) {
    if(remoteAddr != NULL){
        char msg2[256];
        sprintf(msg2, "%s-(%s:%d)", msg, inet_ntoa((*remoteAddr).sin_addr), ntohs((*remoteAddr).sin_port));
        printf("%s\n",msg2);
    }else{
        printf("%s\n",msg);
    }
    if(quitflag != NULL){
        *quitflag = true;
    }
    if (dir && *dir) closedir(*dir);
    if (buffData && *buffData) free(*buffData);
    if (fileFd && *fileFd) fclose(*fileFd);
    if (acceptFd >= 0) {
        close(acceptFd);
        addInsertThreadPool("QUIT",inet_ntoa(remoteAddr -> sin_addr),0,"",1,dbCon);
    }
}

bool sendData(int acceptFd, char **buffData, ResourceHead resourceHead, DIR **dir,struct sockaddr_in * remoteAddr,bool * quitFlag) {
    ssize_t bytesSend = 0;
    int eintrTryNum = 0;
    while (bytesSend < resourceHead.dataSize) {
        ssize_t bytes = send(acceptFd, (*buffData) + bytesSend, resourceHead.dataSize - bytesSend, 0);
        if (bytes <= 0) {
            if (bytes == -1 && errno == EINTR && ++eintrTryNum < 16) {
                continue;
            } else {
                if(bytes == 0 || errno == EBADF){
                    handleDisconnect("Client actively disconnects(sending data)", -1, dir, buffData, NULL, remoteAddr, quitFlag);
                }else{
                    handleError("Error sending data", acceptFd, dir, buffData, NULL, remoteAddr, quitFlag);
                }
                return false;
            }
        }
        bytesSend += bytes;
        eintrTryNum = 0;
    }
    return true;
}

bool recvData(int acceptFd, char **buffData, ResourceHead resourceHead, FILE **fileFd,struct sockaddr_in * remoteAddr,bool * quitFlag) {
    ssize_t bytesRead = 0;
    int eintrTryNum = 0;
    while (bytesRead < resourceHead.dataSize) {
        ssize_t bytes = recv(acceptFd, (*buffData) + bytesRead, resourceHead.dataSize - bytesRead,0);
        if (bytes <= 0) {
            if (bytes == -1 && errno == EINTR && ++eintrTryNum < 16) {
                continue;
            } else {
                if(bytes == 0 || errno == EBADF){
                    handleDisconnect("Client actively disconnects(recving data)", -1, NULL, buffData, NULL, remoteAddr, quitFlag);
                }else{
                    handleError("Error recving data", acceptFd, NULL, buffData, NULL, remoteAddr, quitFlag);
                }
                return false;
            }
        }
        bytesRead += bytes;
        eintrTryNum = 0;
    }
    return true;
}

bool sendResourceHead(int acceptFd, ResourceHead resourceHead, DIR **dir, char **fileList,struct sockaddr_in * remoteAddr,bool * quitFlag) {
    ssize_t bytesSend = 0;
    int eintrTryNum = 0;
    while (bytesSend < sizeof(resourceHead)) {
        ssize_t bytes = send(acceptFd, ((char *)&resourceHead) + bytesSend, sizeof(resourceHead) - bytesSend, 0);
        if (bytes <= 0) {
            if (bytes == -1 && errno == EINTR && ++eintrTryNum < 16) {
                continue;
            } else {
                if(bytes == 0 || errno == EBADF){
                    handleDisconnect("Client actively disconnects(sending resource head)", -1, dir, fileList, NULL, remoteAddr, quitFlag);
                }else{
                    handleError("Error sending resource head", acceptFd, dir, fileList, NULL, remoteAddr, quitFlag);
                }
                return false;
            }
        }
        bytesSend += bytes;
        eintrTryNum = 0;
    }
    if(remoteAddr != NULL){
        addInsertThreadPool(requestInstructionNames[resourceHead.instruction],inet_ntoa(remoteAddr -> sin_addr),resourceHead.dataSize,resourceHead.filename,1,dbCon);
    }
    return true;
}

bool recvResourceHead(int acceptFd, ResourceHead *resourceHead,struct sockaddr_in * remoteAddr,bool * quitFlag) {
    ssize_t bytesRead = 0;
    int eintrTryNum = 0;
    while (bytesRead < sizeof(*resourceHead)) {
        ssize_t bytes = recv(acceptFd, ((char *)resourceHead) + bytesRead, sizeof(*resourceHead) - bytesRead,0);
        if (bytes <= 0) {
            if (bytes == -1 && errno == EINTR && ++eintrTryNum < 16) {
                continue;
            } else {
                if(bytes == 0 || errno == EBADF){
                    handleDisconnect("Client actively disconnects(recving resource head)", -1, NULL, NULL, NULL, remoteAddr, quitFlag);
                }else{
                    handleError("Error recving resource head", acceptFd, NULL, NULL, NULL, remoteAddr, quitFlag);
                }
                return false;
            }
        }
        bytesRead += bytes;
        eintrTryNum = 0;
    }
    addInsertThreadPool(requestInstructionNames[resourceHead -> instruction],inet_ntoa(remoteAddr -> sin_addr),resourceHead -> dataSize,resourceHead -> filename,0,dbCon);
    return true;
}

bool doList(int acceptFd,struct sockaddr_in remoteAddr,bool * quitFlag){
    ResourceHead resourceResponseHead;//传回list头,避免粘包
    memset(&resourceResponseHead,0,sizeof(resourceResponseHead));
    resourceResponseHead.instruction = LIST;
    resourceResponseHead.isEnd = 1;//没啥意义,统一规范方便上层可用性
    DIR *dir = opendir(FilePath);
    //读取路径
    if (!dir) {
        handleError("opendir failed", acceptFd, NULL, NULL, NULL, &remoteAddr, quitFlag);
        return true;//处理错误,跳出循环,这里bool值很容易搞混
    }
    char * fileList = NULL;
    int totalLength = 1; //1是'\0'的大小;
    fileList = (char *)malloc(totalLength);
    if (!fileList) {
        handleError("malloc failed", acceptFd, &dir, NULL, NULL, &remoteAddr, quitFlag);
        return true;//处理错误,跳出循环
    }
    //读取文件夹所有文件,并存入数组
    fileList[0] = '\0';// 初始化fileList为空字符串
    struct dirent *entry;
    while((entry = readdir(dir)) != NULL){
        if (entry -> d_type == DT_REG){
            totalLength += strlen(entry->d_name); //文件名大小
            totalLength += 1; //分割符的大小
            char *temp = realloc(fileList, totalLength);
            if (temp == NULL) {
                perror("realloc");
                close(acceptFd);
                closedir(dir);
                free(fileList);
                *quitFlag = true;
                addInsertThreadPool("QUIT",inet_ntoa(remoteAddr.sin_addr),0,"",1,dbCon);
                return true; // 处理错误, 跳出循环
            }
            fileList = temp;
            strcat(fileList,entry -> d_name);
            strcat(fileList,",");
        }
    }
    fileList[totalLength - 1] = '\0';
    if (totalLength > 1)
        fileList[totalLength - 2] = '\0';
    resourceResponseHead.dataSize = totalLength;
    //发送包头给客户端
    if(!sendResourceHead(acceptFd,resourceResponseHead,&dir,&fileList,&remoteAddr,quitFlag)){
        return true;//错误,跳出循环
    }
    //发送包体给客户端
    if(!sendData(acceptFd,&fileList,resourceResponseHead,&dir,&remoteAddr,quitFlag)){
        return true;//错误,跳出循环
    }
    // close(acceptFd);不能随意关闭acceptFd,以便后续步骤使用
    closedir(dir);
    free(fileList);
    return true;//处理完成,退出循环
}

bool doGet(ResourceHead resourceHead,int acceptFd,struct sockaddr_in remoteAddr,bool * quitFlag){
    char path[strlen(FilePath) + 102]; //那个1是/的长度,100是数组最大可承载长度
    int written = sprintf(path, "%s/%s", FilePath, resourceHead.filename);
    ResourceHead responseResourceHead;
    memset(&responseResourceHead,0,sizeof(responseResourceHead));
    responseResourceHead.dataSize = 0;
    responseResourceHead.isEnd = false;
    responseResourceHead.isStart = 0;
    strcpy(responseResourceHead.filename,resourceHead.filename);
    strcpy(responseResourceHead.getPath,resourceHead.getPath);
    responseResourceHead.instruction = GET;

    if (written < 0) {
        responseResourceHead.instruction = UNKNOWN_ERROR;
        sendResourceHead(acceptFd,responseResourceHead,NULL,NULL,&remoteAddr,quitFlag);//无论是否正确,除了该函数自带的处理,我也做不了特殊处理了...
        return true;//跳出循环,但无需结束套接字,也无需结束该客户端的连接.(不过发送失败就是另一回事了,但sendResourceHead会自行处理)
    }
    FILE * file = fopen(path,"rb");
    if (!file) {
        responseResourceHead.instruction = ERROR_FILE_NOT_EXIT_OR_NO_PERMISSION;
        sendResourceHead(acceptFd,responseResourceHead,NULL,NULL,&remoteAddr,quitFlag);//无论是否正确,除了该函数自带的处理,我也做不了特殊处理了...
        return true;//跳出循环,但无需结束套接字,也无需结束该客户端的连接.(不过发送失败就是另一回事了,但sendResourceHead会自行处理)
    }
    char * buffStr = (char *)malloc(BuffSize);
    if (fseek(file, resourceHead.currentSize, SEEK_SET) != 0) {
        perror("Error seeking in file");
        fclose(file);
        free(buffStr);
        responseResourceHead.instruction = UNKNOWN_ERROR;
        sendResourceHead(acceptFd,responseResourceHead,NULL,NULL,&remoteAddr,quitFlag);//无论是否正确,除了该函数自带的处理,我也做不了特殊处理了...
        return true;//跳出循环,但无需结束套接字,也无需结束该客户端的连接.(不过发送失败就是另一回事了,但sendResourceHead会自行处理)
    }
    size_t bytesRead = fread(buffStr, 1, BuffSize, file);
    fclose(file);
    if(bytesRead <= 0){
        free(buffStr);
        responseResourceHead.isEnd = true;
        sendResourceHead(acceptFd,responseResourceHead,NULL,NULL,&remoteAddr,quitFlag);//无论是否正确,除了该函数自带的处理,我也做不了特殊处理了...
        return true;//跳出循环,但无需结束套接字,也无需结束该客户端的连接.(不过发送失败就是另一回事了,但sendResourceHead会自行处理)
    }
    responseResourceHead.dataSize = bytesRead;
    responseResourceHead.currentSize = resourceHead.currentSize + bytesRead;
    if(!sendResourceHead(acceptFd,responseResourceHead,NULL,NULL,&remoteAddr,quitFlag)){
        free(buffStr);
        return true;
    }
    if(!sendData(acceptFd,&buffStr,responseResourceHead,NULL,&remoteAddr,quitFlag)){
        return true;
    }
    free(buffStr);
    return false;
}

bool doPut(ResourceHead resourceHead,int acceptFd,struct sockaddr_in remoteAddr,bool * quitFlag){//这里的bool是是否需要跳出循环
    // 检查是否结束
    if (resourceHead.isEnd) {
         //buffData已经free且未创建
        // close(acceptFd); //用途可能不止put,不能直接关闭连接
        return true;//结束,可以跳出循环
    }

    // 生成文件路径
    char path[strlen(FilePath) + 102]; //那个1是/的长度,100是数组最大可承载长度
    sprintf(path, "%s/%s", FilePath, resourceHead.filename);
    if (resourceHead.isStart) {
        FILE *initFileFd = fopen(path, "wb");
        if (initFileFd == NULL) {
            handleError("Error opening file", acceptFd, NULL, NULL, NULL, &remoteAddr, quitFlag);
            return true; //错误,跳出循环
        }
        fclose(initFileFd);
        // strcpy(lastFilename,resourceHead.filename);多线程情况下文件不一致是正常情况
    }
    FILE * fileOp = fopen(path, "ab");
    FILE ** fileFd = &fileOp; //历史遗留问题,单线程转轮转的代价,这样改最方便.
    // 打开文件
    if (!(*fileFd)) {
        handleError("Error opening file", acceptFd, NULL, NULL, NULL, &remoteAddr, quitFlag);
        return true; //错误,跳出循环
    }

    //多线程情况下文件不一致是正常情况
    // if(strcmp(lastFilename,resourceHead.filename) != 0){ //前后传输文件名不一致.
    //     handleError("The file name defined in the header and footer is inconsistent.",acceptFd,NULL,NULL,fileFd,&remoteAddr,quitFlag);
    //     return true;  //错误,跳出循环
    // }

    // 分配缓冲区并接收数据
    char *buffData = malloc(resourceHead.dataSize);  //不定长,没法优化,不过反正基本不涉及内核态切换就是了~
    //使用malloc是因为c语言规范:不能使用变量创建静态数组~又不想浪费内存~
    if (!buffData) {
        handleError("Memory allocation failed", acceptFd, NULL, NULL , fileFd, &remoteAddr, quitFlag);
        return true;  //错误,跳出循环
    }

    if(!recvData(acceptFd,&buffData,resourceHead,fileFd,&remoteAddr,quitFlag)){
        return true;  //错误,跳出循环
    }

    // 写入文件并清理资源
    size_t bytesWritten = fwrite(buffData, 1, resourceHead.dataSize, *fileFd);
    if (bytesWritten != resourceHead.dataSize) {
        handleError("Error writing to file", acceptFd, NULL, &buffData, fileFd, &remoteAddr, quitFlag);
        return true;
    }
    fclose(*fileFd);   //本想减少重复打开关闭fileFd以节省切换内核态的不必要花费,奈何要做轮转结构不能这么玩
    free(buffData);
    return false;//isEnd为false,需要继续循环;(未结束循环,不能关闭acceptFd)
}

bool doDel(ResourceHead resourceHead,int acceptFd,struct sockaddr_in remoteAddr,bool * quitFlag){
    char path[strlen(FilePath) + 102]; //那个1是/的长度,100是数组最大可承载长度
    int written = sprintf(path, "%s/%s", FilePath, resourceHead.filename);
    ResourceHead responseResourceHead;
    memset(&responseResourceHead,0,sizeof(responseResourceHead));
    responseResourceHead.dataSize = 0;
    responseResourceHead.isEnd = 1;
    strcpy(responseResourceHead.filename,resourceHead.filename);
    if (written < 0) {
        responseResourceHead.instruction = UNKNOWN_ERROR;
        sendResourceHead(acceptFd,responseResourceHead,NULL,NULL,&remoteAddr,quitFlag);//无论是否正确,除了该函数自带的处理,我也做不了特殊处理了...
        return true;//跳出循环,但无需结束套接字,也无需结束该客户端的连接.(不过发送失败就是另一回事了,但sendResourceHead会自行处理)
    }
    // 删除文件并检查结果
    if (remove(path) == 0) {
        responseResourceHead.instruction = FILE_DELETE_SUCCESS;
        sendResourceHead(acceptFd,responseResourceHead,NULL,NULL,&remoteAddr,quitFlag);//无论是否正确,除了该函数自带的处理,我也做不了特殊处理了...
        return true;
    }
    responseResourceHead.instruction = ERROR_FILE_NOT_EXIT_OR_NO_PERMISSION;
    sendResourceHead(acceptFd,responseResourceHead,NULL,NULL,&remoteAddr,quitFlag);//无论是否正确,除了该函数自带的处理,我也做不了特殊处理了...
    return true;
}

bool doRecord(ResourceHead resourceHead,int acceptFd,struct sockaddr_in remoteAddr,bool * quitFlag){
    int recordNum = resourceHead.recordNum;
    if(recordNum > 100){
        recordNum = 100;
    }else if(recordNum < 1){
        recordNum = 1;
    }
    ResourceHead resourceResponseHead;//传回list头,避免粘包
    memset(&resourceResponseHead,0,sizeof(resourceResponseHead));
    resourceResponseHead.instruction = RECORD;
    resourceResponseHead.isEnd = 1;
    resourceResponseHead.isStart = true;
    char readStatement[100];
    createTable();
    time_t currentTime = time(NULL);
    // 将时间转换为当地时间
    struct tm *localTime = localtime(&currentTime);
    // 获取年月日
    int year = localTime->tm_year + 1900;  // tm_year 是自 1900 年以来的年份，需要加上 1900
    int month = localTime->tm_mon + 1;     // tm_mon 是从 0 到 11，需要加上 1
    int day = localTime->tm_mday;
    sprintf(readStatement,SelectSQL,year,month,day,recordNum);
    char * result = query_concat(dbCon,readStatement);
    resourceResponseHead.dataSize = strlen(result) + 1;
    if(!sendResourceHead(acceptFd,resourceResponseHead,NULL,&result,&remoteAddr,quitFlag)){
        return true;
    }
    if(!sendData(acceptFd,&result,resourceResponseHead,NULL,&remoteAddr,quitFlag)){
        return true;
    }
    free(result);
    return true;
}

void * timeout(void * args){ //超时关闭~
    _Atomic bool ** isTimeout = ((TimeOutArgs *)args) -> isTimeout;
    bool ** isClose = ((TimeOutArgs *)args) -> isClose;
    int acceptFd = ((TimeOutArgs *)args) -> acceptFd;
    free(args);
    while(1){
        if(*isClose == NULL || *isTimeout == NULL){
            if(*isClose == NULL && *isTimeout == NULL){
                //不做任何事情
            }else if(*isClose == NULL){
                free(*isTimeout);
                *isTimeout = NULL;
            }else if(*isTimeout == NULL){
                free(*isClose);
                *isClose = NULL;
            }
            pthread_exit(NULL);
        }else if(**isClose){
            free(*isTimeout);
            *isTimeout = NULL;
            free(*isClose);
            *isClose = NULL;
            pthread_exit(NULL);
        }
        compare_and_swap_bool(*isTimeout,false,true);
        sleep(240);//无输入超时时间
        if(*isClose == NULL || *isTimeout == NULL){
            if(*isClose == NULL && *isTimeout == NULL){
                //不做任何事情
            }else if(*isClose == NULL){
                free(*isTimeout);
                *isTimeout = NULL;
            }else if(*isTimeout == NULL){
                free(*isClose);
                *isClose = NULL;
            }
            pthread_exit(NULL);
        }else if(**isClose){
            free(*isTimeout);
            *isTimeout = NULL;
            free(*isClose);
            *isClose = NULL;
            pthread_exit(NULL);
        }else if(**isTimeout){
            printf("time out...\n");
            free(*isTimeout);
            *isTimeout = NULL;
            free(*isClose);
            *isClose = NULL;
            ResourceHead resourceHead;
            memset(&resourceHead,0,sizeof(resourceHead));
            resourceHead.dataSize = 0;
            resourceHead.instruction = QUIT;
            resourceHead.isEnd = true;
            resourceHead.isStart = true;
            sendResourceHead(acceptFd,resourceHead,NULL,NULL,NULL,NULL);
            // close(acceptFd);
            pthread_exit(NULL);
        }
    }
}

void * threadMethod(void * threadArgs){
    int acceptFd = ((ThreadArgs *)threadArgs) -> acceptFd;
    struct sockaddr_in remoteAddr = ((ThreadArgs *)threadArgs) -> remoteAddr;
    free(threadArgs);//传完就可以free了(卸磨杀驴?)
    threadArgs = NULL;

    //TODO: 超时断连机制改进
    // _Atomic bool * isTimeout = (_Atomic bool *)malloc(sizeof(_Atomic bool));
    // atomic_init(isTimeout, false);
    // bool * isClose = (bool *)malloc(sizeof(bool));
    // (*isClose) = false;
    // TimeOutArgs * timeOutArgs = (TimeOutArgs *)malloc(sizeof(timeOutArgs));
    // timeOutArgs -> isTimeout = &isTimeout;
    // timeOutArgs -> isClose = &isClose;
    // timeOutArgs -> acceptFd = acceptFd;
    // pthread_t pthreadT;
    // pthread_create(&pthreadT,&attr,timeout,timeOutArgs);


    // int flag = 0;  //PUT使用,已弃用
    bool quitFlag = false; //退出码
    // char lastFilename[100] = {0}; //记录上次文件名,用于错误处理.由于兼容轮转,已弃用
    int requestTypeFlag = -1;  //记录操作
    while (1) { //内部循环,为了方便PUT分块上传
        // 接收资源头信息
        ResourceHead resourceHead;
        memset(&resourceHead, 0, sizeof(resourceHead));
        if(!recvResourceHead(acceptFd,&resourceHead,&remoteAddr,NULL)){
            goto end;
        }

        //TODO: 待改进
        // //如果超时标识为true,将其置为false,失败也没关系.
        // if(isTimeout != NULL){
        //     compare_and_swap_bool(isTimeout,true,false);
        // }


        //显示包头详细信息
        time_t timeNow;
        time(&timeNow);
        if(strcmp(resourceHead.filename,"")){
            printf("Resource head: %s---%d---%s---%d(%ld)\n",requestInstructionNames[resourceHead.instruction],resourceHead.dataSize,resourceHead.filename,resourceHead.isEnd,timeNow);
        }else{
            printf("Resource head: %s---%d---%d(%ld)\n",requestInstructionNames[resourceHead.instruction],resourceHead.dataSize,resourceHead.isEnd,timeNow);
        }
        
        if(resourceHead.instruction == LIST){//因为list最多只需一次循环即可解决,故可以将所需变量定义于此.(PUt等操作切记不可!)
            doList(acceptFd,remoteAddr,&quitFlag);
            // close(acceptFd);  不能这样简单的关闭,需要分情况在不同时间段关闭
            requestTypeFlag = LIST;
            if(quitFlag){
                goto end;
            }
            // break;
        }else if(resourceHead.instruction == GET){
            doGet(resourceHead,acceptFd,remoteAddr,&quitFlag);
            requestTypeFlag = GET;
            if(quitFlag){
                goto end;
            }
        }else if(resourceHead.instruction == PUT){
            doPut(resourceHead,acceptFd,remoteAddr,&quitFlag);
            // close(acceptFd);  同上
            requestTypeFlag = PUT;
            // if (fileFd != NULL) { //防止第一次定义后错误
            //     fclose(fileFd);
            // }单线程下可行,但这个是轮转...
            if(quitFlag){
                goto end;
            }
            // break;
        }else if(resourceHead.instruction == DEL){
            doDel(resourceHead,acceptFd,remoteAddr,&quitFlag);
            requestTypeFlag = DEL;
            if(quitFlag){
                goto end;
            }
            // break;
        }else if(resourceHead.instruction == QUIT){
            close(acceptFd);
            requestTypeFlag = QUIT;
            goto end;
        }else if(resourceHead.instruction == RECORD){
            doRecord(resourceHead,acceptFd,remoteAddr,&quitFlag);
            requestTypeFlag = RECORD;
            if(quitFlag){
                goto end;
            }
        }
        printf("requset: %s:%d --- %s has done...\n",inet_ntoa(remoteAddr.sin_addr),ntohs(remoteAddr.sin_port),requestInstructionNames[requestTypeFlag]);
    }
    end:
    // if(isClose != NULL){
    //     (*isClose) = true;
    // }
    printf("requset: %s:%d --- %s has done...\n",inet_ntoa(remoteAddr.sin_addr),ntohs(remoteAddr.sin_port),requestInstructionNames[requestTypeFlag]);
    return NULL;
}

int main() {
    // 初始化套接字
    int socketFd = 0;
    if (!initSocket(&socketFd)) {
        exit(EXIT_FAILURE);
    }
    //分离状态设计啦~我是没想到没用在主要任务上,用在超时计时器上...
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    initThreadPool(10);
    dbCon = create_connection("ConnectedData.db");
    // 接收连接
    while(1){
        printf("Start listening for new clients.\n");
        //记录客户端信息的那啥结构体
        struct sockaddr_in remoteAddr;
        socklen_t remoteLen = sizeof(remoteAddr);
        memset(&remoteAddr, 0, sizeof(remoteAddr));
        //接收的那啥码
        int acceptFd = 0;
        if ((acceptFd = accept(socketFd, (struct sockaddr *)&remoteAddr, &remoteLen)) < 0) {
            perror("Accept failed");
            close(socketFd);
            return -1;
        }
        //直接传acceptFd然后给清空给我整麻了,检测半天差点没查出问题...
        ThreadArgs * threadArgs = (ThreadArgs *)malloc(sizeof(ThreadArgs));
        threadArgs -> acceptFd = acceptFd;
        threadArgs -> remoteAddr = remoteAddr;
        printf("Received a new client connection---IP: %s:%d\n",inet_ntoa(remoteAddr.sin_addr),ntohs(remoteAddr.sin_port));
        addInsertThreadPool("CONNECT",inet_ntoa(remoteAddr.sin_addr),0,"",0,dbCon);
        //PS: VS的TODO颜色和普通注释一样是真的难受.
        // pthread_t pthreadId;
        addTask(threadMethod,(void *)threadArgs);//改为线程池
        // if (pthread_create(&pthreadId, &attr, threadMethod, (void *)threadArgs) != 0) {//错误检测机制
        //     free(threadArgs);
        //     handleError("Thread creation failed",acceptFd,NULL,NULL,NULL,&remoteAddr);
        // }
    }
    sqlite3_close(dbCon); //其实执行不到这,但还是加上吧...
}
