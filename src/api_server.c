//Implementazione delle funzioni dell'api per comunicare con il server. 

#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/types.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <libgen.h>

#define DIM_MSG 100
#define UNIX_PATH_MAX 108 /* man 7 unix */ 
// utility macro
#define SYSCALL(c,e) \
    if(c==-1) { errno=e;return -1; }

int connesso = 0;//FLAG PER SAPERE SE MI SONO CONNESSO O NO 
int sc; //SOCKET FD
char socket_name[PATH_MAX];
char response[DIM_MSG];

//FUNZIONI DI UTILITA'
int msleep(long tms);
int compare_time (struct timespec a, struct timespec b);
int mkdir_p(const char *path);


//SE CONNESSO RITORNA 0, ALTRIMENTI -1 E SETTA ERRNO
int openConnection(const char* sockname, int msec,const struct timespec abstime) {
    
    struct sockaddr_un sa;
    memset(&sa,'0',sizeof(sa));
    strncpy(sa.sun_path,sockname,UNIX_PATH_MAX);
    sa.sun_family=AF_UNIX;

    if ((sc = socket(AF_UNIX,SOCK_STREAM,0)) == -1) {
        errno=EINVAL;
        return -1;
    }

    struct timespec ct;
    while (connect(sc,(struct sockaddr*)&sa,sizeof(sa)) == -1 && compare_time(ct,abstime)==-1) {
        msleep(msec);
        //printf("Riprovo\n");
    }

    if (compare_time(ct,abstime)>0) {
        errno=ETIMEDOUT;
        return -1;
    }

    SYSCALL(read(sc,response,DIM_MSG),EREMOTEIO);
    printf("%s\n",response);
    connesso = 1;
    strcpy(socket_name,sockname);
    return 0;

}

//ritorna 0 se ha successo -- ha successo sse sono connesso al socket sockname (equivale ad aver fatto prima una openConnection con sockname)
//errore se sockname non corretto, o se non connesso 
int closeConnection(const char* sockname) {

    if (connesso==0) {
        errno=EPERM;
        return -1;
    }

    if (strcmp(socket_name,sockname)==0) {
        SYSCALL(close(sc),EREMOTEIO);
        printf ("Close connection...\n");
        return 0;
    }else{
        errno=EINVAL;
        return -1;
    }
}

//FLAGS (O_CREATE) VALE 0 o 1
int openFile(const char* pathname, int flags) {
    
    if (connesso==0) {
        errno=ENOTCONN;
        return -1;
    }

    char * buffer = malloc(DIM_MSG*sizeof(char));
    sprintf(buffer, "openFile,%s,%d",pathname,flags);

    SYSCALL(write(sc,buffer,DIM_MSG),EREMOTEIO);

    SYSCALL(read(sc,response,DIM_MSG),EREMOTEIO);
    //printf("From Server : %s\n",response);

    char * t;
    t = strtok(response,",");

    if (strcmp(t,"-1")==0) { //ERRORE DAL SERVER
        t = strtok(NULL,",");
        errno = atoi(t);
        return -1;
    }else{ //SUCCESSO DAL SERVER 
        return 0;
    }

}

