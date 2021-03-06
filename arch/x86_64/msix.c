/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *   x86_64/msix.c
 *   Author: Naredula Janardhana Reddy  (naredula.jana@gmail.com, naredula.jana@yahoo.com)
 *
 */
#define DEBUG_ENABLE 1
#include "pci.h"
#include "mm.h"
#include "task.h"

/*
 * The address register = 0xfee0100c goes like this:
Bits 31 to 20 = 0xFEE = address is in the local APIC area
Bits 19 to 12 = Destination APIC ID for the IRQ = 1
Bit 3 = RH = Redirection Hint = 1
Bit 2 = DM = Destination Mode (same meaning as for IPIs) = 1
Bits 0 to 1 = Don't care
 */
#define LAPIC_BASE    0xfee00000
static void update_msix_table(struct pcicfg_msix *msix, int mask) {
	unsigned long address;
	int i;

	for (i = 0; i < msix->msix_msgnum; i++) {
		address = LAPIC_BASE;

//	address = address | 0x1008; /* Working apic-id=1 as destination, to send to the second cpu, */
	address = address | 0x008; /* Working apic-id=0 as destination, to send to the first cpu, */
//		address = address | 0xf00c; /* using logic mode for apic id's Working apic-id=1 as destination, to send to the second cpu, */
		msix->msix_table[i].upper_add = address >> 32;
		msix->msix_table[i].lower_addr = address & 0xffffffff;
		msix->msix_table[i].data =( msix->isr_vector + i) | 0x100 ;  /* lowest priority mode=0x100 */
		msix->msix_table[i].control = mask;
		ut_log(" %d: MSIX  data :%x address:%x \n",i,msix->msix_table[i].data,address);
	}
	return;

}
#define MSI_VECTORS_START 101
static int msi_start_vector = MSI_VECTORS_START;
int pci_read_msi_withoutbars(pci_addr_t *addr, pci_dev_header_t *pci_hdr, struct pcicfg_msix *msix,unsigned long msix_table_res ) {
	uint32_t ret;
	uint16_t buf;
	uint32_t val;
	uint32_t bar_offset;

	uint8_t pos=pci_hdr->capabilities_pointer;

	ret = pci_read(addr, pos, 2, &buf);
	ret = pci_read(addr, pos + PCIR_MSIX_CTRL, 2, &msix->msix_ctrl);

	msix->msix_msgnum = (msix->msix_ctrl & PCIM_MSIXCTRL_TABLE_SIZE) + 1;
	if (msix->msix_msgnum > 0) {
		msix->isr_vector = msi_start_vector;
		msi_start_vector = msi_start_vector + msix->msix_msgnum;
		ut_log(" msi vector start :%d num:%d msiztableres: %x\n",msix->isr_vector,msix->msix_msgnum,msix_table_res);
	} else {
		msix->isr_vector = 0;
	}
	//ret = pci_read(addr, pos + PCIR_MSIX_TABLE, 4, &bar_offset);
	//msix->msix_table_bar = PCIR_BAR(bar_offset & PCIM_MSIX_BIR_MASK);
	//if (bar_offset >= bars_total) return 0;
	msix->msix_table_res =msix_table_res;

	msix->msix_table  = vm_create_kmap("msix",0x1000,PROT_WRITE,MAP_FIXED,msix->msix_table_res);
	//ut_log(" msix table :%x  bar addr:%x  baroffset:%d \n",msix->msix_table,bars[bar_offset].addr,bar_offset);

	ret = pci_read(addr, pos + PCIR_MSIX_PBA, 4, &val);
	msix->msix_pba_bar = PCIR_BAR(val & PCIM_MSIX_BIR_MASK);
	return msix->isr_vector;
}

int pci_read_msi(pci_addr_t *addr, pci_dev_header_t *pci_hdr,pci_bar_t *bars, uint32_t bars_total,  struct pcicfg_msix *msix) {
	uint32_t ret;
	uint16_t buf;
	uint32_t val;
	uint32_t bar_offset;

	uint8_t pos=pci_hdr->capabilities_pointer;

	ret = pci_read(addr, pos, 2, &buf);
	ret = pci_read(addr, pos + PCIR_MSIX_CTRL, 2, &msix->msix_ctrl);

	msix->msix_msgnum = (msix->msix_ctrl & PCIM_MSIXCTRL_TABLE_SIZE) + 1;
	if (msix->msix_msgnum > 0) {
		msix->isr_vector = msi_start_vector;
		msi_start_vector = msi_start_vector + msix->msix_msgnum;
		ut_log(" msi vector start :%d num:%d \n",msix->isr_vector,msix->msix_msgnum);
	} else {
		msix->isr_vector = 0;
	}
	ret = pci_read(addr, pos + PCIR_MSIX_TABLE, 4, &bar_offset);
	msix->msix_table_bar = PCIR_BAR(bar_offset & PCIM_MSIX_BIR_MASK);
	if (bar_offset >= bars_total) return 0;
	msix->msix_table_res = bars[bar_offset].addr;

	msix->msix_table  = vm_create_kmap("msix",0x1000,PROT_WRITE,MAP_FIXED,msix->msix_table_res);
	ut_log(" msix table :%x  bar addr:%x  baroffset:%d msix tableres:%x\n",msix->msix_table,bars[bar_offset].addr,bar_offset,msix->msix_table_res);

	ret = pci_read(addr, pos + PCIR_MSIX_PBA, 4, &val);
	msix->msix_pba_bar = PCIR_BAR(val & PCIM_MSIX_BIR_MASK);
	return msix->isr_vector;
}

int pci_enable_msix(pci_addr_t *addr ,struct pcicfg_msix *msix,uint8_t capabilities_pointer){
	update_msix_table(msix, 0);
	msix->msix_ctrl = msix->msix_ctrl | 0x8000; // enable msix
	pci_write(addr, capabilities_pointer + PCIR_MSIX_CTRL, 2, &msix->msix_ctrl);

	ut_log("MSIX... Configured ISR vector:%d  numvector:%d ctrl:%x\n", msix->isr_vector, msix->msix_msgnum, msix->msix_ctrl);
	return 1;
}


