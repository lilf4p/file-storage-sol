#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <ctype.h>
#include <assert.h>
#include <limits.h>
#include <math.h>

#define UNIX_PATH_MAX 108 /* man 7 unix */
#define DIM_MSG 100 
// utility syscall socket
#define SYSCALL_SOCKET(s,c,e) \
    if (c==s) { \
        perror(e); \
        int fine=0; \
        write(pfd,&cfd,sizeof(cfd)); \
        write(pfd,&fine,sizeof(fine)); \
        return; \
    }
// utility macro pthread
#define SYSCALL_PTHREAD(e,c,s) \
    if((e=c)!=0) { errno=e;perror(s);fflush(stdout);exit(EXIT_FAILURE); }
// utility exit
#define SYSCALL_EXIT(c,e) \
    if(c==-1) { perror(e);exit(EXIT_FAILURE); }
// utility break;
#define SYSCALL_BREAK(c,e) \
    if(c==-1) { perror(e);break; }



typedef struct node {
    int data;
    struct node  * next;
} node;

typedef struct file {
    char path[PATH_MAX];
    char * data;
    node * client_open;
    int client_write; //FILE DESCRIPTOR DEL CLIENT CHE HA ESEGUITO COME ULTIMA OPERAZIONE UNA openFile con flag O_CREATE
    //info utili per ogni file
    struct file * next; 
} file;


//TODO : STRUTTURA DATI PER SALVARE I FILE
file * cache_file = NULL;
pthread_mutex_t lock_cache = PTHREAD_MUTEX_INITIALIZER;
int dim_byte; //DIMENSIONE CACHE IN BYTE
int num_files; //NUMERO FILE IN CACHE

//LIMITI DI MEMORIA
int max_dim;
int max_files;  

//x statistiche finali
int top_file=0;
int top_dim=0;
int resize=0;

//CODA DI COMUNICAZIONE MANAGER --> WORKERS / RISORSA CONDIVISA / CODA FIFO
node * coda = NULL;  
pthread_mutex_t lock_coda = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t not_empty = PTHREAD_COND_INITIALIZER;

volatile sig_atomic_t term = 0; //FLAG SETTATO DAL GESTORE DEI SEGNALI DI TERMINAZIONE

void * worker (void * arg);

void insertNode (node ** list, int data);
int removeNode (node ** list);
int updatemax(fd_set set, int fdmax);
static void gestore_term (int signum);

void eseguiRichiesta (char * request, int cfd,int pfd);

int addFile (char * path, int flags, int cfd);
char * getFile (char * path, int cfd);
int removeFile (char * path);
int removeClient (char * path, int cfd);
int writeData(char * path, char * data, int size, int cfd);//SCRIVE I DATI NEL FILE SSE CLIENT_WRITE == CFD
int sizeList ();
void printFile ();
int containFile (char * path); //1 se lo contiene, 0 se non lo contiene
void printClient (node * list); //STAMPA LISTA DI CLIENT (FILE DESCRIPTOR SOCKET)
int fileOpen(node * list, int cfd);
int appendData (char * path, char * data, int size, int cfd);//SCRIVE I DATI NEL FILE SSE CLIENT_OPEN CONTIENE CFD
int resize_cache (int dim_data); //POLITICA DI SOSTITUZIONE IN CACHE FIFO 
long isNumber(const char* s);
void freeList(node ** head);
void freeCache();