int writeFile(const char* pathname, const char* dirname) {

    if (connesso==0) {
        errno=ENOTCONN;
        return -1;
    }

    int fd;
    int size_file;
    if ((fd = open(pathname,O_RDONLY)) == -1) {
        errno = ENOENT;
        return -1;
    }

    char * buffer = malloc(DIM_MSG*sizeof(char));
    sprintf(buffer, "writeFile,%s",pathname);

    SYSCALL(write(sc,buffer,DIM_MSG),EREMOTEIO);

    SYSCALL(read(sc,response,DIM_MSG),EREMOTEIO);
    //printf("From Server : %s\n",response);

    char * t;
    t = strtok(response,",");

    if (strcmp(t,"-1")==0) { //ERRORE DAL SERVER
        t = strtok(NULL,",");
        errno = atoi(t);
        return -1;
    }else{ //SUCCESSO DAL SERVER 
        //return 0;
    }

    //POSSO INVIARE IL FILE 
    struct stat st;
    fstat(fd,&st);
    size_file = st.st_size;
    char * file_buffer = malloc((size_file+1)*sizeof(char));
    if (file_buffer==NULL) {
        errno = ENOMEM;
        return -1;
    }

    //LEGGO IL FILE E LO SCRIVO NEL BUFFER DA INVIARE 
    int newLen = read(fd,file_buffer,size_file);
    if (newLen==-1) {
        errno = EREMOTEIO;
        free(file_buffer);
        return -1;
    }else{
        file_buffer[newLen++] = '\0';
    }
    close(fd);

    //INVIO SIZE FILE
    char *tmp = malloc(DIM_MSG*sizeof(char));
    sprintf(tmp,"%d",size_file);
    SYSCALL(write(sc,tmp,sizeof(tmp)),EREMOTEIO);

    //CONFERMA DAL SERVER
    char conf[DIM_MSG];
    SYSCALL(read(sc,conf,DIM_MSG),EREMOTEIO);

    //INVIO FILE
    SYSCALL(write(sc,file_buffer,size_file+1),EREMOTEIO);

    //RISPOSTA SERVER 
    char * result = malloc(DIM_MSG*sizeof(char));
    SYSCALL(read(sc,result,DIM_MSG),EREMOTEIO);
    //printf("From Server : %s\n",response);

    char * t1;
    t1 = strtok(result,",");

    if (strcmp(t1,"-1")==0) { //ERRORE DAL SERVER
        t1 = strtok(NULL,",");
        errno = atoi(t1);
        free(file_buffer);
        return -1;
    }else{ //SUCCESSO DAL SERVER 
        free(file_buffer);
        return 0;
    }
    
}

int closeFile(const char* pathname) {

    if (connesso==0) {
        errno=ENOTCONN;
        return -1;
    }

    char * buffer = malloc(DIM_MSG*sizeof(char));
    sprintf(buffer, "closeFile,%s",pathname);

    SYSCALL(write(sc,buffer,DIM_MSG),EREMOTEIO);

    SYSCALL(read(sc,response,DIM_MSG),EREMOTEIO);
    //printf("From Server : %s\n",response);

    char * t;
    t = strtok(response,",");

    if (strcmp(t,"-1")==0) { //ERRORE DAL SERVER
        t = strtok(NULL,",");
        errno = atoi(t);
        return -1;
    }else{ //SUCCESSO DAL SERVER 
        return 0;
    }

}

int removeFile(const char* pathname) {

    if (connesso==0) {     
        errno=ENOTCONN;
        return -1;
    }

    char * buffer = malloc(DIM_MSG*sizeof(char));
    sprintf(buffer, "removeFile,%s",pathname);

    SYSCALL(write(sc,buffer,DIM_MSG),EREMOTEIO);

    SYSCALL(read(sc,response,DIM_MSG),EREMOTEIO);
    //printf("From Server : %s\n",response);

    char * t;
    t = strtok(response,",");

    if (strcmp(t,"-1")==0) { //ERRORE DAL SERVER
        t = strtok(NULL,",");
        errno = atoi(t);
        return -1;
    }else{ //SUCCESSO DAL SERVER 
        return 0;
    }

}

int appendToFile(const char* pathname, void* buf,size_t size, const char* dirname) {

    if (connesso==0) {
        errno=ENOTCONN;
        return -1;
    }

    char * buffer = malloc(DIM_MSG*sizeof(char));
    sprintf(buffer, "appendToFile,%s",pathname);

    SYSCALL(write(sc,buffer,DIM_MSG),EREMOTEIO);

    SYSCALL(read(sc,response,DIM_MSG),EREMOTEIO);
    //printf("From Server : %s\n",response);

    char * t;
    t = strtok(response,",");

    if (strcmp(t,"-1")==0) { //ERRORE DAL SERVER
        t = strtok(NULL,",");
        errno = atoi(t);
        return -1;
    }else{ //SUCCESSO DAL SERVER 
        //return 0;
    }

    //INVIO SIZE FILE
    char *tmp = malloc(DIM_MSG*sizeof(char));
    sprintf(tmp,"%ld",size);
    SYSCALL(write(sc,tmp,sizeof(tmp)),EREMOTEIO);

    //CONFERMA DAL SERVER
    char conf[DIM_MSG];
    SYSCALL(read(sc,conf,DIM_MSG),EREMOTEIO);

    //INVIO FILE
    SYSCALL(write(sc,buf,size+1),EREMOTEIO);

    //RISPOSTA SERVER 
    char * result = malloc(DIM_MSG*sizeof(char));
    SYSCALL(read(sc,result,DIM_MSG),EREMOTEIO);
    //printf("From Server : %s\n",response);

    char * t1;
    t1 = strtok(result,",");

    if (strcmp(t1,"-1")==0) { //ERRORE DAL SERVER
        t1 = strtok(NULL,",");
        errno = atoi(t1);
        //free(buf);
        return -1;
    }else{ //SUCCESSO DAL SERVER 
        //free(buf);
        return 0;
    }

}

