/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
*   jdevice.cc
*   Naredula Janardhana Reddy  (naredula.jana@gmail.com, naredula.jana@yahoo.com)
*
*/
#include "jdevice.h"

#define MAX_DEVICES 200
static class jdevice *jdevice_list[MAX_DEVICES];
static int device_count = 0;
#define MAX_DRIVERS 100
class jdriver *jdriver_list[MAX_DRIVERS];
static int driver_count = 0;
typedef struct {
	unsigned char *description;
	  uint16_t vendor_id;
	  uint16_t device_id;
}pci_device_vendor_t ;

pci_device_vendor_t pci_device_vendor_list []={
		{"PIIX3 IDE Interface (Triton II)",0x8086,0x7010},
		{"PIIX3 PCI-to-ISA Bridge (Triton II)", 0x8086,0x7000},
		{"Intel Pro 1000/MT",0x8086,0x100e},
		{"PIIX4/4E/4M Power Management Controller",0x8086,0x7113},
		{"PCI & Memory", 0x8086, 0x1237},
		{"VGA controller",0x1013,0xb8},
		{"Intel(R) 82371AB/EB PCI Bus Master IDE Controller",0x8086,0x7111},
		{"virtio disk device",VIRTIO_PCI_VENDOR_ID,VIRTIO_PCI_BLOCK_DEVICE_ID},
		{"virtio scsi device",VIRTIO_PCI_VENDOR_ID,VIRTIO_PCI_SCSI_DEVICE_ID},
		{"virtio console device",VIRTIO_PCI_VENDOR_ID,VIRTIO_PCI_CONSOLE_DEVICE_ID},
		{"virtio net device",VIRTIO_PCI_VENDOR_ID,VIRTIO_PCI_NET_DEVICE_ID},
		{"virtio p9 device",VIRTIO_PCI_VENDOR_ID,VIRTIO_PCI_9P_DEVICE_ID},
		{"virtio ballon device",VIRTIO_PCI_VENDOR_ID,VIRTIO_PCI_BALLOON_DEVICE_ID},
		{0,0,0}
};
jdevice::jdevice(unsigned char *arg_name, int arg_type){
	ut_snprintf(name ,MAX_DEVICE_NAME,arg_name);
	file_type = arg_type;
}
int jdevice::init_pci(uint8_t bus, uint8_t device, uint8_t function) {
	int i;

	pci_device.pci_addr.bus = bus;
	pci_device.pci_addr.device = device;
	pci_device.pci_addr.function = function;
	pci_dev_header_t *pci_hdr;
	pci_bar_t *bars;
	int ret,len;
	driver = 0;

	ret = pci_generic_read(&this->pci_device.pci_addr, 0, sizeof(pci_dev_header_t), &this->pci_device.pci_header);
	if (ret != 0 || this->pci_device.pci_header.vendor_id == 0xffff)
		return JFAIL;
	pci_device.description = 0;
	for (i=0; pci_device_vendor_list[i].description!=0; i++){
		if (pci_device_vendor_list[i].device_id==pci_device.pci_header.device_id  && pci_device_vendor_list[i].vendor_id==pci_device.pci_header.vendor_id){
			pci_device.description = pci_device_vendor_list[i].description;
			break;
		}
	}
	INIT_LOG("			--------------------------\n		scan devices %d:%d:%d  %x:%x\n", bus, device, function, this->pci_device.pci_header.vendor_id,
			this->pci_device.pci_header.device_id);

	INIT_LOG("		reading pci info : bus:dev:fuc : %x:%x:%x \n", pci_device.pci_addr.bus, pci_device.pci_addr.device,
			pci_device.pci_addr.function);
	if (read_pci_info_new(&pci_device) != JSUCCESS) {
		INIT_LOG("		ERROR: reading pci device failed\n");
		return JFAIL;
	}

	pci_hdr = &pci_device.pci_header;
	bars = &pci_device.pci_bars[0];
	len = pci_device.pci_bar_count;
	INIT_LOG("		succeded reading pci info : bus:dev:fuc : %x:%x:%x barcount:%d\n",pci_device.pci_addr.bus,pci_device.pci_addr.device,pci_device.pci_addr.function,len);

	//if (bars[0].addr != 0) {
	if (len != 0){
		pci_device.pci_ioaddr = bars[0].addr - 1;
		pci_device.pci_iolen = bars[0].len;
		pci_device.pci_mmio = bars[1].addr;
		pci_device.pci_mmiolen = bars[1].len;
	} else {
		INIT_LOG("		ERROR in initializing PCI driver %x : %x \n", bars[0].addr,bars[1].addr);
		//return JFAIL;
	}
	return JSUCCESS;
}
void jdevice::print_stats(unsigned char *arg1,unsigned char *arg2) {
	int i;
	int all=0;

	if (arg1!=0 && ut_strcmp(arg1,"all")==0){
		all = 1;
	}

	ut_printf("name: %s: ", name);

     if (1){
		unsigned char *drv_name="-NA-";
		if (driver != 0){
			drv_name=driver->name;
		}
		ut_printf("pci: %x:%x:%x ven/dev: %2x:%2x :%s: int:%x driver:%s\n", pci_device.pci_addr.bus, pci_device.pci_addr.device,
				pci_device.pci_addr.function,pci_device.pci_header.vendor_id,pci_device.pci_header.device_id,pci_device.description,pci_device.pci_header.interrupt_line, drv_name);
		if (all==0){
			return;
		}
		for (i=0; pci_device.pci_bars[i].addr!=0 && i<MAX_PCI_BARS; i++)
			ut_printf("   addr :%x (%d) \n",pci_device.pci_bars[i].addr,pci_device.pci_bars[i].len);
	}
	if (driver != 0) {
		driver->print_stats(arg1,arg2);
	}
	ut_printf("\n");
}
int jdevice::read(unsigned long unused, unsigned char *buf, int len, int flags){
	if (driver != 0){
		return driver->read(buf,len, flags);
	}
	return -1;
}
int jdevice::write(unsigned long unused, unsigned char *buf, int len, int flags){
	if (driver != 0){
		return driver->write(buf,len, flags);
	}
	return -1;
}
int jdevice::ioctl(unsigned long arg1,unsigned long arg2){
	if (driver != 0){
		return driver->ioctl(arg1,arg2);
	}
	return -1;
}
int jdevice::close(){
	if (driver != 0){
		/* TODO : ioctl function need to implement in driver */
		return -1;
	}
	return -1;
}

