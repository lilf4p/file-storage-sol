//Implementazione delle funzioni dell'api per comunicare con il server 

#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/types.h>
#include <errno.h>
#include <time.h>

#define N 100
#define UNIX_PATH_MAX 108 /* man 7 unix */ 
// utility macro
#define SYSCALL(c,e) \
    if(c==-1) { errno=e;return -1; }

int sc; //SOCKET FD
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
    return 0;

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

