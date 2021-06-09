#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include "api_server.h"

/*

gcc -c api_server.c -o api_server.o
ar rcs libapi.a api_server.o
gcc client.c -o client -L ./ -lapi

*/

#define DIM_MSG 100
// utility macro
#define APICALL(c,e) \
    if(c==-1) { perror(e);exit(EXIT_FAILURE); }

int flag_stampa=0;

long isNumber(const char* s);

int main (int argc, char * argv[]) {

    char opt;
    int hfnd, ffnd, pfnd;
    char *farg, *warg, *Warg, *rarg, *Rarg, *darg, *targ, *carg;

    //opzioni -f -h -p non possono essere ripetute
    hfnd=0;
    ffnd=0;
    pfnd=0;

    //OK GESTIONE ARGOMENTI
    //UNICO ARGOMENTO DA TOKENIZZARE CON TOKEN ','

    while ((opt = getopt(argc,argv,"hpf:w:W:r:R:d:t:c:")) != -1) {
        switch (opt) {

            case 'h':
                hfnd++;
                if (hfnd==1) {
                    if (flag_stampa==1) printf("Operazione : -h (help message)\n");
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
                    flag_stampa=1;
                    printf("Operazione : -p (abilta stampe)\n");
                }else if (pfnd>1) {
                    printf("L'opzione -p non puo' essere ripetuta\n");
                    exit(EXIT_FAILURE);
                }
                break;

            case 'f':
                ffnd++;
                if (ffnd==1) {
                    farg=optarg;
                    if (flag_stampa==1) printf("Operazione : -f (connessione) - file : %s\n",farg);
                }else if (ffnd>1) {
                    printf("L'opzione -f non puo' essere ripetuta\n");
                    exit(EXIT_FAILURE);
                }
                
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME,&ts);
                ts.tv_sec = ts.tv_sec+60;
                APICALL(openConnection(farg,1000,ts),"openConnection");
                break;

            case 'w':
                warg=optarg;
                printf("Opzione -w con argomento %s\n",warg);
                //TODO : ESEGUI COMANDO CON API
                char * save1 = NULL;
                char * token1 = strtok_r(warg,",",&save1);
                
                while(token1) {
                    char * file = token1;
                    //per ogni file passato come argomento esegui open-write-close
                    if (openFile(file,-1)==-1) {
                        perror("openFile");
                    }
                    printf("TOKEN\n");
                    token1 = strtok_r(NULL,",",&save1);

                }
                break;

            case 'W':
                Warg=optarg;
                printf("Opzione -W con argomento %s\n",Warg);
                char * save2 = NULL;
                char * token2 = strtok_r(Warg,",",&save2);
                
                while(token2) {
                    char * file = token2;
                    //per ogni file passato come argomento esegui open-write-close
                    if (openFile(file,1)==-1) {
                        perror("openFile");
                    }
                    printf("TOKEN\n");
                    token2 = strtok_r(NULL,",",&save2);

                }

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
    
    //FINITE LE OPZIONI DA LINEA DI COMANDO CHIUDO LA CONNESSIONE SE ERA APERTA
    sleep(20);
    closeConnection(farg);

    return 0;
}

long isNumber(const char* s) {
   char* e = NULL;
   long val = strtol(s, &e, 0);
   if (e != NULL && *e == (char)0) return val; 
   return -1;
}