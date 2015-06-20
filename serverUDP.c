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
#define MAXBUF 1024
#define BACKLOG 3
#define MAXFILE 1024
#define MAXDIR 1024

struct DirFile
{
	char Name[MAXFILE];
	char Op;
	int perc;
};
struct DirFile files[MAXDIR];
int DirLen;
pthread_mutex_t opID;
int opid;
uint32_t GenerateOpID()
{
	pthread_mutex_lock(&opID);
	return opid++;
	pthread_mutex_unlock(&opID);
}
void LockDirectory()
{
}
void LockFile(int id)
{
}
void UnLockDirectory()
{
}
void UnLockFile(int id)
{
	
}


void DecodeFile(char* buf,struct DirFile f)
{
	switch(f.Op)
	{
		case 'D':
	sprintf(buf,"%s Downloading %d/100 \n",f.Name,f.perc);
	return;
	case 'U':
	sprintf(buf,"%s Uploading %d/100 \n",f.Name,f.perc);
	return;
	}
	sprintf(buf,"%s",f.Name);
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
			fprintf(stderr,"DEBUG: response port = 0 (listenport = %d) \n",listenport);
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
	for(i=0;i<dataLength;i++) m->data[i] = buf[i+9];
}
void SerializeMessage(char* buf,struct Message m)
{
	int i;
	uint32_t Number = htonl(m.id);
	int port = m.responseport;
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
void SendMessage(int fd,struct Message m,struct sockaddr_in addr)
{
	
	char MessageBuf[MAXBUF];
	m.responseport = listenport;
	memset(MessageBuf,0,MAXBUF);
	SerializeMessage(MessageBuf,m);
	fprintf(stderr,"Beginning send port %d (htonsed), message id %d message kind %c response port %d message data %s \n",addr.sin_port,m.id,m.Kind,m.responseport,m.data);
	if(TEMP_FAILURE_RETRY(sendto(fd,MessageBuf,sizeof(struct Message),0,&addr,sizeof(addr)))<0) ERR("send:");	
}
pthread_mutex_t SuperMutex;
pthread_mutex_t MessageMutex;
pthread_mutex_t GateMutex;
void WaitOnMessage()
{
	pthread_mutex_lock(&MessageMutex);
}
void WaitOnSuper()
{
	pthread_mutex_lock(&SuperMutex);
}
void WakeMessage()
{
	pthread_mutex_unlock(&MessageMutex);
}
void WaitOnGate()
{
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
		memset(m,0,sizeof(struct Message));
		DeserializeMessage(MessageBuf,m);
		fprintf(stderr,"Changing port from %d to %d\n",addr->sin_port,m->responseport);
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
void ReceiveMessage(int fd,struct Message* m,struct sockaddr_in* addr,int expectedid)
{
	char MessageBuf[MAXBUF];
	fprintf(stderr,"%p DEBUG",(void*)m);
	memset(MessageBuf,0,MAXBUF);
	socklen_t size = sizeof(struct sockaddr_in);
	while(1)
	{
	WaitOnSuper();
	fprintf(stderr,"Regular passed through super (Expected id= %d\n");
	WaitOnMessage();
	if(TEMP_FAILURE_RETRY(recvfrom(fd,MessageBuf,sizeof(struct Message),MSG_PEEK,(struct sockaddr*)addr,&size))<0) ERR("read:");
	fprintf(stderr,"DEBUG: ReceivedMessage %s , preparing for serialization\n",MessageBuf);
	memset(m,0,sizeof(struct Message));
	DeserializeMessage(MessageBuf,m);
	if(m->id==expectedid)
	{
	
		if(TEMP_FAILURE_RETRY(recvfrom(fd,MessageBuf,sizeof(struct Message),0,(struct sockaddr*)addr,&size))<0) ERR("read:");
		memset(m,0,sizeof(struct Message));
		DeserializeMessage(MessageBuf,m);
		addr->sin_port = m->responseport;
		WakeMessage();
		WakeGate();
		return;
	}
	WakeMessage();
	WakeSuper();
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
ssize_t bulk_fread(FILE* fd,char* buf,size_t count)
{
	int c;
	size_t len=0;
	do
	{
		
		c=TEMP_FAILURE_RETRY(fread(buf,1,count,fd));
		fprintf(stderr,"DEBUG: Fread %d msg: %s\n",c,buf);
		if(c==0) break;
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
	len = strlen(buf)+1;
	if(count>len) count=len;
	len = 0;
	do
	{
		c=TEMP_FAILURE_RETRY(fwrite(buf,1,count,fd));
		fflush(fd);
		fprintf(stderr,"DEBUG: Fwrite %d msg: %s\n",c,buf);
		if(c<0) return c;
		buf+=c;
		len+=c;
		count-=c;
	}
	while(count>0);
	return len ;
}
ssize_t bulk_write(int fd, char *buf, size_t count)
{
	int c;
	size_t len=0;
	do
	{
		c=TEMP_FAILURE_RETRY(write(fd,buf,count));
		if(c<0) return c;
		buf+=c;
		len+=c;
		count-=c;
	}
	while(count>0);
	return len ;
}
char DirectoryPath[MAXDIR];
void DownloadFile(int sendfd,int listenfd,struct Message m,struct sockaddr_in address)
{
	char File[MAXDIR];
		strcpy(File,m.data);
		fprintf(stderr,"DEBUG: File Data %s %s \n",File,m.data);
char FilePath[MAXDIR];
memset(FilePath,0,MAXDIR);
strcat(FilePath,DirectoryPath);
strcat(FilePath,"/");
strcat(FilePath,File);
    FILE * F = fopen(FilePath,"r");
	struct stat sizeGetter;
	int count,i,id,fd;
	stat(File,&sizeGetter);
	fprintf(stderr,"Received file stats for downloading\n");
	m = PrepareMessage(GenerateOpID(),'D');
	id = m.id;
	fprintf(stderr,"Initializing downloading of a file %s generated id: %d\n",FilePath,id);

		//getfile size and put it into data
	SerializeNumber((uint32_t)sizeGetter.st_size,m.data);
	fprintf(stderr,"Preparing to send filesize %ld\n",sizeGetter.st_size);
	SendMessage(sendfd,m,address);
	ReceiveMessage(listenfd,&m,&address,m.id);
	count = 1+(((int)sizeGetter.st_size)/dataLength);
	
	///Dziel plik na fragmenty a następnie rozsyłaj
	for(i =0;i<count;i++)
	{
		fprintf(stderr,"DEBUG: Sending Part %d of %d of file %s id %d\n",i,count,File,m.id);
		m = PrepareMessage(id,'D');
		SerializeNumber(i,m.data);
		fprintf(stderr,"DEBUG: Preparing Read from file\n");
		bulk_fread(F,m.data+4,dataLength);
		SendMessage(sendfd,m,address);
		
		
		
	}
	//CALC md5 sum of file
	fprintf(stderr,"DEBUG: Sent whole file id: %d filename %s \n",m.id,File);
	m = PrepareMessage(m.id,'F');
	
	SendMessage(sendfd,m,address);
	
	
	ReceiveMessage(listenfd,&m,&address,m.id);
	
	//if(m.data!=md5sum) uncorrect
	//else
	m = PrepareMessage(m.id,'C');
	SendMessage(sendfd,m,address);
	fclose(F);
}
void AddFile(char* FileName)
{
	LockDirectory();
	files[DirLen].Op='N';
	files[DirLen].perc=0;
	strcpy(files[DirLen].Name,FileName);
	UnLockDirectory();
}
void UploadFile(int sendfd,int listenfd,struct Message m,struct sockaddr_in address)
{
	char File[dataLength];
	
	FILE* F;
	int i;
	uint32_t chunk;
	int size;
	strcpy(File,m.data);
	F = fopen(File,"w+");
	m = PrepareMessage(GenerateOpID(),'D');
	
	SendMessage(sendfd,m,address);
	ReceiveMessage(listenfd,&m,&address,m.id);
	if(m.Kind!='C')
	{
		///ERR;
		
		return;
	}
	size = DeserializeNumber(m.data);
	for(i=0;i<size;i++)
	{
		fwrite(" ",1,1,F);
	}
	fseek(F,0,SEEK_SET);
	SendMessage(sendfd,m,address);
	while(1)
	{
		ReceiveMessage(listenfd,&m,&address,m.id);
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
	SendMessage(sendfd,m,address);
	ReceiveMessage(listenfd,&m,&address,m.id);
	fclose(F);
	
	if(m.Kind!='C')
	{
		fprintf(stderr,"Error creating file %s\n",File);
		rename(File,"err.tmp");//delete file;
	}
	else
	{
		AddFile(File);
	}
}

void DeleteFile(int sendfd,int listenfd,struct Message m,struct sockaddr_in address)
{
	char* name = m.data;
	int i;
	LockDirectory();
	m.id = GenerateOpID();
	fprintf(stderr,"DEBUG: Going to generate %d comparisons\n",DirLen);
	for(i=0;i<DirLen;i++)
	{
		fprintf(stderr,"DEBUG: Comparing %s %s \n",name,files[i].Name);
		if(0==strcmp(name,files[i].Name))
		{
			char FilePath[MAXDIR];
memset(FilePath,0,MAXDIR);
strcat(FilePath,DirectoryPath);
strcat(FilePath,"/");
strcat(FilePath,name);
			int j;
	
			
			if(files[i].Op != 'N')
			{
				fprintf(stderr,"File is busy %s \n",name);
				UnLockDirectory();
				break;
			}
					//Delete file from disk
			if(unlink(FilePath)<0)
			{
				fprintf(stderr,"Error unlinking the file %s \n",name);
				UnLockDirectory();
				break;
			}
			fprintf(stderr,"DEBUG: moving files %d %d \n",i,DirLen);
			for(j=i;j<DirLen-1;j++)
			{
				files[j]=files[j+1];
			}
			DirLen--;
			m = PrepareMessage(m.id,'C');
			SendMessage(sendfd,m,address);
			UnLockDirectory();
			return;
		}
	}
	m = PrepareMessage(m.id,'E');
	SendMessage(sendfd,m,address);
}
void ListDirectory(int sendfd,int listenfd,struct Message m,struct sockaddr_in address)
{
	
}
struct Thread_Arg
{
	struct Message m;
	int sendfd;
	int listenfd;
	struct sockaddr_in address;
	
};
void RegisterClient(int fd,int fd2,struct Message m,struct sockaddr_in client)
{
		client.sin_port = m.responseport;
		SendMessage(fd2,m,client);
}
void* HandleMessage(void* arg)
{
	struct Thread_Arg t = *((struct Thread_Arg*)(arg));
	if(t.m.Kind=='D')
	{
		DownloadFile(t.sendfd,t.listenfd,t.m,t.address);
	}
	else if(t.m.Kind=='U')
	{
		UploadFile(t.sendfd,t.listenfd,t.m,t.address);
	}
	else if(t.m.Kind=='M')
	{
		DeleteFile(t.sendfd,t.listenfd,t.m,t.address);
	}
	else if(t.m.Kind=='L')
	{
		ListDirectory(t.sendfd,t.listenfd,t.m,t.address);
	}
	else if(t.m.Kind == 'R')
	{
		RegisterClient(t.listenfd,t.sendfd,t.m,t.address);
	}
	return NULL;
}

void MessageQueueWork(int listenfd,int sendfd)
{
	struct sockaddr_in client;
	struct Message m;
	while(1)
	{
		pthread_t thread;
		struct Thread_Arg t;
		SuperReceiveMessage(listenfd,&m,&client);
		
		t.listenfd = listenfd;
		t.sendfd = sendfd;
		memcpy((void*)&(t.address),(void*)&client,sizeof(struct sockaddr_in));
		memcpy((void*)&(t.m),(void*)&m,sizeof(struct Message));
			pthread_create(&thread,NULL,HandleMessage,(void*)&t);
		}
}
void usage(char* c)
{
	fprintf(stderr,"USAGE: %s port directory\n",c);
}
void print_ip(unsigned long int ip)
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
		int listenfd,sendfd,i,otherfd;
		struct sockaddr_in client;
		
		struct dirent* dirStruct;
		DIR* directory;
		struct Message m;
		opid=1;
		
		if(argc!=3)
		{
			usage(argv[0]);
			return EXIT_FAILURE;
		}
		realpath(argv[2],DirectoryPath);
		directory = opendir(DirectoryPath);
		if(directory == NULL)
		{
			ERR("Can't open directory");
		}
		
		pthread_mutex_init(&SuperMutex,NULL);
		pthread_mutex_init(&MessageMutex,NULL);
		pthread_mutex_init(&opID,NULL);
		pthread_mutex_init(&GateMutex,NULL);
		WaitOnGate();
		WaitOnSuper();
		memset(&m,0,sizeof(struct Message));
		memset(&client,0,sizeof(struct sockaddr_in));
		listenport = htons(atoi(argv[1]));
		if(listenport == 0)
		{
			ERR("PORT:");
		}
		while(1)
		{
			dirStruct = readdir(directory);
			if(dirStruct == NULL)
			{
				break;
			}
			strcpy(files[DirLen].Name,dirStruct->d_name);
			files[DirLen].Op='N';
			files[DirLen].perc=0;
			DirLen++;
		}
		fprintf(stdout,"Prepared Directory List\n");
			//Prepare list of files in directory
		listenfd = bind_inet_socket(atoi(argv[1]),SOCK_DGRAM,INADDR_ANY,SO_BROADCAST);
	
		sendfd = makesocket(SOCK_DGRAM,0);
		MessageQueueWork(listenfd,sendfd);
		
		pthread_mutex_destroy(&SuperMutex);
		pthread_mutex_destroy(&MessageMutex);
		pthread_mutex_destroy(&opID);
		pthread_mutex_destroy(&GateMutex);
return 0;
		
}
