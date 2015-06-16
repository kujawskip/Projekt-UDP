#define _GNU_SOURCE 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <signal.h>
#include <netdb.h>
#include <dirent.h>
#include <pthread.h>
#define ERR(source) (perror(source),\
		     fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
			exit(EXIT_FAILURE))
#define dataLength 567
#define BROADCAST 0xFFFFFFFF
#define Preamble 4
#define MAXBUF 1024
#define BACKLOG 3
#define MAXFILE 1024
#define MAXDIR 1024
#define CORRECT 'S'
#define SENDER 'C'
ssize_t bulk_fread(FILE* fd,char* buf,size_t count)
{
	int c;
	size_t len=0;
	do
	{
		c=TEMP_FAILURE_RETRY(fread(buf,1,count,fd));
		if(c<0) return c;
		buf+=c;
		len+=c;
		count-=c;
	}
	while(count>0);
	return len ;
}
ssize_t bulk_fwrite(FILE* fd,char* buf,size_t count)
{
	int c;
	size_t len=0;
	do
	{
		c=TEMP_FAILURE_RETRY(fwrite(buf,1,count,fd));
		if(c<0) return c;
		buf+=c;
		len+=c;
		count-=c;
	}
	while(count>0);
	return len ;
}
int listenport;
struct Message
{
	char Kind;
	uint32_t id;
	int responseport;
	char data[dataLength];
};
struct Message PrepareMessage(uint32_t id,char type)
{
	struct Message m = {.Kind = type, .id = id,.responseport=listenport};
	
	memset(m.data,0,dataLength);
	if(m.responseport==0)
	{
		fprintf(stderr,"DEBUG: response port = 0\n");
	}
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
	 	for(i=0;i<sizeof(uint32_t)/sizeof(char);i++) 
	{
		((char*)&(m->responseport))[i] = buf[i+5];
	}