int main (int argc, char * argv[]) {

    int i;

    char * path_config = "config/config.txt";
    if (argc==3) {
        if (strcmp(argv[1],"-s")==0) {
            path_config = argv[2];
        }
    }

    //VALORI DI DEFAULT
    int num_thread = 1;
    max_files = 100; 
    max_dim = 50;   
    char * socket_name = "./tmp/LSOfilestorage.sk";

    //parsing file config.txt -- attributo=valore -- se trovo errore uso attributi di default 
    char str [200];
    FILE * fp;
    fp = fopen(path_config,"r");
    if (fp == NULL) {
        perror("Errore config file");
        exit(EXIT_FAILURE);
    }
    
    char arg [100];
    char val [100];
    while (fgets(str,200,fp)!=NULL) {
        
        if (str[0]!='\n') {

            int nt;
            nt = sscanf (str,"%[^=]=%s",arg,val);
            if (nt!=2) {
                printf("Errore config file : formato non corretto\n");
                printf ("Server avviato con parametri di DEFAULT\n");
                break;
            }

            if (arg!=NULL) {
                if (strcmp(arg,"num_thread")==0) {
                   
                    if (val!=NULL) { 
                        int n;
                        if ((n=isNumber(val))==-1 || n<=0) {
                            printf("Errore config file : num_thread richiede un numero>0 come valore\n");
                            printf ("Server avviato con parametri di DEFAULT\n");
                            break;
                        }else{
                            num_thread = n;
                        }
                    }
                } else if (strcmp(arg,"max_files")==0) {
                    
                    if (val!=NULL) { 
                        int n;
                        if ((n=isNumber(val))==-1 || n<=0) {
                            printf("Errore config file : max_files richiede un numero>0 come valore\n");
                            printf ("Server avviato con parametri di DEFAULT\n");
                            break;
                        }else{
                            max_files = n;
                        }
                    }
                } else if (strcmp(arg,"max_dim")==0) {
                    
                    if (val!=NULL) { 
                        int n;
                        if ((n=isNumber(val))==-1 || n<=0) {
                            printf("Errore config file : max_dim richiede un numero>0 come valore\n");
                            printf ("Server avviato con parametri di DEFAULT\n");
                            break;
                        }else{
                            max_dim = n;
                        }
                    }
                } else if (strcmp(arg,"socket_name")==0) {
                    
                    if (val!=NULL) {
                        strcpy(socket_name,val);
                    }
                }
            } 
            
        }
        
    }
    fclose(fp);

    printf("socket_name:%s num_thread:%d max_files:%d max_dim:%d\n",socket_name,num_thread,max_files,max_dim); 

    //--------GESTIONE SEGNALI---------//
    struct sigaction s;
    sigset_t sigset;
    SYSCALL_EXIT(sigfillset(&sigset),"sigfillset");
    SYSCALL_EXIT(pthread_sigmask(SIG_SETMASK,&sigset,NULL),"pthread_sigmask");
    memset(&s,0,sizeof(s));
    s.sa_handler = gestore_term;

    SYSCALL_EXIT(sigaction(SIGINT,&s,NULL),"sigaction");
    SYSCALL_EXIT(sigaction(SIGQUIT,&s,NULL),"sigaction");
    SYSCALL_EXIT(sigaction(SIGHUP,&s,NULL),"sigaction"); //TERMINAZIONE SOFT

    //ignoro SIGPIPE
    s.sa_handler = SIG_IGN;
    SYSCALL_EXIT(sigaction(SIGPIPE,&s,NULL),"sigaction");

    SYSCALL_EXIT(sigemptyset(&sigset),"sigemptyset");
    int e;
    SYSCALL_PTHREAD(e,pthread_sigmask(SIG_SETMASK,&sigset,NULL),"pthread_sigmask");
    //-----------------------------------------//

    //CREO PIPE
    int pip[2]; //COMUNICAZIONE WORKERS --> MANAGER  
    SYSCALL_EXIT(pipe(pip),"Create pipe");

    //------THREAD POOL--------//
    pthread_t * thread_pool = malloc(num_thread*sizeof(pthread_t));
    if (thread_pool==NULL) {
        perror("Errore creazione thread pool");
        exit(EXIT_FAILURE);    
    }
    int err;
    for (i=0;i<num_thread;i++) {
        SYSCALL_PTHREAD(err,pthread_create(&thread_pool[i],NULL,worker,(void*)(&pip[1])),"Errore creazione thread pool");
    }
    //------------------------//

    int fd;
    int num_fd = 0;
    fd_set set;
    fd_set rdset;
    //PER LA TERMINAZIONE SOFT --> con SIGHUP aspetta che tutti i client si disconnettano
    int num_client = 0; 
    int soft_term = 0; 

    struct sockaddr_un sa;
    strncpy(sa.sun_path,socket_name,UNIX_PATH_MAX);
    sa.sun_family=AF_UNIX;
    int sfd;
    if ((sfd = socket(AF_UNIX,SOCK_STREAM,0)) == -1) {
        perror("Errore creazione socket");
        exit(EXIT_FAILURE);
    }
    unlink(socket_name);
    SYSCALL_EXIT(bind(sfd,(struct sockaddr *)&sa,sizeof(sa)),"Bind");
    SYSCALL_EXIT(listen(sfd,SOMAXCONN),"Listen");

    //MANTENGO IL MAX INDICE DI DESCRITTORE ATTIVO IN NUM_FD
    if (sfd > num_fd) num_fd = sfd;
    //REGISTRO IL WELCOME SOCKET
    FD_ZERO(&set);
    FD_SET(sfd,&set);
    //REGISTRO LA PIPE
    if (pip[0] > num_fd) num_fd = pip[0]; 
    FD_SET(pip[0],&set);

    printf("Listen for clients...\n");
    while (1) {
        rdset = set;
        if (select(num_fd+1,&rdset,NULL,NULL,NULL) == -1) {
            if (term==1) break;
            else if (term==2) { 
                if (num_client==0) break;
                else {
                    printf("Chiusura Soft...\n");
                    FD_CLR(sfd,&set);
                    if (sfd == num_fd) num_fd = updatemax(set,num_fd);
                    close(sfd);
                    rdset = set;
                    SYSCALL_BREAK(select(num_fd+1,&rdset,NULL,NULL,NULL),"Errore select"); 
                }
            }else {
                perror("select");
                break;
            }
        }

        int cfd;
        for (fd=0;fd<= num_fd;fd++) {
            if (FD_ISSET(fd,&rdset)) {
                if (fd == sfd) { //WELCOME SOCKET PRONTO X ACCEPT
                    if ((cfd = accept(sfd,NULL,0)) == -1) {
                        if (term==1) break;
                        else if (term==2) {
                            if (num_client==0) break;
                        }else {
                            perror("Errore accept client");
                        }
                    }
                    FD_SET(cfd,&set);
                    if (cfd > num_fd) num_fd = cfd;
                    num_client++;
                    printf ("Connection accepted from client!\n");
                    if (write(cfd,"Welcome to lilf4p server!",26)==-1) {
                        perror("Errore write welcome message");
                        FD_CLR(cfd,&set);
                        if (cfd == num_fd) num_fd = updatemax(set,num_fd);
                        close(cfd);
                        num_client--;
                    }

                } else if (fd == pip[0]) { //CLIENT DA REINSERIRE NEL SET -- PIPE PRONTA IN LETTURA
                    int cfd1;
                    int len;
                    int flag;
                    if ((len = read(pip[0],&cfd1,sizeof(cfd1))) > 0) { //LEGGO UN INTERO == 4 BYTES
                        //printf ("Master : client %d ritornato\n",cfd1);
                        SYSCALL_EXIT(read(pip[0],&flag,sizeof(flag)),"Master : read pipe");
                        if (flag == -1) { //CLIENT TERMINATO LO RIMUOVO DAL SET DELLA SELECT
                            printf("Closing connection with client...\n");
                            FD_CLR(cfd1,&set);
                            if (cfd1 == num_fd) num_fd = updatemax(set,num_fd);
                            close(cfd1);
                            num_client--;
                            if (term==2 && num_client==0) {
                                printf("Chiusura soft\n");
                                soft_term=1;
                            }
                        }else{
                            FD_SET(cfd1,&set);
                            if (cfd1 > num_fd) num_fd = cfd1;
                        }
                    }else if (len == -1){ 
                        perror("Master : read pipe");
                        exit(EXIT_FAILURE);
                    }

                } else { //SOCKET CLIENT PRONTO X READ 
                    //printf("Master : Client pronto in read\n");
                    //QUINDI INSERISCO FD SOCKET CLIENT NELLA CODA
                    insertNode(&coda,fd);
                    FD_CLR(fd,&set);  
                }
            }
        }
        if (soft_term==1) break;
    } 

    //TODO : STAMPA STATISTICHE RIASSUNTE  

    printf("Closing server...\n");
    for (int i=0;i<num_thread;i++) {
        insertNode(&coda,-1);
    }
    for (i=0;i<num_thread;i++) {
        SYSCALL_PTHREAD(e,pthread_join(thread_pool[i],NULL),"Errore join thread");
    }

    printf("---------STATISTICHE SERVER----------\n");
    printf("Numero di file massimo = %d\n",top_file);
    printf("Dimensione massima = %f Mbytes\n",(top_dim/pow(10,6)));
    printf("Chiamate algoritmo di rimpiazzamento cache = %d\n",resize);
    printFile();
    printf("-------------------------------------\n");

    if (close(sfd)==-1) perror("close");
    remove("/tmp/mysock");
    freeList(&coda);
    free(thread_pool);
    freeCache();

    return 0;
}

