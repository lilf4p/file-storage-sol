#include <sys/types.h> 
#include <sys/socket.h> 
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

#define UNIX_PATH_MAX 108 /* man 7 unix */ 
#define SOCKNAME "/tmp/LSOfilestorage.sk"
#define N 100

// utility macro
#define SYSCALL(c,e) \
    if(c==-1) { perror(e);exit(EXIT_FAILURE); }

int main (void) {

    char op[N];
    char response[N];
    int sc;
    struct sockaddr_un sa;
    strncpy(sa.sun_path,SOCKNAME,UNIX_PATH_MAX);
    sa.sun_family=AF_UNIX;

    if ((sc = socket(AF_UNIX,SOCK_STREAM,0)) == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    while (connect(sc,(struct sockaddr*)&sa,sizeof(sa)) == -1) {
        if (errno == ENOENT) {
            sleep(1);
        }else{
            perror("Connect");
            exit(EXIT_FAILURE);
        }
    }
    SYSCALL(read(sc,response,N),"read");
    printf("%s\n",response);
    fflush(stdin);
    while (1) {

        printf("> ");
        if (fgets(op,N,stdin) == NULL) {
            perror("fgets");
            exit(EXIT_FAILURE);
        }

        if (strncmp(op,"quit",4) == 0) {
            printf("Sto uscendo...\n");
            break;
        }

        SYSCALL(write(sc,op,sizeof(op)),"write");

        SYSCALL(read(sc,response,N),"read");
        printf("From Server : %s\n",response);

    }

    close(sc);

    return 0;
}