	m->id = DeserializeNumber(buf+1);
	for(;i<dataLength;i++) m->data[i] = buf[i+9];
}
void SerializeMessage(char* buf,struct Message m)
{
	int i;
	uint32_t Number = htonl(m.id);
	uint32_t port = m.responseport;
	buf[0] = m.Kind;
	
	for(i=0;i<sizeof(uint32_t)/sizeof(char);i++)
	{
		buf[i+1] = ((char*)&Number)[i];
	}
	for(i=0;i<4;i++)
		buf[i+5] = ((char*)&port)[i];
	for(i=0;i<dataLength;i++)
	{
		buf[i+5+(sizeof(uint32_t)/sizeof(char))] = m.data[i];
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
	fprintf(stderr,"Beginning send port %d (htonsed), message id %d message kind %c response port %d message data %s \n",addr.sin_port,m.id,m.Kind,m.responseport,m.data);
	SerializeMessage(MessageBuf,m);
	fprintf(stderr,"Serialized message to %s \n",MessageBuf);
	if(TEMP_FAILURE_RETRY(sendto(fd,MessageBuf,sizeof(struct Message),0,&addr,sizeof(struct sockaddr_in)))<0) ERR("send:");	
	sleep(1);
}
pthread_mutex_t SuperMutex;
pthread_mutex_t MessageMutex;
pthread_mutex_t GateMutex;
void WaitOnMessage()
{
	fprintf(stderr,"Locking on Message\n");
	pthread_mutex_lock(&MessageMutex);
	fprintf(stderr,"Passed through Message\n");
}
void WaitOnSuper()
{
	fprintf(stderr,"Locking on Super\n");

	pthread_mutex_lock(&SuperMutex);
}
void WakeMessage()
{
	pthread_mutex_unlock(&MessageMutex);
}
void WaitOnGate()
{
	fprintf(stderr,"Locking on Gate\n");

	pthread_mutex_lock(&GateMutex);
}
void WakeGate()
{
	pthread_mutex_unlock(&GateMutex);
}
void WakeSuper()
{
	pthread_mutex_unlock(&SuperMutex);
}
void SuperReceiveMessage(int fd,struct Message* m,struct sockaddr_in* addr)
{
	char MessageBuf[MAXBUF];
	fprintf(stderr,"%p DEBUG",(void*)m);
	memset(MessageBuf,0,MAXBUF);
	socklen_t size = sizeof(struct sockaddr_in);
	while(1)
	{
		fprintf(stderr,"DEBUG Super\n");
		WaitOnMessage();
		fprintf(stderr,"Super passed mutex\n");
		if(TEMP_FAILURE_RETRY(recvfrom(fd,MessageBuf,sizeof(struct Message),MSG_PEEK,(struct sockaddr*)addr,&size))<0) ERR("read:");
		memset(m,0,sizeof(struct Message));
		DeserializeMessage(MessageBuf,m);
		fprintf(stderr,"Super peeked message with id= %d and type = %c\n",m->id,m->Kind);
		if(m->id==0)
		{
			if(TEMP_FAILURE_RETRY(recvfrom(fd,MessageBuf,sizeof(struct Message),0,(struct sockaddr*)addr,&size))<0) ERR("read:");
			addr->sin_port = m->responseport;
			WakeMessage();
			return;
		}
		WakeSuper();
		WakeMessage();
		sleep(1);
		fprintf(stderr,"Waiting on Gate");
		WaitOnGate();
	}
}
void ReceiveMessage(int fd,struct Message* m,struct sockaddr_in* addr,int expectedid,int passsecurity)
{
	char MessageBuf[MAXBUF];
	fprintf(stderr,"%p DEBUG",(void*)m);
	memset(MessageBuf,0,MAXBUF);
	socklen_t size = sizeof(struct sockaddr_in);
	while(1)
	{
if(!passsecurity)	WaitOnSuper();
	fprintf(stderr,"Regular passed through super (Expected id= %d\n",expectedid);
if(!passsecurity)	WaitOnMessage();
	fprintf(stderr,"Regular beginning read. Expected id = %d\n",expectedid);
	if(TEMP_FAILURE_RETRY(recvfrom(fd,MessageBuf,sizeof(struct Message),MSG_PEEK,(struct sockaddr*)addr,&size))<0) ERR("read:");
	fprintf(stderr,"DEBUG: ReceivedMessage %s , preparing for serialization\n",MessageBuf);
	memset(m,0,sizeof(struct Message));
	DeserializeMessage(MessageBuf,m);
	if(expectedid==0 || m->id==expectedid)
	{
	
		if(TEMP_FAILURE_RETRY(recvfrom(fd,MessageBuf,sizeof(struct Message),0,(struct sockaddr*)addr,&size))<0) ERR("read:");
		memset(m,0,sizeof(struct Message));
		DeserializeMessage(MessageBuf,m);
			addr->sin_port = m->responseport;
		if(!passsecurity) 
{
fprintf(stderr,"Waking the gate\n");
WakeGate();
}
		if(!passsecurity) WakeMessage();
		return;
	}
if(!passsecurity)	WakeMessage();
if(!passsecurity)	WakeSuper();
	}
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
	ReceiveMessage(listenfd,&m,&server,0,0);
	SendMessage(sendfd,m,server);
	ReceiveMessage(listenfd,&m,&server,m.id,0);
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
	strcpy(m.data,File);
	fprintf(stderr,"DEBUG: prepared file %s to write\n",File);
	SendMessage(sendfd,m,server);
	ReceiveMessage(listenfd,&m,&server,0,0);
	if(m.Kind!='D')
	{
		///ERR;
		return;
	}
	size = DeserializeNumber(m.data);
	fprintf(stderr,"DEBUG: received filesize of %d \n",size);
	SendMessage(sendfd,m,server);
	for(i=0;i<size;i++) fwrite(" ",1,1,F);
	
	while(1)
	{
		ReceiveMessage(listenfd,&m,&server,m.id,0);
		if(m.Kind == 'F')
		{
			break;
		}
		chunk = DeserializeNumber(m.data);
		fseek(F,chunk*dataLength,SEEK_SET);
		bulk_fwrite(F,m.data+4,dataLength);
		//TODO: Write in a file in exact position
		
	}
	//CALC md5 sum of file
	m = PrepareMessage(m.id,'F');
	//m.data = md5sum
	SendMessage(sendfd,m,server);
	ReceiveMessage(listenfd,&m,&server,m.id,0);
	if(m.Kind!='C')
	{
		//delete file;
	}
	
	
}
void UploadFile(int sendfd,int listenfd,struct sockaddr_in server,char* path)
{
	struct Message m = PrepareMessage(0,'U');
	int size; //getsize
	//add filename and size to data;
	SendMessage(sendfd,m,server);
	ReceiveMessage(listenfd,&m,&server,0,0);
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
		
		//TODO: Write in a file in exact position
	}
	//CALC md5 sum of file
	m = PrepareMessage(m.id,'F');
	
	SendMessage(sendfd,m,server);
	ReceiveMessage(listenfd,&m,&server,m.id,0);
	//if(m.data!=md5sum) uncorrect 
	
	
}
void DeleteFile(int sendfd,int listenfd,struct sockaddr_in server,char* path)
{
	struct Message m = PrepareMessage('M',0);
	SendMessage(sendfd,m,server);
	ReceiveMessage(listenfd,&m,&server,0,0);
	if(m.Kind == 'C')
	{
		fprintf(stdout,"File Deleted\n");
	}
	else
	{
		fprintf(stdout,"Unable to delete file %s\n",m.data);
	}
}
int DiscoverAddress(int broadcastfd,int port,struct sockaddr_in* server)
{
	struct sockaddr_in addr = {.sin_family=AF_INET, .sin_addr.s_addr=htonl(INADDR_BROADCAST), .sin_port=htons(port)};
	struct sockaddr_in temp;
	socklen_t size = sizeof(addr);
	
	int listenfd = bind_inet_socket(0,SOCK_DGRAM,INADDR_ANY,0);
	getsockname(listenfd,&temp,&size);
	
	
	
	
	
	listenport = temp.sin_port;
	struct Message m = PrepareMessage(0,'R');
	SerializeNumber(ntohs(temp.sin_port),m.data);
	SendMessage(broadcastfd,m,addr);
	ReceiveMessage(listenfd,&m,server,0,1);
	server->sin_port = htons(port);	
	return listenfd;
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
void* MessageQueueWork(void* arg)
{
	struct sockaddr_in client;
	struct Message m;
	int sp = *((int*)arg);
	while(1)
	{
		 SuperReceiveMessage(sp,&m,&client);
		
	}
	return NULL;
}
void StartListening(int* listenfd)
{
	pthread_t thread;
		pthread_create(&thread,NULL,MessageQueueWork,(void*)listenfd);
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
	pthread_mutex_init(&SuperMutex,NULL);
		pthread_mutex_init(&MessageMutex,NULL);
	//	pthread_mutex_init(&opID,NULL);
		pthread_mutex_init(&GateMutex,NULL);
		WaitOnGate();
		WaitOnSuper();
	broadcastfd=makesocket(SOCK_DGRAM,SO_BROADCAST);
	sendfd = makesocket(SOCK_DGRAM,0);
	listenfd = DiscoverAddress(broadcastfd,atoi(argv[1]),&server);
	StartListening(&listenfd);
	
	
	print_ip((long int)server.sin_addr.s_addr);
	DownloadFile(sendfd,listenfd,server,"mama.txt");
	
		pthread_mutex_destroy(&SuperMutex);
		pthread_mutex_destroy(&MessageMutex);
	//	pthread_mutex_destroy(&opID);
		pthread_mutex_destroy(&GateMutex);
	return 0;
	
}
