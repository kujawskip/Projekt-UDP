
all: server client
debug: debugserver debugclient
debugserver: serverUDP.c
	gcc -Wall -g serverUDP.c -o server -lpthread
debugclient: clientUDP.c
	gcc -Wall -g clientUDP.c -o client -lpthread
server: serverUDP.c
	gcc -Wall serverUDP.c -o server -lpthread
client: clientUDP.c
	gcc -Wall clientUDP.c -o client -lpthread
clean: 
	rm server; rm client
