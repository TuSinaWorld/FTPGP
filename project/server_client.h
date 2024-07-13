#ifndef __SERVER__CLIENT__H__
#define __SERVER__CLIENT__H__ 1
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <errno.h>
#include <stdatomic.h>
#include <time.h>
#include "pthread_pool.h"


// #include <winsock2.h> // ��Ҫ����winsock2.hͷ�ļ�
// #include <ws2tcpip.h> // ��Ҫ����ws2tcpip.hͷ�ļ�

#define LocalIp "0.0.0.0"
#define RemoteIp "115.230.124.114"
#define ServerPort 11522
#define FilePath "/root/remoteFile"
#define BuffSize 65536
#define InsertSQL "INSERT INTO ConnectRecord_%d_%d_%d (type, ip, datasize, filename, direction) VALUES ('%s', '%s', %d, '%s', %d);"
#define SelectSQL "SELECT * FROM ConnectRecord_%d_%d_%d ORDER BY timestamp DESC LIMIT 0, %d;"
#define CreateTableSQL "CREATE TABLE IF NOT EXISTS ConnectRecord_%d_%d_%d (" \
  "  id INTEGER PRIMARY KEY AUTOINCREMENT," \
  "  type TEXT NOT NULL," \
  "  ip TEXT NOT NULL," \
  "  datasize INTEGER NOT NULL," \
  "  filename TEXT," \
  "  direction INTEGER NOT NULL," \
  "  timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP" \
  ");"

enum requestInstruction{//����ö��
    LIST,
    GET,
    PUT,
    DEL,
    QUIT,
    FILE_DELETE_SUCCESS,
    ERROR_FILE_NOT_EXIT_OR_NO_PERMISSION,
    UNKNOWN_ERROR,
    RECORD
};

const char* requestInstructionNames[] = {//������ʾö����
    "LIST",
    "GET",
    "PUT",
    "DEL",
    "QUIT",
    "FILE_DELETE_SUCCESS",
    "ERROR_FILE_NOT_EXIT_OR_NO_PERMISSION",
    "UNKNOWN_ERROR",
    "RECORD"
};

typedef struct threadArgs{
    int acceptFd;
    struct sockaddr_in remoteAddr;
}ThreadArgs;

typedef struct resourceHead {  
    enum requestInstruction instruction; // 4 �ֽ�
    int dataSize;                     // 4 �ֽ�
    int currentSize;                  // 4 �ֽ�
    int recordNum;
    char filename[100];               // 100 �ֽ�
    char getPath[200];                // 200 �ֽ�
    bool isEnd;                       // 1 �ֽ�
    bool isStart;                     // 1 �ֽ�
    char padding[2];                  // �ֶ����,�����ض�ϵͳ(����bool��,int��,����ϵͳ������...)2 �ֽ� (��ȷ���ܴ�С��4�ı���)
} ResourceHead;

typedef struct clientArgs{
    char inputChar;
    char * args;
    char * args2;
    int recordNum;
}ClientArgs;

typedef struct timeOutArgs{
    _Atomic bool ** isTimeout;
    bool ** isClose;
    int acceptFd;
}TimeOutArgs;

typedef struct getArgs{
    struct resourceHead getResourceHead;
    char * buffStr;
}GetArgs;

typedef struct {
    long *size;
    long *length;
    char **result;
} QueryResult;

#endif