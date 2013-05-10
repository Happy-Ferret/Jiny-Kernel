/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *   kernel/network.c
 *   Author: Naredula Janardhana Reddy  (naredula.jana@gmail.com, naredula.jana@yahoo.com)
 */

/*  Description:
 *   - Middle layer between network drivers(like virtio..)  and protocol stacks(lwip, udp responser .. etc)
 *   - It works as a bottom Half for network packets.
 *   - It should come up before drivers and protocol stacks, so that they hook to this layer
 */
#define DEBUG_ENABLE 1
#include "common.h"


#define MAX_QUEUE_LENGTH 250
struct queue_struct{
	wait_queue_t waitq;
	int producer,consumer;
	struct {
		unsigned char *buf;
		unsigned int len;
	} data[MAX_QUEUE_LENGTH];
    int stat_processed[MAX_CPUS];
};
static struct queue_struct queue;

#define MAX_HANDLERS 10
struct net_handlers{
	void *private;
	int (*callback)(unsigned char *buf, unsigned int len, void *private_data);
};
static int count_net_drivers=0;
static int count_protocol_drivers=0;
static struct net_handlers net_drivers[MAX_HANDLERS];
static struct net_handlers protocol_drivers[MAX_HANDLERS];
static int add_to_queue(unsigned char *buf, int len) {
	if (buf==0 || len==0) return 0;
	if (queue.data[queue.producer].buf == 0) {
		queue.data[queue.producer].len = len;
		queue.data[queue.producer].buf = buf;
		queue.producer++;
		if (queue.producer >= MAX_QUEUE_LENGTH)
			queue.producer = 0;
		return 1;
	}
	return 0;
}
static int remove_from_queue(unsigned char **buf, unsigned int *len){
	if ((queue.data[queue.consumer].buf != 0) && (queue.data[queue.consumer].len != 0)) {
		*buf = queue.data[queue.consumer].buf;
		*len = queue.data[queue.consumer].len;
		queue.data[queue.consumer].len = 0;
		queue.data[queue.consumer].buf = 0;
		queue.consumer++;
		queue.stat_processed[getcpuid()]++;
		if (queue.consumer >= MAX_QUEUE_LENGTH)
			queue.consumer = 0;
		return 1;
	}
	return 0;
}
/*   Release the buffer after calling the callback */
static int netRx_BH(void *arg) {
	unsigned char *data;
	unsigned int len;

	while (1) {
		ipc_waiton_waitqueue(&queue.waitq, 10);
		while (remove_from_queue(&data, &len) == 1) {
			int ret=0;
			if (count_protocol_drivers > 0) {
				ret=protocol_drivers[0].callback(data, len, protocol_drivers[0].private);
			}
			if (ret==0){

			}
			mm_putFreePages(data, 0);
		}
	}
	return 1;
}

int registerNetworkHandler(int type,
		int (*callback)(unsigned char *buf, unsigned int len, void *private_data),
		void *private_data)
{
	if (type == NETWORK_PROTOCOLSTACK){
		protocol_drivers[count_protocol_drivers].callback = callback;
		protocol_drivers[count_protocol_drivers].private = private_data;
		count_protocol_drivers++;
	}else{
		net_drivers[count_net_drivers].callback = callback;
		net_drivers[count_net_drivers].private = private_data;
		count_net_drivers++;
	}
    return 1;
}
int netif_rx(unsigned char *data, unsigned int len) {
	if (add_to_queue(data,len)==0 && data!=0 && len!=0){
		mm_putFreePages(data, 0);
	}
	return 1;
}
int netif_tx(unsigned char *data, unsigned int len) {
	int ret;
	if (data==0 || len==0) return 0;
	if (count_net_drivers > 0) {
		ret=net_drivers[0].callback(data, len, net_drivers[0].private);

		return 1;
	}else{

	}
	return 0;
}
int init_networking(){
	int ret;

    ut_memset((unsigned char *)&queue,0,sizeof(struct queue_struct));
	ipc_register_waitqueue(&queue.waitq ,"netRx_BH");
	ret = sc_createKernelThread(netRx_BH, 0, (unsigned char *)"netRx_BH");
	return 1;
}
/*******************************************************************************************
Socket layer
********************************************************************************************/

static struct Socket_API *socket_api=0;
static int socketLayerRegistered=0;
int register_to_socketLayer(struct Socket_API *api){
	if (api==0) return 0;
	socket_api=api;
	socketLayerRegistered=1;
	return 1;
}

