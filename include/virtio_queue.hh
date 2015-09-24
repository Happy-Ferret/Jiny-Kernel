/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
*   include/virtio_queue.hh
*   Naredula Janardhana Reddy  (naredula.jana@gmail.com, naredula.jana@yahoo.com)
*
*/
#ifndef __JVIRTIOQUEUE_H__
#define __JVIRTIOQUEUE_H__
extern "C" {
#include "common.h"
#include "pci.h"
#include "interface.h"
#include "mach_dep.h"
}



/* Status byte for guest to report progress, and synchronize features. */
/* We have seen device and processed generic fields (VIRTIO_CONFIG_F_VIRTIO) */
#define VIRTIO_CONFIG_S_ACKNOWLEDGE 1
/* We have found a driver for the device. */
#define VIRTIO_CONFIG_S_DRIVER      2
/* Driver has used its parts of the config, and is happy */
#define VIRTIO_CONFIG_S_DRIVER_OK   4
/* We've given up on this device. */
#define VIRTIO_CONFIG_S_FAILED      0x80
/* Some virtio feature bits (currently bits 28 through 31) are reserved for the
 * transport being used (eg. virtio_ring), the rest are per-device feature
 * bits. */
#define VIRTIO_TRANSPORT_F_START    28
#define VIRTIO_TRANSPORT_F_END      32

/* Do we get callbacks when the ring is completely used, even if we've
 * suppressed them? */
#define VIRTIO_F_NOTIFY_ON_EMPTY    24
/* A 32-bit r/o bitmask of the features supported by the host */
#define VIRTIO_PCI_HOST_FEATURES	0
/* A 32-bit r/w bitmask of features activated by the guest */
#define VIRTIO_PCI_GUEST_FEATURES	4
/* A 32-bit r/w PFN for the currently selected queue */
#define VIRTIO_PCI_QUEUE_PFN		8
/* A 16-bit r/o queue size for the currently selected queue */
#define VIRTIO_PCI_QUEUE_NUM		12
/* A 16-bit r/w queue selector */
#define VIRTIO_PCI_QUEUE_SEL		14
/* A 16-bit r/w queue notifier */
#define VIRTIO_PCI_QUEUE_NOTIFY		16
/* An 8-bit device status register.  */
#define VIRTIO_PCI_STATUS		18

/* An 8-bit r/o interrupt status register.  Reading the value will return the
 * current contents of the ISR and will also clear it.  This is effectively
 * a read-and-acknowledge. */
#define VIRTIO_PCI_ISR			19
/* The bit of the ISR which indicates a device configuration change. */
#define VIRTIO_PCI_ISR_CONFIG		0x2

/* MSI-X registers: only enabled if MSI-X is enabled. */
/* A 16-bit vector for configuration changes. */
#define VIRTIO_MSI_CONFIG_VECTOR        20
/* A 16-bit vector for selected queue notifications. */
#define VIRTIO_MSI_QUEUE_VECTOR         22
/* Vector value used to disable MSI for queue */
#define VIRTIO_MSI_NO_VECTOR            0xffff

/* The remaining space is defined by each driver as the per-driver
 * configuration space */
#define VIRTIO_PCI_CONFIG(dev)		((dev)->msix_enabled ? 24 : 20)

/* Virtio ABI version, this must match exactly */
#define VIRTIO_PCI_ABI_VERSION		0

/* How many bits to shift physical queue address written to QUEUE_PFN.
 * 12 is historical, and due to x86 page size. */
#define VIRTIO_PCI_QUEUE_ADDR_SHIFT	12

/* The alignment to use between consumer and producer parts of vring.
 * x86 pagesize again. */
#define VIRTIO_PCI_VRING_ALIGN		4096




/* This marks a buffer as continuing via the next field. */
#define VRING_DESC_F_NEXT	1
/* This marks a buffer as write-only (otherwise read-only). */
#define VRING_DESC_F_WRITE	2
/* This means the buffer contains a list of buffer descriptors. */
#define VRING_DESC_F_INDIRECT	4
/* The Host uses this in used->flags to advise the Guest: don't kick me when
 * you add a buffer.  It's unreliable, so it's simply an optimization.  Guest
 * will still kick if it's out of buffers. */
