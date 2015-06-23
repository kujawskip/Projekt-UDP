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
volatile sig_atomic_t doWork;
void SigActionHandler(int k)
{
	if(k==SIGINT) doWork=0;
}
ssize_t bulk_fread(FILE* fd,char* buf,size_t count)
{
	int c;
	size_t len=0;
	do
	{		
		c=TEMP_FAILURE_RETRY(fread(buf,1,count,fd));
		
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
	int i,j=0;
	while(1)
	{
		i = bulk_fread(F,buf,1);
		if(i==0) return (j==0)?0:1;
		if(i<0) return -1;
		buf+=i;
		j+=i;
		if(*(buf-1)=='\n') return 1;
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
		
		if(c<0) return c;
		buf+=c;
		len+=c;
		count-=c;
	}
	while(count>0);
	return len ;
}
pthread_mutex_t OperationSaveMutex;
void SaveOperation(int id,char Kind,char* data,int finished)
{
	char buf[MAXBUF];
	char fdata[MAXFILE];
	char fkind;
	int fid,pos,temp;
	///TODO: AddSync
	if(finished)
	{
		fseek(OperationSaver,0,SEEK_SET);
		while(1)
		{
		pthread_mutex_lock(&OperationSaveMutex);
		pos = ftell(OperationSaver);
		if(ReadLine(OperationSaver,buf)<0) return;
		sscanf(buf,"id:%d kind:%c data:%s finished:%d",&fid,&fkind,fdata,&temp);
			if(id == fid)
			{
				fseek(OperationSaver,pos,SEEK_SET);
				pos = sprintf(buf,"id:%d kind:%c data:%s finished:%d\n",id,Kind,data,finished);
				if(pos<0)
				{
					pthread_mutex_unlock(&OperationSaveMutex);
					perror("SAVE");
					return;
				}
				bulk_fwrite(OperationSaver,buf,pos);
				fseek(OperationSaver,0,SEEK_SET);
				pthread_mutex_unlock(&OperationSaveMutex);
				return;
			}
		pthread_mutex_unlock(&OperationSaveMutex);
		}
	}
	else
	{
		struct stat sizeGetter;
		pos = fileno(OperationSaver);
		fstat(pos,&sizeGetter);
		temp = (int)sizeGetter.st_size;
		pthread_mutex_lock(&OperationSaveMutex);
		fseek(OperationSaver,temp,SEEK_SET);
		temp = sprintf(buf,"id:%d kind:%c data:%s finished:%d\n",id,Kind,data,finished);
		if(temp<0)
		{
			perror("SAVE");
			return;
		}
		bulk_fwrite(OperationSaver,buf,temp);
		fseek(OperationSaver,0,SEEK_SET);
		pthread_mutex_unlock(&OperationSaveMutex);
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
	SerializeMessage(MessageBuf,m);
	if(TEMP_FAILURE_RETRY(sendto(fd,MessageBuf,sizeof(struct Message),0,&addr,sizeof(struct sockaddr_in)))<0) ERR("send:");	
	sleep(1);
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
	memset(MessageBuf,0,MAXBUF);
	socklen_t size = sizeof(struct sockaddr_in);
	while(1)
	{
		WaitOnMessage();		
		while(recvfrom(fd,MessageBuf,sizeof(struct Message),MSG_PEEK,(struct sockaddr*)addr,&size)<0)
		{
			if(errno==EINTR)
			{
				if(doWork==0) return;			
			}
			else ERR("RECV");
		}
		memset(m,0,sizeof(struct Message));
		DeserializeMessage(MessageBuf,m);
		if(m->id==0)
		{
			while(recvfrom(fd,MessageBuf,sizeof(struct Message),0,(struct sockaddr*)addr,&size)<0)
			{
				if(errno==EINTR)
				{
					if(doWork==0) return;
				}
				else ERR("RECV");
			}
			addr->sin_port = m->responseport;
			WakeMessage();
			return;
		}
		WakeSuper();
		WakeMessage();
		sleep(1);
		WaitOnGate();
	}
}
void ReceiveMessage(int fd,struct Message* m,struct sockaddr_in* addr,int expectedid,int passsecurity)
{
	char MessageBuf[MAXBUF];	
	memset(MessageBuf,0,MAXBUF);
	socklen_t size = sizeof(struct sockaddr_in);
	while(1)
	{
	if(!passsecurity)	WaitOnSuper();	
	if(!passsecurity)	WaitOnMessage();
	while(recvfrom(fd,MessageBuf,sizeof(struct Message),MSG_PEEK,(struct sockaddr*)addr,&size)<0)
	{
		if(errno==EINTR)
		{
			if(doWork==0) return;			
		}
		else ERR("RECV");
	}
	memset(m,0,sizeof(struct Message));
	DeserializeMessage(MessageBuf,m);
	if(expectedid==0 || m->id==expectedid)
	{
	
		while(recvfrom(fd,MessageBuf,sizeof(struct Message),0,(struct sockaddr*)addr,&size)<0)
		{
			if(errno==EINTR)
			{
				if(doWork==0) return;
			}
		else ERR("RECV");
	}
		memset(m,0,sizeof(struct Message));
		DeserializeMessage(MessageBuf,m);
		addr->sin_port = m->responseport;
		if(!passsecurity) 
		{
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
	if(restart>0) m=PrepareMessage(restart,'R');
	ReceiveMessage(listenfd,&m,&server,0,0);	
	if(m.Kind != 'L')
	{
		fprintf(stderr,"Server responded - LS Impossible \n");
		return;
	}
	if(restart==0) SaveOperation(m.id,'L',"",0);
	size = DeserializeNumber(m.data);
	m.responseport = listenport;
	Dir = (char*)malloc(size*sizeof(char));
	if(Dir==NULL)
	{
		fprintf(stderr,"Malloc error");
		perror("malloc");
		return;
	}
	memset(Dir,0,size);
	SendMessage(sendfd,m,server);
	while(1)
	{
		ReceiveMessage(listenfd,&m,&server,m.id,0);
		if(m.Kind == 'F') break;
		chunk = DeserializeNumber(m.data);		
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
	if(restart>0) m=PrepareMessage(restart,'R');
	strcpy(m.data,File);
	SendMessage(sendfd,m,server);
	ReceiveMessage(listenfd,&m,&server,0,0);
	if(m.Kind!='D')
	{
		fclose(F);
		fprintf(stderr,"File: %s not found on the server",File);
		return;
	}
	if(restart==0) SaveOperation(m.id,m.Kind,path,0);
	size = DeserializeNumber(m.data);
	SendMessage(sendfd,m,server);
	for(i=0;i<size;i++) fwrite(" ",1,1,F);	
	while(1)
	{
		ReceiveMessage(listenfd,&m,&server,m.id,0);
		if(m.Kind == 'F') break;				
		chunk = DeserializeNumber(m.data);
		fseek(F,chunk*(dataLength-Preamble),SEEK_SET);
		bulk_fwrite(F,m.data+4,dataLength);
	}
	fclose(F);
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
	}
	else
	{
		fprintf(stdout,"Finished downloading file %s \n",File);
	}	
}
void UploadFile(int sendfd,int listenfd,struct sockaddr_in server,char* FilePath,int restart)
{
	struct Message m = PrepareMessage(0,'U');	
	int size;
	char md5_sum[MD5_LEN];	
    FILE * F;
	struct stat sizeGetter;
	int count,i;
	if(restart>0) m=PrepareMessage(restart,'R');
	stat(FilePath,&sizeGetter);
	size = (int)sizeGetter.st_size;
	SerializeNumber(size,m.data);
	strcpy(m.data+4,FilePath);
	count = 1+(((int)sizeGetter.st_size)/(dataLength-4));		
	SendMessage(sendfd,m,server);
	ReceiveMessage(listenfd,&m,&server,0,0);
	if(restart==0) SaveOperation(m.id,'U',FilePath,0);
	F = fopen(FilePath,"a+");
	fclose(F);
	F = fopen(FilePath,"r");
	
	if(m.Kind!='U')
	{
		fprintf(stderr,"Upload of file %s failed\n",FilePath); 
		return;
	}
	for(i =0;i<count;i++)
	{
		if(i>0) sleepforseconds(1);
		m = PrepareMessage(m.id,'U');
		SerializeNumber(i,m.data);
	
		bulk_fread(F,m.data+4,dataLength-4);
		SendMessage(sendfd,m,server);		
	}
	m = PrepareMessage(m.id,'F');	
	SendMessage(sendfd,m,server);
	ReceiveMessage(listenfd,&m,&server,m.id,0);	
	if(CalcFileMD5(FilePath,md5_sum)==0)
	{
		m = PrepareMessage(m.id,'E');
		SendMessage(sendfd,m,server);
		return;
	}
	SaveOperation(m.id,'U',FilePath,1);
	if(0!=strcmp(m.data,md5_sum))
	{
		m = PrepareMessage(m.id,'E');
		fprintf(stderr,"Server sent wrong md5sum id:%d file:%s\n",m.id,FilePath);
		SendMessage(sendfd,m,server);
		return;
	}
	m = PrepareMessage(m.id,'C');	
	SendMessage(sendfd,m,server);		
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
pthread_t StartListening(int* listenfd)
{
	pthread_t thread;
	if(pthread_create(&thread,NULL,MessageQueueWork,(void*)listenfd) <0) ERR("START LISTENING");
	return thread;
}
struct ThreadArg
{
	int sendfd;
	int listenfd;
	struct sockaddr_in address;
	int restart;
	char Kind;
	char data[MAXBUF];
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
pthread_t StartOperation(int sendfd,int listenfd,struct sockaddr_in address,char* data,char Kind,int restart,struct ThreadArg* trarg)
{
	pthread_t thread;
	trarg->sendfd = sendfd;
	trarg->listenfd=listenfd;
	memcpy((void*)&(trarg->address),(void*)&address,sizeof(struct sockaddr_in));
	trarg->restart=restart;
	trarg->Kind=Kind;
	strcpy(trarg->data,data);
	if(pthread_create(&thread,NULL,BeginOperation,(void*)(trarg))<0) ERR("THREAD CREATE");
	return thread;
}
void RestoreOperations(int sendfd,int listenfd,struct sockaddr_in address,pthread_t* ts,int* ti)
{
	char buf[MAXBUF];
	char fdata[MAXBUF];
	char fkind;
	int fid,temp;
	memset(buf,0,MAXBUF);
	while(1)
	{
		struct ThreadArg trarg;		
		if(ReadLine(OperationSaver,buf)<=0) return;		
		sscanf(buf,"id:%d kind:%c data:%s finished:%d",&fid,&fkind,fdata,&temp);	
		if(0==temp)
		{
			fprintf(stdout,"Restarting operation of id: %d, kind: %c %s\n",fid,fkind,fdata);
			ts[(*ti)++] = StartOperation(sendfd,listenfd,address,fdata,fkind,fid,&trarg);
		}
	}
}
void MenuWork(int sendfd,int listenfd,struct sockaddr_in server)
{
	int ti,i;
	struct ThreadArg t;
	pthread_t Threads[MAXBUF];
	Threads[0] = StartListening(&listenfd);
	ti=1;
	RestoreOperations(sendfd,listenfd,server,Threads,&ti);
	while(doWork)
	{
		char buf[MAXBUF];
		fprintf(stdout,"Menu: DELETE DOWNLOAD LS UPLOAD\n");
		scanf("%s",buf);
		if(strcmp(buf,"DELETE")==0)
		{
			fprintf(stdout,"Input filename\n");
			scanf("%s",buf);
			Threads[ti++] = StartOperation(sendfd,listenfd,server,buf,'M',0,&t);
		}
		else if(strcmp(buf,"LS") == 0)
		{			
			Threads[ti++] = StartOperation(sendfd,listenfd,server,"",'L',0,&t);
		}
		else if(strcmp(buf,"DOWNLOAD") == 0)
		{
			fprintf(stdout,"Input filename\n");
			scanf("%s",buf);
			Threads[ti++] = StartOperation(sendfd,listenfd,server,buf,'D',0,&t);
		}
		else if(strcmp(buf,"UPLOAD")==0)
		{
			fprintf(stdout,"Input filename\n");
			scanf("%s",buf);
			Threads[ti++] = StartOperation(sendfd,listenfd,server,buf,'U',0,&t);
		}
	}
	for(i=0;i<ti;i++) pthread_cancel(Threads[i]);
}
int main(int argc,char** argv)
{
	int listenfd,broadcastfd,sendfd;
	struct sockaddr_in server;
	struct sigaction new_sa;
	sigfillset(&new_sa.sa_mask);
	new_sa.sa_handler = SigActionHandler;
	new_sa.sa_flags = 0;
	doWork=1;
	if (sigaction(SIGINT, &new_sa, NULL)<0)
	{
		ERR("SIGINT SIGACTION");
	}
	if(argc!=2) 
	{
		usage(argv[0]);
		return EXIT_FAILURE;
	}
	OperationSaver = fopen(savefile,"a+");
	fclose(OperationSaver);
	OperationSaver = fopen(savefile,"r+");
	memset(&server,0,sizeof(struct sockaddr_in));
	pthread_mutex_init(&SuperMutex,NULL);
	pthread_mutex_init(&OperationSaveMutex,NULL);
	pthread_mutex_init(&MessageMutex,NULL);
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
	MenuWork(sendfd,listenfd,server);
	print_ip((long int)server.sin_addr.s_addr);	
	fclose(OperationSaver);
	pthread_mutex_destroy(&SuperMutex);
	pthread_mutex_destroy(&MessageMutex);
	pthread_mutex_destroy(&GateMutex);
	pthread_mutex_destroy(&OperationSaveMutex);
	return 0;	
}
