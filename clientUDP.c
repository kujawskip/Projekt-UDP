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
#define dataLength 571
#define Preamble 4
#define BROADCAST 0xFFFFFFFF
#define MAXBUF 1024
#define BACKLOG 3
#define CORRECT 'S'
#define SENDER 'C'
struct Message
{
	char Kind;
	uint32_t id;
	
	char data[dataLength];
};
struct Message PrepareMessage(uint32_t id,char type)
{
	struct Message m = {.Kind = type, .id = id};
	memset(m.data,0,dataLength);
	return m;
}
void SerializeNumber(int number,char* buf)
{
	uint32_t i,Number = htonl(number);
	for(i=0;i<sizeof(uint32_t)/sizeof(char);i++) 
	{
		 buf[i] = ((char*)&Number)[i];
	}
	
}
uint32_t DeserializeNumber(char* buf)
{
	uint32_t i,Number = 0;
	for(i=0;i<sizeof(uint32_t)/sizeof(char);i++) 
	{
		((char*)&Number)[i] = buf[i];
	}
	return (ntohl(Number));

}
void DeserializeMessage(char* buf,struct Message* m)
{
	int i=0;
	m->Kind = buf[0];
	
	m->id = DeserializeNumber(buf+1);
	for(;i<dataLength;i++) m->data[i] = buf[i+5];
}
void SerializeMessage(char* buf,struct Message m)
{
	int i;
	uint32_t Number = htonl(m.id);
	buf[0] = m.Kind;
	
	for(i=0;i<sizeof(uint32_t)/sizeof(char);i++)
	{
		buf[i+1] = ((char*)&Number)[i];
	}	
	for(i=0;i<dataLength;i++)
	{
		buf[i+1+(sizeof(uint32_t)/sizeof(char))] = m.data[i];
	}
	
	
}
int makesocket(int type,int flag)
{
	int socketfd,t=1;
	socketfd = socket(PF_INET,type,0);
	if(socketfd<0) ERR("socket:");
	if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR,&t, sizeof(t))) ERR("setsockopt");
	if(flag>0) if (setsockopt(socketfd, SOL_SOCKET, flag,&t, sizeof(t))) ERR("setsockopt");
	return socketfd;
}
int bind_inet_socket(uint16_t port,int type,uint32_t addres,int flag){
	struct sockaddr_in addr;
	
	int socketfd = makesocket(type,flag);
	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(addres);
	if(bind(socketfd,(struct sockaddr*) &addr,sizeof(addr)) < 0)  ERR("bind");
	if(SOCK_STREAM==type)
		if(listen(socketfd, BACKLOG) < 0) ERR("listen");
	return socketfd;
}

