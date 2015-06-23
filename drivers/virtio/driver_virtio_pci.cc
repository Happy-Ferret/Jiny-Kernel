
/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
*   drivers/driver_virtio_pci.cc
*   Naredula Janardhana Reddy  (naredula.jana@gmail.com, naredula.jana@yahoo.com)
*
*/
#include "file.hh"
#include "network.hh"
extern "C" {
#include "common.h"
#include "pci.h"
#include "interface.h"

#include "virtio.h"
#include "virtio_ring.h"
#include "virtio_pci.h"
#include "net/virtio_net.h"
#include "mach_dep.h"
extern int p9_initFs(void *p);
extern void print_vq(struct virtqueue *_vq);
extern int init_tarfs(jdriver *driver_arg);
int g_conf_net_send_int_disable = 1;

atomic_t  g_conf_stat_pio = ATOMIC_INIT(0);
}
#include "jdevice.h"
extern int register_netdevice(jdevice *device);

/************************   utilities used by vitio drivr ***************************/
struct virtio_feature_desc {
	int feature_bit;
	const char *name;
};
static void display_virtiofeatures(unsigned long feature, struct virtio_feature_desc *desc) {
	int i, j, bit;
	for (i = 0; i < 32; i++) {
		bit = (feature >> i) & (0x1);
		if (bit) {
			for (j = 0; (desc[j].name != NULL); j++) {
				if (desc[j].feature_bit == i){
					ut_log("[%d] %s,",i,desc[j].name);
				}

			}
		}
	}
	ut_log("\n");
}
static unsigned char virtio_get_pcistatus(unsigned long pci_ioaddr) {
	uint16_t addr = pci_ioaddr + VIRTIO_PCI_STATUS;
	return inb(addr);
}
static void virtio_set_pcistatus(unsigned long pci_ioaddr, unsigned char status) {
	uint16_t addr = pci_ioaddr + VIRTIO_PCI_STATUS;
	outb(addr, status);
}
/* the notify function used when creating a virt queue */
static void notify(struct virtqueue *vq) {
	atomic_inc(&g_conf_stat_pio);
	outw(vq->pci_ioaddr + VIRTIO_PCI_QUEUE_NOTIFY, vq->queue_number);
}
static void callback(struct virtqueue *vq) {
	DEBUG("  VIRTIO CALLBACK in VIRT queue :%x\n", vq);
}
static int get_order(unsigned long size) {
	int order;

	size = (size - 1) >> (PAGE_SHIFT - 1);
	order = -1;
	do {
		size >>= 1;
		order++;
	} while (size);
	return order;
}

/*******************************  virtio_jdriver ********************************/
void virtio_jdriver::print_stats(unsigned char *arg1,unsigned char *arg2) {
	ut_printf("		Network device: %s: Send(P,K,I): %d,%i,%d  Recv(P,K,I):%d,%i,%d allocs:%d free:%d ERR(send no space):%d TotalKick:%i\n", this->name, this->stat_sends,
			this->stat_send_kicks, this->stat_send_interrupts, this->stat_recvs, this->stat_recv_kicks, this->stat_recv_interrupts, this->stat_allocs,
			this->stat_frees, this->stat_err_nospace,this->stat_kicks);
}

void virtio_jdriver::queue_kick(struct virtqueue *vq){
	if (vq ==0 ) return;
	if (virtio_queuekick(vq) == 1){
		atomic_inc(&stat_kicks);
		if (vq->qType == VQTYPE_RECV){
			atomic_inc(&stat_recv_kicks);
		}else{
			atomic_inc(&stat_send_kicks);
		}
	}
}
struct virtqueue *virtio_jdriver::virtio_create_queue(uint16_t index, int qType) {
	int size;
	uint16_t num;
	unsigned long ring_addr;
	struct virtqueue *virt_q;
	unsigned long pci_ioaddr = device->pci_device.pci_ioaddr;

	outw(pci_ioaddr + VIRTIO_PCI_QUEUE_SEL, index);

	num = inw(pci_ioaddr + VIRTIO_PCI_QUEUE_NUM);
	//num = 1024;
	INIT_LOG("		New virtio create queue NUM-%d : num %x(%d)  :%x\n", index, num, num, vring_size(num, VIRTIO_PCI_VRING_ALIGN));
	if (num == 0) {
		return 0;
	}
	size = PAGE_ALIGN(vring_size(num, VIRTIO_PCI_VRING_ALIGN));

	//vring_size(num);
	ring_addr = mm_getFreePages(MEM_CLEAR, get_order(size));

	/* activate the queue */
	outl(pci_ioaddr + VIRTIO_PCI_QUEUE_PFN, __pa(ring_addr) >> VIRTIO_PCI_QUEUE_ADDR_SHIFT);

	/* create the vring */
	INIT_LOG("		virtioqueue Creating queue:%x(pa:%x) size-order:%x  size:%x\n", ring_addr, __pa(ring_addr),get_order(size),size);
	virt_q = (struct virtqueue * )vring_new_virtqueue(num, VIRTIO_PCI_VRING_ALIGN, device->pci_device.pci_ioaddr, (void *) ring_addr, &notify,
			&callback, "VIRTQUEUE", index);
	virtqueue_enable_cb_delayed(virt_q);
	virt_q->qType = qType;


	return virt_q;
}
/******************************************* virtio net *********************************/

struct virtio_feature_desc vtnet_feature_desc[] = { { VIRTIO_NET_F_CSUM, "TxChecksum" },
		{ VIRTIO_NET_F_GUEST_CSUM, "RxChecksum" }, { VIRTIO_NET_F_MAC, "MacAddress" }, { VIRTIO_NET_F_GSO, "TxAllGSO" }, {
				VIRTIO_NET_F_GUEST_TSO4, "RxTSOv4" }, { VIRTIO_NET_F_GUEST_TSO6, "RxTSOv6" }, { VIRTIO_NET_F_GUEST_ECN, "RxECN" },
		{ VIRTIO_NET_F_GUEST_UFO, "RxUFO" }, { VIRTIO_NET_F_HOST_TSO4, "TxTSOv4" }, { VIRTIO_NET_F_HOST_TSO6, "TxTSOv6" }, {
				VIRTIO_NET_F_HOST_ECN, "TxTSOECN" }, { VIRTIO_NET_F_HOST_UFO, "TxUFO" }, { VIRTIO_NET_F_MRG_RXBUF, "MrgRxBuf" }, {
				VIRTIO_NET_F_STATUS, "Status" }, { VIRTIO_NET_F_CTRL_VQ, "ControlVq" }, { VIRTIO_NET_F_CTRL_RX, "RxMode" }, {
				VIRTIO_NET_F_CTRL_VLAN, "VLanFilter" }, { VIRTIO_NET_F_CTRL_RX_EXTRA, "RxModeExtra" }, { VIRTIO_NET_F_MQ, "Multi queue" }, { 0, NULL } };
