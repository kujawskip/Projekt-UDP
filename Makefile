
all: server client
debug: debugserver debugclient
debugserver: serverUDP.c
	gcc -Wall -g serverUDP.c -o server
debugclient: clientUDP.c
	gcc -Wall -g clientUDP.c -o client
server: serverUDP.c
	gcc -Wall serverUDP.c -o server
client: clientUDP.c
	gcc -Wall clientUDP.c -o client
clean: 
	rm server; rm client