int readFile(const char* pathname, void** buf, size_t* size) {

    if (connesso==0) {
        errno=ENOTCONN;
        return -1;
    }

    char * buffer = malloc(DIM_MSG*sizeof(char));
    sprintf(buffer, "readFile,%s",pathname);

    SYSCALL(write(sc,buffer,DIM_MSG),EREMOTEIO);

    //RICEVO SIZE FILE
    SYSCALL(read(sc,response,DIM_MSG),EREMOTEIO);
    //printf("From Server : %s\n",response);
    
    char * t;
    t = strtok(response,",");
    int size_file;
    if (strcmp(t,"-1")==0) { //ERRORE DAL SERVER
        t = strtok(NULL,",");
        errno = atoi(t);
        return -1;
    }else{ //SIZE FILE 
        size_file = atoi(t);
    }
    *size = size_file;
    char *file = malloc (size_file*sizeof(char));
    if (file == NULL) {
        errno=ENOTRECOVERABLE;
        return -1;
    }

    //INVIO CONFERMA AL SERVER 
    char * buf1 = malloc(DIM_MSG*sizeof(char));
    if (buf1!=NULL) {
        buf1="0";
        SYSCALL(write(sc,buf1,sizeof(buf1)),EREMOTEIO);
    }//invia errore

    //RICEVO FILE 
    SYSCALL(read(sc,file,size_file),EREMOTEIO);
    *buf = malloc(size_file*sizeof(char));
    *buf = file;

    //printf("From Server : %s\n",file);
    
    return 0;

}

