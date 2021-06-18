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

#define UNIX_PATH_MAX 108 /* man 7 unix */
#define DIM_MSG 100 
// utility macro
#define SYSCALL(c,e) \
    if(c==-1) { perror(e);exit(EXIT_FAILURE); }
// utility macro pthread
#define SYSCALL_PTHREAD(e,c,s) \
    if((e=c)!=0) { errno=e;perror(s);fflush(stdout);exit(EXIT_FAILURE); }

typedef struct node {
    int data;
    struct node  * next;
} node;

typedef struct file {
    char * path;
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
int writeData(char * path, char * data, int cfd);//SCRIVE I DATI NEL FILE SSE CLIENT_WRITE == CFD
int sizeList ();
void printFile ();
int containFile (char * path); //1 se lo contiene, 0 se non lo contiene
void printClient (node * list); //STAMPA LISTA DI CLIENT (FILE DESCRIPTOR SOCKET)
int fileOpen(node * list, int cfd);
int appendData (char * path, char * data, int cfd);//SCRIVE I DATI NEL FILE SSE CLIENT_OPEN CONTIENE CFD
int resize_cache (int dim_data); //POLITICA DI SOSTITUZIONE IN CACHE FIFO 
long isNumber(const char* s);
//TODO : VEDERE SE POSSIBILE NON ELIMINARE I FILE APERTI DA CLIENT -> ELIMINA IL PIU' VECCHIO NON APERTO -> SE TUTTI APERTI ALLORA ELIMINA IL PIU' VECCHIO 

int main (int argc, char * argv[]) {

    int i;

    char * path_config = "config.txt";
    if (argc==3) {
        if (strcmp(argv[1],"-s")==0) {
            path_config = argv[2];
        }
    }

    //VALORI DI DEFAULT
    int num_thread = 2;
    max_files = 5; 
    max_dim = 200;   
    char * socket_name = "/tmp/LSOfilestorage.sk";

    //parsing file config.txt -- attributo=valore -- se trovo errore uso attributi di default 
    char str [200];
    FILE * fp;
    fp = fopen(path_config,"r");
    if (fp == NULL) {
        perror("Errore config file");
        exit(EXIT_FAILURE);
    }
    
    char * token;
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
    SYSCALL(sigfillset(&sigset),"sigfillset");
    SYSCALL(pthread_sigmask(SIG_SETMASK,&sigset,NULL),"pthread_sigmask");
    memset(&s,0,sizeof(s));
    s.sa_handler = gestore_term;

    SYSCALL(sigaction(SIGINT,&s,NULL),"sigaction");
    SYSCALL(sigaction(SIGQUIT,&s,NULL),"sigaction");
    SYSCALL(sigaction(SIGHUP,&s,NULL),"sigaction"); //TERMINAZIONE SOFT

    //ignoro SIGPIPE
    s.sa_handler = SIG_IGN;
    SYSCALL(sigaction(SIGPIPE,&s,NULL),"sigaction");

    SYSCALL(sigemptyset(&sigset),"sigemptyset");
    SYSCALL(pthread_sigmask(SIG_SETMASK,&sigset,NULL),"pthread_sigmask");
    //-----------------------------------------//

    //CREO PIPE
    int pip[2]; //COMUNICAZIONE WORKERS --> MANAGER  
    SYSCALL(pipe(pip),"Create pipe");

    //------THREAD POOL--------//
    pthread_t * thread_pool = malloc(num_thread*sizeof(pthread_t));
    pthread_t t;
    int err;
    for (i=0;i<num_thread;i++) {
        SYSCALL_PTHREAD(err,pthread_create(&t,NULL,worker,(void*)(&pip[1])),"create");
        thread_pool[i] = t;  
    }
    //------------------------//

    int fd;
    int num_fd = 0;
    fd_set set;
    fd_set rdset;
    char buf_pipe[4];
    //PER LA TERMINAZIONE SOFT --> con SIGHUP aspetta che tutti i client si disconnettano
    int num_client = 0; 
    int soft_term = 0; 

    struct sockaddr_un sa;
    strncpy(sa.sun_path,socket_name,UNIX_PATH_MAX);
    sa.sun_family=AF_UNIX;
    int sfd;
    if ((sfd = socket(AF_UNIX,SOCK_STREAM,0)) == -1) {
        perror("Socket");
        exit(EXIT_FAILURE);
    }
    unlink(socket_name);
    SYSCALL(bind(sfd,(struct sockaddr *)&sa,sizeof(sa)),"Bind");
    SYSCALL(listen(sfd,SOMAXCONN),"Listen");

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
                    SYSCALL(select(num_fd+1,&rdset,NULL,NULL,NULL),"select"); 
                }
            }else {
                perror("select");
                exit(EXIT_FAILURE);
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
                            perror("accept");
                            exit(EXIT_FAILURE);
                        }
                    }
                    FD_SET(cfd,&set);
                    if (cfd > num_fd) num_fd = cfd;
                    num_client++;
                    printf ("Connection accepted from client!\n");
                    SYSCALL(write(cfd,"Welcome to lilf4p server!",26),"Write Socket");

                } else if (fd == pip[0]) { //CLIENT DA REINSERIRE NEL SET -- PIPE PRONTA IN LETTURA
                    int cfd1;
                    int len;
                    int flag;
                    if ((len = read(pip[0],&cfd1,sizeof(cfd1))) > 0) { //LEGGO UN INTERO == 4 BYTES
                        printf ("Master : client %d ritornato\n",cfd1);
                        SYSCALL(read(pip[0],&flag,sizeof(flag)),"Master : read pipe");
                        if (flag == -1) { //CLIENT TERMINATO LO RIMUOVO DAL SET DELLA SELECT
                            printf("Closing connection with client...\n");
                            FD_CLR(cfd1,&set);
                            //rdset=set;
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
                    printf("Master : Client pronto in read\n");
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
    close(sfd);
    remove("/tmp/mysock");
    free(coda);
    free(thread_pool);
    free(cache_file);

    return 0;
}

//GESTISCE SOLO UNA RICHIESTA DI UNO DEI CLIENT CONNESSI 
//PRELEVA UN CLIENT DALLA CODA -> SERVE UNA SOLA RICHIESTA -> LO METTE NELLA PIPE -> NE PRENDE UN ALTRO 
void * worker (void * arg) {

    int pfd = *((int *)arg);
    int new = 1;
    int cfd;
    while (1) {
        char * request = malloc(DIM_MSG);
        //PRELEVA UN CLIENT DALLA CODA
        cfd = removeNode(&coda);
        if (cfd==-1) break;
        
        //SERVO IL CLIENT
        int len;
        int fine; //FLAG COMUNICATO AL MASTER PER SAPERE QUANDO TERMINA IL CLIENT
        if ((len = read(cfd,request,DIM_MSG)) == 0) {  
            fine = -1;
            SYSCALL(write(pfd,&cfd,sizeof(cfd)),"THREAD : pipe write");
            SYSCALL(write(pfd,&fine,sizeof(fine)),"THREAD : pipe write");
        }else if (len == -1) {
            perror("THREAD : READ1");
            exit(EXIT_FAILURE);
        }else{
            printf ("From Client : %s\n",request);
            fflush(stdout);

            //STAMPE DEBUG
            //printf("NUMERO FILE : %d",sizeList());
            //printFile();
            eseguiRichiesta(request,cfd,pfd);
            
            //RITORNA IL CLIENT AL MANAGER TRAMITE LA PIPE
            SYSCALL(write(pfd,&cfd,sizeof(cfd)),"THREAD : pipe write");
            fine = 0;
            SYSCALL(write(pfd,&fine,sizeof(fine)),"THREAD : pipe write");
    
        }

        //free(request);
    }
    close(cfd);
    printf("Closing worker\n");
    fflush(stdout);
    return 0;

}

//REQUEST CONTIENE : COMANDO,ARGOMENTI SEPARATI DA VIRGOLA
void eseguiRichiesta (char * request, int cfd, int pfd) {
    char response[DIM_MSG];
    char * token;
    token = strtok(request,",");

    if (strcmp(token,"openFile")==0) {
        //openFile,pathname,flags

        //ARGOMENTI 
        token = strtok(NULL,",");
        char * path = token;
        token = strtok(NULL,",");
        int flag = atoi(token);

        //ELABORA COMANDO
        if (addFile(path,flag,cfd) == -1) {
            sprintf(response,"-1,%d",EPERM);
        }else{
            sprintf(response,"0");
        }
        SYSCALL(write(cfd,response,sizeof(response)),"THREAD : socket write");
        printf("NUMERO FILE : %d\n",sizeList());
        printFile();

    }else if (strcmp(token,"closeFile")==0) {
        //closeFile,pathname

        //ARGOMENTI
        token = strtok(NULL,",");
        char * path = token;

        if (removeClient(path,cfd) == -1) {
            sprintf(response,"-1,%d",EPERM);
        }else{
            sprintf(response,"0");
        }
        SYSCALL(write(cfd,response,sizeof(response)),"THREAD : socket write");
        printf("NUMERO FILE : %d\n",sizeList());
        printFile();

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
        SYSCALL(write(cfd,response,sizeof(response)),"THREAD : socket write");
        printf("NUMERO FILE : %d\n",sizeList());
        printFile();

    }else if (strcmp(token,"writeFile")==0) {
        //writeFile,pathname
        
        //ARGOMENTI
        token = strtok(NULL,",");
        char * path = token;

        //INVIA AL CLIENT PERMESSO PER INVIARE FILE O ERRORE
        sprintf(response,"0");
        SYSCALL(write(cfd,response,sizeof(response)),"THREAD : socket write");
            
        //RICEVO DAL CLIENT SIZE FILE 
        char * buf1 = malloc(DIM_MSG*sizeof(char));
        SYSCALL(read(cfd,buf1,sizeof(buf1)),"THREAD : socket read2");
        printf("FROM CLIENT SIZE : %s\n",buf1);
        fflush(stdout);
        int size_file = atoi(buf1);

        //E IL FILE
        char * buf2 = malloc ((size_file+1)*sizeof(char));
        if (buf2==NULL) {
            fprintf(stderr,"malloc fail\n");
            exit(EXIT_FAILURE);
        }
        SYSCALL(read(cfd,buf2,(size_file+1)),"THREAD : socket read3");
        printf("FROM CLIENT FILE : %s\n",buf2);
        fflush(stdout);
        //INSERISCO I DATI NELLA CACHE 
        char * result = malloc(DIM_MSG*sizeof(char));
        if (writeData(path, buf2,cfd) == -1) {
            sprintf(result,"-1,%d",EPERM);
        } else {
            sprintf(result,"0");
        }
        SYSCALL(write(cfd,result,sizeof(response)),"THREAD : socket write");

    } else if (strcmp(token,"appendToFile")==0) {
        //appendToFile,pathname,buf,size

        //ARGOMENTI
        token = strtok(NULL,",");
        char * path = token;

        //INVIA AL CLIENT PERMESSO PER INVIARE FILE O ERRORE
        sprintf(response,"0");
        SYSCALL(write(cfd,response,sizeof(response)),"THREAD : socket write");
            
        //RICEVO DAL CLIENT SIZE FILE 
        char * buf1 = malloc(DIM_MSG*sizeof(char));
        SYSCALL(read(cfd,buf1,sizeof(buf1)),"THREAD : socket read2");
        printf("FROM CLIENT SIZE : %s\n",buf1);
        fflush(stdout);
        int size_file = atoi(buf1);

        //E IL FILE
        char * buf2 = malloc ((size_file+1)*sizeof(char));
        if (buf2==NULL) {
            fprintf(stderr,"malloc fail\n");
            exit(EXIT_FAILURE);
        }
        SYSCALL(read(cfd,buf2,(size_file+1)),"THREAD : socket read3");
        printf("FROM CLIENT FILE : %s\n",buf2);
        fflush(stdout);

        //TODO: AGGIUNGO I DATI AL FILE IN CACHE
        char * result = malloc(DIM_MSG*sizeof(char));
        if (appendData(path,buf2,cfd)==-1) {
            sprintf(result,"-1,%d",EPERM);
        }else{
            sprintf(result,"0");
        }
        SYSCALL(write(cfd,result,sizeof(response)),"THREAD : socket write");

    } else if (strcmp(token,"readFile")==0) {
        //readFile,path

        //ARGOMENTI
        token = strtok(NULL,",");
        char * path = token;

        char * fb = getFile(path,cfd);
        printf("%s\n",fb);
        fflush(stdout);

        char * buf = malloc(DIM_MSG*sizeof(char));
        if (fb==NULL) {
            //INVIO ERRORE
            sprintf(buf,"-1,%d",EPERM);
            SYSCALL(write(cfd,buf,sizeof(buf)),"THREAD : socket write");
        }else{
            //INVIO SIZE FILE
            sprintf(buf,"%ld",strlen(fb));
            SYSCALL(write(cfd,buf,sizeof(buf)),"THREAD : socket write");

            char * buf1 = malloc(DIM_MSG*sizeof(char));
            SYSCALL(read(cfd,buf1,sizeof(buf1)),"THREAD :  socket read");
            printf("FROM CLIENT : %s",buf1);
            fflush(stdout);
            if (strcmp(buf1,"0")==0) {
                //INVIO FILE 
                SYSCALL(write(cfd,fb,strlen(fb)),"THREAD : socket write");
            }
        }

    } else if (strcmp(token,"readNFiles")==0) {//TODO : BUG
        //readNFiles,N
        int fine=0;
        int e;

        //ARGOMENTI
        token = strtok(NULL,",");
        int n_richiesti = atoi(token);
        //printf ("RICHIESTI=%d ESISTENTI=%d\n",n_richiesti,num_files);

        SYSCALL_PTHREAD(e,pthread_mutex_lock(&lock_cache),"LOCK IN READNFILES");

        //INVIO NUMERO DI FILE AL CLIENT
        if (n_richiesti <= 0 || n_richiesti > num_files) n_richiesti=num_files;
        printf("RICHIESTI=%d DISPONIBILI=%d\n",n_richiesti,num_files);
        char * nbuf = malloc(DIM_MSG*sizeof(char));
        sprintf(nbuf,"%d",n_richiesti);
        SYSCALL(write(cfd,nbuf,sizeof(nbuf)),"THREAD : WRITE 1");
        
        //RICEVO LA CONFERMA DEL CLIENT
        char * conf0 = malloc(DIM_MSG*sizeof(char));
        //ERR MALLOC 
        SYSCALL(read(cfd,conf0,sizeof(conf0)),"THREAD : READ 2");
        printf("From client : %s\n",conf0);
        //fflush(stdout);
        //if (strcmp(conf0,"ok")!=0) return;
        
        //INVIO GLI N FILES
        file * curr = cache_file;
        for (int i=0;i<n_richiesti;i++) {
            
            //INVIA PATH AL CLIENT
            char * path = malloc(DIM_MSG*sizeof(char));
            //ERR MALLOC
            sprintf(path,"%s",curr->path);
            SYSCALL(write(cfd,path,DIM_MSG),"THREAD : WRITE 2");

            //RICEVO LA CONFERMA DEL CLIENT
            char * conf1 = malloc(DIM_MSG*sizeof(char));
            //ERR MALLOC 
            SYSCALL(read(cfd,conf1,sizeof(conf1)),"THREAD : READ 3");
            printf("From client : %s\n",conf1);
            //fflush(stdout);
            //if (strcmp(conf1,"0")!=0) return;

            //INVIO SIZE 
            char * ssize = malloc(DIM_MSG*sizeof(char));
            sprintf(ssize,"%ld",strlen(curr->data));
            SYSCALL(write(cfd,ssize,sizeof(ssize)),"THREAD : WRITE 3");

            //RICEVO LA CONFERMA DEL CLIENT
            char * conf2 = malloc(DIM_MSG*sizeof(char));
            //ERR MALLOC 
            SYSCALL(read(cfd,conf2,DIM_MSG),"THREAD : READ 4");
            printf("From client : %s\n",conf2);
            //fflush(stdout);
            //if (strcmp(conf2,"ok")!=0) return;

            //INVIA FILE
            SYSCALL(write(cfd,curr->data,strlen(curr->data)),"THREAD : WRITE 4");

            //RICEVO LA CONFERMA DEL CLIENT
            //char * conf3 = malloc(DIM_MSG*sizeof(char));
            //ERR MALLOC 
            //SYSCALL(read(cfd,conf3,sizeof(conf3)),"THREAD : READ 5");
            //printf("From client : %s\n",conf3);
            //if (strcmp(conf3,"0")!=0) return;

            //free(path);
            //free(ssize);
            //free(conf0);
            //free(conf1);
            //free(conf2);
            //free(conf3);
            curr = curr->next;
        }
        pthread_mutex_unlock(&lock_cache);
        //free(conf0);
        //SYSCALL(write(cfd,"0",2),"THREAD : WRITE FINE");

    } else {
        //ENOSYS
        sprintf(response,"-1,%d",ENOSYS);
        SYSCALL(write(cfd,response,sizeof(response)),"THREAD : socket write");
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
        
        //CONTROLLA SE < MAX_FILES 
        if (num_files+1 > max_files) {
            //TODO : RIMUOVI ULTIMO FILE DELLA LISTA --> FIFO
            file * tmp = *list;
            
            if (tmp==NULL) {
                res=-1;
            } else if (tmp->next == NULL) {
                *list = NULL;
                free(tmp);
                num_files--;
            } else {

                file * prec = NULL;
                while (tmp->next != NULL) {
                    prec = tmp;
                    tmp = tmp->next;
                } 
                prec->next = NULL;
                free(tmp);
                num_files--;
            }
        }
        if (res==0) {
            printf("ADDFILE : CREO FILE\n");
            fflush(stdout);
            file * f = malloc(sizeof(file));
            f->path = path;
            f->data = NULL;
            f->client_write = cfd;
            f->client_open = NULL;
            node * new = malloc (sizeof(node));
            new->data = cfd;
            new->next = f->client_open;
            f->client_open = new;
            f->next = *list;
            *list =  f;
            num_files++;
        }
    }else if (flag==0 && trovato==1) { //APRO IL FILE PER CFD
        printf("ADDFILE : APRO FILE\n");
        //TODO : INSERIRE SSE LA LISTA CLIENT_OPEN NON CONTINE GIA' IL CFD -- CONTROLLO DUPLICATI
        node * new = malloc (sizeof(node));
        new->data = cfd;
        new->next = curr->client_open;
        curr->client_open = new;
    }else {
        res=-1; //ERRORE
        printf("ADDFILE : ERRORE\n");
        printf("PATH : %s\n",path);
        printf("trovato = %d\n",trovato);
        printf("flag = %d\n",flag);
        fflush(stdout);
    }

    pthread_mutex_unlock(&lock_cache);

    return res;

}

//RECUPERA UN FILE DALLA LISTA SE PRESENTE, "-1" SE NON ESISTE , "-2" SE NON E' APERTO IL FILE 
char * getFile (char * path, int cfd) {
    int err;
    char * response = NULL;

    SYSCALL_PTHREAD(err,pthread_mutex_lock(&lock_cache),"Lock Cache");
    file ** list = &cache_file;
    file * curr = cache_file;
    int trovato=0;
    while(curr!=NULL && trovato==0) {
        if (strcmp(curr->path,path)==0) {
            //TODO : CONTROLLA CHE CFD ABBIA APERTO IL FILE !!!
            trovato=1;
            if (fileOpen(curr->client_open,cfd)==1) {
                response = curr->data;
            }else response="-2"; //FILE NON APERTO
        }else{
            curr = curr->next;
        } 
    }

    pthread_mutex_unlock(&lock_cache);

    if (trovato==0) response = "-1"; //FILE NON ESISTE!

    return response; 

}

//ERRORE SSE IL FILE PATH NON ESISTE NEL SERVER
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
int removeClient (char * path, int cfd) {
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
    if (trovato == 0 || rimosso == 0) res=-1;

    pthread_mutex_unlock(&lock_cache);

    return res;
}

int writeData(char * path, char * data, int cfd) {

    int res=0;
    int err;
    SYSCALL_PTHREAD(err,pthread_mutex_lock(&lock_cache),"Lock Cache");

    file ** list = &cache_file;

    int trovato = 0;
    int scritto = 0;
    file * curr = cache_file;
    while (curr!=NULL && trovato==0) {
        if ((strcmp(path,curr->path) == 0)) {
            trovato=1;
            //CONTROLLA CHE LA PRECEDENTE OPERAZIONE DEL CLIENT SIA STATA UNA OPEN CREATE SUL FILE 
            if (curr->client_write == cfd) {
                //CONTROLLA LIMITE MEMORIA
                if (strlen(data)>max_dim) res = -1;
                else if (dim_byte + strlen(data) > max_dim) {
                    //RIMUOVI FILE 
                    if (resize_cache(strlen(data)) == -1) res = -1;  
                }
                if (res==0) {
                    curr->data = malloc(sizeof(data));
                    curr->data = data;
                    curr->client_write = -1; 
                    scritto=1;
                    dim_byte = dim_byte + strlen(data);
                }
            }
        } else curr = curr->next;
    }

    if (trovato==0 || scritto==0) res=-1;

    pthread_mutex_unlock(&lock_cache);

    return res;
}

int appendData (char * path, char * data, int cfd) {
    int res=0;
    int err;
    SYSCALL_PTHREAD(err,pthread_mutex_lock(&lock_cache),"Lock Cache");

    file ** list = &cache_file;

    int trovato = 0;
    int scritto = 0;
    file * curr = cache_file;
    while (curr!=NULL && trovato==0) {
        if ((strcmp(path,curr->path) == 0)) {
            trovato=1;
            //CONTROLLA DI AVER APERTO IL FILE 
            if (fileOpen(curr->client_open,cfd)==1) {
                char * tmp = realloc(curr->data,(strlen(curr->data)+strlen(data)+1)*sizeof(char));
                if (tmp != NULL) {
                    //CONTROLLA LIMITE MEMORIA
                    if (dim_byte + strlen(data) > max_dim) {
                        //TODO : RIMUOVI FILE 
                        if (resize_cache(strlen(data)) == -1) res = -1;  
                    }
                    if (res==0) {
                        strcat(tmp,data);
                        scritto = 1;
                        curr->data = tmp;
                        if (curr->client_write == cfd) curr->client_write = -1;
                        dim_byte = dim_byte + strlen(data);
                    }
                } 
            }
        } else curr = curr->next;
    }

    if (trovato==0 || scritto == 0) res=-1;

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
        printf("WRITE:%d ",curr->client_write);
        printClient(curr->client_open);
        printf("\n");
        if (curr->data!=NULL) {
            printf("CONTENUTO : size=%ld %s\n",strlen(curr->data),curr->data);
        } else {
            printf("CONTENUTO : size=0 \n");
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

