#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include "api_server.h"
#include <errno.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <fcntl.h>
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
static int num_files = 0;

long isNumber(const char* s);
void listDir (char * dirname, int n);

int main (int argc, char * argv[]) {

    char opt;
    int hfnd, ffnd, pfnd, dfnd;
    char *farg, *warg=NULL, *Warg, *rarg, *Rarg, *darg, *targ, *carg;

    //opzioni -f -h -p non possono essere ripetute
    hfnd=0;
    ffnd=0;
    pfnd=0;
    dfnd=0;
    
    char *dir = NULL;
    int tms=0;

    //OK GESTIONE ARGOMENTI
    //UNICO ARGOMENTO DA TOKENIZZARE CON TOKEN ','

    //TODO : CERCA A MANO OPZIONI -d -h -t -p

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
                char * save1 = NULL;
                char * token1 = strtok_r(warg,",",&save1);
                
                char * dirname = token1;
                int n;

                token1 = strtok_r(NULL,",",&save1);
                if (token1!=NULL) {
                    n = isNumber(token1);
                } else n=0;

                if (n>0) {
                    //TODO : SCRIVI I FILE DI DIRNAME
                    listDir(dirname,n);
                }else if (n==0) {
                    listDir(dirname,INT_MAX);
                }else{
                    printf("Utilizzo : -w dirname[,n]\n");
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

                    if (openFile(file,1)==-1) perror("openFile");
                    else {

                        //WRITE FILE
                        if (writeFile(file,NULL)==-1) perror("writeFile");
                        else {  

                            if (closeFile(file)==-1) perror("closeFile");
                        }

                    }

                    token2 = strtok_r(NULL,",",&save2);

                }

                break;

            case 'r':
                rarg=optarg;
                printf("Opzione -r con argomento %s\n",rarg);

                char * save3 = NULL;
                char * token3 = strtok_r(rarg,",",&save3);
                
                while(token3) {
                    char * file = token3;
                    //per ogni file passato come argomento esegui open-write-close

                    if (openFile(file,0)==-1) {
                        perror("openFile");
                    }else{

                        //READ FILE
                        char * buf;
                        size_t size;
                        if (readFile(file,(void**)&buf,&size)==-1) {
                            perror("writeFile");
                        } else {
                            if (dfnd==1) {
                                //TODO : SALVA IN DIR
                                char * path = malloc(100*sizeof(char));
                                sprintf(path,"/%s/%s",dir,file);
                                printf("%s\n",path);
                                FILE* of;
                                of = fopen(path,"w");
                                if (of==NULL) {
                                    printf("Errore aprendo il file\n");
                                } else {
                                    fprintf(of,"%s",buf);
                                    fclose(of);
                                    free(path);
                                }
                            }
                            printf ("FROM SERVER\nSIZE: %ld\nFILE: %s\n",size,buf); 

                            if (closeFile(file)==-1) {
                                perror("closeFile");
                                break;
                            }
                        }

                    }

                    token3 = strtok_r(NULL,",",&save3);

                }


                break;

            case 'R':
                Rarg=optarg;
                int val;
                if ((val = isNumber(Rarg))==-1) printf("L'opzione -R vuole un numero come argomento\n");
                else {
                    printf("Opzione -R con argomento %s\n",Rarg);
                    int n;
                    if ((n=readNFiles(val,dir))==-1) {
                        perror("readNFiles");
                    }else{
                        printf("FILE LETTI : %d\n",n);
                    }
                }

                break;

            case 'd':
                dir=optarg;
                printf("Opzione -d con argomento %s\n",dir);
                dfnd = 1;
                break;

            case 't': 
                targ=optarg;
                if ((tms=isNumber(targ))==-1) {
                    printf("L'opzione -t vuole un numero\n");
                }else{
                    printf("Opzione -t con argomento %s\n",targ);
                }
                break;

            case 'c': 
                carg=optarg;
                printf("Opzione -c con argomento %s\n",carg);
                char * save4 = NULL;
                char * token4 = strtok_r(carg,",",&save4);
                
                while(token4) {
                    char * file = token4;
                    //per ogni file passato come argomento esegui open-write-close
                    if (removeFile(file)==-1) {
                        perror("removeFile");
                    }
                    //printf("TOKEN\n");
                    token4 = strtok_r(NULL,",",&save4);

                }
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

        msleep(tms);
    }
    
    //FINITE LE OPZIONI DA LINEA DI COMANDO CHIUDO LA CONNESSIONE SE ERA APERTA
    closeConnection(farg);

    return 0;
}

long isNumber(const char* s) {
   char* e = NULL;
   long val = strtol(s, &e, 0);
   if (e != NULL && *e == (char)0) return val; 
   return -1;
}

void listDir (char * dirname, int n) {

	DIR * dir;
	struct dirent* entry;

	if ((dir=opendir(dirname))==NULL || num_files == n) {
		return;
	}

    printf ("Directory: %s\n",dirname);
	while ((entry = readdir(dir))!=NULL && (num_files < n)) {
        
		char path[100];
		snprintf(path, sizeof(path), "%s/%s", dirname, entry->d_name); 

        struct stat info;
        if (stat(path,&info)==-1) {
		perror("stat");
		exit(EXIT_FAILURE);
	    }

		//SE FILE E' UNA DIRECTORY 
		if (S_ISDIR(info.st_mode)) {
			if (strcmp(entry->d_name,".")==0 || strcmp(entry->d_name,"..")==0) continue;
			listDir(path,n);
		} else {
            //STAMPO INFO FILE
            //printf ("\n%s\t%ld\t",path,info.st_size);
            //E' UN FILE --> OPEN,WRITE,CLOSE
            if (openFile(path,1)==-1) perror("openFile");
            else {
                num_files++;
                //WRITE FILE
                if (writeFile(path,NULL)==-1) perror("writeFile");
                else {  
                    if (closeFile(path)==-1) perror("closeFile");
                }
            }
            
        }



	}

	if ((closedir(dir))==-1) {
		perror("closing directory");
		exit(EXIT_FAILURE);
	}
}