static int virtio_net_poll_device(void *private_data, int enable_interrupt, int total_pkts) {
	unsigned char *addr;
	unsigned int len = 0;
	virtio_net_jdriver *driver = (virtio_net_jdriver *) private_data;
	unsigned char *replace_buf;
	int i;
	int ret = 0;

	for (i = 0; i < total_pkts; i++) {
		addr = driver->remove_buf_from_vq(driver->queues[0].recv,(int *)&len);
		if (addr != 0) {
			driver->stat_recvs++;
			replace_buf = 0;
			//netif_rx(addr, len);
			net_sched.netif_rx(addr, len);

			driver->addBufToNetQueue(0,VQTYPE_RECV, replace_buf, 4096);
			ret = ret + 1;
		} else {
			break;
		}
	}
	if (ret > 0) {
		driver->queue_kick(driver->queues[0].recv);
	}
	if (enable_interrupt && driver->recv_interrupt_disabled==0) {
		virtio_enable_cb(driver->queues[0].recv);
	}else{
		//virtio_disable_cb(driver->queues[0].recv);
	}
	return ret;
}


static int netdriver_xmit(unsigned char* data, unsigned int len, void *private_data) {
	virtio_net_jdriver *net_driver = (virtio_net_jdriver *) private_data;
	return net_driver->write(data, len, 0);
}
int g_conf_netbh_enable = 1;
static int virtio_net_recv_interrupt(void *private_data) {
	jdevice *dev;
	virtio_net_jdriver *driver = (virtio_net_jdriver *) private_data;

	dev = (jdevice *) driver->device;
	if (dev->pci_device.msi_enabled == 0){
		inb(dev->pci_device.pci_ioaddr + VIRTIO_PCI_ISR);
	}

	driver->stat_recv_interrupts++;

	if (driver->recv_interrupt_disabled == 0){
	  virtio_disable_cb(driver->queues[0].recv); /* disabling interrupts have Big negative impact on packet recived when smp enabled */
	}

    if (g_conf_netbh_enable == 1){  /* handing over the packets to net bx thread  */
    	net_sched.netif_rx_enable_polling(private_data, virtio_net_poll_device);
    }else{ /* without net bx  */
    	virtio_net_poll_device(private_data,1,1000);
    }
	return 0;

}
static int virtio_net_send_interrupt(void *private_data) {
	jdevice *dev;
	virtio_net_jdriver *driver = (virtio_net_jdriver *) private_data;

	dev = (jdevice *) driver->device;
	if (dev->pci_device.msi_enabled == 0){
		inb(dev->pci_device.pci_ioaddr + VIRTIO_PCI_ISR);
	}

	driver->stat_send_interrupts++;
	virtio_disable_cb(driver->queues[0].send);
	driver->send_waitq->wakeup();
	return 0;

}

int virtio_net_jdriver::probe_device(class jdevice *jdev) {

	if ((jdev->pci_device.pci_header.vendor_id == VIRTIO_PCI_VENDOR_ID)
			&& (jdev->pci_device.pci_header.device_id == VIRTIO_PCI_NET_DEVICE_ID)) {
		ut_log(" Matches inside the NETPROBE.... \n");
		return JSUCCESS;
	}
	return JFAIL;
}
static int net_devices=0;

struct virtio_net_ctrl_mq {
         uint16_t virtqueue_pairs;
 };
#define VIRTIO_NET_CTRL_MQ   4
#define VIRTIO_NET_CTRL_MQ_VQ_PAIRS_SET        0
static int virtnet_send_command(struct virtqueue *vq,
                                uint16_t mq_len )
{
	struct scatterlist sg[2];
	int ret;
	int len;

	unsigned char *buf = 0;

	if (buf == 0) {
		buf = (unsigned char *) jalloc_page(MEM_NETBUF);
		len = 4096; /* page size */
	}
	if (buf == 0) {
		BRK;
	}

	ut_memset(buf, 0, sizeof(struct virtio_net_hdr));
	struct virtio_net_ctrl_hdr *ctrl;
	struct virtio_net_ctrl_mq *mq;

	ctrl = (struct virtio_net_ctrl_hdr *)buf;
	ctrl->var_class = VIRTIO_NET_CTRL_MQ;
	ctrl->cmd = VIRTIO_NET_CTRL_MQ_VQ_PAIRS_SET;
	mq = (struct virtio_net_ctrl_mq *)(buf + 1024);
	mq->virtqueue_pairs = mq_len;

	sg[0].page_link = (unsigned long) buf;
	sg[0].length = sizeof(struct virtio_net_ctrl_hdr);
	sg[0].offset = 0;
	sg[1].page_link = (unsigned long) (mq);
	sg[1].length = sizeof(struct virtio_net_ctrl_mq);
	sg[1].offset = 0;
	sg[2].page_link = buf+2048;
	sg[2].length = 1024;
	sg[2].offset = 0;
	//DEBUG(" scatter gather-0: %x:%x sg-1 :%x:%x \n",sg[0].page_link,__pa(sg[0].page_link),sg[1].page_link,__pa(sg[1].page_link));

	ret = virtio_add_buf_to_queue(vq, sg, 2, 1, (void *) sg[0].page_link, 0);/* send q */
	virtio_queuekick(vq);
	//sc_sleep(1000);
	ut_log(" New111 SEND the maxq control command \n");
/* TODO:  wait for the response and free the buf */
	return ret;
}