//GESTISCE SOLO UNA RICHIESTA DI UNO DEI CLIENT CONNESSI 
//PRELEVA UN CLIENT DALLA CODA -> SERVE UNA SOLA RICHIESTA -> LO METTE NELLA PIPE -> NE PRENDE UN ALTRO 
void * worker (void * arg) {

    int pfd = *((int *)arg);
    int cfd;
    while (1) {
        char request [DIM_MSG];
        request[0] = '\0';
        //PRELEVA UN CLIENT DALLA CODA
        cfd = removeNode(&coda);
        if (cfd==-1) {
            break;
        }
        //SERVO IL CLIENT
        int len;
        int fine; //FLAG COMUNICATO AL MASTER PER SAPERE QUANDO TERMINA IL CLIENT
        if ((len = read(cfd,request,DIM_MSG)) == 0) {  
            fine = -1;
            SYSCALL_EXIT(write(pfd,&cfd,sizeof(cfd)),"THREAD : pipe write");
            SYSCALL_EXIT(write(pfd,&fine,sizeof(fine)),"THREAD : pipe write");
        }else if (len == -1) {
            perror("THREAD : READ 0");
        }else{
            
            //RICHIESTA RICEVUTA DAL CLIENT 
            //printf ("From Client : %s\n",request);
            //fflush(stdout);
            eseguiRichiesta(request,cfd,pfd);
    
        }
    }
    close(cfd);
    printf("Closing worker\n");
    fflush(stdout);
    return 0;

}

