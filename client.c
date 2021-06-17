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
#include <libgen.h>
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

typedef struct node {
    char * cmd;
    char * arg;
    struct node * next;
} node;

long isNumber(const char* s);
void listDir (char * dirname, int n);
void addLast (node ** list,char * cmd, char * arg);
void printList(node * list);
int containCMD (node ** list, char * cmd, char ** arg);

int main (int argc, char * argv[]) {

    char opt;
    int hfnd, ffnd, pfnd, dfnd, tfnd;
    char *farg, *warg, *Warg, *rarg, *Rarg, *targ, *carg;
    
    hfnd=0;
    ffnd=0;
    pfnd=0;
    dfnd=0;
    tfnd=0;
    
    char *dir = NULL;
    int tms=0;

    node * listCmd = NULL;
    char * resolvedPath = NULL;
    

    while ((opt = getopt(argc,argv,"hpf:w:W:r:R:d:t:c:")) != -1) {
        switch (opt) {

            case 'h':
                if (hfnd==0) {
                    hfnd=1;
                    addLast(&listCmd,"h",NULL);
                }else{
                    printf("L'opzione -h non puo' essere ripetuta\n");
                }
                break;

            case 'p':
                if (pfnd==0) {
                    pfnd=1;
                    addLast(&listCmd,"p",NULL);
                }else{
                    printf("L'opzione -p non puo' essere ripetuta\n");
                }
                break;

            case 'f':
                if (ffnd==0) {
                    ffnd=1;
                    farg=optarg;
                    addLast(&listCmd,"f",farg);
                }else{
                    printf("L'opzione -f non puo' essere ripetuta\n");
                }
                break;

            case 'w':
                
                warg=optarg;
                
                addLast(&listCmd,"w",warg);
                
                break;

            case 'W':
                
                Warg = optarg;
                addLast(&listCmd,"W",Warg);
                break;

            case 'r':
                rarg=optarg;
                addLast(&listCmd,"r",rarg);
                break;

            case 'R':
                Rarg=optarg;
                addLast(&listCmd,"R",Rarg);
                break;

            case 'd':
                if (dfnd==0) {
                    dfnd = 1;
                    dir=optarg;
                    addLast(&listCmd,"d",dir);
                }else{
                    printf("L'opzione -d non puo' essere ripetuta\n");
                }
                break;

            case 't':
                if (tfnd==0) { 
                    tfnd=1;
                    targ=optarg;
                    addLast(&listCmd,"t",targ);
                }else{
                    printf("L'opzione -t non puo' essere ripetuta\n");
                }
                break;

            case 'c': 
                carg=optarg;
                addLast(&listCmd,"c",carg);
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

    //printList(listCmd);

    //CONTROLLA SE PRESENTI OPZIONI -h -p -t -d -f
    char * arg; 
    if (containCMD(&listCmd,"h",&arg)==1) {
        if (flag_stampa==1) printf("Operazione : -h (help message)\n");
        printf("Operazioni supportate : \n-h\n-f filename\n-w dirname[n=0]\n-W file1[,file2]\n-r file1[,file2]\n-R [n=0]\n-d dirname\n-t time\n-c file1[,file2]\n-p\n");
    }
    if (containCMD(&listCmd,"p",&arg)==1) {
        flag_stampa=1;
        printf("Operazione : -p (abilta stampe)\n");
    }
    if (containCMD(&listCmd,"t",&arg)==1) {
        int tms=0;
        if ((tms=isNumber(arg))==-1) {
            printf("L'opzione -t vuole un numero\n");
        }else{
            printf("Opzione -t con argomento %d\n",tms);
        }
    }
    if (containCMD(&listCmd,"d",&arg)==1) {
        printf("Opzione -d con argomento %s\n",arg);
        dfnd = 1;
    }
    if (containCMD(&listCmd,"f",&arg)==1) {
        if (flag_stampa==1) printf("Operazione : -f (connessione) - file : %s\n",farg);
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME,&ts);
        ts.tv_sec = ts.tv_sec+60;
        APICALL(openConnection(farg,1000,ts),"openConnection");
    }

    //printList(listCmd);

    //ESEGUI LE OPZIONI RIMANENTI -w -W -r -R -c
    node * curr = listCmd;
    while (curr!=NULL) {
        
        msleep(tms);

        if (strcmp(curr->cmd,"w")==0) {

            printf("Opzione -w con argomento %s\n",curr->arg);
            char * save1 = NULL;
            char * token1 = strtok_r(curr->arg,",",&save1);
                
            char * namedir = token1;
            int n;

            struct stat info_dir;
            if (stat(namedir,&info_dir)==-1) {
                printf("Directory %s non esiste\n",namedir);
            }else{

                if (S_ISDIR(info_dir.st_mode)) {

                    token1 = strtok_r(NULL,",",&save1);
                    if (token1!=NULL) {
                        n = isNumber(token1);
                    } else n=0;

                    if (n>0) {
                        //TODO : SCRIVI I FILE DI NAMEDIR
                        listDir(namedir,n);
                    }else if (n==0) {
                        listDir(namedir,INT_MAX);
                    }else{
                        printf("Utilizzo : -w dirname[,n]\n");
                    }

                }else{
                    printf("%s non e' una directory\n",namedir);
                }
            }

        }else if (strcmp(curr->cmd,"W")==0) {

            printf("Opzione -W con argomento %s\n",curr->arg);
            char * save2 = NULL;
            char * token2 = strtok_r(curr->arg,",",&save2);

            while(token2) {
                char * file = token2;
                //per ogni file passato come argomento esegui open-write-close

                if ((resolvedPath = realpath(file,resolvedPath))==NULL) {
                    printf("Il file %s non esiste\n",file);
                    perror("realpath");
                }else{

                    if (openFile(resolvedPath,1)==-1) perror("openFile");
                    else {

                        //WRITE FILE
                        if (writeFile(resolvedPath,NULL)==-1) perror("writeFile");
                        else {  

                            if (closeFile(resolvedPath)==-1) perror("closeFile");
                        }

                    }
                }

                token2 = strtok_r(NULL,",",&save2);

            }


        }else if (strcmp(curr->cmd,"r")==0) {

            printf("Opzione -r con argomento %s\n",curr->arg);

            char * save3 = NULL;
            char * token3 = strtok_r(curr->arg,",",&save3);
            
            while(token3) {
                char * file = token3;
                //per ogni file passato come argomento esegui open-write-close

                if ((resolvedPath = realpath(file,resolvedPath))==NULL) {
                    printf("Il file %s non esiste\n",file);
                    perror("realpath");
                }else{
                    if (openFile(resolvedPath,0)==-1) {
                        perror("openFile");
                    }else{
                        //READ FILE
                        char * buf;
                        size_t size;
                        if (readFile(resolvedPath,(void**)&buf,&size)==-1) {
                            perror("writeFile");
                        } else {
                            if (dfnd==1) {
                                //SALVA IN DIR
                                char path[PATH_MAX];
                                char * file_name = basename(resolvedPath);
                                sprintf(path,"%s/%s",dir,file_name);
                                printf("FILE : %s\n",path);
                                
                                //CREA DIR SE NON ESISTE 
                                printf("DIRECTORY : %s\n",dir);
                                mkdir_p(dir);
                                //CREA FILE SE NON ESISTE
                                FILE* of;
                                of = fopen(path,"w");
                                if (of==NULL) {
                                    printf("Errore aprendo il file\n");
                                } else {
                                    fprintf(of,"%s",buf);
                                    fclose(of);
                                }
                            }
                            printf ("FROM SERVER\nSIZE: %ld\nFILE: %s\n",size,buf); 
                            if (closeFile(resolvedPath)==-1) {
                                perror("closeFile");
                                break;
                            }
                        }
                    }
                }
                token3 = strtok_r(NULL,",",&save3);
            }
            if (token3!=NULL) free(token3);

        }else if (strcmp(curr->cmd,"R")==0) {

            int val;
            if ((val = isNumber(curr->arg))==-1) printf("L'opzione -R vuole un numero come argomento\n");
            else {
                printf("Opzione -R con argomento %s\n",curr->arg);
                int n;
    
                if ((n=readNFiles(val,dir))==-1) {
                    perror("readNFiles");
                }else{
                    printf("FILE LETTI : %d\n",n);
                }
            }

        } else if (strcmp(curr->cmd,"c")==0) {
            
            printf("Opzione -c con argomento %s\n",curr->arg);
            char * save4 = NULL;
            char * token4 = strtok_r(curr->arg,",",&save4);
            
            while(token4) {
                char * file = token4;
                if ((resolvedPath = realpath(file,resolvedPath))==NULL) {
                    printf("Il file %s non esiste\n",file);
                    perror("realpath");
                }else{
                    //per ogni file passato come argomento esegui close
                    if (removeFile(resolvedPath)==-1) {
                        perror("removeFile");
                    }
                }
                //printf("TOKEN\n");
                token4 = strtok_r(NULL,",",&save4);
            }

        }

        curr = curr->next;

    }

    if (listCmd!=NULL) free(listCmd);
    free(resolvedPath);

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
            char * resolvedPath = NULL;
            if ((resolvedPath = realpath(path,resolvedPath))==NULL) {
                printf("Il file %s non esiste\n",path);
                perror("realpath");
            }else{
                if (openFile(resolvedPath,1)==-1) perror("openFile");
                else {
                    num_files++;
                    //WRITE FILE
                    if (writeFile(resolvedPath,NULL)==-1) perror("writeFile");
                    else {  
                        if (closeFile(resolvedPath)==-1) perror("closeFile");
                    }
                }
                free(resolvedPath);
            }
            
        }



	}

	if ((closedir(dir))==-1) {
		perror("closing directory");
		exit(EXIT_FAILURE);
	}
}

