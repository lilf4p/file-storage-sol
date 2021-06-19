#!/bin/bash
echo "Avvio script client"
#TESTA GESTIONE MEMORIA SERVER
./client -f "./tmp/LSOfilestorage.sk" -W dumb.txt -t 200 -p
#sighup al server
killall -s SIGHUP memcheck-amd64-