void SendMessage(int fd,struct Message m,struct sockaddr_in addr)
{
	char MessageBuf[MAXBUF];
	memset(MessageBuf,0,MAXBUF);
	SerializeMessage(MessageBuf,m);
	if(TEMP_FAILURE_RETRY(sendto(fd,MessageBuf,sizeof(struct Message),0,&addr,sizeof(struct sockaddr_in)))<0) ERR("send:");	
	sleep(1);
}
void ReceiveMessage(int fd,struct Message* m,struct sockaddr_in* addr)
{
	char MessageBuf[MAXBUF];
	memset(MessageBuf,0,MAXBUF);
	socklen_t size=sizeof(struct sockaddr_in);
	if(TEMP_FAILURE_RETRY(recvfrom(fd,MessageBuf,sizeof(struct Message),0,(struct sockaddr*)addr,&size))<0) ERR("read:");
	fprintf(stderr,"DEBUG: ReceivedMessage %s , preparing for serialization\n",MessageBuf);
	memset(m,0,sizeof(struct Message));
	DeserializeMessage(MessageBuf,m);
}
ssize_t bulk_write(int fd, char *buf, size_t count)
{
	int c;
	size_t len=0;
	do{
		c=TEMP_FAILURE_RETRY(write(fd,buf,count));
		if(c<0) return c;
		buf+=c;
		len+=c;
		count-=c;
	}while(count>0);
	return len ;
}
void ViewDirectory(int sendfd,int listenfd,struct sockaddr_in server,uint32_t id)
{
	struct Message m = PrepareMessage(id,'L');
	SendMessage(sendfd,m,server);
	ReceiveMessage(listenfd,&m,&server);
	SendMessage(sendfd,m,server);
	ReceiveMessage(listenfd,&m,&server);
	bulk_write(1,m.data+Preamble,dataLength-Preamble);
}
void DownloadFile(int sendfd,int listenfd,struct sockaddr_in server,char* path)
{
	
	char File[dataLength];
	struct Message m;
	FILE* F;
	int i;
	uint32_t chunk;
	int size;
	strcpy(File,path);
	F = fopen(File,"w+");
	m = PrepareMessage(0,'D');
	SendMessage(sendfd,m,server);
	ReceiveMessage(listenfd,&m,&server);
	if(m.Kind!='D')
	{
		///ERR;
		return;
	}
	size = DeserializeNumber(m.data);
	SendMessage(sendfd,m,server);
	while(1)
	{
		ReceiveMessage(listenfd,&m,&server);
		if(m.Kind == 'F')
		{
			break;
		}
		//TODO: Write in a file in exact position
		SendMessage(sendfd,m,server);
	}
	//CALC md5 sum of file
	m = PrepareMessage(id,'F');
	//m.data = md5sum
	SendMessage(sendfd,m,server);
	ReceiveMessage(listenfd,&m,&server);
	if(m.Kind!='C')
	{
		//delete file;
	}
	
	
}
void UploadFile(int sendfd,int listenfd,struct sockaddr_in server,char* path)
{
	struct Message m = PrepareMessage(id,'U');
	int size; //getsize
	//add filename and size to data;
	SendMessage(sendfd,m,server);
	ReceiveMessage(listenfd,&m,&server);
	if(m.Kind!='C')
	{
		///ERR
		return;
	}

	///Dziel plik na fragmenty a następnie rozsyłaj
	while(1)
	{
		SendMessage(sendfd,m,server);
		if(m.Kind == 'F')
		{
			break;
		}
		ReceiveMessage(sendfd,&m,&server);
		//TODO: Write in a file in exact position
	}
	//CALC md5 sum of file
	m = PrepareMessage(id,'F');
	
	SendMessage(sendfd,m,server);
	ReceiveMessage(listenfd,&m,&server);
	//if(m.data!=md5sum) uncorrect 
	
	
}
void DeleteFile(int sendfd,int listenfd,struct sockaddr_in server,uint32_t id,char* path)
{
	struct Message m = PrepareMessage('M',id);
	SendMessage(sendfd,m,server);
	ReceiveMessage(listenfd,&m,&server);
	if(m.Kind == 'C')
	{
		fprintf(stdout,"File Deleted\n");
	}
	else
	{
		fprintf(stdout,"Unable to delete file %s\n",m.data);
	}
}
void DiscoverAddress(int broadcastfd,int port,struct sockaddr_in* server)
{
	struct sockaddr_in addr = {.sin_family=AF_INET, .sin_addr.s_addr=htonl(INADDR_BROADCAST), .sin_port=htons(port)};
	struct sockaddr_in temp;
	socklen_t size = sizeof(addr);
	struct Message m = PrepareMessage(0,'R');
	int listenfd = bind_inet_socket(0,SOCK_DGRAM,INADDR_ANY,0);
	getsockname(listenfd,&temp,&size);
	SerializeNumber(ntohs(temp.sin_port),m.data);
	SendMessage(broadcastfd,m,addr);
	
	ReceiveMessage(listenfd,&m,server);
	//close listenfd
}

void usage(char* c) 
{
	fprintf(stderr,"USAGE: %s port\n",c);
}
void print_ip(long int ip)
{
    unsigned char bytes[4];
    bytes[0] = ip & 0xFF;
    bytes[1] = (ip >> 8) & 0xFF;
    bytes[2] = (ip >> 16) & 0xFF;
    bytes[3] = (ip >> 24) & 0xFF;	
    printf("%d.%d.%d.%d\n", bytes[3], bytes[2], bytes[1], bytes[0]);        
}
int main(int argc,char** argv)
{
	int listenfd,broadcastfd,sendfd;
	struct sockaddr_in server;
		if(argc!=2) 
		{
			usage(argv[0]);
			return EXIT_FAILURE;
		}
	memset(&server,0,sizeof(struct sockaddr_in));
	
	broadcastfd=makesocket(SOCK_DGRAM,SO_BROADCAST);
	sendfd = makesocket(SOCK_DGRAM,0);
	listenfd = bind_inet_socket(atoi(argv[1]),SOCK_DGRAM,INADDR_ANY,0);
	DiscoverAddress(broadcastfd,atoi(argv[1]),&server);
	print_ip((long int)server.sin_addr.s_addr);
	DownloadFile(listenfd,sendfd,server,"mama.txt");
	return 0;
	
}
