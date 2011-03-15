#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "filecache_schema.h"
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/un.h>
#include <pthread.h>
fileCache_t *filecache;
unsigned char *start_addr;
#define MAX_FDS 50
unsigned long write_one=1;
int sock_fd;
int mypos=-1;
struct {
	int fd,position;
} connections[MAX_FDS];
int curr_index=0;
int guestos_fd=-1;
void init_interrupt()
{
	struct sockaddr_un un;
	sock_fd=socket(PF_FILE, SOCK_STREAM|SOCK_CLOEXEC, 0);
	memset(&un, 0, sizeof(un));
	un.sun_family = AF_UNIX;
	snprintf(un.sun_path, sizeof(un.sun_path), "/tmp/jana" );
	if (connect(sock_fd, (struct sockaddr*) &un, sizeof(un)) < 0) {
		return -1;
	}    
	return 1;  
}

int recv_msg()
{
	struct msghdr msg;
	struct iovec iov[1];
	struct cmsghdr *cmptr;
	struct cmsghdr *cmsg;
	size_t len;
	size_t msg_size = sizeof(int);
	//char control[CMSG_SPACE(msg_size)];
	char control[1024],data[1024];
	long posn = 0;
	int added,pos,*posp;
	int i,fd;

	msg.msg_name = 0;
	msg.msg_namelen = 0;
	msg.msg_control = control;
	msg.msg_controllen = sizeof(control);
	msg.msg_flags = 0;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	iov[0].iov_base = data;
	iov[0].iov_len = sizeof(data)-1;
	len = recvmsg(sock_fd, &msg, 0);

	added=0;
	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {

		if (cmsg->cmsg_len != CMSG_LEN(sizeof(int)) ||
				cmsg->cmsg_level != SOL_SOCKET ||
				cmsg->cmsg_type != SCM_RIGHTS)
		{
			fd = *((int *)CMSG_DATA(cmsg));
			printf("DELETING the fd :%d \n",fd);	
			continue;
		}
		posp=&data;
		pos=*posp;
		fd = *((int *)CMSG_DATA(cmsg));
		for (i=0; i<curr_index;i++)
		{
			if (connections[i].fd != -1)  continue;
			connections[i].fd=fd;
			connections[i].position=pos;
		printf("ADDING in existing  FD :%d pos:%d \n",fd,pos);	
		added=1;
			break;
		}
		if (i==curr_index)
		{
			curr_index++;	
			connections[i].fd=fd;
			connections[i].position=pos;
		added=1;
		printf("ADDING in NEW : FD :%d pos:%d \n",fd,pos);	
		}
	}
	if (added==0)
	{
		posp=&data;
		pos=*posp;
		for (i=0; i<curr_index;i++)
		{
			if (connections[i].position==pos)
			{
				printf("DELETING the fd :%d \n",connections[i].fd);	
				connections[i].position=-1;
				connections[i].fd=-1;
				if (i==2 && guestos_fd != -1) 
				{
					close(guestos_fd);
				}	
			}
		}
	}else
	{
		if (i==2) /* guest os fd */
		{
			if (connections[2].fd != -1)
			{
				for (i=1; i<3;i++)
				 if (connections[i].position != connections[0].position) 
					guestos_fd=dup(connections[i].fd);	
			}
		}
	}

}
int send_interrupt()
{
	int ret=0;

	if (guestos_fd == -1 ) ret= -1;
        else if (write(guestos_fd ,&write_one,8)==8) ret= 1;
	else ret= -2;
	printf("Send interrupt fd:%d ret :%d \n",guestos_fd,ret);
	return ret;
}
int init_filecache()
{
	int i;
	
	if (filecache->magic_number!=FS_MAGIC)	
	{
		return 0;
	}
	if (filecache->version!=FS_VERSION) return 0;
	if (filecache->state!=STATE_VALID) return 0;
	printf(" Initalzed the File cache \n");
	return 1;
}
int init()
{
	int fd;
	unsigned char *p;
	init_interrupt();
	fd=open("/dev/shm/ivshmem",O_RDWR);
	if (fd < 1) 
	{
		printf(" ERROR in opening shared memory \n");
		return 0;	
	}
	p=mmap(0, 2*1024*1024,PROT_WRITE|PROT_READ,MAP_SHARED,fd,0);
	if(p==0) 
		return 0;
	start_addr=p;
	filecache=p;	
	while(init_filecache()==0)
	{
		system("sleep 3");
	}
	return 1;
}
int open_file(int c)
{
        int fd,ret;
        unsigned char *p;
	struct stat stat;

        printf("open the file :%s: flags:%d \n",filecache->requests[c].filename,filecache->requests[c].flags);   
	if (filecache->requests[c].flags == FLAG_CREATE)  
        	fd=open(filecache->requests[c].filename,O_WRONLY|O_CREAT|O_EXCL, 0644);
        else
		fd=open(filecache->requests[c].filename,O_RDONLY);
        if (fd > 0)
        {
                ret=fstat(fd,&stat);
		if (ret ==0)
                filecache->requests[c].response_len=stat.st_size;
		else
                filecache->requests[c].response_len=0;
	
                printf("open file :%s: len:%d: offset:%x :\n",filecache->requests[c].filename,ret,filecache->requests[c].shm_offset);

                ret=RESPONSE_DONE;
        }else
        {
                printf(" Response failed \n");
                ret=RESPONSE_FAILED;
        }
	close(fd);	
        filecache->requests[c].response=ret;
        send_interrupt();
        return ret;
}
int rw_file(int c)
{
	int fd,ret;
	unsigned char *p;

	printf("RW the file :%s: \n",filecache->requests[c].filename);	
	if (filecache->requests[c].type == REQUEST_READ)
		fd=open(filecache->requests[c].filename,O_RDONLY);
	else
		fd=open(filecache->requests[c].filename,O_WRONLY);
	if (fd > 0)
	{
		lseek(fd,filecache->requests[c].file_offset,SEEK_SET);
		printf(" file offset :%d \n",filecache->requests[c].file_offset);
		p=start_addr;
		p=p+(filecache->requests[c].shm_offset);
		if (filecache->requests[c].type == REQUEST_READ)
			ret=read(fd,p,filecache->requests[c].request_len);
		else
			ret=write(fd,p,filecache->requests[c].request_len);
		filecache->requests[c].response_len=ret;
		printf("New  Reading the file :%s: len:%d:  p :%x pagecache:%x offset:%x :\n",filecache->requests[c].filename,ret,p,filecache,filecache->requests[c].shm_offset);
		ret=RESPONSE_DONE;
	}else
	{
		printf(" Response failed \n");
		ret=RESPONSE_FAILED;
	}
	filecache->requests[c].response=ret;
	send_interrupt();
	return ret;
}
int process_request(int c)
{
	printf(" Processing the request : %d \n",c);
	switch (filecache->requests[c].type)
	{
	case REQUEST_OPEN : open_file(c); break;
	case REQUEST_READ : 
	case REQUEST_WRITE : rw_file(c); break;
	}
	return ;	
}
void recv_thread(void *arg)
{
  while(1)
        {
                recv_msg();
        }
}
main()
{
	pthread_t thread_id;
	int i;
	int ret;
	if (init() == 0) return;

	pthread_create(&thread_id,NULL,recv_thread,0);
printf(" .. Ready to Process \n");
	while(1)
	{
		for (i=0; i<filecache->request_highindex; i++)
		{
			if (filecache->requests[i].state==STATE_VALID && filecache->requests[i].response==RESPONSE_NONE )
			{
				process_request(i);
			}	
		}		
	}
}


