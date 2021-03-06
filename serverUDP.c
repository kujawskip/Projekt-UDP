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
#define MAXBUF 1024
#define BACKLOG 3
#define MAXFILE 1024
#define MAXDIR 1024
#define MAXTASK 1000000
#define STR_VALUE(val) #val
#define STR(name) STR_VALUE(name)

#define PATH_LEN 256
#define MD5_LEN 32
int minid;
volatile sig_atomic_t doWork;
void SigActionHandler(int k)
{
	if(k==SIGINT) doWork=0;
}
char RetiredIDs[MAXTASK];
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
struct DirFile
{
	char Name[MAXFILE];
	char Op;
	int perc;
	int am;
};
volatile struct DirFile files[MAXDIR];
volatile int DirLen;
pthread_mutex_t opID;
volatile int opid;
FILE* TaskReporter;
uint32_t GenerateOpID(int* id,char TaskType)
{
	char buf[7];
	if(*id>0) return *id;
	pthread_mutex_lock(&opID);
	*id = opid++;
	sprintf(buf,"%d %c\n",*id,TaskType);
	bulk_fwrite(TaskReporter,buf,6);
	pthread_mutex_unlock(&opID);
	return *id;
}

pthread_mutex_t directorymutex;
pthread_mutex_t filemutex[MAXDIR];
void LockDirectory()
{
	pthread_mutex_lock(&directorymutex);
}
void UnLockDirectory()
{
	pthread_mutex_unlock(&directorymutex);
}
void LockFile(int id)
{
	LockDirectory();
	pthread_mutex_lock(&filemutex[id]);
	UnLockDirectory();
}