void addLast (node ** list,char * cmd,char * arg) {

    node * new = malloc (sizeof(node));
    new->cmd = malloc(sizeof(cmd));
    strcpy(new->cmd,cmd);
    if (arg!=NULL) {
        new->arg = malloc(sizeof(arg));
        strcpy(new->arg,arg);
    }else new->arg = NULL;
    new->next = NULL;

    node * last = *list;

    if (*list == NULL) {
        *list = new;
        return;
    }

    while (last->next!=NULL) {
        last = last->next;
    }

    last->next = new;
    
    return;

}

void printList (node * list) {
    node * curr = list;
    while (curr!=NULL) {
        printf("CMD=%s ARG=%s \n",curr->cmd,curr->arg);
        curr = curr->next;
    }

}

//RETURN 1 SSE LIST CONTIENE IL COMANDO CMD (LO RIMUOVE DALLA LISTA), 0 ALTRIMENTI -- SE LO TROVA INSERISCE L'ARGOMENTO IN ARG SE PRESENTE, NULL ALTRIMENTI
int containCMD (node ** list, char * cmd, char ** arg) {

    node * curr = *list;
    node * prec = NULL;
    int trovato = 0;

    while (curr!=NULL && trovato==0) {
        if (strcmp(curr->cmd,cmd)==0) trovato=1;
        else {
            prec = curr;
            curr = curr->next;
        }
    }

    if (trovato==1) {
        if (curr->arg!=NULL) {
            *arg = malloc(sizeof(curr->arg));
            strcpy(*arg,curr->arg);
        } else *arg = NULL;
        if (prec == NULL) {
            node * tmp = *list;
            *list = (*list)->next;
            free(tmp);
        }else{
            prec->next = curr->next;
            free(curr);
        }
    }

    return trovato;

}