int virtio_net_jdriver::net_attach_device() {
	unsigned long addr;
	uint32_t features=0;
	uint32_t guest_features=0;
	uint32_t mask_features=0;
	int i,k;
	pci_dev_header_t *pci_hdr = &device->pci_device.pci_header;
	unsigned long pci_ioaddr = device->pci_device.pci_ioaddr;
	uint32_t msi_vector;
	unsigned char name[MAX_DEVICE_NAME];


	ut_snprintf(name,MAX_DEVICE_NAME,"net%d",net_devices);
	ut_strcpy(device->name,name);
	arch_spinlock_init(&virtionet_lock, device->name );
	net_devices++;

	virtio_set_pcistatus(pci_ioaddr, virtio_get_pcistatus(pci_ioaddr) + VIRTIO_CONFIG_S_ACKNOWLEDGE);
	ut_log("	VirtioNet: Initializing VIRTIO PCI NET status :%x : \n", virtio_get_pcistatus(pci_ioaddr));

	virtio_set_pcistatus(pci_ioaddr, virtio_get_pcistatus(pci_ioaddr) + VIRTIO_CONFIG_S_DRIVER);

	addr = pci_ioaddr + VIRTIO_PCI_HOST_FEATURES;
	features = inl(addr);
	guest_features = features;
	mask_features = (0x4000ff);

	guest_features = guest_features & mask_features;

	INIT_LOG("	VirtioNet:  HOSTfeatures :%x:  capabilitie:%x guestfeatures:%x mask_features:%x\n", features, pci_hdr->capabilities_pointer,guest_features,mask_features);
	display_virtiofeatures(features, vtnet_feature_desc);

	 addr = pci_ioaddr + VIRTIO_PCI_GUEST_FEATURES;
	 outl(addr,guest_features);

	if (pci_hdr->capabilities_pointer != 0) {
		msi_vector = pci_read_msi(&device->pci_device.pci_addr, &device->pci_device.pci_header, &device->pci_device.pci_bars[0],
				device->pci_device.pci_bar_count, &device->pci_device.msix_cfg);
		if (msi_vector > 0)
			pci_enable_msix(&device->pci_device.pci_addr, &device->pci_device.msix_cfg,
					device->pci_device.pci_header.capabilities_pointer);
	} else {
		msi_vector = 0;
	}

	if (msi_vector == 0) {
		device->pci_device.msi_enabled = 0;
		addr = pci_ioaddr + 20;
	} else {
		device->pci_device.msi_enabled = 1;
		addr = pci_ioaddr + 24;
	}
	for (i = 0; i < 6; i++){
		this->mac[i] = inb(addr + i);
	}
	this->max_vqs = 1;
	if ((features >> VIRTIO_NET_F_MQ) & 0x1){
		this->max_vqs = inw(addr + 6+2);
	}
	INIT_LOG("	VIRTIONET:  pioaddr:%x MAC address : %x :%x :%x :%x :%x :%x mis_vector:%x   : max_vqs:%x\n", addr, this->mac[0], this->mac[1],
			this->mac[2], this->mac[3], this->mac[4], this->mac[5], msi_vector,this->max_vqs);

	INIT_LOG("	VIRTIONET: initializing vqs:%d\n",max_vqs);
	for (i=0; i<max_vqs; i++){
		if (i==1){/* by default only 3 queues will be present , at this point already 2 queues are configured */
			control_q = virtio_create_queue(2*i,VQTYPE_RECV);
			if (control_q){ /* send mq command */
				virtnet_send_command(control_q,max_vqs);
				//break;
			}
		}
		queues[i].recv = virtio_create_queue(2*i,VQTYPE_RECV);
		if (msi_vector > 0) {
			outw(pci_ioaddr + VIRTIO_MSI_QUEUE_VECTOR, (2*i)+0);
		}
		queues[i].send = virtio_create_queue((2*i)+1,VQTYPE_SEND);
		if (msi_vector > 0) {
			outw(pci_ioaddr + VIRTIO_MSI_QUEUE_VECTOR, (2*i)+1);
		}
	}
#if 0
	if (msi_vector > 0) {
		outw(pci_ioaddr + VIRTIO_MSI_QUEUE_VECTOR, 1);
	//	outw(pci_ioaddr + VIRTIO_MSI_QUEUE_VECTOR, 0xffff);
	}
#endif
	send_waitq = jnew_obj(wait_queue, "waitq_net", 0);

	if (g_conf_net_send_int_disable == 1){
		for (i=0; i<max_vqs; i++){
			virtio_disable_cb(queues[i].send); /* disable interrupts on sending side */
		}
	}

	if (msi_vector > 0) {
		for (i = 0; i < 3; i++){
			char irq_name[MAX_DEVICE_NAME];
			if (i==0){
				ut_snprintf(irq_name,MAX_DEVICE_NAME,"%s_recv_msi",this->name);
				ar_registerInterrupt(msi_vector + i, virtio_net_recv_interrupt, irq_name, (void *) this);
			}
#if 0
			// TODO : enabling sending side interrupts causes freeze in the buffer consumption on the sending side,
			// till  all the buffers are full for the first time. this happens especially on the smp

			if (i!=0){
				ut_snprintf(irq_name,MAX_DEVICE_NAME,"%s_send_msi",jdev->name);
				ar_registerInterrupt(msi_vector + i, virtio_net_send_interrupt, irq_name, (void *) this);
			}
#endif
		}
	}

	for (k = 0; k < max_vqs; k++) {
		if (queues[k].recv == 0 || queues[k].send ==0){
			break;
		}
		for (i = 0; i < 120; i++) /* add buffers to recv q */
			addBufToNetQueue(k, VQTYPE_RECV, 0, 4096);
	}
	inb(pci_ioaddr + VIRTIO_PCI_ISR);
	for (k = 0; k < max_vqs; k++) {
		if (queues[k].recv == 0 || queues[k].send ==0){
			break;
		}
		queue_kick(queues[k].recv);
//	queue_kick(queues[k].send);
	}
	pending_kick_onsend =0;
	recv_interrupt_disabled = 0;


	virtio_set_pcistatus(pci_ioaddr, virtio_get_pcistatus(pci_ioaddr) + VIRTIO_CONFIG_S_DRIVER_OK);
	INIT_LOG("		VirtioNet:  Initialization Completed status:%x\n", virtio_get_pcistatus(pci_ioaddr));

	return 1;
}

jdriver *virtio_net_jdriver::attach_device(class jdevice *jdev) {
	stat_allocs = 0;
	stat_frees = 0;
	stat_err_nospace = 0;
	COPY_OBJ(virtio_net_jdriver, this, new_obj, jdev);
	((virtio_net_jdriver *) new_obj)->net_attach_device();
	jdev->driver = new_obj;
	register_netdevice(jdev);
	return (jdriver *) new_obj;
}

int virtio_net_jdriver::dettach_device(jdevice *jdev) {
	return JFAIL;
}
int virtio_net_jdriver::read(unsigned char *buf, int len, int rd_flags) {
	return 0;
}

