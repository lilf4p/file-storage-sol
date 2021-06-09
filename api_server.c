//Implementazione delle funzioni dell'api per comunicare con il server 

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

#define N 100
#define UNIX_PATH_MAX 108 /* man 7 unix */ 
// utility macro
#define SYSCALL(c,e) \
    if(c==-1) { errno=e;return -1; }

int connesso = 0;//FLAG PER SAPERE SE MI SONO CONNESSO O NO 
int sc; //SOCKET FD
char *socket_name;
char response[N];

//FUNZIONI DI UTILITA'
int msleep(long tms);
int compare_time (struct timespec a, struct timespec b);

//SE CONNESSO RITORNA 0, ALTRIMENTI -1 E SETTA ERRNO
int openConnection(const char* sockname, int msec,const struct timespec abstime) {
    
    struct sockaddr_un sa;
    strncpy(sa.sun_path,sockname,UNIX_PATH_MAX);
    sa.sun_family=AF_UNIX;

    if ((sc = socket(AF_UNIX,SOCK_STREAM,0)) == -1) {
        errno=EINVAL;
        return -1;
    }

    struct timespec ct;
    while (connect(sc,(struct sockaddr*)&sa,sizeof(sa)) == -1 && compare_time(ct,abstime)==-1) {
        msleep(msec);
        printf("Riprovo\n");
    }

    if (compare_time(ct,abstime)>0) {
        errno=ETIMEDOUT;
        return -1;
    }

    SYSCALL(read(sc,response,N),EREMOTEIO);
    printf("%s\n",response);
    fflush(stdin);
    connesso = 1;
    socket_name = sockname;
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

    char buffer[N];
    sprintf(buffer, "openFile,%s,%d",pathname,flags);

    SYSCALL(write(sc,buffer,N),EREMOTEIO);

    SYSCALL(read(sc,response,N),EREMOTEIO);
    printf("From Server : %s\n",response);

    char * t;
    t = strtok(response,",");

    if (strcmp(t,"-1")==0) { //ERRORE DAL SERVER
        t = strtok(NULL,",");
        errno = atoi(t);
        return -1;
    }else{ //SUCCESSO DAL SERVER 
        return 0;
    }
    
    return 0;

}

int writeFile(const char* pathname, const char* dirname) {

    if (connesso==0) {
        errno=ENOTCONN;
        return -1;
    }

    FILE *fp;
    int size_file;
    if ((fp = fopen(pathname,"r")) == NULL) {
        errno = EREMOTEIO;
        return -1;
    }
    struct stat st;
    stat(pathname,&st);
    size_file = st.st_size;
    char * file_buffer = malloc((size_file+1)*sizeof(char));
    if (file_buffer==NULL) {
        errno = ENOMEM;
        return -1;
    }

    //PRIMA INVIO IL PATHNAME AL SERVER PER CONTROLLARE DI POTER SCRIVERE IL FILE
    char buffer[N];
    sprintf(buffer,"writeFile,%s",pathname);

    SYSCALL(write(sc,buffer,N),EREMOTEIO);

    SYSCALL(read(sc,response,N),EREMOTEIO);
    printf("From Server : %s\n",response);

    char * t;
    t = strtok(response,",");

    if (strcmp(t,"-1")==0) { //ERRORE DAL SERVER
        t = strtok(NULL,",");
        errno = atoi(t);
        return -1;
    }

    //POSSO SCRIVERE IL FILE SUL SERVER 

    //LEGGO IL FILE E LO SCRIVO NEL BUFFER DA INVIARE 
    size_t newLen = fread(file_buffer,sizeof(char),size_file,fp);
    if (ferror(fp) != 0) {
        errno = EREMOTEIO;
        return -1;
    }else{
        file_buffer[newLen++] = '\0';
    }
    fclose(fp);

    //INVIO SIZE FILE
    SYSCALL(write(sc,&size_file,sizeof(size_file)),EREMOTEIO);

    //INVIO FILE
    SYSCALL(write(sc,file_buffer,size_file),EREMOTEIO);

    //SEVER RISPONDE 0 SE OK -1,ERRNO SE ERRORE
    char response2[N];
    SYSCALL(read(sc,response2,N),EREMOTEIO);
    printf("From Server : %s\n",response2);

    char * t1;
    t1 = strtok(response2,",");

    if (strcmp(t1,"-1")==0) { //ERRORE DAL SERVER
        t1 = strtok(NULL,",");
        errno = atoi(t1);
        return -1;
    }else{ //SUCCESSO DAL SERVER 
        return 0;
    }

    free(file_buffer);

}

int closeFile(const char* pathname) {

    if (connesso==0) {
        errno=ENOTCONN;
        return -1;
    }

    char buffer[N];
    sprintf(buffer, "closeFile,%s",pathname);

    SYSCALL(write(sc,buffer,N),EREMOTEIO);

    SYSCALL(read(sc,response,N),EREMOTEIO);
    printf("From Server : %s\n",response);

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

    char buffer[N];
    sprintf(buffer, "removeFile,%s",pathname);

    SYSCALL(write(sc,buffer,N),EREMOTEIO);

    SYSCALL(read(sc,response,N),EREMOTEIO);
    printf("From Server : %s\n",response);

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

