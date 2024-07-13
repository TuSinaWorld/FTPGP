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

pthread_attr_t attr; //��������Ϊ����״̬,�ʾ�������pthread_join,�������÷���ֵ~

sqlite3 * dbCon;

// CAS�������,���̶��Ż�����~~~
bool compare_and_swap_bool(_Atomic bool *ptr, bool old_val, bool new_val) {
    return __atomic_compare_exchange_n(ptr, &old_val, new_val, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

// ��ʼ���׽���
bool initSocket(int *socketFd) {
    // �����׽���
    *socketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (*socketFd < 0) {
        perror("Socket creation failed");
        return false;
    }

    struct linger so_linger;
    so_linger.l_onoff = 1;  // ��SO_LINGERѡ��
    so_linger.l_linger = 5; // ���õȴ�ʱ��Ϊ0
    int result;
    result = setsockopt(*socketFd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));
    if (result == -1) {
        perror("setsockopt");
        close(*socketFd);
        exit(EXIT_FAILURE);
    }

    // ���õ�ַ�ṹ��
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ServerPort);
    addr.sin_addr.s_addr = inet_addr(LocalIp);

    // ���׽���
    if (bind(*socketFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        close(*socketFd); // �ر��׽���
        return false;
    }

    // ��������
    if (listen(*socketFd, 8) < 0) {
        perror("Listen failed");
        close(*socketFd); // �ر��׽���
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
        long additional_length = argv[i] ? (strlen(argv[i]) + 1) : 5; // 1�ǡ�|���ĳ���, 5��"NULL"�ĳ���
        *(qr -> length) += additional_length;
        if (*(qr -> length) >= *(qr -> size)) {
            *(qr -> size) = *(qr -> size) * 2;
            *(qr -> result) = realloc(*(qr -> result), *(qr -> size));
            if (*(qr -> result) == NULL) {
                fprintf(stderr, "Memory reallocation failed\n");
                return 1; // ��ֹ�ص����������ش���
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
    // ��ʱ��ת��Ϊ����ʱ��
    struct tm *localTime = localtime(&currentTime);
    // ��ȡ������
    int year = localTime->tm_year + 1900;  // tm_year ���� 1900 ����������ݣ���Ҫ���� 1900
    int month = localTime->tm_mon + 1;     // tm_mon �Ǵ� 0 �� 11����Ҫ���� 1
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
    result[0] = '\0';  // ȷ������ַ���Ϊ��
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
    char insertStatement[1000] = {0}; // ����һ����������ַ���
    time_t currentTime = time(NULL);
    // ��ʱ��ת��Ϊ����ʱ��
    struct tm *localTime = localtime(&currentTime);
    // ��ȡ������
    int year = localTime->tm_year + 1900;  // tm_year ���� 1900 ����������ݣ���Ҫ���� 1900
    int month = localTime->tm_mon + 1;     // tm_mon �Ǵ� 0 �� 11����Ҫ���� 1
    int day = localTime->tm_mday;
    sprintf(insertStatement, InsertSQL, year, month, day, params -> type, params -> ip, params -> datasize, filename, params -> direction);
    createTable();
    // printf("%s\n",insertStatement);
    char *err_msg = 0;
    int rc = sqlite3_exec(params -> db, insertStatement, 0, 0, &err_msg); // ִ�в������
    if (rc != SQLITE_OK) { // ���ִ�н��
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
    ResourceHead resourceResponseHead;//����listͷ,����ճ��
    memset(&resourceResponseHead,0,sizeof(resourceResponseHead));
    resourceResponseHead.instruction = LIST;
    resourceResponseHead.isEnd = 1;//ûɶ����,ͳһ�淶�����ϲ������
    DIR *dir = opendir(FilePath);
    //��ȡ·��
    if (!dir) {
        handleError("opendir failed", acceptFd, NULL, NULL, NULL, &remoteAddr, quitFlag);
        return true;//�������,����ѭ��,����boolֵ�����׸��
    }
    char * fileList = NULL;
    int totalLength = 1; //1��'\0'�Ĵ�С;
    fileList = (char *)malloc(totalLength);
    if (!fileList) {
        handleError("malloc failed", acceptFd, &dir, NULL, NULL, &remoteAddr, quitFlag);
        return true;//�������,����ѭ��
    }
    //��ȡ�ļ��������ļ�,����������
    fileList[0] = '\0';// ��ʼ��fileListΪ���ַ���
    struct dirent *entry;
    while((entry = readdir(dir)) != NULL){
        if (entry -> d_type == DT_REG){
            totalLength += strlen(entry->d_name); //�ļ�����С
            totalLength += 1; //�ָ���Ĵ�С
            char *temp = realloc(fileList, totalLength);
            if (temp == NULL) {
                perror("realloc");
                close(acceptFd);
                closedir(dir);
                free(fileList);
                *quitFlag = true;
                addInsertThreadPool("QUIT",inet_ntoa(remoteAddr.sin_addr),0,"",1,dbCon);
                return true; // �������, ����ѭ��
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
    //���Ͱ�ͷ���ͻ���
    if(!sendResourceHead(acceptFd,resourceResponseHead,&dir,&fileList,&remoteAddr,quitFlag)){
        return true;//����,����ѭ��
    }
    //���Ͱ�����ͻ���
    if(!sendData(acceptFd,&fileList,resourceResponseHead,&dir,&remoteAddr,quitFlag)){
        return true;//����,����ѭ��
    }
    // close(acceptFd);��������ر�acceptFd,�Ա��������ʹ��
    closedir(dir);
    free(fileList);
    return true;//�������,�˳�ѭ��
}

bool doGet(ResourceHead resourceHead,int acceptFd,struct sockaddr_in remoteAddr,bool * quitFlag){
    char path[strlen(FilePath) + 102]; //�Ǹ�1��/�ĳ���,100���������ɳ��س���
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
        sendResourceHead(acceptFd,responseResourceHead,NULL,NULL,&remoteAddr,quitFlag);//�����Ƿ���ȷ,���˸ú����Դ��Ĵ���,��Ҳ���������⴦����...
        return true;//����ѭ��,����������׽���,Ҳ��������ÿͻ��˵�����.(��������ʧ�ܾ�����һ������,��sendResourceHead�����д���)
    }
    FILE * file = fopen(path,"rb");
    if (!file) {
        responseResourceHead.instruction = ERROR_FILE_NOT_EXIT_OR_NO_PERMISSION;
        sendResourceHead(acceptFd,responseResourceHead,NULL,NULL,&remoteAddr,quitFlag);//�����Ƿ���ȷ,���˸ú����Դ��Ĵ���,��Ҳ���������⴦����...
        return true;//����ѭ��,����������׽���,Ҳ��������ÿͻ��˵�����.(��������ʧ�ܾ�����һ������,��sendResourceHead�����д���)
    }
    char * buffStr = (char *)malloc(BuffSize);
    if (fseek(file, resourceHead.currentSize, SEEK_SET) != 0) {
        perror("Error seeking in file");
        fclose(file);
        free(buffStr);
        responseResourceHead.instruction = UNKNOWN_ERROR;
        sendResourceHead(acceptFd,responseResourceHead,NULL,NULL,&remoteAddr,quitFlag);//�����Ƿ���ȷ,���˸ú����Դ��Ĵ���,��Ҳ���������⴦����...
        return true;//����ѭ��,����������׽���,Ҳ��������ÿͻ��˵�����.(��������ʧ�ܾ�����һ������,��sendResourceHead�����д���)
    }
    size_t bytesRead = fread(buffStr, 1, BuffSize, file);
    fclose(file);
    if(bytesRead <= 0){
        free(buffStr);
        responseResourceHead.isEnd = true;
        sendResourceHead(acceptFd,responseResourceHead,NULL,NULL,&remoteAddr,quitFlag);//�����Ƿ���ȷ,���˸ú����Դ��Ĵ���,��Ҳ���������⴦����...
        return true;//����ѭ��,����������׽���,Ҳ��������ÿͻ��˵�����.(��������ʧ�ܾ�����һ������,��sendResourceHead�����д���)
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

bool doPut(ResourceHead resourceHead,int acceptFd,struct sockaddr_in remoteAddr,bool * quitFlag){//�����bool���Ƿ���Ҫ����ѭ��
    // ����Ƿ����
    if (resourceHead.isEnd) {
         //buffData�Ѿ�free��δ����
        // close(acceptFd); //��;���ܲ�ֹput,����ֱ�ӹر�����
        return true;//����,��������ѭ��
    }

    // �����ļ�·��
    char path[strlen(FilePath) + 102]; //�Ǹ�1��/�ĳ���,100���������ɳ��س���
    sprintf(path, "%s/%s", FilePath, resourceHead.filename);
    if (resourceHead.isStart) {
        FILE *initFileFd = fopen(path, "wb");
        if (initFileFd == NULL) {
            handleError("Error opening file", acceptFd, NULL, NULL, NULL, &remoteAddr, quitFlag);
            return true; //����,����ѭ��
        }
        fclose(initFileFd);
        // strcpy(lastFilename,resourceHead.filename);���߳�������ļ���һ�����������
    }
    FILE * fileOp = fopen(path, "ab");
    FILE ** fileFd = &fileOp; //��ʷ��������,���߳�ת��ת�Ĵ���,���������.
    // ���ļ�
    if (!(*fileFd)) {
        handleError("Error opening file", acceptFd, NULL, NULL, NULL, &remoteAddr, quitFlag);
        return true; //����,����ѭ��
    }

    //���߳�������ļ���һ�����������
    // if(strcmp(lastFilename,resourceHead.filename) != 0){ //ǰ�����ļ�����һ��.
    //     handleError("The file name defined in the header and footer is inconsistent.",acceptFd,NULL,NULL,fileFd,&remoteAddr,quitFlag);
    //     return true;  //����,����ѭ��
    // }

    // ���仺��������������
    char *buffData = malloc(resourceHead.dataSize);  //������,û���Ż�,���������������漰�ں�̬�л�������~
    //ʹ��malloc����Ϊc���Թ淶:����ʹ�ñ���������̬����~�ֲ����˷��ڴ�~
    if (!buffData) {
        handleError("Memory allocation failed", acceptFd, NULL, NULL , fileFd, &remoteAddr, quitFlag);
        return true;  //����,����ѭ��
    }

    if(!recvData(acceptFd,&buffData,resourceHead,fileFd,&remoteAddr,quitFlag)){
        return true;  //����,����ѭ��
    }

    // д���ļ���������Դ
    size_t bytesWritten = fwrite(buffData, 1, resourceHead.dataSize, *fileFd);
    if (bytesWritten != resourceHead.dataSize) {
        handleError("Error writing to file", acceptFd, NULL, &buffData, fileFd, &remoteAddr, quitFlag);
        return true;
    }
    fclose(*fileFd);   //��������ظ��򿪹ر�fileFd�Խ�ʡ�л��ں�̬�Ĳ���Ҫ����,�κ�Ҫ����ת�ṹ������ô��
    free(buffData);
    return false;//isEndΪfalse,��Ҫ����ѭ��;(δ����ѭ��,���ܹر�acceptFd)
}

bool doDel(ResourceHead resourceHead,int acceptFd,struct sockaddr_in remoteAddr,bool * quitFlag){
    char path[strlen(FilePath) + 102]; //�Ǹ�1��/�ĳ���,100���������ɳ��س���
    int written = sprintf(path, "%s/%s", FilePath, resourceHead.filename);
    ResourceHead responseResourceHead;
    memset(&responseResourceHead,0,sizeof(responseResourceHead));
    responseResourceHead.dataSize = 0;
    responseResourceHead.isEnd = 1;
    strcpy(responseResourceHead.filename,resourceHead.filename);
    if (written < 0) {
        responseResourceHead.instruction = UNKNOWN_ERROR;
        sendResourceHead(acceptFd,responseResourceHead,NULL,NULL,&remoteAddr,quitFlag);//�����Ƿ���ȷ,���˸ú����Դ��Ĵ���,��Ҳ���������⴦����...
        return true;//����ѭ��,����������׽���,Ҳ��������ÿͻ��˵�����.(��������ʧ�ܾ�����һ������,��sendResourceHead�����д���)
    }
    // ɾ���ļ��������
    if (remove(path) == 0) {
        responseResourceHead.instruction = FILE_DELETE_SUCCESS;
        sendResourceHead(acceptFd,responseResourceHead,NULL,NULL,&remoteAddr,quitFlag);//�����Ƿ���ȷ,���˸ú����Դ��Ĵ���,��Ҳ���������⴦����...
        return true;
    }
    responseResourceHead.instruction = ERROR_FILE_NOT_EXIT_OR_NO_PERMISSION;
    sendResourceHead(acceptFd,responseResourceHead,NULL,NULL,&remoteAddr,quitFlag);//�����Ƿ���ȷ,���˸ú����Դ��Ĵ���,��Ҳ���������⴦����...
    return true;
}

bool doRecord(ResourceHead resourceHead,int acceptFd,struct sockaddr_in remoteAddr,bool * quitFlag){
    int recordNum = resourceHead.recordNum;
    if(recordNum > 100){
        recordNum = 100;
    }else if(recordNum < 1){
        recordNum = 1;
    }
    ResourceHead resourceResponseHead;//����listͷ,����ճ��
    memset(&resourceResponseHead,0,sizeof(resourceResponseHead));
    resourceResponseHead.instruction = RECORD;
    resourceResponseHead.isEnd = 1;
    resourceResponseHead.isStart = true;
    char readStatement[100];
    createTable();
    time_t currentTime = time(NULL);
    // ��ʱ��ת��Ϊ����ʱ��
    struct tm *localTime = localtime(&currentTime);
    // ��ȡ������
    int year = localTime->tm_year + 1900;  // tm_year ���� 1900 ����������ݣ���Ҫ���� 1900
    int month = localTime->tm_mon + 1;     // tm_mon �Ǵ� 0 �� 11����Ҫ���� 1
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

void * timeout(void * args){ //��ʱ�ر�~
    _Atomic bool ** isTimeout = ((TimeOutArgs *)args) -> isTimeout;
    bool ** isClose = ((TimeOutArgs *)args) -> isClose;
    int acceptFd = ((TimeOutArgs *)args) -> acceptFd;
    free(args);
    while(1){
        if(*isClose == NULL || *isTimeout == NULL){
            if(*isClose == NULL && *isTimeout == NULL){
                //�����κ�����
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
        sleep(240);//�����볬ʱʱ��
        if(*isClose == NULL || *isTimeout == NULL){
            if(*isClose == NULL && *isTimeout == NULL){
                //�����κ�����
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
    free(threadArgs);//����Ϳ���free��(жĥɱ¿?)
    threadArgs = NULL;

    //TODO: ��ʱ�������ƸĽ�
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


    // int flag = 0;  //PUTʹ��,������
    bool quitFlag = false; //�˳���
    // char lastFilename[100] = {0}; //��¼�ϴ��ļ���,���ڴ�����.���ڼ�����ת,������
    int requestTypeFlag = -1;  //��¼����
    while (1) { //�ڲ�ѭ��,Ϊ�˷���PUT�ֿ��ϴ�
        // ������Դͷ��Ϣ
        ResourceHead resourceHead;
        memset(&resourceHead, 0, sizeof(resourceHead));
        if(!recvResourceHead(acceptFd,&resourceHead,&remoteAddr,NULL)){
            goto end;
        }

        //TODO: ���Ľ�
        // //�����ʱ��ʶΪtrue,������Ϊfalse,ʧ��Ҳû��ϵ.
        // if(isTimeout != NULL){
        //     compare_and_swap_bool(isTimeout,true,false);
        // }


        //��ʾ��ͷ��ϸ��Ϣ
        time_t timeNow;
        time(&timeNow);
        if(strcmp(resourceHead.filename,"")){
            printf("Resource head: %s---%d---%s---%d(%ld)\n",requestInstructionNames[resourceHead.instruction],resourceHead.dataSize,resourceHead.filename,resourceHead.isEnd,timeNow);
        }else{
            printf("Resource head: %s---%d---%d(%ld)\n",requestInstructionNames[resourceHead.instruction],resourceHead.dataSize,resourceHead.isEnd,timeNow);
        }
        
        if(resourceHead.instruction == LIST){//��Ϊlist���ֻ��һ��ѭ�����ɽ��,�ʿ��Խ�������������ڴ�.(PUt�Ȳ����мǲ���!)
            doList(acceptFd,remoteAddr,&quitFlag);
            // close(acceptFd);  ���������򵥵Ĺر�,��Ҫ������ڲ�ͬʱ��ιر�
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
            // close(acceptFd);  ͬ��
            requestTypeFlag = PUT;
            // if (fileFd != NULL) { //��ֹ��һ�ζ�������
            //     fclose(fileFd);
            // }���߳��¿���,���������ת...
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
    // ��ʼ���׽���
    int socketFd = 0;
    if (!initSocket(&socketFd)) {
        exit(EXIT_FAILURE);
    }
    //����״̬�����~����û�뵽û������Ҫ������,���ڳ�ʱ��ʱ����...
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    initThreadPool(10);
    dbCon = create_connection("ConnectedData.db");
    // ��������
    while(1){
        printf("Start listening for new clients.\n");
        //��¼�ͻ�����Ϣ����ɶ�ṹ��
        struct sockaddr_in remoteAddr;
        socklen_t remoteLen = sizeof(remoteAddr);
        memset(&remoteAddr, 0, sizeof(remoteAddr));
        //���յ���ɶ��
        int acceptFd = 0;
        if ((acceptFd = accept(socketFd, (struct sockaddr *)&remoteAddr, &remoteLen)) < 0) {
            perror("Accept failed");
            close(socketFd);
            return -1;
        }
        //ֱ�Ӵ�acceptFdȻ�����ո���������,��������û�������...
        ThreadArgs * threadArgs = (ThreadArgs *)malloc(sizeof(ThreadArgs));
        threadArgs -> acceptFd = acceptFd;
        threadArgs -> remoteAddr = remoteAddr;
        printf("Received a new client connection---IP: %s:%d\n",inet_ntoa(remoteAddr.sin_addr),ntohs(remoteAddr.sin_port));
        addInsertThreadPool("CONNECT",inet_ntoa(remoteAddr.sin_addr),0,"",0,dbCon);
        //PS: VS��TODO��ɫ����ͨע��һ�����������.
        // pthread_t pthreadId;
        addTask(threadMethod,(void *)threadArgs);//��Ϊ�̳߳�
        // if (pthread_create(&pthreadId, &attr, threadMethod, (void *)threadArgs) != 0) {//���������
        //     free(threadArgs);
        //     handleError("Thread creation failed",acceptFd,NULL,NULL,NULL,&remoteAddr);
        // }
    }
    sqlite3_close(dbCon); //��ʵִ�в�����,�����Ǽ��ϰ�...
}
