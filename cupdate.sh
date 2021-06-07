#!/bin/bash

gcc -c api_server.c -o api_server.o
ar rcs libapi.a api_server.o
gcc client.c -o client -L ./ -lapi