int virtio_net_jdriver::addBufToNetQueue(int qno, int type, unsigned char *buf, unsigned long len) {
	struct scatterlist sg[2];
	int ret;
	struct virtqueue *vq;

	if (type == VQTYPE_RECV){
		vq= queues[qno].recv;
	}else{
		vq= queues[qno].send;
	}

	if (buf == 0) {
		buf = (unsigned char *) jalloc_page(MEM_NETBUF);
		len = 4096; /* page size */
	}
	if (buf == 0){
		BRK;
	}

	ut_memset(buf, 0, sizeof(struct virtio_net_hdr));
#if 0
add_buf((unsigned long)buf,qno);
#endif

	sg[0].page_link = (unsigned long) buf;
	sg[0].length = sizeof(struct virtio_net_hdr);
	sg[0].offset = 0;
	sg[1].page_link = (unsigned long) (buf + sizeof(struct virtio_net_hdr));
	sg[1].length = len - sizeof(struct virtio_net_hdr);
	sg[1].offset = 0;
	//DEBUG(" scatter gather-0: %x:%x sg-1 :%x:%x \n",sg[0].page_link,__pa(sg[0].page_link),sg[1].page_link,__pa(sg[1].page_link));
	if (vq->qType == VQTYPE_RECV) {
		ret = virtio_add_buf_to_queue(vq, sg, 0, 2, (void *) sg[0].page_link, 0);/* recv q*/
	} else {
		ret = virtio_add_buf_to_queue(vq, sg, 2, 0, (void *) sg[0].page_link, 0);/* send q */
	}

	return ret;
}
unsigned long virtio_net_jdriver::remove_buf_from_vq(struct virtqueue *virt_q,int *len){
	unsigned long addr;

	addr = (unsigned long) virtio_removeFromQueue(virt_q, (unsigned int *) len);

	return addr;
}
int virtio_net_jdriver::free_send_bufs(){
	int i;
	unsigned long flags;
	unsigned long addr;
	int len;
	int qno;
	int ret=0;

	for (qno = 0; qno < max_vqs; qno++) {
		i = 0;
		while (i < 50) {
			i++;
			spin_lock_irqsave(&virtionet_lock, flags);
			addr = remove_buf_from_vq(queues[qno].send, &len);
			spin_unlock_irqrestore(&virtionet_lock, flags);
			if (addr != 0) {
				free_page(addr);
				stat_frees++;
				ret++;
			} else {
				goto next;
			}
		}
		next: ;
	}
	return ret;
}
int g_conf_net_sendbuf_delay=0;
int virtio_net_jdriver::write(unsigned char *data, int len, int wr_flags) {
	jdevice *dev;
	int i, ret;
	unsigned long flags;
	unsigned long addr;

	dev = (jdevice *) this->device;
	if (dev == 0 || data == 0)
		return JFAIL;

	if (wr_flags & WRITE_BUF_CREATED) {
		addr = data-10;
	} else {
		addr = (unsigned long) jalloc_page(MEM_NETBUF);
		if (addr == 0){
			return JFAIL;
		}
		stat_allocs++;
		ut_memset((unsigned char *) addr, 0, 10);
		ut_memcpy((unsigned char *) addr + 10, data, len);
	}

	ret = -ERROR_VIRTIO_ENOSPC;
	spin_lock_irqsave(&virtionet_lock, flags);

#if 0
	for (i=0; i<2 && ret == -ERROR_VIRTIO_ENOSPC; i++){
		ret = addBufToNetQueue(this->vq[1], (unsigned char *) addr, len + 10);
		if (ret == -ERROR_VIRTIO_ENOSPC){
			stat_err_nospace++;
			spin_unlock_irqrestore(&virtionet_lock, flags);
			free_send_bufs();
			spin_lock_irqsave(&virtionet_lock, flags);
			ret = addBufToNetQueue(this->vq[1], (unsigned char *) addr, len + 10);
		}
		if (ret == -ERROR_VIRTIO_ENOSPC){
			stat_err_nospace++;
			queue_kick(1);
			//virtio_enable_cb(this->vq[1]);
		}
	}
#else
	send_count++; /* send continously few packets per each queue */
	if (send_count > 5000){
		current_send_q++;
		send_count=0;
	}
	int qno= current_send_q % max_vqs ;

	ret = addBufToNetQueue(qno,VQTYPE_SEND, (unsigned char *) addr, len + 10);
#endif

	if (ret == -ERROR_VIRTIO_ENOSPC) {
		stat_err_nospace++;
		if ((wr_flags & WRITE_SLEEP_TILL_SEND) == 0){
			free_page((unsigned long) addr);
			stat_frees++;
			ret = JSUCCESS;
			pending_kick_onsend = 1;
		}
	}else{
		stat_sends++;
		ret = JSUCCESS;
		pending_kick_onsend = 1;
	}

	spin_unlock_irqrestore(&virtionet_lock, flags);

	free_send_bufs();
	return ret;  /* Here Sucess indicates the buffer is freed or consumed */
}

int virtio_net_jdriver::ioctl(unsigned long arg1, unsigned long arg2) {
	unsigned char *arg_mac = (unsigned char *) arg2;
	int i;

	if (arg1 == NETDEV_IOCTL_GETMAC) {
		if (arg_mac == 0)
			return JFAIL;
		else {
			ut_memcpy(arg_mac, mac, 6);
			return JSUCCESS;
		}
	} else if (arg1 == NETDEV_IOCTL_FLUSH_SENDBUF) {
		if (pending_kick_onsend!=0){
			unsigned long flags;
			int qno= current_send_q % max_vqs ;

			spin_lock_irqsave(&virtionet_lock, flags);
			queue_kick(queues[qno].send);
#if 0
			for (i=0; i<max_vqs && i<MAX_VIRT_QUEUES; i++){
				queue_kick(queues[i].send);
			}
#endif
			pending_kick_onsend=0;
			spin_unlock_irqrestore(&virtionet_lock, flags);
			return JSUCCESS;
		}else{
			return JFAIL;
		}
	} else if (arg1 == NETDEV_IOCTL_DISABLE_RECV_INTERRUPTS){
		virtio_disable_cb(queues[0].recv);
		recv_interrupt_disabled = 1;
		if (g_conf_netbh_enable == 1){  /* handing over the packets to net bx thread  */
		    	net_sched.netif_rx_enable_polling(this, virtio_net_poll_device);
		 }
	} else if (arg1 == NETDEV_IOCTL_ENABLE_RECV_INTERRUPTS){
		virtio_enable_cb(queues[0].recv);
		recv_interrupt_disabled = 0;
	} else if (arg1 == NETDEV_IOCTL_PRINT_STAT){
		ut_printf("Total queues: %d\n",max_vqs);
		for (i=0; i<max_vqs && i<MAX_VIRT_QUEUES; i++){
			if (queues[i].recv==0 || queues[i].send==0){
				break;
			}
			ut_printf("VQ-%d :\n",i);
			print_vq(queues[i].recv);
			print_vq(queues[i].send);
		}
		ut_printf("Control-Q :\n");
		print_vq(control_q);

	}
}
/*****************************  Virtio Disk ********************************************/
#define VIRTIO_BLK_T_IN 0
#define VIRTIO_BLK_T_OUT 1
#define VIRTIO_BLK_T_SCSI_CMD 2
#define VIRTIO_BLK_T_SCSI_CMD_OUT 3
#define VIRTIO_BLK_T_FLUSH 4
#define VIRTIO_BLK_T_FLUSH_OUT 5
#define VIRTIO_BLK_T_BARRIER 0x80000000

