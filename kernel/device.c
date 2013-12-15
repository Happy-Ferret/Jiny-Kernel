#define DEBUG_ENABLE 1
#include "device.h"
#include "interface.h"

device_class_t deviceClass_root = { "root",NULL, NULL, NULL,NULL,
	NULL, NULL, NULL};


int add_deviceClass(void *addr){
	device_class_t *devClass,*parent;

	devClass = addr;
    parent = devClass->parent;

    if (parent ==0 ) return 0;
    if (devClass != parent->children)
    	devClass->sibling = parent->children;
    else
    	devClass->sibling = 0;
    parent->children = devClass;
    return 1;
}

#define MAX_DEVICES 200
struct {
	pci_addr_t addr;
	pci_dev_header_t header;
}device_list[MAX_DEVICES];
static int device_count=0;

static void scan_devices(){
	int i,j,k;
	int ret;
#define MAX_BUS 32
#define MAX_PCI_DEV 32
#define MAX_PCI_FUNC 32

	device_count=0;
	for (i = 0; i < MAX_BUS ; i++) {
		for (j = 0; j < MAX_PCI_DEV; j++) {
	        for (k=0;  k< MAX_PCI_FUNC; k++){
	        	if (device_count >= (MAX_DEVICES-1)) return 0;

	        	device_list[device_count].addr.bus=i;
	        	device_list[device_count].addr.device=j;
	        	device_list[device_count].addr.function=k;
	        	device_list[device_count].header.vendor_id=0;
	        	device_list[device_count].header.device_id=0;

	        	ret = pci_generic_read(&device_list[device_count].addr, 0, sizeof(pci_dev_header_t), &device_list[device_count].header);
	        	if (ret != 0 || device_list[device_count].header.vendor_id==0xffff) continue;
	        	DEBUG("scan devices %d:%d:%d  %x:%x\n",i,j,k,device_list[device_count].header.vendor_id,device_list[device_count].header.device_id);
	        	device_count++;
	        }
		}
	}
}
int init_devClasses(unsigned long unused) {
	device_t *dev;
	device_class_t *devClass;
	int (*probe)(device_t *dev);
	int (*attach)(device_t *dev);
	int i,ret;

	/* get all devices */
	scan_devices();

	for (i = 0; i < device_count; i++) {
		dev = ut_malloc(sizeof(device_t));
		memset(dev, 0, sizeof(device_t));
		ut_memcpy(&dev->pci_addr, &device_list[i].addr, sizeof(pci_addr_t));
		ut_memcpy(&dev->pci_hdr, &device_list[i].header,sizeof(pci_dev_header_t));
		ret = 0;
		devClass = deviceClass_root.children;
		while (devClass != NULL && ret == 0) {
			probe = devClass->probe;
			attach = devClass->attach;
			if (probe != NULL && attach != NULL) {
				if (probe(dev) == 1) {
					ret=devClass->attach(dev);
#if 1
					dev->next=devClass->devices;
					devClass->devices = dev;
#endif
					break;
				}
			}
			devClass = devClass->sibling;
		}
		if (ret == 0) {
			ut_log("	Unable to attach the device to any driver: bus:dev;fun:%x:%x:%x devdor:device:id:%x-%x \n",dev->pci_addr.bus, dev->pci_addr.device, dev->pci_addr.function,dev->pci_hdr.vendor_id, dev->pci_hdr.device_id);
			ut_free(dev);
		}
	}
	return 0;
}
static int list_devClasses(device_class_t *parent, int level){
	int count=0;
    if (parent ==0) return 0;
	while (parent != NULL) {
	   device_t *dev;
       ut_printf("Level:%d  %s\n",level,parent->name);

       dev = parent->devices;
       while(dev != 0){
    	   ut_printf("    devices addr(bus:device:function) %d:%d:%d vend:device_id -> %x:%x\n",dev->pci_addr.bus,dev->pci_addr.device,dev->pci_addr.function,dev->pci_hdr.vendor_id,dev->pci_hdr.device_id);
    	   dev = dev->next;
       }
       level++;
       count = count+list_devClasses(parent->children,level);
       parent = parent->sibling;
       count = count+1;
	}
	return count;
}
int Jcmd_lspci(){
	int i;

	list_devClasses(&deviceClass_root,0);
	ut_printf(" Complete List of pci devices:\n");
	for (i=0; i<device_count; i++)
		ut_printf("devices addr(bus:device:function) %d:%d:%d vend:device_id -> %x:%x\n",device_list[i].addr.bus,device_list[i].addr.device,device_list[i].addr.function,device_list[i].header.vendor_id,device_list[i].header.device_id);
}
/***********************************************************************************************
 * Modules:
 ***********************************************************************************************/

module_t MODULE_root = { "root",NULL, NULL, NULL,0,
	NULL, NULL, NULL};


int add_module(void *addr){
	module_t *module,*parent;

	module = addr;
    parent = module->parent;

    if (parent == 0 ) return 0;
    module->sibling = parent->children;
    parent->children = module;
    return 1;
}

int init_modules(unsigned long unused) {
	module_t *module;

	module = MODULE_root.children;
	while (module != NULL ) {
		module->load();
		module = module->sibling;
	}
	return 0;
}
static int list_modules(module_t *parent, int level){
	int count=0;
    if (parent ==0) return 0;
	while (parent != NULL) {
       ut_printf("Module level:%d  %s\n",level,parent->name);
       count = count+list_modules(parent->children,level++);
       parent = parent->sibling;
       count = count+1;
	}
	return count;
}
int Jcmd_module_stat(){
	list_modules(MODULE_root.children,0);
}
