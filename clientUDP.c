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
#include <ctype.h>
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
#define STR_VALUE(val) #val
#define STR(name) STR_VALUE(name)

#define PATH_LEN 256
#define MD5_LEN 32
#define savefile "savefile.dat"
FILE* OperationSaver;
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
int ReadLine(FILE* F,char* buf)
{
	int i;
	while(1)
	{
		i = bulk_fread(F,buf,1);
		if(i==0) return 0;
		if(i<0) return -1;
		buf+=i;
		if(*(buf-1)=='\n') return 0;
	}
}
ssize_t bulk_fwrite(FILE* fd,char* buf,size_t count)
{
	int c;
	size_t len=0;
	len = strlen(buf);
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
void SaveOperation(int id,char Kind,char* data,int finished)
{
	char buf[MAXBUF];
	char fdata[MAXFILE];
	char fkind;
	int fid,pos,temp;
	
	if(finished)
	{
		fseek(OperationSaver,0,SEEK_SET);
		while(1)
		{
		pos = ftell(OperationSaver);
		if(ReadLine(OperationSaver,buf)<0) return;
		sscanf(buf,"id:%d kind:%c data:%s finished:%d",&fid,&fkind,fdata,&temp);
			if(id == fid)
			{
				fseek(OperationSaver,pos,SEEK_SET);
				pos = sprintf(buf,"id:%d kind %c data:%s finished:%d\n",id,Kind,data,finished);
				bulk_fwrite(OperationSaver,buf,pos);
				fseek(OperationSaver,0,SEEK_SET);
				return;
			}
		}
	}
	else
	{
		struct stat sizeGetter;
		pos = fileno(OperationSaver);
		fstat(pos,&sizeGetter);
		temp = (int)sizeGetter.st_size;
		fseek(OperationSaver,temp,SEEK_SET);
		temp = sprintf(buf,"id:%d kind %c data:%s finished:%d\n",id,Kind,data,finished);
		bulk_fwrite(OperationSaver,buf,temp);
		fseek(OperationSaver,0,SEEK_SET);
	}
}
void sleepforseconds(int sec)
{
	struct timespec tim, tim2;
   tim.tv_sec = 1;
   tim.tv_nsec = 0;

   while(nanosleep(&tim , &tim2) < 0 )   
   {
     if (errno==EINTR)
	 {
		 tim = tim2;
	 }
	 else ERR("SLEEP");
   }


}
int CalcFileMD5(char *file_name, char *md5_sum)
{
    #define MD5SUM_CMD_FMT "md5sum %." STR(PATH_LEN) "s 2>/dev/null"
    char cmd[PATH_LEN + sizeof (MD5SUM_CMD_FMT)];
    sprintf(cmd, MD5SUM_CMD_FMT, file_name);
    #undef MD5SUM_CMD_FMT

    FILE *p = popen(cmd, "r");
    if (p == NULL) return 0;

    int i, ch;
    for (i = 0; i < MD5_LEN && isxdigit(ch = fgetc(p)); i++) {
        *md5_sum++ = ch;
    }

    *md5_sum = '\0';
    pclose(p);
    return i == MD5_LEN;
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
	m.responseport = listenport;
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
void ViewDirectory(int sendfd,int listenfd,struct sockaddr_in server,int restart)
{
	struct Message m = PrepareMessage(0,'L');
	char* Dir;
	int size,i,chunk;
	SendMessage(sendfd,m,server);
	ReceiveMessage(listenfd,&m,&server,0,0);
	
	if(m.Kind != 'L')
	{
		
	}
	SaveOperation(m.id,'L',"",0);
	size = DeserializeNumber(m.data);
	m.responseport = listenport;
	Dir = (char*)malloc(size*sizeof(char));
	if(Dir==NULL)
	{
		
	}
	SendMessage(sendfd,m,server);
	while(1)
	{
		ReceiveMessage(listenfd,&m,&server,m.id,0);
		if(m.Kind == 'F')
		{
			break;
		}
		
		chunk = DeserializeNumber(m.data);
		fprintf(stderr,"DEBUG: %s \n",m.data+4);
		for(i=0;i<dataLength-Preamble;i++)
		{
			if(m.data[i+4]=='\0') break;
			Dir[(chunk *(dataLength-Preamble))+i] =m.data[i+4];
		}
	}
	SaveOperation(m.id,'L',"",1);
	bulk_write(1,Dir,size);
	free(Dir);
}
void RenameFile(char* FilePath)
{
	char FileName[dataLength];
		strcpy(FileName,FilePath);
		strcat(FileName,".err");
		if(rename(FilePath,FileName)<0)
		{
			fprintf(stderr,"File %s:",FilePath);
				perror("Error renaming the file");
		}
}
void DownloadFile(int sendfd,int listenfd,struct sockaddr_in server,char* path,int restart)
{
	
	char File[dataLength],md5_sum[MD5_LEN];
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
		fclose(F);
		fprintf(stderr,"File: %s",File);
		perror("File not found");
		return;
	}
	SaveOperation(m.id,m.Kind,path,0);
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
		fprintf(stderr,"DEBUG: Received chunk %d of file %s id: %d \n",chunk,File,m.id);
		fseek(F,chunk*(dataLength-Preamble),SEEK_SET);
		bulk_fwrite(F,m.data+4,dataLength);
		//TODO: Write in a file in exact position
		
	}
	fprintf(stderr,"DEBUG: Finished receiving file %s id: %d \n",File,m.id);
	fclose(F);
	//CALC md5 sum of file
	if(CalcFileMD5(File,md5_sum)==0)
	{
		fprintf(stderr,"Error calculating md5 checksum of file %s \n",File);
		RenameFile(File);
		return;
	}

	m = PrepareMessage(m.id,'F');
	strcpy(m.data,md5_sum);
	SendMessage(sendfd,m,server);
	ReceiveMessage(listenfd,&m,&server,m.id,0);
	SaveOperation(m.id,'D',path,1);
	if(m.Kind!='C')
	{
		RenameFile(File);
		//delete file;
	}
	else
	{
		fprintf(stdout,"Finished downloading file %s \n",File);
	}
	
}
void UploadFile(int sendfd,int listenfd,struct sockaddr_in server,char* FilePath,int restart)
{
	struct Message m = PrepareMessage(0,'U');
	int size; //getsize
	//add filename and size to data;
	char md5_sum[MD5_LEN];	
    FILE * F;
	struct stat sizeGetter;
	int count,i;
	stat(FilePath,&sizeGetter);
	size = (int)sizeGetter.st_size;
	SerializeNumber(size,m.data);
	strcpy(m.data+4,FilePath);
		count = 1+(((int)sizeGetter.st_size)/(dataLength-4));
	SendMessage(sendfd,m,server);
	ReceiveMessage(listenfd,&m,&server,0,0);
	SaveOperation(m.id,'U',FilePath,0);
	F = fopen(FilePath,"r");
	if(m.Kind!='U')
	{
		///ERR
		return;
	}

	///Dziel plik na fragmenty a następnie rozsyłaj
	for(i =0;i<count;i++)
	{
		if(i>0) sleepforseconds(1);
		fprintf(stderr,"DEBUG: Sending Part %d of %d of file %s id %d\n",i,count,FilePath,m.id);
		m = PrepareMessage(m.id,'U');
		SerializeNumber(i,m.data);
		fprintf(stderr,"DEBUG: Preparing Read from file\n");
		bulk_fread(F,m.data+4,dataLength-4);
		SendMessage(sendfd,m,server);
		
		
		
	}
	//CALC md5 sum of file
	m = PrepareMessage(m.id,'F');
	
	SendMessage(sendfd,m,server);
	ReceiveMessage(listenfd,&m,&server,m.id,0);
	if(CalcFileMD5(FilePath,md5_sum)==0)
	{
		fprintf(stderr,"Error calculating md5 checksum of file %s \n",FilePath);	
		m = PrepareMessage(m.id,'E');
		SendMessage(sendfd,m,server);
		return;
	}
	fprintf(stderr,"DEBUG: comparing %s %s",m.data,md5_sum);
	SaveOperation(m.id,'U',FilePath,1);
	if(0!=strcmp(m.data,md5_sum))
	{
		m = PrepareMessage(m.id,'E');
		SendMessage(sendfd,m,server);
		return;
	}
	m = PrepareMessage(m.id,'C');
	
	SendMessage(sendfd,m,server);
	
	//if(m.data!=md5sum) uncorrect 
	
	
}
void DeleteFile(int sendfd,int listenfd,struct sockaddr_in server,char* path,int restart)
{
	struct Message m = PrepareMessage(0,'M');
	strcpy(m.data,path);
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
struct ThreadArg
{
	int sendfd;
	int listenfd;
	struct sockaddr_in address;
	char data[MAXBUF];
	int restart;
	char Kind;
};
void* BeginOperation(void * arg)
{
	struct ThreadArg trarg = *((struct ThreadArg*)arg);
	if(trarg.Kind=='D')
	{
		DownloadFile(trarg.sendfd,trarg.listenfd,trarg.address,trarg.data,trarg.restart);
	}
	else if(trarg.Kind=='U')
	{
		UploadFile(trarg.sendfd,trarg.listenfd,trarg.address,trarg.data,trarg.restart);
	}
	else if(trarg.Kind=='M')
	{
		DeleteFile(trarg.sendfd,trarg.listenfd,trarg.address,trarg.data,trarg.restart);
	}
	else if(trarg.Kind=='L')
	{
		ViewDirectory(trarg.sendfd,trarg.listenfd,trarg.address,trarg.restart);
	}
	
	return NULL;

}
void StartOperation(int sendfd,int listenfd,struct sockaddr_in address,char* data,char Kind,int restart,struct ThreadArg* trarg)
{
	(*trarg) = {.sendfd = sendfd,.listenfd=listenfd,.address = address,.restart=restart,.Kind=Kind};
	strcpy(trarg.data,data);
	pthread_t thread;
	pthread_create(&thread,BeginOperation,(void*)(trarg));
}
void RestoreOperations(int sendfd,int listenfd,struct sockaddr_in address)
{
	char buf[MAXBUF];
	char fdata[MAXFILE];
	char fkind;
	int fid,temp;
		while(1)
		{
			struct ThreadArg trarg;
		pos = ftell(OperationSaver);
		if(ReadLine(OperationSaver,buf)<0) return;
		sscanf(buf,"id:%d kind:%c data:%s finished:%d",&fid,&fkind,fdata,&temp);
			if(0==temp)
			{
				StartOperation(sendfd,listenfd,address,fdata,fkind,fid,&trarg);
			}
		}
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
	OperationSaver = fopen(savefile,"w+");
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
	if(listenport == 0)
	{
		ERR("PORT:");
	}
	StartListening(&listenfd);
	
	RestoreOperations(sendfd,listenfd,server);
	print_ip((long int)server.sin_addr.s_addr);
	struct ThreadArg t;
	while(1)
	{
		
		char buf[MAXBUF];
		fprintf(stdout,"Menu: DELETE DOWNLOAD LS UPLOAD\n");
		scanf("%s",buf);
		if(strcmp(buf,"DELETE")==0)
		{
			fprintf(stdout,"Input filename\n");
			scanf("%s",buf);
			StartOperation(sendfd,listenfd,server,buf,'M',0,&t);
		}
		else if(strcmp(buf,"LS") == 0)
		{
			
			StartOperation(sendfd,listenfd,server,"",'L',0,&t);
		}
		else if(strcmp(buf,"DOWNLOAD") == 0)
		{
			fprintf(stdout,"Input filename\n");
			scanf("%s",buf);
			StartOperation(sendfd,listenfd,server,buf,'D',0,&t);
		}
		else if(strcmp(buf,"UPLOAD")==0)
		{
			fprintf(stdout,"Input filename\n");
			scanf("%s",buf);
			StartOperation(sendfd,listenfd,server,buf,'U',0,&t);
		}
	}
	
		pthread_mutex_destroy(&SuperMutex);
		pthread_mutex_destroy(&MessageMutex);
	//	pthread_mutex_destroy(&opID);
		pthread_mutex_destroy(&GateMutex);
	return 0;
	
}
