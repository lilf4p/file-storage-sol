#!/bin/bash
sleep 2 

echo "Avvio script client"

#TESTA GESTIONE MEMORIA SERVER
./client -f "./tmp/LSOfilestorage.sk" -t 200 -w testfile/pdf -W testfile/prova4.txt,testfile/big.txt -r /home/lilf4p/Documenti/sol/file-storage-sol/testfile/pdf/progettosol-20_21.pdf,/home/lilf4p/Documenti/sol/file-storage-sol/testfile/pdf/Linux.pdf -d downloadTest1 -R 2 -c /home/lilf4p/Documenti/sol/file-storage-sol/testfile/big.txt -p
#sighup al server
killall -s SIGHUP memcheck-amd64-