//TODO : DA RIFARE -- SERVONO ANCHE I PATH DEI FILE !!!
//TODO : SALVA FILE IN DIRNAME
int readNFiles(int N, const char* dirname) { // TODO : BUG

    if (connesso==0) {
        errno=ENOTCONN;
        return -1;
    }
    
    if (dirname!=NULL) {
        //CREA DIR SE NON ESISTE 
        //printf("DIRECTORY : %s\n",dirname);
        mkdir_p(dirname);
    }

    //INVIA IL COMANDO AL SERVER 
    char bufsend [DIM_MSG];
    memset(bufsend,0,DIM_MSG);
    sprintf(bufsend, "readNFiles,%d",N);
    SYSCALL(write(sc,bufsend,DIM_MSG),EREMOTEIO);

    //RICEVE IL NUMERO DI FILE CHE IL SERVER INVIA
    char bufrec [DIM_MSG];
    memset(bufrec,0,DIM_MSG);
    SYSCALL(read(sc,bufrec,DIM_MSG),EREMOTEIO);
    //printf("From Server : NUM FILE = %s\n",bufrec);
    
    char * t = strtok(bufrec,",");
    if (strcmp(t,"-1")==0) { //ERRORE DAL SERVER
        t = strtok(NULL,",");
        errno = atoi(t);
        return -1;
    }
    int nf = atoi(t);//NUMERO DI FILE CONCORDATO CON IL SERVER 
    int i=0;
    
    //CONFERMA AL SERVER 
    SYSCALL(write(sc,"ok",3),EREMOTEIO);
    
    //RICEVI N VOLTE PATH,SIZE,DATA
    for (i=0;i<nf;i++) {
        
        //RICEVO PATH
        char path [PATH_MAX];
        memset(path,0,PATH_MAX);
        SYSCALL(read(sc,path,sizeof(path)),EREMOTEIO);
        //printf("From Server : PATH = %s\n",path);
        
        char *t1 = strtok(path,",");
        if (strcmp(t1,"-1")==0) { //ERRORE DAL SERVER
            t1 = strtok(NULL,",");
            errno = atoi(t1);
            return -1;
        }

        //CONFERMA AL SERVER 
        SYSCALL(write(sc,"ok",3),EREMOTEIO);
    
        //RICEVO SIZE
        char ssize [DIM_MSG];
        memset(ssize,0,DIM_MSG);
        SYSCALL(read(sc,ssize,sizeof(ssize)),EREMOTEIO);
        //printf("From Server : SIZE = %s\n",ssize);
        
        char *t2 = strtok(ssize,",");
        if (strcmp(t2,"-1")==0) { //ERRORE DAL SERVER
            t2 = strtok(NULL,",");
            errno = atoi(t2);
            return -1;
        }
        
        int size_file = atoi(ssize);

        //CONFERMA AL SERVER 
        SYSCALL(write(sc,"ok",3),EREMOTEIO);

        //RICEVO FILE  
        char fbuf [size_file];
        memset(fbuf,0,size_file);
        SYSCALL(read(sc,fbuf,size_file),EREMOTEIO);
        //printf("From Server : FILE = %s\n",fbuf);
        
        char *t3 = strtok(fbuf,",");
        if (t3!=NULL) {
            if (strcmp(t3,"-1")==0) { //ERRORE DAL SERVER
                t3 = strtok(NULL,",");
                errno = atoi(t3);
                return -1;
            }
        }

        if (dirname!=NULL) {
            //SALVA IN DIR
            char sp[PATH_MAX];
            memset(sp,0,PATH_MAX);
            char * file_name = basename(path);
            sprintf(sp,"%s/%s",dirname,file_name);
            //printf("FILE : %s\n",sp);
            //CREA FILE SE NON ESISTE
            FILE* of;
            of = fopen(sp,"w");
            if (of==NULL) {
                printf("Errore aprendo il file\n");
            } else {
                fprintf(of,"%s",fbuf);
                fclose(of);
            }
        }
        //printf("PATH=%s SIZE=%d CONTENUTO=%s\n",path,size_file,fbuf);

    }

    //SUCCESSO --> RITORNO IL NUMERO DI FILE LETTI 
    return nf;

}





//-------------FUNZIONI DI UTILITY--------------//

//implementa sleep in millisecondi 
int msleep(long tms) {

    struct timespec ts;
    int ret;

    if (tms < 0)
    {
        errno = EINVAL;
        return -1;
    }

    ts.tv_sec = tms / 1000;
    ts.tv_nsec = (tms % 1000) * 1000000;

    do {
        ret = nanosleep(&ts, &ts);
    } while (ret && errno == EINTR);

    return ret;
}

//0 se sono uguali, 1 se a>b, -1 se a<b
int compare_time (struct timespec a, struct timespec b) {
    clock_gettime(CLOCK_REALTIME,&a);
    if (a.tv_sec == b.tv_sec) {
        if (a.tv_nsec > b.tv_nsec) return 1;
        else if (a.tv_nsec == b.tv_nsec) return 0;
        else return -1;
    }else if (a.tv_sec > b.tv_sec) return 1;
    else return -1;
}

int mkdir_p(const char *path) {
    
    const size_t len = strlen(path);
    char _path[PATH_MAX];
    char *p; 

    errno = 0;

    if (len > sizeof(_path)-1) {
        errno = ENAMETOOLONG;
        return -1; 
    }   
    strcpy(_path, path);

    for (p = _path + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';

            if (mkdir(_path, S_IRWXU) != 0) {
                if (errno != EEXIST)
                    return -1; 
            }

            *p = '/';
        }
    }   

    if (mkdir(_path, S_IRWXU) != 0) {
        if (errno != EEXIST)
            return -1; 
    }   

    return 0;
}


