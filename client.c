#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include "api_server.h"

#define DIM_MSG 100
// utility macro
#define SYSCALL(c,e) \
    if(c==-1) { perror(e);exit(EXIT_FAILURE); }

long isNumber(const char* s);

int main (int argc, char * argv[]) {

    char opt;
    int hfnd, ffnd, pfnd;
    char *farg, *warg, *Warg, *rarg, *Rarg, *darg, *targ, *carg;

    //opzioni -f -h -p non possono essere ripetute
    hfnd=0;
    ffnd=0;
    pfnd=0;

    while ((opt = getopt(argc,argv,"hpf:w:W:r:R:d:t:c:")) != -1) {
        switch (opt) {
            case 'h':
                hfnd++;
                if (hfnd==1) {
                    printf("Operazioni supportate : \n-h\n-f filename\n-w dirname[n=0]\n-W file1[,file2]\n-r file1[,file2]\n-R [n=0]\n-d dirname\n-t time\n-c file1[,file2]\n-p\n");
                    return 0;
                }else if (hfnd>1) {
                    printf("L'opzione -h non puo' essere ripetuta\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'p':
                pfnd++;
                if (pfnd==1) {
                    printf("Opzione -p\n");
                }else if (pfnd>1) {
                    printf("L'opzione -p non puo' essere ripetuta\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'f':
                ffnd++;
                if (ffnd==1) {
                    farg=optarg;
                    printf("Opzione -f con argomento : %s\n",farg);
                }else if (ffnd>1) {
                    printf("L'opzione -f non puo' essere ripetuta\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'w':
                warg=optarg;
                printf("Opzione -w con argomento %s\n",warg);
                //TODO : ESEGUI COMANDO CON API
                break;
            case 'W':
                Warg=optarg;
                printf("Opzione -W con argomento %s\n",Warg);
                break;
            case 'r':
                rarg=optarg;
                printf("Opzione -r con argomento %s\n",rarg);
                break;
            case 'R':
                Rarg=optarg;
                printf("Opzione -R con argomento %s\n",Rarg);
                break;
            case 'd':
                darg=optarg;
                printf("Opzione -d con argomento %s\n",darg);
                break;
            case 't': 
                targ=optarg;
                printf("Opzione -t con argomento %s\n",targ);
                break;
            case 'c': 
                carg=optarg;
                printf("Opzione -c con argomento %s\n",carg);
                break;
            case '?':
                printf("l'opzione '-%c' non e' gestita\n", optopt);
                fprintf (stderr,"%s -h per vedere la lista delle operazioni supportate\n",argv[0]);
                break;
            case ':':
                printf("l'opzione '-%c' richiede un argomento\n", optopt);
                break;
            default:;
        }
    }
    


    return 0;
}

long isNumber(const char* s) {
   char* e = NULL;
   long val = strtol(s, &e, 0);
   if (e != NULL && *e == (char)0) return val; 
   return -1;
}