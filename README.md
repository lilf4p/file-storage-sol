# file-storage-sol
Progetto finale per il corso di Sistemi Operativi e Laboratorio (SOL) dell'Università di Pisa. 
Il progetto prevede la realizzazione di un file storage server, un client e una API per la comunicazione del client con il server.
Il server memorizza i file in memoria principale e offre operazioni per aggiungere, modificare, leggere e rimuovere i file.


Comandi da eseguire nella directory principale : 

- make 		        : costruisce gli eseguibili "server" e "client"
- make test1	    : avvia il primo test
- make test2	    : avvia il secondo test
- make clean	    : elimina gli eseguibili "server" e "client" 
- make cleanall	  : elimina tutti i file generati da make (eseguibili, oggetto, temporanei, librerie, ...)

CLI server :
- -s config.txt    : specifica il file di config da usare

CLI client : 
- -h                  : lista operazioni client
- -f filename         : connettiti al socket filename
- -w dirname[,n=0]    : scrivi sul server n file contenuti nella directory dirname, se n=0 o non specificato scrivili tutti
- -W file1[,file2]    : scrivi i file sul server 
- -r file1[,file2]    : leggi i file dal server
- -R [n=0]            : leggi n file dal server, se n=0 o non specificato leggili tutti
- -d dirname          : salva i file letti nella directory dirname del client
- -t time             : ritardo in ms tra le richieste al server 
- -c file1[,file2]    : elimina i file dal server
- -p                  : abilita stampe su stdout per ogni operazione

Struttura directory : 
- configTest1 : contiene config.tx per il test 1
- configTest2 : contiene config.txt per il test 2
- downloadTest1 : file scaricati dal test 1
- downloadTest2 : file scaricati dal test 2
- testfile : contiene i file utilizzati dai test 1 e 2
- tmp : contiene il file socket .sk
- src : contiene i file sorgente .c
- objs : contiene i file oggetto .o
- lib : contiene le librearie .a  