int socket_close(struct file *file){
	if (socketLayerRegistered==0) return 0; /* TCP/IP sockets are not supported */
	if (socket_api->close == 0) return 0;
	return socket_api->close(file->private);
}

int socket_read(struct file *file, unsigned char *buff, unsigned long len){
	if (socketLayerRegistered==0) return 0; /* TCP/IP sockets are not supported */
	if (socket_api->read == 0) return 0;
	return socket_api->read(file->private, buff, len);
}

int socket_write(struct file *file, unsigned char *buff, unsigned long len){
	if (socketLayerRegistered==0) return 0; /* TCP/IP sockets are not supported */
	if (socket_api->write == 0) return 0;
	return socket_api->write(file->private, buff, len);
}
/*
 * socket(AF_INET,SOCK_STREAM,0);
 */
int SYS_socket(int family,int type, int z){
	void *conn;
	if (socketLayerRegistered==0) return 0; /* TCP/IP sockets are not supported */
	SYSCALL_DEBUG("socket : family:%x type:%x arg3:%x\n",family,type,z);
	if (socket_api->open){
		conn = socket_api->open(type);
		if (conn==0) return -2;
	}else{
		return -1;
	}

	int i= SYS_fs_open("/dev/sockets",0,0);
	if (i<0) return i;
	struct file *file = g_current_task->mm->fs.filep[i];
	if (file == 0){
		socket_api->close(conn);
		return -3;
	}
	file->private = conn;
	return i;
}

int SYS_bind(int fd, struct sockaddr  *addr, int len){
	SYSCALL_DEBUG("bind %d \n",fd);
	if (socketLayerRegistered==0 || socket_api==0) return 0; /* TCP/IP sockets are not supported */
	SYSCALL_DEBUG("Bind fd:%d addr:%x len\n",fd,addr,len);
	if (socket_api->bind == 0) return 0;
    if (fd > MAX_FDS || fd <0 ) return 0;
	struct file *file = g_current_task->mm->fs.filep[fd];
	if (file==0) return -1;
	return socket_api->bind(file->private, addr);
}

int SYS_accept(int fd){
	struct file *file;
	SYSCALL_DEBUG("accept %d \n",fd);
	if (socketLayerRegistered==0) return 0; /* TCP/IP sockets are not supported */

	if (socket_api->accept == 0) return 0;
	file = g_current_task->mm->fs.filep[fd];
	if (file==0) return -1;
	void *conn=socket_api->accept(file->private);
	if (conn==0)return -1;
	int i= SYS_fs_open("/dev/sockets",0,0);
	if (i==0) {
		socket_api->close(conn);
	}
	file = g_current_task->mm->fs.filep[i];
	file->private = conn;
	return i;
}

int SYS_listen(int fd,int length){
	SYSCALL_DEBUG("listen fd:%d len:%d\n",fd,length);
	if (socketLayerRegistered==0 || socket_api==0) return 0; /* TCP/IP sockets are not supported */
    return 1;
}
int SYS_connect(int fd, struct sockaddr  *addr, int len) {
	int ret;
	struct sockaddr ksock_addr;

	SYSCALL_DEBUG("connect %d  addr:%x len:%d\n",fd, addr, len);
	if (socketLayerRegistered==0 || socket_api==0) return 0; /* TCP/IP sockets are not supported */

	if (socket_api->connect == 0) return 0;
    if (fd > MAX_FDS || fd <0 ) return 0;
	struct file *file = g_current_task->mm->fs.filep[fd];
	if (file==0) return -1;
	ksock_addr.addr = addr->addr;

	ret = socket_api->connect(file->private, &(ksock_addr.addr), addr->sin_port);
	SYSCALL_DEBUG("connect ret:%d  addr:%x\n",ret,ksock_addr.addr);
	return ret;

}
unsigned long SYS_sendto(int sockfd, const void *buf, size_t len, int flags,  const struct sockaddr *dest_addr, int addrlen){
	SYSCALL_DEBUG("SENDTO fd:%d buf:%x len:%d flags:%x dest_addr:%x addrlen:%d\n",sockfd, buf, len, flags, dest_addr, addrlen);
	if (socketLayerRegistered==0 || socket_api==0 || g_current_task->mm->fs.filep[sockfd]==0) return 0;
	struct file *file = g_current_task->mm->fs.filep[sockfd];
	if (dest_addr !=0)
	     socket_api->bind(file->private, dest_addr);
	return (socket_api->write(file->private, buf, len));
}

int SYS_recvfrom(fd) /* TODO */
{
	SYSCALL_DEBUG("RECVfrom fd:%d TODO\n",fd);
    return 1;
}
