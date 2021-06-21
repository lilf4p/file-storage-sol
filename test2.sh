#!/bin/bash
sleep 2 #aspetto che si sia avviato il server 
echo "Avvio script client"
#TESTA RIMPIAZZAMENTO IN CACHE SERVER
./client -f "./tmp/LSOfilestorage.sk" -w testfile/subdir -p
./client -f "./tmp/LSOfilestorage.sk" -w testfile/prova -p
./client -f "./tmp/LSOfilestorage.sk" -w testfile/dumb -p
./client -f "./tmp/LSOfilestorage.sk" -w testfile/subdir2 -p
./client -f "./tmp/LSOfilestorage.sk" -R 5 -p -d downloadTest2
#sighup al server
killall -s SIGHUP server