void UnLockFile(int id)
{
		LockDirectory();
	pthread_mutex_unlock(&filemutex[id]);
	UnLockDirectory();
}
int DecodeFile(char* buf,struct DirFile f)
{
	switch(f.Op)
	{
		case 'D':
	return sprintf(buf,"%s Downloading %d/1000 \n",f.Name,f.perc);
	 
	case 'U':
	return sprintf(buf,"%s Uploading %d/1000 \n",f.Name,f.perc);
	
	}
	return sprintf(buf,"%s\n",f.Name);
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
		if(m->id==0 || (m->id<minid && m->Kind == 'R'))
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
		WakeMessage();
		return;
		}	
		WakeSuper();
		WakeMessage();
		sleep(1);
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
	if(m->id==expectedid)
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
int bind_inet_socket(uint16_t port,int type,uint32_t addres,int flag)
{
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
	memset(buf,0,count);
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
void PrepareAndSendMessage(int it,struct sockaddr_in address,int id,char k)
{
	struct Message m = PrepareMessage(id,k);
	SendMessage(it,m,address);
}
int FindFileId(char* name)
{
	int i;
	for(i=0;i<DirLen;i++)
	{
		fprintf(stderr,"DEBUG: Comparing %s %s \n",name,(char*)(files[i].Name));
		if(0==strcmp(name,(char*)(files[i].Name)))
		{
			return i;
		}
	}
	return -1;
}
void DownloadFile(int sendfd,int listenfd,struct Message m,struct sockaddr_in address)
{
	char File[MAXDIR],md5_sum[MD5_LEN];		
	char FilePath[MAXDIR];	
    FILE * F;
	struct stat sizeGetter;
	int count,i,id=m.id,fd,iter;
	LockDirectory();
	strcpy(File,m.data);
		fprintf(stderr,"DEBUG: File Data %s %s \n",File,m.data);
	fd = FindFileId(File);
	if(fd<0 || files[fd].Op=='U')
	{
		UnLockDirectory();
		PrepareAndSendMessage(sendfd,address,GenerateOpID(&id,'D'),'E');
		return;
	}
	UnLockDirectory();
	LockFile(fd);
	files[fd].Op='D';
	files[fd].am++;
	UnLockFile(fd);
	memset(FilePath,0,MAXDIR);
	sprintf(FilePath,"%s/%s",DirectoryPath,File);	
	F = fopen(FilePath,"r");
	stat(File,&sizeGetter);
	fprintf(stderr,"Received file stats for downloading\n");
	while(1)
	{
	m = PrepareMessage(GenerateOpID(&id,'D'),'D');	
	m.id = id;
	SerializeNumber((uint32_t)sizeGetter.st_size,m.data);
	SendMessage(sendfd,m,address);
	ReceiveMessage(listenfd,&m,&address,m.id);
	if(m.Kind == 'R')
	{
		continue;
	}
	count = 1+(((int)sizeGetter.st_size)/(dataLength-4));
	iter = 1000/count;
	///Dziel plik na fragmenty a następnie rozsyłaj

	for(i =0;i<count;i++)
	{
		if(i>0) sleepforseconds(1);
		m = PrepareMessage(id,'D');
		SerializeNumber(i,m.data);
		bulk_fread(F,m.data+4,dataLength-4);
		SendMessage(sendfd,m,address);
		LockFile(fd);
		files[fd].Op ='D';
		if(files[fd].perc<iter*i) files[fd].perc=iter*i;
		UnLockFile(fd);
	}
	LockFile(fd);
	
	files[fd].Op ='D';
	files[fd].perc = 1000;
	UnLockFile(fd);
	//CALC md5 sum of file	
	m = PrepareMessage(m.id,'F');	
	SendMessage(sendfd,m,address);	
	ReceiveMessage(listenfd,&m,&address,m.id);
	if(m.Kind == 'R')
	{
		continue;
	}
	fclose(F);
	LockFile(fd);
	files[fd].am--;
	if(files[fd].am==0)files[fd].Op = 'N';
	if(files[fd].am==0)files[fd].perc = 0;
	UnLockFile(fd);
	if(CalcFileMD5(FilePath,md5_sum)==0)
	{
		m = PrepareMessage(m.id,'E');
		SendMessage(sendfd,m,address);
		return;
	}
	if(0!=strcmp(m.data,md5_sum))
	{
		m = PrepareMessage(m.id,'E');
		fprintf(stderr,"Client sent wrong md5sum id:%d file:%s\n",m.id,File);
		SendMessage(sendfd,m,address);
		return;
	}
	m = PrepareMessage(m.id,'C');
	SendMessage(sendfd,m,address);
	return;
	}
}
int AddFile(char* FileName)
{	
	int i;
	LockDirectory();
	files[DirLen].Op='N';
	files[DirLen].perc=0;
	pthread_mutex_init(&filemutex[DirLen],NULL);
	strcpy((char*)(files[DirLen].Name),FileName);
	pthread_mutex_init(&filemutex[DirLen],NULL);
	i = DirLen++;
	UnLockDirectory();
	return i;
}
void RemoveFile(int i)
{
	int j;
	pthread_mutex_lock(&filemutex[i]);
	for(j=i;j<DirLen-1;j++)
	{
		pthread_mutex_lock(&filemutex[j+1]);
		files[j]=files[j+1];
		pthread_mutex_unlock(&filemutex[j]);
	}
	pthread_mutex_unlock(&filemutex[j]);
	pthread_mutex_destroy(&filemutex[j]);
	DirLen--;
}
void UploadFile(int sendfd,int listenfd,struct Message m,struct sockaddr_in address)
{
	char File[dataLength], md5_sum[MD5_LEN];
	FILE* F;
	char FilePath[MAXDIR];
	int i,flag=0,id=m.id,fd,iter;
	uint32_t chunk;
	int size;
	size = DeserializeNumber(m.data);
	strcpy(File,m.data+4);
	memset(FilePath,0,MAXDIR);
	fd = FindFileId(File);
	if(sprintf(FilePath,"%s/%s",DirectoryPath,File)<0 || fd>=0)
	{
		perror("sprintf");
		PrepareAndSendMessage(sendfd,address,GenerateOpID(&id,'U'),'E');
		return;
	}
	F = fopen(FilePath,"w+");
	if(F==NULL)
	{
		fprintf(stderr,"Error in file %s\n",FilePath);
		perror("FILE");
		PrepareAndSendMessage(sendfd,address,GenerateOpID(&id,'U'),'E');
		return;
	}
	fprintf(stderr,"DEBUG: Opened %s for write\n",FilePath);
	fd = AddFile(File);
	iter = 1000/size;
	while(1)
	{
		LockFile(fd);
	files[fd].Op ='U';
	files[fd].perc = 0;
	UnLockFile(fd);
		m = PrepareMessage(GenerateOpID(&id,'U'),'U');
		SendMessage(sendfd,m,address);
		for(i=0;i<size;i++)	fwrite(" ",1,1,F);
		fseek(F,0,SEEK_SET);	
		while(1)
		{
			ReceiveMessage(listenfd,&m,&address,m.id);
			if(m.Kind == 'R') 
			{
				flag=1;
				break;
			}
			if(m.Kind == 'F') break;
			chunk = DeserializeNumber(m.data);
			fseek(F,chunk*(dataLength-4),SEEK_SET);
			bulk_fwrite(F,m.data+4,dataLength);
			LockFile(fd);
			files[fd].Op ='U';
			files[fd].perc+=iter;
			UnLockFile(fd);
		}
		if(flag==1)
		{
			flag=0;
			continue;
		}
		if(CalcFileMD5(FilePath,md5_sum)==0)
		{
			fprintf(stderr,"Error calculating md5 checksum of file %s \n",File);
			RenameFile(FilePath);
			return;
		}
		m = PrepareMessage(m.id,'F');
		strcpy(m.data,md5_sum);
		SendMessage(sendfd,m,address);
		ReceiveMessage(listenfd,&m,&address,m.id);
		if(m.Kind == 'R') continue;	
		fclose(F);	
		LockFile(fd);
		files[fd].Op = 'N';
		if(m.Kind!='C')
		{
			LockDirectory();
			RemoveFile(fd);
			UnLockDirectory();
			fprintf(stderr,"Error creating file %s\n",File);
			RenameFile(FilePath);
		}
		else fprintf(stdout,"Uploaded file %s id:%d  \n",File,m.id);
		return;
	}
}

void DeleteFile(int sendfd,int listenfd,struct Message m,struct sockaddr_in address)
{
	char* name = m.data;
	int i,id=m.id;
	LockDirectory();
	m.id = GenerateOpID(&id,'M');
	for(i=0;i<DirLen;i++)
	{
		if(0==strcmp(name,(char*)(files[i].Name)))
		{
			char FilePath[MAXDIR];
			memset(FilePath,0,MAXDIR);
			if(sprintf(FilePath,"%s/%s",DirectoryPath,name)<0)
			{
				perror("sprintf");
				break;
			}
			if(files[i].Op != 'N')
			{
				fprintf(stderr,"File is busy %s \n",name);
				UnLockDirectory();
				break;
			}
			if(unlink(FilePath)<0)
			{
				fprintf(stderr,"File %s:",FilePath);
				perror("Error unlinking the file");				
				UnLockDirectory();
				break;
			}
			RemoveFile(i);
			PrepareAndSendMessage(sendfd,address,m.id,'C');
			UnLockDirectory();
			return;
		}
	}
	m = PrepareMessage(m.id,'E');
	SendMessage(sendfd,m,address);
}
void ListDirectory(int sendfd,int listenfd,struct Message m,struct sockaddr_in address)
{
	char* Dir;	
	int size,truesize=0,i,j,id=m.id;
	while(1)
	{
	
	LockDirectory();
	size = DirLen*(dataLength);
	Dir = (char*)malloc(size*sizeof(char));
	memset(Dir,0,size);
	if(Dir == NULL)
	{
		ERR("MALLOC");
	}
	for(i=0;i<DirLen;i++)
	{
		char S[dataLength];
		truesize+= DecodeFile(S,files[i]);
		strcat(Dir,S);
	}
	UnLockDirectory();
	truesize++;
	fprintf(stderr,"DEBUG: %d %s \n",truesize,Dir);
	m = PrepareMessage(GenerateOpID(&id,'L'),'L');
	SerializeNumber(truesize,m.data);
	SendMessage(sendfd,m,address);
	ReceiveMessage(listenfd,&m,&address,m.id);
	if(m.Kind == 'R')
	{		
		continue;
	}
	if(m.Kind == 'E')
	{		
		free(Dir);
		return;
	}
	size = 1 + (truesize/(dataLength-4));
	for( i=0;i<size;i++)
	{
		if(i>0) sleepforseconds(1);
		m = PrepareMessage(m.id,'D');
		SerializeNumber(i,m.data);
		for(j=0;j<dataLength-4;j++)
		{
			if(Dir[(i*(dataLength-4))+j]=='\0') break;
			m.data[j+4] = Dir[(i*(dataLength-4)) + j];
		}
		SendMessage(sendfd,m,address);
	}
	PrepareAndSendMessage(sendfd,address,m.id,'F');
	free(Dir);	
	return;
	}
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
	char Kind='N';
	if(doWork==0) return NULL;
	if(t.m.id>0)
	{
		Kind = RetiredIDs[t.m.id];
	}
	if(t.m.Kind=='D' || Kind == 'D' )
	{
		DownloadFile(t.sendfd,t.listenfd,t.m,t.address);
	}
	else if(t.m.Kind=='U' || Kind == 'U')
	{
		UploadFile(t.sendfd,t.listenfd,t.m,t.address);
	}
	else if(t.m.Kind=='M' || Kind == 'M')
	{
		DeleteFile(t.sendfd,t.listenfd,t.m,t.address);
	}
	else if(t.m.Kind=='L' || Kind == 'L')
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
	pthread_t Threads[MAXBUF];
	int ti=0,i;
	while(doWork)
	{
		pthread_t thread;
		struct Thread_Arg t;
		SuperReceiveMessage(listenfd,&m,&client);		
		t.listenfd = listenfd;
		t.sendfd = sendfd;
		memcpy((void*)&(t.address),(void*)&client,sizeof(struct sockaddr_in));
		memcpy((void*)&(t.m),(void*)&m,sizeof(struct Message));
		pthread_create(&thread,NULL,HandleMessage,(void*)&t);
		Threads[ti++]=thread;
	}
	for(i=0;i<ti;i++) pthread_cancel(Threads[i]);
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
void InitializeFile(struct dirent* dirStruct,int id)
{
	strcpy((char*)(files[id].Name),dirStruct->d_name);
	files[id].Op='N';
	files[id].perc=0;
	files[id].am = 0;
}
void InitializeMutexes()
{
	pthread_mutex_init(&directorymutex,NULL);
	pthread_mutex_init(&SuperMutex,NULL);
	pthread_mutex_init(&MessageMutex,NULL);
	pthread_mutex_init(&opID,NULL);
	pthread_mutex_init(&GateMutex,NULL);
	WaitOnGate();
	WaitOnSuper();
}
int main(int argc,char** argv)
{
	int listenfd,sendfd,i;
	struct sockaddr_in client;
	char filebuf[7];
	struct dirent* dirStruct;
	DIR* directory;
	struct Message m;	
	struct sigaction new_sa;
	sigfillset(&new_sa.sa_mask);
	new_sa.sa_handler = SigActionHandler;
	new_sa.sa_flags = 0;
	memset(filebuf,0,7);
	if (sigaction(SIGINT, &new_sa, NULL)<0)
	{
		ERR("SIGINT SIGACTION");
	}
	opid=1;
	doWork=1;
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
	TaskReporter = fopen("serversave.dat","a+");
	minid=1;
	RetiredIDs[0] = 'N';
	while(ReadLine(TaskReporter,filebuf)>0)
	{
		int fid;
		char c;
		sscanf(filebuf,"%d %c",&fid,&c);
		RetiredIDs[fid]=c;
		if(minid <fid+1) minid=fid+1;			
	}
	opid = minid;
	InitializeMutexes();
	memset(&m,0,sizeof(struct Message));
	memset(&client,0,sizeof(struct sockaddr_in));
	listenport = htons(atoi(argv[1]));
	if(listenport == 0)
	{
		ERR("PORT:");
	}
	while(1)
	{
		struct stat st;
		dirStruct = readdir(directory);
		if(dirStruct == NULL)
		{
			break;
		}
		lstat(dirStruct->d_name, &st);
		if(S_ISDIR(st.st_mode)) continue;
		pthread_mutex_init(&filemutex[DirLen],NULL);
		InitializeFile(dirStruct,DirLen);
		DirLen++;
	}
	closedir(directory);
	fprintf(stdout,"Prepared Directory List\n");
	listenfd = bind_inet_socket(atoi(argv[1]),SOCK_DGRAM,INADDR_ANY,SO_BROADCAST);
	sendfd = makesocket(SOCK_DGRAM,0);
	MessageQueueWork(listenfd,sendfd);
	pthread_mutex_destroy(&directorymutex);
	for( i=0;i<DirLen;i++) pthread_mutex_destroy(&filemutex[DirLen]);
	fclose(TaskReporter);
	pthread_mutex_destroy(&SuperMutex);
	pthread_mutex_destroy(&MessageMutex);
	pthread_mutex_destroy(&opID);
	pthread_mutex_destroy(&GateMutex);
	return 0;		
}
