#define DEBUG_ENABLE 1
#include "common.h"
#include "pci.h"
#include "mm.h"
#include "vfs.h"
#include "task.h"
#include "interface.h"
#include <virtio.h>
#include <virtio_ring.h>
#include <virtio_pci.h>
#include <virtio_net.h>
#include "9p.h"

extern virtio_dev_t virtio_devices[];
extern int virtio_dev_count;
static struct wait_struct p9_waitq;

static int virtio_addToP9Queue(struct virtqueue *vq, unsigned long buf,
		unsigned long out_len, unsigned long in_len);

static unsigned char vp_get_status(virtio_dev_t *dev) {
	uint16_t addr = dev->pci_ioaddr + VIRTIO_PCI_STATUS;
	return inb(addr);
}
static void vp_set_status(virtio_dev_t *dev, unsigned char status) {
	uint16_t addr = dev->pci_ioaddr + VIRTIO_PCI_STATUS;
	outb(addr, status);
}

int init_virtio_9p_pci(pci_dev_header_t *pci_hdr, virtio_dev_t *dev) {
	unsigned long addr;
	unsigned long features;

	vp_set_status(dev, vp_get_status(dev) + VIRTIO_CONFIG_S_ACKNOWLEDGE);
	DEBUG("Initializing VIRTIO PCI p9 status :%x :  \n",vp_get_status(dev));

	vp_set_status(dev, vp_get_status(dev) + VIRTIO_CONFIG_S_DRIVER);

	addr = dev->pci_ioaddr + VIRTIO_PCI_HOST_FEATURES;
	features = inl(addr);
	DEBUG(" driver Initialising VIRTIO PCI 9P hostfeatures :%x:\n",features);

	virtio_createQueue(0, dev, 2);

	vp_set_status(dev, vp_get_status(dev) + VIRTIO_CONFIG_S_DRIVER_OK);
	DEBUG(" NEW Initialising..9P INPUT  VIRTIO PCI COMPLETED with driver ok :%x \n",vp_get_status(dev));
	inb(dev->pci_ioaddr + VIRTIO_PCI_ISR);

	sc_register_waitqueue(&p9_waitq);
	return 1;
}
unsigned long p9_write_rpc(p9_client_t *client, const char *fmt, ...) { /* The call will be blocked till the reply is receivied */
	p9_fcall_t pdu;
	int ret;
	unsigned long addr;
	va_list ap;
	va_start(ap,fmt);

	p9pdu_init(&pdu, client->type, client->tag, client,client->pkt_buf, client->pkt_len);
	ret = p9pdu_write(&pdu, fmt, ap);
	va_end(ap);
	p9pdu_finalize(&pdu);

	struct scatterlist sg[4];
	unsigned int out, in;
	sg[0].page_link = client->pkt_buf;
	sg[0].length = 1024;
	sg[0].offset = 0;
	out = 1;
	if (client->type == P9_TYPE_TREAD) {
		sg[1].page_link = client->pkt_buf + 1024;
		sg[1].length = 11; /* exactly 11 bytes for read response header , data will be from user buffer*/
		sg[1].offset = 0;
		sg[2].page_link = client->user_data;
		sg[2].length = client->userdata_len;
		sg[2].offset = 0;
		in = 2;
	} else  if  (client->type == P9_TYPE_TWRITE) {
		sg[1].page_link = client->user_data;
		sg[1].length = client->userdata_len;
		sg[1].offset = 0;
		sg[0].length =23 ; /* this for header , eventhough it is having space pick the data from sg[1] */

		sg[2].page_link = client->pkt_buf + 1024;
		sg[2].length = 1024;
		sg[2].offset = 0;
		out = 2;
		in =1;
	} else {
		sg[1].page_link = client->pkt_buf + 1024;
		sg[1].length = 1024;
		sg[1].offset = 0;
		in = 1;
	}
	virtqueue_add_buf_gfp(virtio_devices[0].vq[0], sg, out, in, sg[0].page_link, 0);
	virtqueue_kick(virtio_devices[0].vq[0]);

	sc_wait(&p9_waitq, 100);
	unsigned int len;
	len = 0;
	addr = virtio_removeFromQueue(virtio_devices[0].vq[0], &len);
	if (addr != client->pkt_buf) {
		DEBUG("9p write : got invalid address : %x \n",addr);
		return 0;
	}
	return client->pkt_buf;
}

int p9_read_rpc(p9_client_t *client, const char *fmt, ...) {
	unsigned char *recv;
	p9_fcall_t pdu;
	int ret;
	uint32_t total_len;
	unsigned char type;
	uint16_t tag;

	va_list ap;
	va_start(ap,fmt);

	recv = client->pkt_buf + 1024;
	p9pdu_init(&pdu, 0, 0, client,recv, 1024);
	ret = p9pdu_read_v(&pdu, "dbw", &total_len, &type, &tag);
	client->recv_type = type;
	ret = p9pdu_read(&pdu, fmt, ap);
	va_end(ap);
	DEBUG("Recv Header ret:%x total len :%x stype:%x(%d) rtype:%x(%d) tag:%x \n",ret ,total_len,client->type,client->type,type,type,tag);
	DEBUG(" : c:%x:%x:%x:%x :%x:%x:%x:%x data:%x:%x:%x:%x\n", recv[0], recv[1], recv[2], recv[3], recv[4], recv[5], recv[6], recv[7],recv[12], recv[13], recv[14], recv[15]);
	if (type == 107) {
		recv[100]='\0';
		DEBUG(" recv error data :%s: \n ",&recv[9]);
	}
	return ret;
}

int p9_cmd(char *arg1, char *arg2) {
	unsigned char buf[100];
	unsigned long  fp;
	int ret;
   fp=fs_open(arg1,1,0);

 ut_strcpy(buf,"ABCjanardhana reddy abc 123451111111111");
   ret=fs_write(fp,buf, 99);
   buf[10]=0;
   DEBUG("Before fdatasync \n");
   fs_fdatasync(fp);
   DEBUG(" WRITE len :%d data:%s:",ret,buf);

   return 1;
}
void virtio_9p_interrupt(registers_t regs) {
	unsigned char isr;
	int ret;

	isr = inb(virtio_devices[0].pci_ioaddr + VIRTIO_PCI_ISR);
	ret = sc_wakeUp(&p9_waitq, NULL); /* wake all the waiting processes */
}