#define DISK_READ 0
#define DISK_WRITE 1

#define VIRTIO_BLK_S_OK 0
#define VIRTIO_BLK_S_IOERR 1
#define VIRTIO_BLK_S_UNSUPP 2


#define VIRTIO_BLK_DATA_SIZE (4096)
struct virtio_blk_req {
	uint32_t type ;
	uint32_t ioprio ;
	uint64_t sector ;
	char data[VIRTIO_BLK_DATA_SIZE]; /*TODO:  currently it made fixed, actually it is  variable size data here like data[][512]; */
	uint8_t status ;
	int len;
};
//struct virtqueue *disk_vq;
static int virtio_disk_interrupt(void *private_data) {

	jdevice *dev;
	virtio_disk_jdriver *driver = (virtio_disk_jdriver *) private_data;

	dev = (jdevice *) driver->device;
	if (dev->pci_device.msi_enabled == 0){
		inb(dev->pci_device.pci_ioaddr + VIRTIO_PCI_ISR);
	}

	driver->stat_recv_interrupts++;
	if (driver->waitq != 0){
		driver->waitq->wakeup();
	}
	return 0;
}

struct virtio_scsi_blk_req {
	/* out hdr-1, common to block and scsi */
	uint32_t type ;
	uint32_t ioprio ;
	uint64_t sector ;

	/* out hdr-2, common to block and scsi */
	uint8_t scsi_cmd[100];

	/* in for read /out for write , common to block and scsi */
	uint8_t data[VIRTIO_BLK_DATA_SIZE]; /*TODO:  currently it made fixed, actually it is  variable size data here like data[][512]; */

	/* in hdr-1, common to block and scsi */
	#define SCSI_SENSE_BUFFERSIZE 96
	uint8_t sense[SCSI_SENSE_BUFFERSIZE] ;

	/* in hdr-2, common to block and scsi */
uint32_t errors;
uint32_t data_len;
uint32_t sense_len ;
uint32_t residual ;

    /* in hdr-3, common to block and scsi */
	uint8_t status ;
	int len;
};

enum {
SCSI_CMD_TEST_UNIT_READY = 0x00,
SCSI_CMD_REQUEST_SENSE = 0x03,
SCSI_CMD_INQUIRY = 0x12,
SCSI_CMD_READ_16 = 0x88,
SCSI_CMD_WRITE_16 = 0x8A,
SCSI_CMD_READ_CAPACITY = 0x9E,
SCSI_CMD_SYNCHRONIZE_CACHE_10 = 0x35,
SCSI_CMD_SYNCHRONIZE_CACHE_16 = 0x91,
SCSI_CMD_REPORT_LUNS = 0xA0,
};
struct cdb_readwrite_16 {
	uint8_t command;
	uint8_t flags;
	uint64_t lba;
	uint32_t count;
	uint8_t group_number;
	uint8_t control;
} __attribute__((packed));