void register_jdriver(class jdriver *driver) {

	jdriver_list[driver_count] = driver;
	driver_count++;
}

int g_conf_obj_count=1;
/*
calling new :  new (arg1,arg2..) type
 */
void *operator new(int sz,const char *name) {
    void *obj = ut_calloc(sz);

    if(g_conf_obj_count == 1){
    	unsigned long tmp_p = obj;
    	tmp_p=tmp_p+8;
    	class jobject *jobj = tmp_p;
    	jobj->jobject_id = ut_count_obj_add(name);
    }
    return obj;
}
void jfree_obj(unsigned long addr){

    if(g_conf_obj_count == 1){
    	unsigned long tmp_p = addr;

    	tmp_p=tmp_p+8;
    	class jobject *obj=tmp_p;
    	ut_count_obj_free(obj->jobject_id);
    }
	ut_free(addr);
}

/*********************************************************************************/
extern "C" {

static int scan_pci_devices() {
	int i, j, k, d;
	int ret;
#define MAX_BUS 32
#define MAX_PCI_DEV 32
#define MAX_PCI_FUNC 32

	for (i = 0; i < MAX_BUS && i < 2; i++) {
		for (j = 0; j < MAX_PCI_DEV; j++) {
			for (k = 0; k < MAX_PCI_FUNC; k++) {
				if (device_count >= (MAX_DEVICES - 1))
					return JSUCCESS;

				jdevice_list[device_count] = jnew_obj(jdevice,"pci", 0) ;

				if (jdevice_list[device_count]->init_pci(i, j, k) == JFAIL) {
					jfree_obj(jdevice_list[device_count]);
					continue;
				}

				/* attach the device to the know driver */
				for (d = 0; d < driver_count; d++) {
					if (jdriver_list[d]->probe_device(jdevice_list[device_count]) == JSUCCESS) {
						jdevice_list[device_count]->driver = jdriver_list[d]->attach_device(jdevice_list[device_count]);
						jdriver_list[d]->instances++;
						break;
					}
				}
				device_count++;
			}
		}
	}
	return JSUCCESS;
}
extern void init_virtio_p9_jdriver();
extern void init_virtio_net_jdriver();
extern void init_virtio_disk_jdriver();
extern void init_keyboard_jdriver();
extern void init_serial_jdriver();
static struct jdevice *keyboard_device,*serial1_device,*serial2_device;
static struct jdevice *vga_device;

jdriver *disk_drivers[MAX_DISK_DEVICES];
int init_jdevices(unsigned long unused_arg1) {
	device_count = 0;
	int d,k,i;

	for (i=0; i<MAX_DISK_DEVICES; i++){
		disk_drivers[i]=0;
	}
	init_virtio_p9_jdriver();
	init_virtio_net_jdriver();
	init_virtio_disk_jdriver();
	init_keyboard_jdriver();
	init_serial_jdriver();

	keyboard_device = jnew_obj(jdevice, "/dev/keyboard",IN_FILE);
	vga_device = jnew_obj(jdevice,"/dev/vga",OUT_FILE);
	serial1_device = jnew_obj(jdevice,"/dev/serial1",IN_FILE | OUT_FILE);
	serial2_device = jnew_obj(jdevice,"/dev/serial2",IN_FILE | OUT_FILE);

	jdevice_list[0] = keyboard_device;
	jdevice_list[1] = vga_device;
	jdevice_list[2] = serial1_device;
	jdevice_list[3] = serial2_device;

	device_count=4;

	/* attach the device to the know driver */
	for (k = 0; k < device_count; k++) {
		for (d = 0; d < driver_count; d++) {
			if (jdriver_list[d]->probe_device(jdevice_list[k]) == JSUCCESS) {
				jdevice_list[k]->driver = jdriver_list[d]->attach_device(
						jdevice_list[k]);
				break;
			}
		}
	}

	scan_pci_devices();
	INIT_LOG("		-------------\n");

	return JSUCCESS;
}
void *get_keyboard_device(int device_type,int file_type){
	if (device_type == DEVICE_KEYBOARD){
		if (file_type == IN_FILE)
			return (void *)keyboard_device;
		else
			return (void *)vga_device;
	}else{
		if (file_type == IN_FILE || file_type == OUT_FILE)
			return (void *)serial1_device;
	}
}
void Jcmd_jdevices(unsigned char *arg1,unsigned char *arg2) {
	int i;

	ut_printf("---------------\Device List: count:%d\n",device_count);
	for (i = 0; i < device_count; i++) {
		jdevice_list[i]->print_stats(arg1,arg2);
	}
	ut_printf("---------------\nDriver List: count:%d\n",driver_count);
	for (i = 0; i < driver_count; i++) {
		ut_printf("%s(%d) : \n",jdriver_list[i]->name,jdriver_list[i]->instances);
	}
}
int test_i=0;
void Jcmd_read() {
	unsigned char buf[800];
	int ret;
	if (disk_drivers[0] != 0) {
		ret = disk_drivers[0]->read(buf, 50, test_i);
		test_i = test_i + 50;
		buf[50] = 0;
		ut_printf(" Read from disk ret:%d: :%s: \n", ret, buf);
	}
}
extern "C" void __cxa_pure_virtual() { while (1); }  /* TODO : to avoid compilation error */
}
