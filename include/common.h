#ifndef __COMMON_H__
#define __COMMON_H__

#define VIRTIO 1
#include "types.h"
#include "atomic.h"
#include "list.h"
#include "isr.h"
#include "descriptor_tables.h"
#include "multiboot.h"
#include "task.h"
#include "spinlock.h"
//#include "ipc.h"

#ifndef NULL
#define NULL ((void *) 0)
#endif
#define POW2(n) (1 << (n))

/* return to system calls */
#define SYSCALL_SUCCESS 0
#define SYSCALL_FAIL -1

/* return from different subsystems */
#define JSUCCESS 1
#define JFAIL 0

extern int g_boot_completed;
extern int g_conf_syscall_debug;
extern int g_conf_debug_level;
extern spinlock_t g_userspace_stdio_lock;
extern spinlock_t g_global_lock;
//#define SYSCALL_DEBUG(x...) if (g_conf_syscall_debug==1)	{ }
#if 1
#define SYSCALL_DEBUG(x...) do { \
	if (g_conf_syscall_debug==1)	{\
		unsigned long sys_debug_flags; \
		spin_lock_irqsave(&g_userspace_stdio_lock, sys_debug_flags); \
		ut_printf("SYSCALL(%x :%d uip: %x :%d jiff:%d) ",g_current_task->task_id,getcpuid(),g_cpu_state[getcpuid()].md_state.user_ip,g_current_task->stats.syscall_count,g_jiffies); ut_printf(x); \
		spin_unlock_irqrestore(&g_userspace_stdio_lock, sys_debug_flags); \
	}\
} while (0) 
#endif
#define assert(x) do {  if (!(x)) { BUG() ;} } while(0)

 //#define DEBUG_ENABLE 1
#ifdef DEBUG_ENABLE 
#define DEBUG(x...) do { \
	if ( g_conf_debug_level==1)	ut_printf(x); \
} while (0) 
#else
#define DEBUG(x...) do { \
} while (0) 
#endif

extern int g_init_loglevel;
#define INIT_LOG(x...) if (g_init_loglevel > 0){ ut_log(x); }
extern int g_conf_stat_counters_enable;
#define STAT_INC(x) if (g_conf_stat_counters_enable==1){ x=x+1; }


extern int brk_pnt;
#define BRK while(brk_pnt==0)

#include "symbol_table.h"


typedef struct registers
{
	addr_t ds;                  // Data segment selector
	addr_t edi, esi, ebp, esp, ebx, edx, ecx, eax; // Pushed by pusha.
	addr_t int_no, err_code;    // Interrupt number and error code (if applicable)
	addr_t eip, cs, eflags, useresp, ss; // Pushed by the processor automatically.
} registers_t;


typedef struct {
	unsigned long tv_sec;
	unsigned long tv_usec;
}time_t;


#define unlikely(x)     (x) /* TODO */
#define BUG() do {  \
	cli(); while(1) ; ut_log(" BUG at :\n"); ut_getBackTrace(0,0,0);while(1) ; } while(0)
#define BUG_ON(condition) do { if (unlikely((condition)!=0)) BUG(); } while(0)



// Enables registration of callbacks for interrupts or IRQs.
// For IRQs, to ease confusion, use the #defines above as the
// first parameter.
typedef int (*isr_t)(void *arg);
extern int g_serial_output;
extern void *g_inode_lock; /* vfs */
extern void *g_netBH_lock; /* netBH */
#define printk ut_printf
unsigned char kb_getchar();
void register_interrupt_handler(uint8_t n, isr_t handler);
int serial_write( char *buf , int len);
void *kmalloc (long size, int flags);
void kfree (const void *objp);
extern addr_t g_jiffies;

//#define MAX_DMESG_LOG 1200000
#define MAX_DMESG_LOG 1000000
#include "interface.h"

#endif
