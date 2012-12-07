#include "common.h"
#include "task.h"
#include "descriptor_tables.h"
#include "mach_dep.h"

static void init_fs_and_gs(int cpuid)
{

	/*msr_write(MSR_GS_BASE, 0); */
	msr_write(MSR_GS_BASE, (uint64_t)&g_cpu_state[cpuid]);
	msr_write(MSR_KERN_GS_BASE,(uint64_t)&g_cpu_state[cpuid]);
	msr_write(MSR_FS_BASE, 0);
	__asm__ volatile ("swapgs");
}

static inline void bit_set(void *bitmap, int bit)
{
	*(unsigned long *)bitmap |= (1 << bit);
}

static inline void efer_set_feature(int ftr_bit)
{
	volatile uint64_t efer = (volatile uint64_t)msr_read(MSR_EFER);

	bit_set((void *)&efer, ftr_bit);
	msr_write(MSR_EFER, efer);
}
extern void syscall_entry(void);
void init_syscall(int cpuid)
{
	efer_set_feature(EFER_SCE);
	msr_write(MSR_STAR,
			((uint64_t)(GDT_SEL(KCODE_DESCR) | SEG_DPL_KERNEL) << 32) |
			((uint64_t)(GDT_SEL(KDATA_DESCR) | SEG_DPL_USER) << 48));
	msr_write(MSR_LSTAR, (uint64_t)syscall_entry);
	msr_write(MSR_SF_MASK, 0x200);
	init_fs_and_gs(cpuid);

}