static __inline uint32_t bswap32(uint32_t __x)
{
return (__x>>24) | (__x>>8&0xff00) | (__x<<8&0xff0000) | (__x<<24);
}
static __inline uint64_t bswap64(uint64_t __x)
{
return ((bswap32(__x)+0ULL)<<32) | bswap32(__x>>32);
}
static void req_construct(unsigned char *buf, unsigned long offset, unsigned long len){
	struct cdb_readwrite_16 *req = (struct cdb_readwrite_16 *)buf;
	uint64_t lba;
	uint32_t count;

	lba = offset/512;
	count = len/512;
	req->lba = bswap64(lba);
	req->count = bswap32(count);
	req->command = SCSI_CMD_READ_16;
}
void *virtio_disk_jdriver::scsi_addBufToQueue(int type, unsigned char *buf_arg, uint64_t len, uint64_t sector, uint64_t data_len) {
	struct virtqueue *tmp_vq = this->vq[0];
	struct virtio_scsi_blk_req *req;
	struct scatterlist sg[8];
	int ret;
	int transfer_len=len;
	int out,in;
	unsigned char *buf=0;
ut_printf("Sending the SCSI request sector:%x len:%d  :%d \n",sector,len,data_len);
	if (buf == 0) {
		//buf = (unsigned char *) jalloc_page(0);
		buf = mm_getFreePages(0,1);  /* TODO: for write request does not work, need to copy the data buf */
	}
	if (buf == 0 ){
		BRK;
	}

	req = (struct virtio_scsi_blk_req *)buf;
	ut_memset(buf, 0, sizeof(struct virtio_scsi_blk_req));
	req->sector = sector;
	if (type == DISK_READ){
		req->type = VIRTIO_BLK_T_SCSI_CMD;
		out = 2;
		in = 4;
	}else{
		req->type = VIRTIO_BLK_T_OUT;
		if (data_len>VIRTIO_BLK_DATA_SIZE){
			ut_memcpy(buf+16, buf_arg, VIRTIO_BLK_DATA_SIZE);
		}else{
			ut_memcpy(buf+16, buf_arg, data_len);
		}
		out = 2;
		in = 1;

	}
	req->status = 0xff;
	int cmd_len=100;
	int scsi_sense_hdr =  SCSI_SENSE_BUFFERSIZE;
	int resp_len=4*4;  /* scsi response */
	if (transfer_len < blk_size){
		transfer_len=blk_size;
	}

	sg[0].page_link = (unsigned long) buf;
	sg[0].length = 16;
	sg[0].offset = 0;

	req_construct(buf+16, sector*512, len );
	sg[1].page_link = (unsigned long) buf + 16;
	sg[1].length = 16;
	sg[1].offset = 0;

	sg[2].page_link = (unsigned long) (buf + 16 + cmd_len);
	sg[2].length =  transfer_len;
	sg[2].offset = 0;


	sg[3].page_link = (unsigned long) (buf + 16 + cmd_len + VIRTIO_BLK_DATA_SIZE);
	sg[3].length =  scsi_sense_hdr;
	sg[3].offset = 0;

	sg[4].page_link = (unsigned long) (buf + 16 + cmd_len + VIRTIO_BLK_DATA_SIZE + scsi_sense_hdr);
	sg[4].length =  resp_len;
	sg[4].offset = 0;

	sg[5].page_link = (unsigned long) (buf + 16 + cmd_len +  VIRTIO_BLK_DATA_SIZE + scsi_sense_hdr + resp_len);
	sg[5].length =  1;
	sg[5].offset = 0;

	//DEBUG(" scatter gather-0: %x:%x sg-1 :%x:%x \n",sg[0].page_link,__pa(sg[0].page_link),sg[1].page_link,__pa(sg[1].page_link));

	virtio_disable_cb(tmp_vq);
	ret = virtio_add_buf_to_queue(tmp_vq, sg, out, in, (void *) sg[0].page_link, 0);/* send q */
	queue_kick(vq[0]);

	virtio_enable_cb(tmp_vq);
sc_sleep(2000);
	return (void *)buf;
}
void *virtio_disk_jdriver::addBufToQueue(int type, unsigned char *buf_arg, uint64_t len, uint64_t sector, uint64_t data_len) {
	struct virtqueue *tmp_vq = this->vq[0];
	struct virtio_blk_req *req;
	struct scatterlist sg[4];
	int ret;
	int transfer_len=len;
	int out,in;
	unsigned char *buf=0;

	if (buf == 0) {
		//buf = (unsigned char *) jalloc_page(0);
		buf = mm_getFreePages(0,1);  /* TODO: for write request does not work, need to copy the data buf */
	}
	if (buf == 0 ){
		BRK;
	}

	req = (struct virtio_blk_req *)buf;
	ut_memset(buf, 0, sizeof(struct virtio_blk_req));
	req->sector = sector;
	if (type == DISK_READ){
		req->type = VIRTIO_BLK_T_IN;
		out = 1;
		in = 2;
	}else{
		req->type = VIRTIO_BLK_T_OUT;
		if (data_len>VIRTIO_BLK_DATA_SIZE){
			ut_memcpy(buf+16, buf_arg, VIRTIO_BLK_DATA_SIZE);
		}else{
			ut_memcpy(buf+16, buf_arg, data_len);
		}
		out = 2;
		in = 1;

	}
	req->status = 0xff;

	sg[0].page_link = (unsigned long) buf;
	sg[0].length = 16;
	sg[0].offset = 0;

	sg[1].page_link = (unsigned long) (buf + 16);
	if (transfer_len < blk_size){
		transfer_len=blk_size;
	}
	sg[1].length =  transfer_len;
	sg[1].offset = 0;

	sg[2].page_link = (unsigned long) (buf + 16 + VIRTIO_BLK_DATA_SIZE);
	sg[2].length =  1;
	sg[2].offset = 0;

	//DEBUG(" scatter gather-0: %x:%x sg-1 :%x:%x \n",sg[0].page_link,__pa(sg[0].page_link),sg[1].page_link,__pa(sg[1].page_link));

	virtio_disable_cb(tmp_vq);
	ret = virtio_add_buf_to_queue(tmp_vq, sg, out, in, (void *) sg[0].page_link, 0);/* send q */
	queue_kick(vq[0]);

	virtio_enable_cb(tmp_vq);

	return (void *)buf;
}
uint64_t virtio_config64(unsigned long pcio_addr){
	uint64_t ret;
	auto addr = pcio_addr  + VIRTIO_MSI_CONFIG_VECTOR ;
	ret = inl(addr);
	ret=ret + (inl(addr+4)* (0x1 << 32));
	return ret;
}
uint32_t virtio_config32(unsigned long pcio_addr){
	uint64_t ret;
	auto addr = pcio_addr  + VIRTIO_MSI_CONFIG_VECTOR;
	ret = inl(addr);
	return ret;
}
int virtio_disk_jdriver::disk_attach_device(class jdevice *jdev) {
	auto pci_ioaddr = jdev->pci_device.pci_ioaddr;
	unsigned long features;

	this->device = jdev;
	virtio_set_pcistatus(pci_ioaddr, virtio_get_pcistatus(pci_ioaddr) + VIRTIO_CONFIG_S_ACKNOWLEDGE);
	ut_log("	Virtio disk: Initializing status :%x : \n", virtio_get_pcistatus(pci_ioaddr));

	virtio_set_pcistatus(pci_ioaddr, virtio_get_pcistatus(pci_ioaddr) + VIRTIO_CONFIG_S_DRIVER);
	auto addr = pci_ioaddr + VIRTIO_PCI_HOST_FEATURES;
	features = inl(addr);
	INIT_LOG("	Virtio disk: Initializing VIRTIO PCI hostfeatures :%x: status :%x :\n", features,  virtio_get_pcistatus(pci_ioaddr));

	if (jdev->pci_device.pci_header.device_id != VIRTIO_PCI_SCSI_DEVICE_ID) {
		this->vq[0] = this->virtio_create_queue(0, VQTYPE_SEND);
	}else{
		this->vq[0] = this->virtio_create_queue(0,VQTYPE_SEND);
		this->vq[1] = this->virtio_create_queue(1,VQTYPE_RECV);
		this->vq[2] = this->virtio_create_queue(2,VQTYPE_SEND);
	}

	if (jdev->pci_device.msix_cfg.isr_vector > 0) {
#if 0
		outw(pci_ioaddr + VIRTIO_MSI_QUEUE_VECTOR,0);
		outw(pci_ioaddr + VIRTIO_MSI_QUEUE_VECTOR,0xffff);
		ar_registerInterrupt(msi_vector, virtio_disk_interrupt, "virtio_disk_msi",this);
#endif
	}
	virtio_set_pcistatus(pci_ioaddr, virtio_get_pcistatus(pci_ioaddr) + VIRTIO_CONFIG_S_DRIVER_OK);
	INIT_LOG("	Virtio disk:  VIRTIO PCI COMPLETED with driver ok :%x \n", virtio_get_pcistatus(pci_ioaddr));
	inb(pci_ioaddr + VIRTIO_PCI_ISR);
	ar_registerInterrupt(32 + jdev->pci_device.pci_header.interrupt_line, virtio_disk_interrupt, "virt_disk_irq", (void *) this);

	int i;
	unsigned long config_data;
	disk_size=0;
	/*
	 * struct v i r t i o _ b l k _ c o n f i g {
		u64 capacity ;
		u32 size_max ;
		u32 seg_max ;
		s t r u c t v i r t i o _ b l k _ g e o m e t r y {
			u16 cylinders ;
			u8 heads;
			u8 sectors;
		} g e o m e t r y ;
		u32 blk_size;
	}
	 */
	if (jdev->pci_device.pci_header.device_id != VIRTIO_PCI_SCSI_DEVICE_ID) {
		disk_size = virtio_config64(pci_ioaddr + 0) * 512;
		blk_size = virtio_config32(pci_ioaddr + 20);
		INIT_LOG(" 	Virtio Disk size:%d(%x)  blk_size:%d\n", disk_size, disk_size,blk_size);

	} else {
		/*
		 *    virtio_stl_p(vdev, &scsiconf->num_queues, s->conf.num_queues);
		 virtio_stl_p(vdev, &scsiconf->seg_max, 128 - 2);
		 virtio_stl_p(vdev, &scsiconf->max_sectors, s->conf.max_sectors);
		 virtio_stl_p(vdev, &scsiconf->cmd_per_lun, s->conf.cmd_per_lun);
		 virtio_stl_p(vdev, &scsiconf->event_info_size, sizeof(VirtIOSCSIEvent));
		 virtio_stl_p(vdev, &scsiconf->sense_size, s->sense_size);
		 virtio_stl_p(vdev, &scsiconf->cdb_size, s->cdb_size);
		 virtio_stw_p(vdev, &scsiconf->max_channel, VIRTIO_SCSI_MAX_CHANNEL);
		 virtio_stw_p(vdev, &scsiconf->max_target, VIRTIO_SCSI_MAX_TARGET);
		 virtio_stl_p(vdev, &scsiconf->max_lun, VIRTIO_SCSI_MAX_LUN)
		 */
		INIT_LOG("	SCSI Num of Reques Queues: %d \n", virtio_config32(pci_ioaddr + 0));
		INIT_LOG("	SCSI seg max: %d \n", virtio_config32(pci_ioaddr + 4));
		INIT_LOG("	SCSI max sector: %d \n", virtio_config32(pci_ioaddr + 8));
		INIT_LOG("	SCSI cmd_per_lun: %d \n", virtio_config32(pci_ioaddr + 12));
		INIT_LOG("	SCSI event_info_size: %d \n", virtio_config32(pci_ioaddr + 16));
		INIT_LOG("	SCSI sense size: %d \n", virtio_config32(pci_ioaddr + 20));
		INIT_LOG("	SCSI cdb size: %d \n", virtio_config32(pci_ioaddr + 24));
	}
	INIT_LOG("		driver status:  %x :\n",virtio_get_pcistatus(pci_ioaddr));
	virtio_set_pcistatus(pci_ioaddr, virtio_get_pcistatus(pci_ioaddr) + VIRTIO_CONFIG_S_DRIVER_OK);
	INIT_LOG("		second time	Virtio disk:  VIRTIO PCI COMPLETED with driver ok :%x \n", virtio_get_pcistatus(pci_ioaddr));

	return 1;
}

