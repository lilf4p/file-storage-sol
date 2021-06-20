CC 			= gcc
CFLAGS		= -g -Wall
TARGETS		= server client

.PHONY: all clean cleanall test1 test2

#GENERA ESEGUIBILI SERVER E CLIENT
all : $(TARGETS)

server : src/server.c
	$(CC) -g -Wall $< -o $@ -lpthread

client : src/client.c lib/libapi.a
	$(CC) -g -Wall src/client.c -o $@ -L ./lib/ lib/libapi.a

objs/api_server.o : src/api_server.c
	$(CC) -c src/api_server.c -o $@

lib/libapi.a : objs/api_server.o 
	ar rcs lib/libapi.a objs/api_server.o

#ELIMINA SOLO GLI ESEGUIBILI
clean :
	-rm -f $(TARGETS) 

#ELIMINA I FILE ESEGUIBILI, OGGETTO E TEMPORANEI
cleanall :
	-rm -f $(TARGETS) objs/*.o lib/*.a tmp/* *~

#LANCIA IL PRIMO TEST
test1 : $(TARGETS)
	valgrind --leak-check=full ./server -s config/config_test1.txt &
	chmod +x test1.sh 
	./test1.sh &

#LANCIA SECONDO TEST
test2 : $(TARGETS)
	./server -s config/config_test2.txt &
	chmod +x test2.sh 
	./test2.sh &	


	
