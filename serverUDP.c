#define _GNU_SOURCE 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <signal.h>
#include <netdb.h>
#define ERR(source) (perror(source),\
		     fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
			exit(EXIT_FAILURE))
#define dataLength 12
#define BROADCAST 0xFFFFFFFF
#define MAXBUF 128
#define BACKLOG 3

struct Message
{
	char Type;
	uint32_t id;
	char data[dataLength];
};
void SerializeMessage(char* buf,struct Message m)
{
	int i;
	uint32_t Number = htonl(m.id);
	buf[0] = m.Type;
	for(i=0;i<sizeof(uint32_t)/sizeof(char);i++)
	{
		buf[i+1] = ((char*)&Number)[i];
	}	
	for(i=0;i<dataLength;i++)
	{
		buf[i+1+(sizeof(uint32_t)/sizeof(char))] = m.data[i];
	}
	
	
}
int bind_inet_socket(uint16_t port,int type,uint32_t addres,int flag){
	struct sockaddr_in addr;
	int socketfd,t=1;
	socketfd = socket(PF_INET,type,0);
	if(socketfd<0) ERR("socket:");
	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(addres);
	if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR,&t, sizeof(t))) ERR("setsockopt");
if(flag>0) if (setsockopt(socketfd, SOL_SOCKET, flag,&t, sizeof(t))) ERR("setsockopt");
	if(bind(socketfd,(struct sockaddr*) &addr,sizeof(addr)) < 0)  ERR("bind");
	if(SOCK_STREAM==type)
		if(listen(socketfd, BACKLOG) < 0) ERR("listen");
	return socketfd;
}

void DiscoverAddress(int broadcastfd,int listenfd,int port,struct sockaddr_in* server)
{
char MessageBuf[MAXBUF];
socklen_t size;
struct sockaddr_in addr = {.sin_family=AF_INET, .sin_addr.s_addr=htonl(INADDR_BROADCAST), .sin_port=htons(port)};
struct Message m = {.Type = 'D', .id = 0 };
memset(m.data,0,dataLength);
SerializeMessage(MessageBuf,m);
if(TEMP_FAILURE_RETRY(sendto(broadcastfd,MessageBuf,sizeof(struct Message),0,&addr,sizeof(addr)))<0) ERR("send:");	
if(TEMP_FAILURE_RETRY(recvfrom(listenfd,MessageBuf,sizeof(struct Message),0,(struct sockaddr*)&server,&size))<0) ERR("read:");

}
void usage(char* c) {
	fprintf(stderr,"USAGE: %s port Labyrinth client-port\n",c);
}

int main(int argc,char** argv)
{
	int listenfd,broadcastfd;
	struct sockaddr_in server;
		if(argc!=2) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	listenfd=bind_inet_socket(atoi(argv[1]),SOCK_DGRAM,INADDR_ANY,0);
	broadcastfd=bind_inet_socket(atoi(argv[1]),SOCK_DGRAM,BROADCAST,SO_BROADCAST);
	DiscoverAddress(broadcastfd,listenfd,atoi(argv[1]),&server);
	return 0;

}