int virtio_disk_jdriver::probe_device(class jdevice *jdev) {
	if ((jdev->pci_device.pci_header.vendor_id == VIRTIO_PCI_VENDOR_ID)
			&& ((jdev->pci_device.pci_header.device_id == VIRTIO_PCI_BLOCK_DEVICE_ID) ||
		(jdev->pci_device.pci_header.device_id == VIRTIO_PCI_SCSI_DEVICE_ID))){
		ut_log("		Matches the disk Probe :%d\n",jdev->pci_device.pci_header.device_id);
		return JSUCCESS;
	}
	return JFAIL;
}
extern jdriver *disk_drivers[];

jdriver *virtio_disk_jdriver::attach_device(class jdevice *jdev) {
	int i;

	stat_allocs = 0;
	stat_frees = 0;
	stat_err_nospace = 0;
	COPY_OBJ(virtio_disk_jdriver, this, new_obj, jdev);
	((virtio_disk_jdriver *) new_obj)->disk_attach_device(jdev);
	for (i=0; i<5; i++){
		if (disk_drivers[i]==0){
			disk_drivers[i]=(jdriver *) new_obj;
			break;
		}
	}

	((virtio_disk_jdriver *) new_obj)->waitq = jnew_obj(wait_queue, "waitq_disk", 0);
	spin_lock_init(&((virtio_disk_jdriver *)new_obj)->io_lock);
	init_tarfs((jdriver *)new_obj);
	return (jdriver *) new_obj;
}

int virtio_disk_jdriver::dettach_device(jdevice *jdev) {
	/*TODO:  Need to free the resources */
	return JFAIL;
}

