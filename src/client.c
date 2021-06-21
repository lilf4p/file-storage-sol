#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include "../includes/api_server.h"
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

#define DIM_MSG 300
// utility macro
#define APICALL(c,e) \
    if(c==-1) { perror(e);exit(EXIT_FAILURE); }

int flag_stampa=0;
static int num_files = 0;
int tms;

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
void freeList (node ** list);

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
    tms=0;

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
                fprintf (stderr,"%s -h per la lista dei comandi\n",argv[0]);
                break;

            case ':':
                printf("l'opzione '-%c' richiede un argomento\n", optopt);
                break;

            default:;
        }

    }

    //printList(listCmd);

    //CONTROLLA SE PRESENTI OPZIONI -h -p -t -d -f
    char * arg=NULL; 
    if (containCMD(&listCmd,"h",&arg)==1) {
        printf("Operazioni supportate : \n-h\n-f filename\n-w dirname[,n=0]\n-W file1[,file2]\n-r file1[,file2]\n-R [n=0]\n-d dirname\n-t time\n-c file1[,file2]\n-p\n");
        freeList(&listCmd);
        if (resolvedPath!=NULL) free(resolvedPath);
        return 0;
    }
    if (containCMD(&listCmd,"p",&arg)==1) {
        flag_stampa=1;
        printf("Operazione : -p (abilta stampe) Esito : positivo\n");
    }
    if (containCMD(&listCmd,"t",&arg)==1) {
        if ((tms=isNumber(targ))==-1) {
            if (flag_stampa==1) printf("Operazione : -t (ritardo) Tempo : %s Esito : negativo\n",targ);
            printf("L'opzione -t richiede un numero come argomento\n");
        }else{
            if (flag_stampa==1) printf("Operazione : -t (ritardo) Tempo : %d Esito : positivo\n",tms);
        }
    }
    if (containCMD(&listCmd,"d",&arg)==1) {
        if (flag_stampa==1) printf("Operazione : -d (salva file) Directory : %s Esito : positivo\n",dir);
        dfnd = 1;
    }
    if (containCMD(&listCmd,"f",&arg)==1) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME,&ts);
        ts.tv_sec = ts.tv_sec+60;
        if (openConnection(farg,1000,ts)==-1) {
            if (flag_stampa==1) printf("Operazione : -f (connessione) File : %s Esito : negativo\n",farg);
            perror("Errore apertura connessione");
        }else{
            if (flag_stampa==1) printf("Operazione : -f (connessione) File : %s Esito : positivo\n",farg);
        } 
    }

    //printList(listCmd);

    //ESEGUI LE OPZIONI RIMANENTI -w -W -r -R -c
    node * curr = listCmd;
    while (curr!=NULL) {
        
        msleep(tms);

        if (strcmp(curr->cmd,"w")==0) {

            char * save1 = NULL;
            char * token1 = strtok_r(curr->arg,",",&save1);
                
            char * namedir = token1;
            int n;

            struct stat info_dir;
            if (stat(namedir,&info_dir)==-1) {
                if (flag_stampa==1) printf("Operazione : -w (scrivi directory) Directory : %s Esito : negativo\n",namedir);
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
                        if (flag_stampa==1) printf("Operazione : -w (scrivi directory) Directory : %s Esito : positivo\n",namedir);
                    }else if (n==0) {
                        listDir(namedir,INT_MAX);
                        if (flag_stampa==1) printf("Operazione : -w (scrivi directory) Directory : %s Esito : positivo\n",namedir);
                    }else{
                        if (flag_stampa==1) printf("Operazione : -w (scrivi directory) Directory : %s Esito : negativo\n",namedir);
                        printf("Utilizzo : -w dirname[,n]\n");
                    }

                }else{
                    if (flag_stampa==1) printf("Operazione : -w (scrivi directory) Directory : %s Esito : negativo\n",namedir);
                    printf("%s non e' una directory\n",namedir);
                }
            }

        }else if (strcmp(curr->cmd,"W")==0) {

            char * save2 = NULL;
            char * token2 = strtok_r(curr->arg,",",&save2);

            while(token2) {
                char * file = token2;
                //per ogni file passato come argomento esegui open-write-close

                if ((resolvedPath = realpath(file,resolvedPath))==NULL) {
                    if (flag_stampa==1) printf("Operazione : -W (scrivi file) File : %s Esito : negativo\n",file);
                    printf("Il file %s non esiste\n",file);
                }else{
                    struct stat info_file;
                    stat(resolvedPath,&info_file);

                    if (S_ISREG(info_file.st_mode)) {

                        if (openFile(resolvedPath,1)==-1) {
                            if (flag_stampa==1) printf("Operazione : -W (scrivi file) File : %s Esito : negativo\n",file);
                            perror("Errore apertura file");
                        } else {

                            //WRITE FILE
                            if (writeFile(resolvedPath,NULL)==-1) {
                                if (flag_stampa==1) printf("Operazione : -W (scrivi file) File : %s Esito : negativo\n",file);
                                perror("Errore scrittura file");
                            } else {  

                                if (closeFile(resolvedPath)==-1) {
                                    if (flag_stampa==1) printf("Operazione : -W (scrivi file) File : %s Esito : negativo\n",file); 
                                    perror("Errore chiusura file");
                                }else{
                                    if (flag_stampa==1) printf("Operazione : -W (scrivi file) File : %s Esito : positivo\n",file); 
                                }

                            }
                        }

                    } else {
                        if (flag_stampa==1) printf("Operazione : -W (scrivi file) File : %s Esito : negativo\n",file);
                        printf("%s non e' un file regolare\n",file);
                    }
                }
                token2 = strtok_r(NULL,",",&save2);

            }


        }else if (strcmp(curr->cmd,"r")==0) {

            char * save3 = NULL;
            char * token3 = strtok_r(curr->arg,",",&save3);
            
            while(token3) {
                char * file = token3;
                //per ogni file passato come argomento esegui open-write-close

                if (openFile(file,0)==-1) {
                    if (flag_stampa==1) printf("Operazione : -r (leggi file) File : %s Esito : negativo\n",file); 
                    perror("Errore apertura file");
                }else{
                    //READ FILE
                    char * buf = NULL;
                    size_t size;
                    if (readFile(file,(void**)&buf,&size)==-1) {
                        if (flag_stampa==1) printf("Operazione : -r (leggi file) File : %s Esito : negativo\n",file); 
                        perror("Errore lettura file");
                    } else {
                        if (dfnd==1) {
                            //SALVA IN DIR
                            char path[PATH_MAX];
                            memset(path,0,PATH_MAX);
                            char * file_name = basename(file);
                            sprintf(path,"%s/%s",dir,file_name);
                            //printf("FILE : %s\n",path);
                            
                            //CREA DIR SE NON ESISTE 
                            //printf("DIRECTORY : %s\n",dir);
                            mkdir_p(dir);
                            //CREA FILE SE NON ESISTE
                            FILE* of;
                            of = fopen(path,"w");
                            if (of==NULL) {
                                printf("Errore salvataggio file\n");
                            } else {
                                fprintf(of,"%s",buf);
                                fclose(of);
                            }
                        }
                        //printf ("FROM SERVER\nSIZE: %ld\nFILE: %s\n",size,buf); 
                        if (closeFile(file)==-1) {
                            if (flag_stampa==1) printf("Operazione : -r (leggi file) File : %s Esito : negativo\n",file); 
                            perror("Errore chiusura file");
                        }else{
                            if (flag_stampa==1) printf("Operazione : -r (leggi file) File : %s Esito : positivo\n",file); 
                        }
                        free(buf);
                    }
                }
                token3 = strtok_r(NULL,",",&save3);
            }
            if (token3!=NULL) free(token3);

        }else if (strcmp(curr->cmd,"R")==0) {

            int val;
            if ((val = isNumber(curr->arg))==-1) {
                if (flag_stampa==1) printf("Operazione : -R (leggi N file) Esito : negativo\n"); 
                printf("L'opzione -R vuole un numero come argomento\n");
            } else {
                int n;
                if ((n=readNFiles(val,dir))==-1) {
                    if (flag_stampa==1) printf("Operazione : -R (leggi N file) Esito : negativo\n"); 
                    perror("Errore lettura file");
                }else{
                    if (flag_stampa==1) printf("Operazione : -R (leggi N file) Esito : positivo File Letti : %d\n",n); 
                }
            }

        } else if (strcmp(curr->cmd,"c")==0) {
            
            char * save4 = NULL;
            char * token4 = strtok_r(curr->arg,",",&save4);
            
            while(token4) {
                char * file = token4;
                
                //per ogni file passato come argomento esegui close
                if (removeFile(file)==-1) {
                    if (flag_stampa==1) printf("Operazione : -c (rimuovi file) File : %s Esito : negativo\n",file);
                    perror("Errore rimozione file");
                }else{
                    if (flag_stampa==1) printf("Operazione : -c (rimuovi file) File : %s Esito : positivo\n",file);
                }
                
                token4 = strtok_r(NULL,",",&save4);
            }

        }

        curr = curr->next;

    }

    freeList(&listCmd);
    free(resolvedPath);
    free(arg);
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

    //printf ("Directory: %s\n",dirname);
	while ((entry = readdir(dir))!=NULL && (num_files < n)) {
        
		char path[PATH_MAX];
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
                perror("realpath");
            }else{
                if (openFile(resolvedPath,1)==-1) perror("Errore apertura file");
                else {
                    num_files++;
                    //WRITE FILE
                    if (writeFile(resolvedPath,NULL)==-1) perror("Errore scrittura file");
                    else {  
                        if (closeFile(resolvedPath)==-1) perror("Errore chiusura file");
                    }
                }
                if (resolvedPath!=NULL) free(resolvedPath);
            }
            msleep(tms);
            
        }



	}

	if ((closedir(dir))==-1) {
		perror("closing directory");
		exit(EXIT_FAILURE);
	}
}

void addLast (node ** list,char * cmd,char * arg) {

    node * new = malloc (sizeof(node));
    if (new==NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    new->cmd = malloc(sizeof(cmd));
    if (new->cmd==NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    strcpy(new->cmd,cmd);
    if (arg!=NULL) {
        new->arg = malloc(PATH_MAX*sizeof(char));
        if (new->arg==NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
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
            char arg [strlen(curr->arg)];
            strcpy(arg,curr->arg);
        } else *arg = NULL;
        if (prec == NULL) {
            node * tmp = *list;
            *list = (*list)->next;
            free(tmp->arg);
            free(tmp->cmd);
            free(tmp);
        }else{
            prec->next = curr->next;
            free(curr->arg);
            free(curr->cmd);
            free(curr);
        }
    }

    return trovato;

}

void freeList (node ** list) {
    node * tmp;
    while (*list!=NULL) {
        tmp = *list;
        free((*list)->arg);
        free((*list)->cmd);
        (*list)=(*list)->next;
        free(tmp);
    }
    
}
