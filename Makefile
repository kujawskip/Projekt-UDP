all: server client
server: serverUDP.c
	gcc -Wall serverUDP.c -o server
client: clientUDP.c
	gcc -Wall clientUDP.c -o client
.PHONY: clean

clean: 
	rm server; rm client
