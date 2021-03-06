//Header file da includere nel client 

#include <stdio.h>

int openConnection(const char* sockname, int msec,const struct timespec abstime);
int closeConnection(const char* sockname);

int openFile(const char* pathname, int flags);
int readFile(const char* pathname, void** buf, size_t* size);
int readNFiles(int N, const char* dirname);
int writeFile(const char* pathname, const char*dirname);
int appendToFile(const char* pathname, void* buf,size_t size, const char* dirname);
int closeFile(const char* pathname);
int removeFile(const char* pathname);

//UTILITY CLIENT
int msleep(long tms);
int mkdir_p(const char *path);
