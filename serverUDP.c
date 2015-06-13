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
#include <dirent.h>
#define ERR(source) (perror(source),\
		     fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
			exit(EXIT_FAILURE))
#define dataLength 571
#define BROADCAST 0xFFFFFFFF
#define MAXBUF 1024
#define BACKLOG 3
#define MAXFILE 1024
#define MAXDIR 1024
struct Message
{
	char Kind;
	uint32_t id;
	char data[dataLength];
};
struct DirFile
{
	char Name[MAXFILE];
	char Op;
	int perc;
};
struct DirFile files[MAXDIR];
int DirLen;
uint32_t GenerateOpID()
{
	return 0;
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
struct Message PrepareMessage(uint32_t id,char type)
{
	struct Message m = {.Kind = type, .id = id};
	memset(m.data,0,dataLength);
	return m;
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
void SendMessage(int fd,struct Message m,struct sockaddr_in addr)
{
	char MessageBuf[MAXBUF];
	SerializeMessage(MessageBuf,m);
	if(TEMP_FAILURE_RETRY(sendto(fd,MessageBuf,sizeof(struct Message),0,&addr,sizeof(addr)))<0) ERR("send:");	
}
void ReceiveMessage(int fd,struct Message* m,struct sockaddr_in* addr)
{
	char MessageBuf[MAXBUF];
	socklen_t size = sizeof(struct sockaddr_in);
	if(TEMP_FAILURE_RETRY(recvfrom(fd,MessageBuf,sizeof(struct Message),0,(struct sockaddr*)&addr,&size))<0) ERR("read:");
	fprintf(stderr,"DEBUG: ReceivedMessage , preparing for serialization\n");
	DeserializeMessage(MessageBuf,m);
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

void DownloadFile(int sendfd,int listenfd,struct Message m,struct sockaddr_in address)
{
	char* File = m.data;

	m = PrepareMessage(GenerateOpID(),'D');
		//getfile size and put it into data
	SendMessage(sendfd,m,address);
	ReceiveMessage(listenfd,&m,&address);
	///Dziel plik na fragmenty a następnie rozsyłaj
	while(1)
	{
		SendMessage(sendfd,m,address);
		if(m.Kind == 'F')
		{
			break;
		}
		ReceiveMessage(sendfd,&m,&address);
		//TODO: Write in a file in exact position
	}
	//CALC md5 sum of file
	m = PrepareMessage(m.id,'F');
	
	SendMessage(sendfd,m,address);
	
	//if(m.data!=md5sum) uncorrect
	ReceiveMessage(listenfd,&m,&address);
}
void UploadFile(int sendfd,int listenfd,struct Message m,struct sockaddr_in address)
{
	m = PrepareMessage(GenerateOpID(),'D');
	//Add filename do message data
	int size;
	SendMessage(sendfd,m,address);
	ReceiveMessage(listenfd,&m,&address);
	if(m.Kind!='C')
	{
		///ERR;
		return;
	}
	size = DeserializeNumber(m.data);
	SendMessage(sendfd,m,address);
	while(1)
	{
		ReceiveMessage(listenfd,&m,&address);
		if(m.Kind == 'F')
		{
			break;
		}
		//TODO: Write in a file in exact position
		SendMessage(sendfd,m,address);
	}
	//CALC md5 sum of file
	m = PrepareMessage(m.id,'F');
	//m.data = md5sum
	SendMessage(sendfd,m,address);
	ReceiveMessage(listenfd,&m,&address);
	if(m.Kind!='C')
	{
		//delete file;
	}
}

void DeleteFile(int sendfd,int listenfd,struct Message m,struct sockaddr_in address)
{
	char* name = m.data;
	int i;
	LockDirectory();
	
	for(i=0;i<DirLen;i++)
	{
		if(0==strcmp(name,files[i].Name))
		{
			int j;
			//Delete file from disk
			if(files[i].Op != 'N')
			{
				UnLockDirectory();
				break;
			}
			for(j=i;j<DirLen-1;j++)
			{
				files[j]=files[j+1];
			}
			DirLen--;
			m = PrepareMessage('C',m.id);
			
			UnLockDirectory();
			return;
		}
	}
	m = PrepareMessage('E',m.id);
	SendMessage(sendfd,m,address);
}
void ListDirectory(int sendfd,int listenfd,struct Message m,struct sockaddr_in address)
{
	
}

void HandleMessage(struct Message m,int sendfd,int listenfd,struct sockaddr_in address)
{
	if(m.Kind=='D')
	{
		DownloadFile(sendfd,listenfd,m,address);
	}
	else if(m.Kind=='U')
	{
		UploadFile(sendfd,listenfd,m,address);
	}
	else if(m.Kind=='M')
	{
		DeleteFile(sendfd,listenfd,m,address);
	}
	else if(m.Kind=='L')
	{
		ListDirectory(sendfd,listenfd,m,address);
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
		int listenfd,sendfd,i;
		struct sockaddr_in client;
		struct Message m;
		struct dirent* dirStruct;
		DIR* directory;
		if(argc!=3)
		{
			usage(argv[0]);
			return EXIT_FAILURE;
		}
		directory = opendir(argv[2]);
		if(directory == NULL)
		{
			ERR("Can't open directory");
		}
		memset(&m,0,sizeof(struct Message));
		memset(&client,0,sizeof(struct sockaddr_in));
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
		ReceiveMessage(listenfd,&m,&client);
		fprintf(stdout,"%d %c %s\n",m.id,m.Kind,m.data);
		print_ip((unsigned long int)client.sin_addr.s_addr);
		sendfd = bind_inet_socket(atoi(argv[1]),SOCK_DGRAM,ntohl(client.sin_addr.s_addr),0);
		SendMessage(sendfd,m,client);
		
return 0;
		
}
