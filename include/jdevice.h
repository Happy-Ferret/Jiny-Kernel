#ifndef __JDEVICE_H__
#define __JDEVICE_H__
extern "C" {
#include "common.h"
#include "pci.h"
#include "interface.h"
}
#include "file.hh"
class jdriver;
#define MAX_DEVICE_NAME 100
class jdevice: public vinode {

public:
	unsigned char name[MAX_DEVICE_NAME];
	jdriver* driver;

	jdevice(unsigned char *name, int type);
	int read(unsigned long offset, unsigned char *data, int len, int flags, int opt_flags);
	int write(unsigned long offset, unsigned char *data, int len, int flags);
	int close();
	int ioctl(unsigned long arg1,unsigned long arg2);

	/* pci details */
	pci_device_t pci_device;
	int init_pci(uint8_t bus, uint8_t device, uint8_t function);

	void print_stats(unsigned char *arg1,unsigned char *arg2);
};

#define VQTYPE_RECV 1
#define VQTYPE_SEND 2

class jdriver: public jobject {
public:
	unsigned char *name;
	jdevice *device;
	int instances;

	unsigned long stat_sends,stat_recvs,stat_recv_interrupts,stat_send_interrupts;
	/* TODO : Do not change the order of virtual functions, c++ linking is implemented in handcoded */
	virtual int probe_device(jdevice *dev)=0;
	virtual jdriver *attach_device(jdevice *dev)=0; /* attach the driver by creating a new driver if it is sucessfull*/
	virtual int dettach_device(jdevice *dev)=0;
	virtual int read(unsigned char *buf, int len, int flags, int opt_flags)=0;
	virtual int write(unsigned char *buf, int len, int flags)=0;
	virtual void print_stats(unsigned char *arg1,unsigned char *arg2)=0;
	virtual int ioctl(unsigned long arg1,unsigned long arg2)=0;
};

class virtio_jdriver: public jdriver {
public:
	atomic_t stat_send_kicks;
	atomic_t stat_recv_kicks;
	atomic_t stat_kicks;
	unsigned char pending_kick_onsend;

	struct virtqueue *virtio_create_queue(uint16_t index, int qType);
	void print_stats(unsigned char *arg1,unsigned char *arg2);
	void queue_kick(struct virtqueue *vq);

	unsigned long stat_allocs,stat_frees,stat_err_nospace;
};

#define COPY_OBJ(CLASS_NAME,OBJECT_NAME, NEW_OBJ, jdev) jdriver *NEW_OBJ; NEW_OBJ=(jdriver *)ut_calloc(sizeof(CLASS_NAME)); \
	ut_memcpy((unsigned char *)NEW_OBJ,(unsigned char *) (OBJECT_NAME), sizeof(CLASS_NAME)); \
	NEW_OBJ->device = jdev;

class virtio_net_jdriver: public virtio_jdriver {

	int net_attach_device();
	int free_send_bufs();

public:
#define MAX_VIRT_QUEUES 10
	struct virt_queue{
		struct virtqueue *recv,*send;
		int pending_send_kick;
	}queues[MAX_VIRT_QUEUES];
	struct virtqueue *control_q;
	uint16_t max_vqs;
	uint32_t send_count;
	uint32_t current_send_q;
	spinlock_t virtionet_lock;

	unsigned long remove_buf_from_vq(struct virtqueue *v_q,int *len);
	int addBufToNetQueue(int qno, int type, unsigned char *buf, unsigned long len);

	int probe_device(jdevice *dev);
	jdriver *attach_device(jdevice *dev);
	int dettach_device(jdevice *dev);
	int read(unsigned char *buf, int len, int flags, int opt_flags);
	int write(unsigned char *buf, int len, int flags);
	int ioctl(unsigned long arg1,unsigned long arg2);

	wait_queue *send_waitq;
	unsigned char mac[7];
	int recv_interrupt_disabled;
};

#define VIRTIO_BLK_DATA_SIZE (4096)
struct virtio_blk_req {
	uint32_t type;
	uint32_t ioprio;
	uint64_t sector;

	uint8_t status;
	uint8_t pad[3];
	uint32_t len;

	char *user_data; /* this memory block can be used directly to avoid the mem copy, this is if it from pagecache */

	char data[2];  /* here data can be one byte or  1 page depending on the user_data */
};


#define IOCTL_DISK_SIZE 1
class virtio_disk_jdriver: public virtio_jdriver {
	unsigned long disk_size,blk_size;

	int disk_attach_device(jdevice *dev);
	struct virtio_blk_req *createBuf(int type, unsigned char *buf,uint64_t sector,uint64_t data_len);
	//int process_bufs(struct virtio_blk_req *req, int data_len);


	void *scsi_addBufToQueue(int type, unsigned char *buf, uint64_t len, uint64_t sector,uint64_t data_len);
	int disk_io(int type,unsigned char *buf, int len, int offset,int read_ahead);
public:
	struct virt_queue{
		struct virtqueue *recv,*send;
	}queues[MAX_VIRT_QUEUES];
	struct virtqueue *control_q;
	uint16_t max_vqs;
	int interrupts_disabled;

	//spinlock_t io_lock;
	unsigned char *unfreed_req;
	void addBufToQueue(struct virtio_blk_req *req,int transfer_len);

//	struct virtqueue *vq[5];
	wait_queue *waitq;
	int probe_device(jdevice *dev);
	jdriver *attach_device(jdevice *dev);
	int dettach_device(jdevice *dev);
	int read(unsigned char *buf, int len, int flags, int opt_flags);
	int write(unsigned char *buf, int len, int flags);
	int ioctl(unsigned long arg1,unsigned long arg2);
};
class virtio_p9_jdriver: public virtio_jdriver {
	int p9_attach_device(jdevice *dev);

public:
	struct virtqueue *vq[5];
	int probe_device(jdevice *dev);
	jdriver *attach_device(jdevice *dev);
	int dettach_device(jdevice *dev);
	int read(unsigned char *buf, int len, int flags, int opt_flags);
	int write(unsigned char *buf, int len, int flags);
	int ioctl(unsigned long arg1,unsigned long arg2);
	void *virtio_dev; /* TODO : need to remove later */
};


void register_jdriver(class jdriver *driver);
#define MAX_DISK_DEVICES 5
extern "C" {
extern jdriver *disk_drivers[];
}
#endif
