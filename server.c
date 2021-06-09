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
    //info utili per ogni file
    struct file * next; 
} file;


//TODO : STRUTTURA DATI PER SALVARE I FILE
file * cache_file = NULL;
pthread_mutex_t lock_cache = PTHREAD_MUTEX_INITIALIZER;

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
int addFile (char * path, int flags, int cfd);
char * getFile (char * path, int cfd);
void removeFile (char * path, int cfd);
void eseguiRichiesta (char * request, int cfd);
int sizeList ();
void printFile ();
int containFile (char * path); //1 se lo contiene, 0 se non lo contiene
void printClient (node * list); //STAMPA LISTA DI CLIENT (FILE DESCRIPTOR SOCKET)
int removeClient (char * path, int cfd);


int main () {

    int i;

    //parametri server da settare con il config.txt
    int num_thread = 1;
    int max_file;
    int max_dim;
    char * socket_name = "/tmp/LSOfilestorage.sk";

    //TODO : parsing file config.txt -- attributo=valore 

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
        char *request = malloc(DIM_MSG);
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
            perror("THREAD : read socket");
            exit(EXIT_FAILURE);
        }else{
            printf ("From Client : %s\n",request);
            fflush(stdout);

            //STAMPE DEBUG
            //printf("NUMERO FILE : %d",sizeList());
            //printFile();
            eseguiRichiesta(request,cfd);
            
            //RITORNA IL CLIENT AL MANAGER TRAMITE LA PIPE
            SYSCALL(write(pfd,&cfd,sizeof(cfd)),"THREAD : pipe write");
            fine = 0;
            SYSCALL(write(pfd,&fine,sizeof(fine)),"THREAD : pipe write");
    
        }

    }
    close(cfd);
    printf("Closing worker\n");
    fflush(stdout);
    return 0;

}

//REQUEST CONTIENE : COMANDO,ARGOMENTI SEPARATI DA VIRGOLA
void eseguiRichiesta (char * request, int cfd) {
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
        printf("ADDFILE : CREO FILE\n");
        fflush(stdout);
        file * f = malloc(sizeof(file));
        f->path = path;
        f->data = NULL;
        f->client_open = NULL;
        node * new = malloc (sizeof(node));
        new->data = cfd;
        new->next = f->client_open;
        f->client_open = new;
        f->next = *list;
        *list =  f;
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

//RECUPERA UN FILE DALLA LISTA SE PRESENTE, MESSAGGIO DI ERRORE ALTRIMENTI
char * getFile (char * path, int cfd) {
    int err;
    char * response = NULL;

    SYSCALL_PTHREAD(err,pthread_mutex_lock(&lock_cache),"Lock Cache");
    file ** list = &cache_file;
    file * curr = *list;
    int trovato=0;
    while(curr!=NULL && trovato==0) {
        if (strcmp(curr->path,path)==0) {
            response = curr->data;
            trovato=1;
        }else{
            curr = curr->next;
        } 
    }

    pthread_mutex_unlock(&lock_cache);

    if (response == NULL) {
        if (trovato==0) response = "File non trovato!";
        else response = "File vuoto!";
    }

    return response; 

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
        printClient(curr->client_open);
        printf("\n"); 
        fflush(stdout);
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