#define VRING_USED_F_NO_NOTIFY	1
/* The Guest uses this in avail->flags to advise the Host: don't interrupt me
 * when you consume a buffer.  It's unreliable, so it's simply an
 * optimization.  */
#define VRING_AVAIL_F_NO_INTERRUPT	1
/* We support indirect buffer descriptors */
#define VIRTIO_RING_F_INDIRECT_DESC	28
/* The Guest publishes the used index for which it expects an interrupt
 * at the end of the avail ring. Host should ignore the avail->flags field. */
/* The Host publishes the avail index for which it expects a kick
 * at the end of the used ring. Guest should ignore the used->flags field. */
#define VIRTIO_RING_F_EVENT_IDX		29

#define vring_used_event(vr) ((vr)->avail->ring[(vr)->num])
#define vring_avail_event(vr) (*(__u16 *)&(vr)->used->ring[(vr)->num])

#if 0
struct virtio_net_hdr {
	uint8_t flags;
	uint8_t gso_type;
	uint16_t hdr_len;		/* Ethernet + IP + tcp/udp hdrs */
	uint16_t gso_size;		/* Bytes to append to hdr_len per frame */
	uint16_t csum_start;	/* Position to start checksumming from */
	uint16_t csum_offset;	/* Offset after that to place checksum */
};
#endif

/* Virtio ring descriptors: 16 bytes.  These can chain together via "next". */
struct mem_vring_desc {
	/* Address (guest-physical). */
	uint64_t addr;
	/* Length. */
	uint32_t len;
	/* The flags as indicated above. */
	uint16_t flags;
	/* We chain unused descriptors via this, too */
	uint16_t next;
};

struct mem_vring_avail {
	uint16_t flags;
	uint16_t idx;
	uint16_t ring[];
};

/* u32 is used here for ids for padding reasons. */
struct mem_vring_used_elem {
	/* Index of start of used descriptor chain. */
	uint32_t id;
	/* Total length of the descriptor chain which was used (written to) */
	uint32_t len;
};

struct mem_vring_used {
	uint16_t flags;
	uint16_t idx;
	struct mem_vring_used_elem ring[];
};

struct mem_vring {
	unsigned int num;
	struct mem_vring_desc *desc;
	struct mem_vring_avail *avail;
	struct mem_vring_used *used;
};

struct vring_queue{
	const char *name;
	int queue_number;
	unsigned long pci_ioaddr;
	/* Actual memory layout for this queue */
	struct mem_vring vring;
	/* Other side has made a mess, don't try any more. */
	bool broken;
	/* Host supports indirect buffers */
	bool indirect;
	/* Host publishes avail event idx */
	bool event;

	/* Number of free buffers */
	unsigned int num_free;
	/* Head of free buffer list. */
	unsigned int free_head;
	unsigned int free_tail;

	/* Number we've added since last sync. */
	unsigned int num_added;
	/* Last used index we've seen. */
	uint16_t last_used_idx;
	/* They're supposed to lock for us. */
	unsigned long in_use;

	unsigned int stat_alloc,stat_free;
};

class virtio_queue: public jobject {
	struct vring_queue *queue;

	bool virtqueue_enable_cb_delayed();
	void sync_avial_idx(struct vring_queue *vq);
	void detach_buf( unsigned int head);
	void notify();
	void init_virtqueue(unsigned int num,  unsigned int vring_align,  unsigned long pci_ioaddr, void *pages,
					      void (*callback)(struct virtio_queue *), const char *name, int queue_number);

	unsigned long stat_add_success,stat_add_fails,stat_add_pkts;
	unsigned long stat_rem_success,stat_rem_fails,stat_rem_pkts;
public:
	int qType;

	void print_stats(unsigned char *arg1,unsigned char *arg2);
	int check_recv_pkt();
	int virtio_disable_cb();
	bool virtio_enable_cb();
	int virtio_queuekick();
	virtio_queue(jdevice *device, uint16_t index, int type);
	int BulkAddToNetqueue(  struct struct_mbuf *mbuf_list, int list_len, int is_send);
	int BulkRemoveFromNetQueue( struct struct_mbuf *mbuf_list, int list_len);
};

#endif