//REQUEST CONTIENE : COMANDO,ARGOMENTI SEPARATI DA VIRGOLA
void eseguiRichiesta (char * request, int cfd, int pfd) {
    char response[DIM_MSG];
    memset(response,0,DIM_MSG);
    char * token;
    token = strtok(request,",");
    
    if (token!=NULL) {

        if (strcmp(token,"openFile")==0) {
            //openFile,pathname,flags

            //ARGOMENTI 
            token = strtok(NULL,",");
            char path[PATH_MAX];
            strcpy(path,token);
            token = strtok(NULL,",");
            int flag = atoi(token);

            //ELABORA COMANDO
            int res;
            if ((res=addFile(path,flag,cfd)) == -1) {
                sprintf(response,"-1,%d",ENOENT);
            } else if (res == -2) {
                sprintf(response,"-1,%d",EEXIST);
            }else{
                sprintf(response,"0");
            }
            SYSCALL_SOCKET(-1,write(cfd,response,sizeof(response)),"THREAD : socket write");
            //printf("NUMERO FILE : %d\n",sizeList());
            //printFile();

        }else if (strcmp(token,"closeFile")==0) {
            //closeFile,pathname

            //ARGOMENTI
            token = strtok(NULL,",");
            char * path = token;
            int res;
            if ((res=removeClient(path,cfd)) == -1) {
                sprintf(response,"-1,%d",ENOENT);
            }else if (res==-2) {
                sprintf(response,"-1,%d",EPERM);
            }else{
                sprintf(response,"0");
            }
            SYSCALL_SOCKET(-1,write(cfd,response,sizeof(response)),"THREAD : socket write");
            //printf("NUMERO FILE : %d\n",sizeList());
            //printFile();

        }else if (strcmp(token,"removeFile")==0) {
            //removeFile,pathname

            //ARGOMENTI
            token = strtok(NULL,",");
            char * path = token;

            if (removeFile(path)==-1) {
                sprintf(response,"-1,%d",ENOENT);
            }else{
                sprintf(response,"0");
            }
            SYSCALL_SOCKET(-1,write(cfd,response,sizeof(response)),"THREAD : socket write");
            //printf("NUMERO FILE : %d\n",sizeList());
            //printFile();

        }else if (strcmp(token,"writeFile")==0) {
            //writeFile,pathname
            
            //ARGOMENTI
            token = strtok(NULL,",");
            char path[PATH_MAX];
            strcpy(path,token);

            //INVIA AL CLIENT PERMESSO PER INVIARE FILE
            sprintf(response,"0");
            SYSCALL_SOCKET(-1,write(cfd,response,sizeof(response)),"writeFile : socket write2");
                
            //RICEVO DAL CLIENT SIZE FILE 
            char buf1 [DIM_MSG];
            memset(buf1,0,DIM_MSG);
            SYSCALL_SOCKET(0,read(cfd,buf1,sizeof(buf1)),"writeFile : socket read2");
            //printf("FROM CLIENT SIZE : %s\n",buf1);
            //fflush(stdout);
            int size_file = atoi(buf1);

            //INVIO CONFERMA AL CLIENT
            SYSCALL_SOCKET(-1,write(cfd,"0",2),"writeFile : conferma al client");

            //E IL FILE
            char buf2 [size_file+1];
            memset(buf2,0,size_file+1);
            SYSCALL_SOCKET(0,read(cfd,buf2,(size_file+1)),"writeFile : socket read3");
            //printf("FROM CLIENT FILE : %s\n",buf2);
            //fflush(stdout);
            //INSERISCO I DATI NELLA CACHE 
            char result [DIM_MSG];
            memset(result,0,DIM_MSG);
            int res;
            if ((res=writeData(path, buf2, size_file+1, cfd)) == -1) {
                sprintf(result,"-1,%d",ENOENT);
            } else if (res == -2) {
                sprintf(result,"-1,%d",EPERM);
            } else if (res == -3) { 
                sprintf(result,"-1,%d",EFBIG);
                //removeFile(path);
            } else {
                sprintf(result,"0");
            }
            SYSCALL_SOCKET(-1,write(cfd,result,sizeof(result)),"writeFile : socket write3");

        } else if (strcmp(token,"appendToFile")==0) {
            //appendToFile,pathname,buf,size

            //ARGOMENTI
            token = strtok(NULL,",");
            char * path = token;

            //INVIA AL CLIENT PERMESSO PER INVIARE FILE O ERRORE
            sprintf(response,"0");
            SYSCALL_SOCKET(-1,write(cfd,response,sizeof(response)),"appendToFile : socket write2");
                
            //RICEVO DAL CLIENT SIZE FILE 
            char buf1 [DIM_MSG];
            SYSCALL_SOCKET(0,read(cfd,buf1,sizeof(buf1)),"appendToFile : socket read2");
            //printf("FROM CLIENT SIZE : %s\n",buf1);
            int size_file = atoi(buf1);

            //INVIA CONFERMA AL CLIENT
            SYSCALL_SOCKET(-1,write(cfd,"0",2),"writeFile : conferma al client");

            //E IL FILE
            char buf2 [size_file+1];
            SYSCALL_SOCKET(0,read(cfd,buf2,(size_file+1)),"appendToFile : socket read3");
            //printf("FROM CLIENT FILE : %s\n",buf2);
            //fflush(stdout);

            //TODO: AGGIUNGO I DATI AL FILE IN CACHE
            char result [DIM_MSG];
            int res;
            if ((res=appendData(path,buf2,size_file,cfd))==-1) {
                sprintf(result,"-1,%d",ENOENT);
            } else if (res == -2) {
                sprintf(result,"-1,%d",EPERM);
            } else if (res == -3) {
                sprintf(result,"-1,%d",EFBIG);
            }else{
                sprintf(result,"0");
            }
            SYSCALL_SOCKET(-1,write(cfd,result,sizeof(result)),"appendToFile : socket write3");

        } else if (strcmp(token,"readFile")==0) {
            //readFile,path

            //ARGOMENTI
            token = strtok(NULL,",");
            char * path = token;

            char * fb = getFile(path,cfd);
            //printf("%s\n",fb);
            //fflush(stdout);

            char buf [DIM_MSG];
            memset(buf,0,DIM_MSG); 
            if (fb==NULL) {
                //INVIO ERRORE
                sprintf(buf,"-1,%d",EPERM);
                SYSCALL_SOCKET(-1,write(cfd,buf,sizeof(buf)),"readFile : socket write2");
            }else{
                //INVIO SIZE FILE
                sprintf(buf,"%ld",strlen(fb));
                SYSCALL_SOCKET(-1,write(cfd,buf,sizeof(buf)),"readFile : socket write3");

                char buf1 [DIM_MSG];
                memset(buf1,0,DIM_MSG);
                SYSCALL_SOCKET(0,read(cfd,buf1,sizeof(buf1)),"readFile :  socket read2");
                //printf("FROM CLIENT : %s",buf1);
                fflush(stdout);
                if (strcmp(buf1,"0")==0) {
                    //INVIO FILE 
                    SYSCALL_SOCKET(-1,write(cfd,fb,strlen(fb)),"readFile : socket write4");
                }
            }

        } else if (strcmp(token,"readNFiles")==0) {
            //readNFiles,N
            int e;

            //ARGOMENTI
            token = strtok(NULL,",");
            int n_richiesti = atoi(token);
            //printf ("RICHIESTI=%d ESISTENTI=%d\n",n_richiesti,num_files);

            SYSCALL_PTHREAD(e,pthread_mutex_lock(&lock_cache),"LOCK IN READNFILES");

            //INVIO NUMERO DI FILE AL CLIENT
            if (n_richiesti <= 0 || n_richiesti > num_files) n_richiesti=num_files;
            //printf("RICHIESTI=%d DISPONIBILI=%d\n",n_richiesti,num_files);
            char nbuf[DIM_MSG];
            memset(nbuf,0,DIM_MSG);
            sprintf(nbuf,"%d",n_richiesti);
            SYSCALL_SOCKET(-1,write(cfd,nbuf,sizeof(nbuf)),"readNFiles : write1");
            
            //RICEVO LA CONFERMA DEL CLIENT
            char conf0 [DIM_MSG];
            memset(conf0,0,DIM_MSG);
            SYSCALL_SOCKET(0,read(cfd,conf0,sizeof(conf0)),"readNFiles : read2");
            //printf("From client : %s\n",conf0);
            
            
            //INVIO GLI N FILES
            file * curr = cache_file;
            for (int i=0;i<n_richiesti;i++) {
                
                //INVIA PATH AL CLIENT
                char path [PATH_MAX];
                memset(path,0,PATH_MAX);
                //ERR MALLOC
                sprintf(path,"%s",curr->path);
                SYSCALL_SOCKET(-1,write(cfd,path,sizeof(path)),"readNFiles : write2");

                //RICEVO LA CONFERMA DEL CLIENT
                char conf1 [DIM_MSG];
                memset(conf1,0,DIM_MSG);
                SYSCALL_SOCKET(0,read(cfd,conf1,sizeof(conf1)),"readNFiles : read3");
                //printf("From client : %s\n",conf1);

                //INVIO SIZE 
                char ssize [DIM_MSG];
                memset(ssize,0,DIM_MSG);
                sprintf(ssize,"%ld",strlen(curr->data));
                SYSCALL_SOCKET(-1,write(cfd,ssize,sizeof(ssize)),"readNFiles : write3");

                //RICEVO LA CONFERMA DEL CLIENT
                char conf2 [DIM_MSG];
                memset(conf2,0,DIM_MSG);
                SYSCALL_SOCKET(0,read(cfd,conf2,DIM_MSG),"readNFiles : read4");
                //printf("From client : %s\n",conf2);

                //INVIA FILE
                SYSCALL_SOCKET(-1,write(cfd,curr->data,strlen(curr->data)),"readNFiles : write4");

                curr = curr->next;
            }
            pthread_mutex_unlock(&lock_cache);
        
        } else {
            //ENOSYS
            sprintf(response,"-1,%d",ENOSYS);
            SYSCALL_SOCKET(-1,write(cfd,response,sizeof(response)),"THREAD : socket write");
        }

        //RITORNA IL CLIENT AL MANAGER TRAMITE LA PIPE
        SYSCALL_EXIT(write(pfd,&cfd,sizeof(cfd)),"THREAD : pipe write");
        int fine=0;
        SYSCALL_EXIT(write(pfd,&fine,sizeof(fine)),"THREAD : pipe write");

    } else {
        SYSCALL_EXIT(write(pfd,&cfd,sizeof(cfd)),"THREAD : pipe write");
        int fine=-1;
        SYSCALL_EXIT(write(pfd,&fine,sizeof(fine)),"THREAD : pipe write");
    }

}

