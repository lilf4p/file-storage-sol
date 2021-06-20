#!/bin/bash
sleep 2 

echo "Avvio script client"

#TESTA GESTIONE MEMORIA SERVER
./client -f "./tmp/LSOfilestorage.sk" -t 200 -w testfile/subdir -W /home/lilf4p/Documenti/sol/file-storage-sol/testfile/pdf/progettosol-20_21.pdf,testfile/big.txt -r /home/lilf4p/Documenti/sol/file-storage-sol/testfile/pdf/progettosol-20_21.pdf,/home/lilf4p/Documenti/sol/file-storage-sol/testfile/subdir/prova0.txt -d dirsave -R 3 -p
#sighup al server
killall -s SIGHUP memcheck-amd64-