int virtio_disk_jdriver::disk_io(int type,unsigned char *buf, int len, int offset) {
	struct virtio_blk_req *req;
	int sector;
	int data_len;
	int initial_skip, blks;
	unsigned long addr,flags;
	int qlen, ret;
	ret = 0;
	int scsi_type=0;

#if 0
	if (disk_size < 1000000){
		scsi_type=1;
	}
#endif
	if (device->pci_device.pci_header.device_id == VIRTIO_PCI_SCSI_DEVICE_ID){
		scsi_type=1;
		ut_printf(" scsi reading ..\n");
		//BRK;
	}

	sector = offset / blk_size;

	initial_skip = offset - sector * blk_size;
	data_len = len + initial_skip;

	if (data_len > VIRTIO_BLK_DATA_SIZE) {
		data_len = VIRTIO_BLK_DATA_SIZE;
	}
	if ((data_len + offset) >= disk_size) {
		data_len = disk_size - offset;
	}
	if (data_len <= 0) {
		return 0;
	}
	blks = data_len / blk_size;
	if ((blks * blk_size) != data_len) {
		data_len = (blks + 1) * blk_size;
	}
	spin_lock_irqsave(&io_lock, flags);
	//ut_printf(" data_len :%d sector: %d \n",data_len,sector);
	if (scsi_type ==0){
		req = addBufToQueue(type,buf, data_len, sector,len);
	}else{
		req = scsi_addBufToQueue(type,buf, data_len, sector,len);
	}

	int wait_loop = 0;
	if (req->status == 0xff  ){
		spin_unlock_irqrestore(&io_lock, flags);/* unlock before we go to sleep */
		while ((req->status == 0xff) && wait_loop < 500){
			waitq->wait(50);
			wait_loop++;
		}
		if (req->status == 0xff){
			ut_log(" ERROR: Disk io long wait\n");
			BRK;
		}
		spin_lock_irqsave(&io_lock, flags);
	}

	if (req->status != VIRTIO_BLK_S_OK) {
		goto last;
	}
	ret = data_len - initial_skip;
	if (ret > len) {
		ret = len;
	}
	ut_memcpy(buf, &req->data[initial_skip], ret);
	req->status = 0xfe; /* state to free the buf by any thread who remove from vq*/
last:
/* TODO : cannot free some one 's buffers  unless it is consumed */
	req =0;
	int loop=0;
	while (loop < 5){
		if (unfreed_req == 0){
			req = virtio_removeFromQueue(vq[0], &qlen);
		}else{
			req = (virtio_blk_req *)unfreed_req;
			unfreed_req = 0;
		}
		if (req == 0) break;
		if (req->status == 0xfe) {
			mm_putFreePages(req, 1);
		}else{
			unfreed_req = (unsigned char *)req;
			break;
		}
		loop++;
	}
	spin_unlock_irqrestore(&io_lock, flags);


//		ut_printf("%d -> %d  DATA :%x :%x  disksize:%d blksize:%d\n",i,disk_bufs[i]->status,disk_bufs[i]->data[0],disk_bufs[i]->data[1],disk_size,blk_size);
	return ret;
}
int virtio_disk_jdriver::read(unsigned char *buf, int len, int offset) {
	int ret;

	ret = disk_io(DISK_READ,buf,len,offset);
	return ret;
}
int virtio_disk_jdriver::write(unsigned char *buf, int len, int offset) {
	int ret;

	ret = disk_io(DISK_WRITE,buf,len,offset);
	return ret;
}
int virtio_disk_jdriver::ioctl(unsigned long arg1, unsigned long arg2) {
	if (arg1 == IOCTL_DISK_SIZE){
		return disk_size;
	}
	return JSUCCESS;
}
/***************************************************************************************************/
extern wait_queue *p9_waitq;
static int virtio_9p_interrupt(void *private_data) { // TODO: handling similar  type of interrupt generating while serving P9 interrupt.
	virtio_p9_jdriver *driver = (virtio_p9_jdriver *) private_data;

	if (driver->device->pci_device.msi_enabled == 0)
		inb(driver->device->pci_device.pci_ioaddr + VIRTIO_PCI_ISR);

	p9_waitq->wakeup(); /* wake all the waiting processes */

	return 0;
}
int virtio_p9_jdriver::probe_device(class jdevice *jdev) {

	if ((jdev->pci_device.pci_header.vendor_id == VIRTIO_PCI_VENDOR_ID)
			&& (jdev->pci_device.pci_header.device_id == VIRTIO_PCI_9P_DEVICE_ID)) {
		ut_log(" Matches the P9Probe \n");
		return JSUCCESS;
	}
	return JFAIL;
}
int virtio_p9_jdriver::p9_attach_device(class jdevice *jdev) {
	auto pci_ioaddr = jdev->pci_device.pci_ioaddr;
	unsigned long features;

	this->device = jdev;
	virtio_set_pcistatus(pci_ioaddr, virtio_get_pcistatus(pci_ioaddr) + VIRTIO_CONFIG_S_ACKNOWLEDGE);
	ut_log("	Virtio P9: Initializing VIRTIO PCI status :%x : \n", virtio_get_pcistatus(pci_ioaddr));

	virtio_set_pcistatus(pci_ioaddr, virtio_get_pcistatus(pci_ioaddr) + VIRTIO_CONFIG_S_DRIVER);

	auto addr = pci_ioaddr + VIRTIO_PCI_HOST_FEATURES;
	features = inl(addr);
	ut_log("	Virtio P9: Initializing VIRTIO PCI 9P hostfeatures :%x: status:%x\n", features, virtio_get_pcistatus(pci_ioaddr));

	this->vq[0] = this->virtio_create_queue(0, VQTYPE_SEND);
	if (jdev->pci_device.msix_cfg.isr_vector > 0) {
#if 0
		outw(virtio_dev->pci_ioaddr + VIRTIO_MSI_QUEUE_VECTOR,0);
		outw(virtio_dev->pci_ioaddr + VIRTIO_MSI_QUEUE_VECTOR,0xffff);
		ar_registerInterrupt(msi_vector, virtio_9p_interrupt, "virtio_p9_msi");
#endif
	}
	virtio_set_pcistatus(pci_ioaddr, virtio_get_pcistatus(pci_ioaddr) + VIRTIO_CONFIG_S_DRIVER_OK);
	ut_log("	Virtio P9:  VIRTIO PCI COMPLETED with driver ok :%x \n", virtio_get_pcistatus(pci_ioaddr));
	inb(pci_ioaddr + VIRTIO_PCI_ISR);

	p9_waitq= jnew_obj(wait_queue, "waitq_p9", 0);
	ar_registerInterrupt(32 + jdev->pci_device.pci_header.interrupt_line, virtio_9p_interrupt, "virt_p9_irq", (void *) this);

	p9_initFs(this);
	return 1;
}
jdriver *virtio_p9_jdriver::attach_device(class jdevice *jdev) {
	COPY_OBJ(virtio_p9_jdriver, this, new_obj, jdev);
	((virtio_p9_jdriver *) new_obj)->p9_attach_device(jdev);

	return (jdriver *) new_obj;
}
int virtio_p9_jdriver::dettach_device(jdevice *jdev) {
	return JFAIL;
}
int virtio_p9_jdriver::read(unsigned char *buf, int len, int flags) {
	return 0;
}
int virtio_p9_jdriver::write(unsigned char *buf, int len, int flags) {
	return 0;
}
int virtio_p9_jdriver::ioctl(unsigned long arg1, unsigned long arg2) {
	return 0;
}
/*************************************************************************************************/
static virtio_p9_jdriver *p9_jdriver;
static virtio_net_jdriver *net_jdriver;
static virtio_disk_jdriver *disk_jdriver;

extern "C" {

void init_virtio_p9_jdriver() {
	p9_jdriver = jnew_obj(virtio_p9_jdriver);
	p9_jdriver->name = (unsigned char *) "p9_driver";

	register_jdriver(p9_jdriver);
}

void init_virtio_net_jdriver() {
	net_jdriver = jnew_obj(virtio_net_jdriver);
	net_jdriver->name = (unsigned char *) "net_driver";

	register_jdriver(net_jdriver);
}
void init_virtio_disk_jdriver() {
	disk_jdriver = jnew_obj(virtio_disk_jdriver);
	disk_jdriver->name = (unsigned char *) "disk_driver";

	register_jdriver(disk_jdriver);
}

struct virtqueue *virtio_jdriver_getvq(void *driver, int index) {
	virtio_p9_jdriver *jdriver = (virtio_p9_jdriver *) driver;

	return jdriver->vq[index];
}
}