//--------UTILITY PER GESTIONE SERVER----------//

//INSERIMENTO IN TESTA
void insertNode (node ** list, int data) {
    //printf("Inserisco in coda\n");
    //fflush(stdout);
    int err;
    //PRENDO LOCK
    SYSCALL_PTHREAD(err,pthread_mutex_lock(&lock_coda),"Lock coda");
    node * new = malloc (sizeof(node));
    if (new==NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    new->data = data;
    new->next = *list;

    //INSERISCI IN TESTA
    *list = new;
    //INVIO SIGNAL
    SYSCALL_PTHREAD(err,pthread_cond_signal(&not_empty),"Signal coda");
    //RILASCIO LOCK
    pthread_mutex_unlock(&lock_coda);
    
}

//RIMOZIONE IN CODA 
int removeNode (node ** list) {
    int err;
    //PRENDO LOCK
    SYSCALL_PTHREAD(err,pthread_mutex_lock(&lock_coda),"Lock coda");
    //ASPETTO CONDIZIONE VERIFICATA 
    while (coda==NULL) {
        pthread_cond_wait(&not_empty,&lock_coda);
        //printf("Consumatore Svegliato\n");
        //fflush(stdout);
    }
    int data;
    node * curr = *list;
    node * prev = NULL;
    while (curr->next != NULL) {
        prev = curr;
        curr = curr->next;
    }
    data = curr->data;
    //LO RIMUOVO
    if (prev == NULL) {
        free(curr);
        *list = NULL;
    }else{
        prev->next = NULL;
        free(curr);
    }
    //RILASCIO LOCK
    pthread_mutex_unlock(&lock_coda);
    return data;
}

//Funzione di utility per la gestione della select
//ritorno l'indice massimo tra i descrittori attivi
int updatemax(fd_set set, int fdmax) {
    for(int i=(fdmax-1);i>=0;--i)
	if (FD_ISSET(i, &set)) return i;
    assert(1==0);
    return -1;
}

//SIGINT,SIGQUIT --> TERMINA SUBITO (GENERA STATISTICHE)
//SIGHUP --> NON ACCETTA NUOVI CLIENT, ASPETTA CHE I CLIENT COLLEGATI CHIUDANO CONNESSIONE 
static void gestore_term (int signum) {
    if (signum==SIGINT || signum==SIGQUIT) {
        term = 1;
    } else if (signum==SIGHUP) {
        //TODO : gestisci terminazione soft 
        term = 2;
    } 
}

//--------------------------------------------------//

//-------------UTILITY PER GESTIONE CACHE FILE-------------//

//INSERISCO IN TESTA --> I FILE INSERITI PRIMA (DA PIU' TEMPO IN CACHE) SARANNO IN FONDO 
//RETURN 0 SE SUCCESSO, -1 SE FALLITA APERTURA FILE, -2 SE FALLITA CRTEAZIONE FILE 
int addFile (char * path, int flag, int cfd) {
    
    int res=0;
    int err;

    SYSCALL_PTHREAD(err,pthread_mutex_lock(&lock_cache),"Lock Cache");

    file ** list = &cache_file;

    int trovato = 0;
    file * curr = cache_file;
    while (curr!=NULL && trovato==0) {
        if ((strcmp(path,curr->path) == 0)) trovato=1;
        else curr = curr->next;
    }

    if (flag==1 && trovato==0) { //CREO IL FILE -- LO INSERISCO IN TESTA 
        
        //CONTROLLA SE > MAX_FILES 
        if (num_files+1 > max_files) {
            //ALGORITMO RIMOZIONE CACHE
            file * tmp = *list;
            
            if (tmp==NULL) {
                res=-1;
            } else if (tmp->next == NULL) {
                *list = NULL;
                free(tmp);
                num_files--;
                resize++;
            } else {

                file * prec = NULL;
                while (tmp->next != NULL) {
                    prec = tmp;
                    tmp = tmp->next;
                } 
                prec->next = NULL;
                free(tmp->data);
                freeList(&(tmp->client_open));
                free(tmp);
                num_files--;
                resize++;
            }
        }
        if (res==0) {
            //printf("ADDFILE : CREO FILE\n");
            fflush(stdout);
            file * f = malloc(sizeof(file));
            if (f==NULL) {
                perror("malloc");
                exit(EXIT_FAILURE);
            }
            strcpy(f->path,path);
            f->data = NULL;
            f->client_write = cfd;
            f->client_open = NULL;
            node * new = malloc (sizeof(node));
            if (new==NULL) {
                perror("malloc");
                exit(EXIT_FAILURE);
            }
            new->data = cfd;
            new->next = f->client_open;
            f->client_open = new;
            f->next = *list;
            *list =  f;
            num_files++;
            if (num_files>top_file) top_file=num_files;
        }
    }else if (flag==0 && trovato==1) { //APRO IL FILE PER CFD
        //printf("ADDFILE : APRO FILE\n");
        //TODO : INSERIRE SSE LA LISTA CLIENT_OPEN NON CONTINE GIA' IL CFD -- CONTROLLO DUPLICATI
        node * new = malloc (sizeof(node));
        if (new==NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        new->data = cfd;
        new->next = curr->client_open;
        curr->client_open = new;
    }else {
        
        if (flag==0 && trovato==0) res=-1; //FILE NON ESISTE NON PUO' ESSERE APERTO
        else if (flag==1 && trovato==1) res=-2; //FILE ESISTE NON PUO' ESSERE CREATO
        //DEBUG
        //printf("ADDFILE : ERRORE\n");
        //printf("PATH : %s\n",path);
        //printf("trovato = %d\n",trovato);
        //printf("flag = %d\n",flag);
        //fflush(stdout);
    }

    pthread_mutex_unlock(&lock_cache);

    return res;

}

//RECUPERA UN FILE DALLA LISTA SE POSSIBILE, NULL ALTRIMENTI
char * getFile (char * path, int cfd) {
    int err;
    char * response = NULL;

    SYSCALL_PTHREAD(err,pthread_mutex_lock(&lock_cache),"Lock Cache");
    file * curr = cache_file;
    int trovato=0;
    while(curr!=NULL && trovato==0) {
        if (strcmp(curr->path,path)==0) {
            //TODO : CONTROLLA CHE CFD ABBIA APERTO IL FILE !!!
            trovato=1;
            if (fileOpen(curr->client_open,cfd)==1) {
                response = curr->data;
            }
        }else{
            curr = curr->next;
        } 
    }

    pthread_mutex_unlock(&lock_cache);

    return response; 

}

//RITONA 0 SE SUCCESSO, -1 SE FILE NON ESISTE
int removeFile (char * path) {

    int res=0;
    int err;
    SYSCALL_PTHREAD(err,pthread_mutex_lock(&lock_cache),"Lock Cache");

    file ** list = &cache_file;

    int rimosso = 0;
    file * curr = *list;
    file * prec = NULL;
    while (curr!=NULL && rimosso==0) {
        if ((strcmp(path,curr->path) == 0)) {
            rimosso=1;
            if (prec==NULL) {
                *list = curr->next;
            }else{
                prec->next = curr->next;
            }
            dim_byte = dim_byte - strlen(curr->data);
            num_files--;
            free(curr->data);
            freeList(&(curr->client_open));
            free(curr);
        }else{
            prec = curr;
            curr = curr->next;
        }
    }

    if (rimosso==0) res=-1;

    pthread_mutex_unlock(&lock_cache);

    return res;
}

//RIMUOVE IL CLIENT CFD DALLA LISTA DEL FILE PATH DI CHI HA APERTO IL FILE 
//RITONA 0 SE SUCCESSO, -1 SE FILE NON ESISTE, -2 SE OPERAIONE NON PERMESSA
int removeClient (char * path, int cfd) {
    int res=0;
    int err;
    SYSCALL_PTHREAD(err,pthread_mutex_lock(&lock_cache),"Lock Cache");

    int trovato = 0;
    file * curr = cache_file;
    while (curr!=NULL && trovato==0) {
        if ((strcmp(path,curr->path) == 0)) trovato=1;
        else curr = curr->next;
    }

    int rimosso=0;
    if (trovato==1) {
        node * tmp = curr->client_open;
        node * prec = NULL;
        while (tmp!=NULL && rimosso==0) {
            if (tmp->data == cfd) {
                rimosso=1;
                if (prec==NULL) {
                    curr->client_open = tmp->next;
                }else{
                    prec->next = tmp->next;
                }
                free(tmp);
                curr->client_write=-1;
            }else{
                prec = tmp;
                tmp = tmp->next;
            }
        }
    }
    
    //TROVATO == 0 --> FILE NON ESISTE , RIMOSSO == 0 --> FILE NON APERTO DAL CLIENT
    if (trovato == 0) res=-1;
    else if (rimosso==0) res=-2;

    pthread_mutex_unlock(&lock_cache);

    return res;
}

//RITONA 0 SE SUCCESSO, -1 SE FILE NON ESISTE, -2 SE OPERAIONE NON PERMESSA, -3 SE FILE TROPPO GRANDE 
int writeData(char * path, char * data, int size, int cfd) {

    int res=0;
    int err;
    SYSCALL_PTHREAD(err,pthread_mutex_lock(&lock_cache),"Lock Cache");

    int trovato = 0;
    int scritto = 0;
    file * curr = cache_file;
    while (curr!=NULL && trovato==0) {
        if ((strcmp(path,curr->path) == 0)) {
            trovato=1;
            //CONTROLLA CHE LA PRECEDENTE OPERAZIONE DEL CLIENT SIA STATA UNA OPEN CREATE SUL FILE 
            if (curr->client_write == cfd) {
                //CONTROLLA LIMITE MEMORIA
                if (size>max_dim) res = -3;
                else if (dim_byte + size > max_dim) {
                    //RIMUOVI FILE 
                    if (resize_cache(size) == -1) res = -3;
                    else resize++;  
                }
                if (res==0) {
                    curr->data = malloc(size*sizeof(char));
                    if (curr->data==NULL) {
                        perror("malloc");
                        exit(EXIT_FAILURE);
                    }
                    strcpy(curr->data,data);
                    curr->client_write = -1; 
                    scritto=1;
                    dim_byte = dim_byte + size;
                    if (dim_byte>top_dim) top_dim=dim_byte;
                }
            }
        } else curr = curr->next;
    }

    if (trovato==0 && res==0) res=-1;
    else if (scritto==0 && res==0) res=-2;
    /*DEBUG
    printf("path=%s\n",path);
    printf("len=%ld\n",strlen(path));
    printf("trovato=%d scritto=%d\n",trovato,scritto);
    printf("data : %s\n",data);
    */

    pthread_mutex_unlock(&lock_cache);

    return res;
}

//RITONA 0 SE SUCCESSO, -1 SE FILE NON ESISTE, -2 SE OPERAIONE NON PERMESSA, -3 SE FILE TROPPO GRANDE 
int appendData (char * path, char * data, int size, int cfd) {
    int res=0;
    int err;
    SYSCALL_PTHREAD(err,pthread_mutex_lock(&lock_cache),"Lock Cache");

    int trovato = 0;
    int scritto = 0;
    file * curr = cache_file;
    while (curr!=NULL && trovato==0) {
        if ((strcmp(path,curr->path) == 0)) {
            trovato=1;
            //CONTROLLA DI AVER APERTO IL FILE 
            if (fileOpen(curr->client_open,cfd)==1) {
                char * tmp = realloc(curr->data,(strlen(curr->data)+size+1)*sizeof(char));
                if (tmp != NULL) {
                    //CONTROLLA LIMITE MEMORIA
                    if (dim_byte + size > max_dim) {
                        //TODO : RIMUOVI FILE 
                        if (resize_cache(size) == -1) res = -3;
                        else resize++;  
                    }
                    if (res==0) {
                        strcat(tmp,data);
                        scritto = 1;
                        curr->data = tmp;
                        if (curr->client_write == cfd) curr->client_write = -1;
                        dim_byte = dim_byte + size;
                        if (dim_byte>top_dim) top_dim=dim_byte;
                    }
                } 
            }
        } else curr = curr->next;
    }

    if (trovato==0) res=-1;
    else if (scritto==0) res=-2;


    pthread_mutex_unlock(&lock_cache);

    return res;
}

int sizeList () {
    int size=0;
    file * curr = cache_file;
    while (curr!=NULL) {
        size++;
        curr = curr->next;
    }
    return size;
}

void printFile () {
    printf ("Lista File : \n");
    fflush(stdout);
    file * curr = cache_file;
    while (curr!=NULL) {
        printf("%s ",curr->path);
        //printf("WRITE:%d ",curr->client_write);
        //printClient(curr->client_open);
        if (curr->data!=NULL) {
            printf("size=%ld\n",strlen(curr->data));
        } else {
            printf("size=0\n");
        }
        
        curr = curr->next;
    }
}

void printClient (node * list) {
    node * curr = list;
    printf("APERTO DA : ");
    fflush(stdout);
    while (curr!=NULL) {
        printf("%d ",curr->data);
        fflush(stdout);
        curr = curr->next;
    }
}

int containFile (char * path) {

    file * curr = cache_file;
    while (curr!=NULL) {
        if ((strcmp(path,curr->path) == 0)) return 1;
        curr = curr->next;
    }
    return 0;
}

int fileOpen(node * list, int cfd) {
    node * curr = list;
    while (curr!=NULL) {
        if (curr->data==cfd) return 1;
    }
    return 0;
}

int resize_cache (int dim_data) {
    //ELIMINA L'ULTIMO FILE DELLA LISTA FINCHE' NON C'E' ABBASTANZA SPAZIO PER IL NUOVO FILE 
    file ** list = &cache_file;

    while (dim_byte + dim_data > max_dim) {
        //ELIMINA IN CODA
        file * tmp = *list; 
        if (tmp==NULL) {
            break;
        } else if (tmp->next == NULL) {
            *list = NULL;
        } else {

            file * prec = NULL;
            while (tmp->next != NULL) {
                prec = tmp;
                tmp = tmp->next;
            } 
            prec->next = NULL;

        }
        dim_byte = dim_byte - strlen(tmp->data);
        num_files--;
        free(tmp->data);
        freeList(&(tmp->client_open));
        free(tmp);
        
    }
    
    if (*list == NULL && (dim_byte + dim_data > max_dim)) return -1; //DIM_FILE > MAX DIM CACHE

    return 0; 
}

long isNumber(const char* s) {
   char* e = NULL;
   long val = strtol(s, &e, 0);
   if (e != NULL && *e == (char)0) return val; 
   return -1;
}

void freeList(node ** head) {
    node* tmp;
    node * curr = *head;
    while (curr != NULL) {
       tmp = curr;
       curr = curr->next;
       free(tmp);
    }
    *head = NULL;
}

void freeCache () {
    file * tmp;
    file * curr = cache_file;
    while (curr!=NULL) {
        tmp=curr;
        freeList(&(curr->client_open));
        free(curr->data);
        curr = curr->next;
        free(tmp);
    }
    cache_file=NULL;
}

