#!/bin/bash
echo "Avvio script client"
#TESTA RIMPIAZZAMENTO IN CACHE SERVER
./client -f "./tmp/LSOfilestorage.sk" -W dumb.txt -p
./client -f "./tmp/LSOfilestorage.sk" -w prova -p
./client -f "./tmp/LSOfilestorage.sk" -R 5 -p -d dirprova
#sighup al server
killall -s SIGHUP server