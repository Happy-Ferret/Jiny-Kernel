/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *   kernel/sched_task.c
 *   Author: Naredula Janardhana Reddy  (naredula.jana@gmail.com, naredula.jana@yahoo.com)
 *
 */
//#define DEBUG_ENABLE 1
#if 1
#include "arch.hh"
extern "C" {
#include "common.h"
#include "descriptor_tables.h"

unsigned char g_idle_stack[MAX_CPUS + 2][TASK_SIZE] __attribute__ ((aligned (4096)));
struct mm_struct *g_kernel_mm = 0;
task_queue_t g_task_queue;
spinlock_t g_global_lock = SPIN_LOCK_UNLOCKED((unsigned char *)"global");
unsigned long g_jiffies = 0; /* increments for every 10ms =100HZ = 100 cycles per second  */
unsigned long g_jiffie_errors = 0;
unsigned long g_jiffie_tick = 0;

static int curr_cpu_assigned = 0;
//static struct fs_struct *fs_kernel;
static unsigned long free_pid_no = 1; // TODO need to make unique when  wrap around
static wait_queue_t timer_queue;
static int g_conf_cpu_stats = 1;
static int g_conf_dynamic_assign_cpu = 0; /* minimizes the IPI interrupt by reassigning the the task to running cpu */
static int stat_dynamic_assign_errors = 1;

extern kmem_cache_t *g_slab_filep;
extern int ipi_pagetable_interrupt(void *arg);
extern void *g_print_lock;

static int free_mm(struct mm_struct *mm);
static unsigned long push_to_userland();
unsigned long _schedule(unsigned long flags);
static void schedule_kernelSecondHalf();
static void schedule_userSecondHalf();
static int release_resources(struct task_struct *task, int attach_to_parent);

static struct task_struct *alloc_task_struct(void) {
	task_struct *p;
	p = (struct task_struct *) mm_getFreePages(0, 2); /*WARNING: do not change the size it is related TASK_SIZE, 4*4k=16k page size */
	if (p == 0) {
		BUG();
	}
	ut_memset((unsigned char *) p, MAGIC_CHAR, TASK_SIZE);
	return p;
}

static void free_task_struct(struct task_struct *p) {
	mm_putFreePages((unsigned long) p, 2);
	return;
}
/************************************************
 All the below function should be called with holding lock
 ************************************************/
static inline int _add_to_runqueue(struct task_struct * p, int arg_cpuid) /* Add at the first */
{
	int cpuid;
	if (arg_cpuid == -1) {
		cpuid = p->allocated_cpu;
		if (g_cpu_state[cpuid].active == 0){
			p->allocated_cpu =0;
			cpuid=0;
		}
	} else {
		cpuid = arg_cpuid;
	}

	if (p->magic_numbers[0] != MAGIC_LONG || p->magic_numbers[1] != MAGIC_LONG || (p->run_queue.next != 0)) /* safety check */
	{
		ut_printf(" Task Stack Got CORRUPTED task:%x :%x :%x \n", p, p->magic_numbers[0], p->magic_numbers[1]);
		BUG();
	}
	if (p->current_cpu == 0xffff) { /* Avoid adding self adding in to runqueue */
		list_add_tail(&p->run_queue, &g_cpu_state[cpuid].run_queue.head);
		g_cpu_state[cpuid].run_queue_length++;
		p->state = TASK_RUNNING;
		return (g_cpu_state[cpuid].run_queue_length - 1);
	} else {
		BUG();
	}

	return 0;
}

static inline struct task_struct *_del_from_runqueue(struct task_struct *p, int cpuid) {
	if (p == 0) {
		struct list_head *node;

		node = g_cpu_state[cpuid].run_queue.head.next;
		if (g_cpu_state[cpuid].run_queue.head.next == &(g_cpu_state[cpuid].run_queue.head)) {
			return 0;
		}

		p = list_entry(node,struct task_struct, run_queue);
		if (p == 0)
			BUG();
	}
	list_del(&p->run_queue);
	g_cpu_state[cpuid].run_queue_length--;
	return p;
}

/* return 1 if it assigned to running cpu */
int _sc_task_assign_to_cpu(struct task_struct *task) {
	int i;
	int min_length = 99999;
	int min_cpuid = -1;

	if (g_conf_dynamic_assign_cpu == 1) {
		for (i = 0; i < getmaxcpus(); i++) {
			if (g_cpu_state[i].current_task != g_cpu_state[i].idle_task && g_cpu_state[i].run_queue_length < min_length) {
				min_length = g_cpu_state[i].run_queue_length;
				min_cpuid = i;
			}
		}
		if (min_cpuid == -1) {
			min_cpuid = getcpuid();
		}
	}
	_add_to_runqueue(task, min_cpuid);
	if (min_cpuid == -1) {
		return 0;
	} else {
		return 1;
	}
}

int sc_sleep(long ticks) /* each tick is 100HZ or 10ms */
/* TODO : return number ticks elapsed  instead of 1*/
/* TODO : when multiple user level thread sleep it is not returning in correct time , it may be because the idle thread halting*/
{
	return ipc_waiton_waitqueue(&timer_queue, ticks);
}
static backtrace_t temp_bt;

#define DEFAULT_RFLAGS_VALUE (0x10202)
#define GLIBC_PASSING 1
extern void enter_userspace();
static unsigned long push_to_userland() {
	struct user_regs *p;
	int cpuid = getcpuid();
	local_apic_send_eoi(); // Re-enable the APIC interrupts

	DEBUG(" from PUSH113_TO_USERLAND :%d\n", cpuid);
	/* From here onwards DO NOT  call any function that consumes stack */
	asm("cli");
	asm("movq %%rsp,%0" : "=m" (p));
	p = p - 1;
	asm("subq $0xa0,%rsp");
	p->gpres.rbp = 0;
#ifndef GLIBC_PASSING
	p->gpres.rsi=g_current_task->thread.userland.sp; /* second argument to main i.e argv */
	p->gpres.rdi=g_current_task->thread.userland.argc; /* first argument argc */
#else
	p->gpres.rsi = 0;
	p->gpres.rdi = 0;
#endif
	p->gpres.rdx = 0;
	p->gpres.rbx = p->gpres.rcx = 0;
	p->gpres.rax = p->gpres.r10 = 0;
	p->gpres.r11 = p->gpres.r12 = 0;
	p->gpres.r13 = p->gpres.r14 = 0;
	p->gpres.r15 = p->gpres.r9 = 0;
	p->gpres.r8 = 0;
	p->isf.rip = g_current_task->thread.userland.ip;
	p->isf.rflags = DEFAULT_RFLAGS_VALUE;
	p->isf.rsp = g_current_task->thread.userland.sp;
	p->isf.cs = GDT_SEL(UCODE_DESCR) | SEG_DPL_USER;
	p->isf.ss = GDT_SEL(UDATA_DESCR) | SEG_DPL_USER;

	g_cpu_state[cpuid].md_state.user_fs = 0;
	g_cpu_state[cpuid].md_state.user_gs = 0;
	g_cpu_state[cpuid].md_state.user_fs_base = 0;
	g_current_task->callstack_top = 0;
	enter_userspace();
	return 0;
}
static unsigned long clone_push_to_userland() {
	struct user_regs *p;

	DEBUG("CLONE from PUSH113_TO_USERLAND :%d\n", getcpuid());
	/* From here onwards DO NOT  call any function that consumes stack */
	asm("cli");
	asm("movq %%rsp,%0" : "=m" (p));
	p = p - 1;
	asm("subq $0xa0,%rsp");

	ut_memcpy((uint8_t *) p, (uint8_t *) &(g_current_task->thread.user_regs), sizeof(struct user_regs));

	p->gpres.rax = 0; /* return of the clone call */
	p->isf.rflags = DEFAULT_RFLAGS_VALUE;
	if (g_current_task->thread.userland.sp != 0)
		p->isf.rsp = g_current_task->thread.userland.sp;
	p->isf.cs = GDT_SEL(UCODE_DESCR) | SEG_DPL_USER;
	p->isf.ss = GDT_SEL(UDATA_DESCR) | SEG_DPL_USER;
	g_current_task->callstack_top = 0;
	enter_userspace();
	return 0;
}
static int free_fs(struct fs_struct *fs){
	if (fs->count.counter == 0)
		BUG();
	atomic_dec(&fs->count);
	if (fs->count.counter == 0) {
		ut_free(fs);
		return 0;
	}
	atomic_dec(&fs->count);
	return 1;
}
static int free_mm(struct mm_struct *mm) {
	int ret;
	DEBUG("freeing the mm :%x counter:%x \n", mm, mm->count.counter);

	if (mm->count.counter == 0)
		BUG();

	atomic_dec(&mm->count);
	if (mm->count.counter > 0) {
		return 0;
	}

	unsigned long vm_space_size = ~0;
	vm_munmap(mm, 0, vm_space_size);
	DEBUG(" tid:%x Freeing the final PAGE TABLE :%x\n", g_current_task->pid, vm_space_size);
	ret = ar_pageTableCleanup(mm, 0, vm_space_size);
	if (ret != 1) {
		ut_printf("ERROR : clear the pagetables :%d: \n", ret);
	}

//	ut_log(" mm_free alloc_pages: %d free_pages:%d \n", mm->stat_page_allocs, mm->stat_page_free);
	mm_slab_cache_free(mm_cachep, mm);
	return 1;
}

unsigned long sc_createKernelThread(int (*fn)(void *, void *), void **args, unsigned char *thread_name,
		unsigned long clone_flags) {
	unsigned long pid;
	unsigned long flags;
	struct list_head *pos;
	struct task_struct *task;

	pid = SYS_sc_clone(CLONE_VM | CLONE_KERNEL_THREAD | clone_flags, 0, 0, fn, args);

	if (thread_name == 0)
		return pid;
	spin_lock_irqsave(&g_global_lock, flags);
	list_for_each(pos, &g_task_queue.head) {
		task = list_entry(pos, struct task_struct, task_queue);
		if (task->pid == pid) {
			ut_strncpy(task->name, thread_name, MAX_TASK_NAME);
			goto last;
		}
	}
	last:
	spin_unlock_irqrestore(&g_global_lock, flags);

	return pid;
}
void **sc_get_thread_argv() {
	return g_current_task->thread.argv;
}
void sc_set_fsdevice(unsigned int device_in, unsigned int device_out) {
	auto *fs = g_current_task->fs;
	if (fs->filep[0] != 0) {
		fs->filep[0]->vinode = get_keyboard_device(device_in, IN_FILE);
	}
	if (fs->filep[1] != 0) {
		fs->filep[1]->vinode = get_keyboard_device(device_out, OUT_FILE);
	}
	if (fs->filep[2] != 0) {
		fs->filep[2]->vinode = get_keyboard_device(device_out, OUT_FILE);
	}
}

static void init_task_struct(struct task_struct *p, struct mm_struct *mm, struct fs_struct *fs) {
	atomic_set(&p->count, 1);
	p->mm = mm; /* TODO increase the corresponding count */
	p->fs = fs;
	p->locks_sleepable = 0;
	p->locks_nonsleepable = 0;
	p->trace_on = 0;
	p->callstack_top = 0;
	p->ticks = 0;
	p->cpu_contexts = 0;
	INIT_LIST_HEAD(&(p->dead_tasks.head));
	p->pending_signals = 0;
	p->killed = 0;
	p->exit_code = 0;
//	p->cpu = getcpuid();
	p->thread.userland.user_fs = 0;
	p->thread.userland.user_fs_base = 0;

	/* TODO : this is very simple round robin allocation, need to change dynamically with advanced algo */
	p->allocated_cpu = curr_cpu_assigned;
	curr_cpu_assigned++;
	if (curr_cpu_assigned >= getmaxcpus()) {
		curr_cpu_assigned = 0;
	}

	p->current_cpu = 0xffff;
	p->stick_to_cpu = 0xffff;
	p->ppid = g_current_task->pid;
	p->trace_stack_length = 10; /* some initial stack length, we do not know at what point tracing starts */
	p->run_queue.next = 0;
	p->run_queue.prev = 0;
	p->task_queue.next = 0;
	p->task_queue.prev = 0;
	p->wait_for_child_exit = 0;
	p->wait_queue.next = p->wait_queue.prev = (struct list_head *) 0;
	p->stats.ticks_consumed = 0;
	p->status_info[0] = 0;

	p->stats.syscall_count = 0;
	/* link to queue */
	free_pid_no++;
	if (free_pid_no == 0)
		free_pid_no++;
	p->pid = free_pid_no;
}

static int release_resources(struct task_struct *child_task, int attach_to_parent) {
	int i;
	struct mm_struct *mm;
	struct list_head *pos;
	struct task_struct *task, *parent;
	unsigned long flags;

	/* 1. release files */
	mm = child_task->mm;

	if (child_task->fs->count.counter == 1) {
		for (i = 0; i < child_task->fs->total; i++) {
			DEBUG("FREEing the files :%d \n", i);
			fs_close(child_task->fs->filep[i]);
		}
		child_task->fs->total = 0;
	}
	if (mm->count.counter > 1){
		return 0;
	}
	if (mm->exec_fp != 0) {
		fs_close(mm->exec_fp);
	}

	mm->exec_fp = 0;

	/* 2. relase locks */
	if (child_task->locks_sleepable > 0) { // TODO : need to check the mutex held
		ut_log("ERROR:  lock held by the process: %s \n", child_task->name);
		ipc_release_resources(child_task);
	}

	if (attach_to_parent == 0)
		return 1;

	spin_lock_irqsave(&g_global_lock, flags);
	child_task->state = TASK_DEAD;
	list_for_each(pos, &g_task_queue.head) {
		task = list_entry(pos, struct task_struct, task_queue);
		if (task->pid == child_task->ppid && task->state != TASK_DEAD) {
			atomic_inc(&child_task->count);
			list_del(&child_task->wait_queue);
			list_add_tail(&child_task->wait_queue, &(task->dead_tasks.head)); /* wait queue is overloaded, queue yourself to your parent */

			while (1) {
				if (list_empty((child_task->dead_tasks.head.next))) {
					break;
				} else {
					list_move(&(child_task->dead_tasks.head), &(task->dead_tasks.head));
				}
			}
			parent = task;
			goto last;
		}
	}
	last:
	spin_unlock_irqrestore(&g_global_lock, flags);
	if (parent != 0)
		ipc_del_from_waitqueues(parent);
	DEBUG("dead child attached to parent\n");
	return 1;
}

int sc_task_stick_to_cpu(unsigned long pid, int cpu_id) {
	unsigned long flags;
	struct list_head *pos;
	struct task_struct *task;
	int ret = SYSCALL_FAIL;

	spin_lock_irqsave(&g_global_lock, flags);
	list_for_each(pos, &g_task_queue.head) {
		task = list_entry(pos, struct task_struct, task_queue);
		if (task->pid == pid) {
			task->stick_to_cpu = cpu_id;
			ret = SYSCALL_SUCCESS;
			break;
		}
	}
	spin_unlock_irqrestore(&g_global_lock, flags);
	return ret;
}

/******************* schedule related functions **************************/

int ipi_interrupt(void *arg) { /* Do nothing, this is just wake up the core when it is executing HLT instruction  */
	return 0;
}
extern int init_ipc();

static struct fs_struct *create_fs() {
	struct fs_struct *fs = (struct fs_struct *) ut_calloc(sizeof(struct fs_struct));
	atomic_set(&fs->count, 1);

	return fs;
}
static struct mm_struct *create_mm() {
	struct mm_struct *mm;

	mm = (struct mm_struct *) mm_slab_cache_alloc(mm_cachep, 0);
	if (mm == 0)
		BUG();
	ut_memset((uint8_t *) mm, 0, sizeof(struct mm_struct));
	atomic_set(&mm->count, 1);
	DEBUG(" mm :%x counter:%x \n", mm, mm->count.counter);
	mm->pgd = 0;
	mm->mmap = 0;

	return mm;
}

int init_tasking(unsigned long unused) {
	int i;
	unsigned long task_addr;
	struct fs_struct *fs;

	g_global_lock.recursion_allowed = 0; /* this lock ownership will be transfer while task scheduling */
	g_current_task->pid = 1;/* hardcoded pid to make g_global _lock work */
	vm_area_cachep = (kmem_cache_t *) kmem_cache_create((const unsigned char *) "vm_area_struct", sizeof(struct vm_area_struct),
			0, 0, 0, 0);
	mm_cachep = (kmem_cache_t *) kmem_cache_create((const unsigned char *) "mm_struct", sizeof(struct mm_struct), 0, 0, 0, 0);

	g_kernel_mm = create_mm();
	fs = create_fs();
	init_ipc();

	g_inode_lock = mutexCreate((char *) "mutex_vfs");
	g_print_lock = mutexCreate((char *) "mutex_print");

	INIT_LIST_HEAD(&(g_task_queue.head));
	ipc_register_waitqueue(&timer_queue, (char *) "timer", 0);

	atomic_set(&g_kernel_mm->count, 1);
	g_kernel_mm->mmap = 0x0;
	g_kernel_mm->pgd = g_kernel_page_dir;
	fs->total = 3;
	for (i = 0; i < fs->total; i++) {
		fs->filep[i] = (struct file *) mm_slab_cache_alloc(g_slab_filep, 0);
		if (i == 0) {
			fs->filep[i]->vinode = get_keyboard_device(DEVICE_SERIAL1, IN_FILE);
			fs->filep[i]->type = IN_FILE;

		} else {
			fs->filep[i]->vinode = get_keyboard_device(DEVICE_SERIAL1, OUT_FILE);
			fs->filep[i]->type = OUT_FILE;
		}
	}
	ut_strcpy((uint8_t *) fs->cwd, (uint8_t *) "/");

	free_pid_no = 1; /* pid should never be 0 */
#define G_IDLE_TASK  &g_idle_stack[0][0]
	// task_addr=(unsigned long )((unsigned char *)G_IDLE_TASK+TASK_SIZE);
	task_addr = g_current_task;
	ut_log("	Task Addr start :%x  stack:%x current:%x\n", task_addr, &task_addr, g_current_task);
	for (i = 0; i < MAX_CPUS; i++) {
		g_cpu_state[i].md_state.cpu_id = i;
		g_cpu_state[i].idle_task = (struct task_struct *) ((unsigned char *) (task_addr) + i * TASK_SIZE);
		ut_memset((unsigned char *) g_cpu_state[i].idle_task, MAGIC_CHAR, TASK_SIZE - PAGE_SIZE / 2);
		init_task_struct(g_cpu_state[i].idle_task, g_kernel_mm, fs);
		INIT_LIST_HEAD(&(g_cpu_state[i].run_queue.head));

		g_cpu_state[i].idle_task->allocated_cpu = i;
		g_cpu_state[i].idle_task->state = TASK_RUNNING;
		g_cpu_state[i].idle_task->current_cpu = i;
		g_cpu_state[i].current_task = g_cpu_state[i].idle_task;
		g_cpu_state[i].stat_total_contexts = 0;
		g_cpu_state[i].stat_nonidle_contexts = 0;
		g_cpu_state[i].stat_idleticks = 0;
		g_cpu_state[i].active = 1; /* by default when the system starts all the cpu are in active state */
		g_cpu_state[i].intr_disabled = 0; /* interrupts are active */
		g_cpu_state[i].cpu_priority = 0;
		g_cpu_state[i].intr_nested_level = 0;
		g_cpu_state[i].run_queue_length = 0;
		ut_strncpy(g_cpu_state[i].idle_task->name, (unsigned char *) "idle", MAX_TASK_NAME);
	}
	g_current_task->current_cpu = 0;

	ut_log("		Cpu Struct size: %d \n", sizeof(struct cpu_state));

	ar_archSetUserFS(0);
//	init_timer(); //currently apic timer is in use
	ar_registerInterrupt(IPI_INTERRUPT, &ipi_interrupt, (char *) "IPI_GENERIC", NULL);
	ar_registerInterrupt(IPI_CLEARPAGETABLE, &ipi_pagetable_interrupt, (char *) "IPI_PTABLE", NULL);
	return JSUCCESS;
}
/* This function should not block, if it block then the idle thread may block */
void sc_delete_task(struct task_struct *task) {
	unsigned long intr_flags;

	if (task == 0)
		return;
	atomic_dec(&task->count);
	if (task->count.counter != 0) {
		DEBUG("Not deleted Task :%d\n", task->pid);
		return;
	}

	DEBUG("DELETING TASK :%x\n", task->pid);
	//ut_log("DELETING TASK deleting task id:%d name:%s  curretask:%s\n",task->pid,task->name,g_current_task->name);

	spin_lock_irqsave(&g_global_lock, intr_flags);
	list_del(&task->wait_queue);
	list_del(&task->run_queue);
	list_del(&task->task_queue);
	while (1) {
		struct list_head *node;
		struct task_struct *child_task = 0;

		node = task->dead_tasks.head.next;
		if (list_empty((task->dead_tasks.head.next))) {
			break;
		} else {
			child_task = list_entry(node,struct task_struct, wait_queue);
			if (child_task == 0)
				BUG();
			list_del(&child_task->wait_queue);
		}

		spin_unlock_irqrestore(&g_global_lock, intr_flags);
		sc_delete_task(child_task);
		spin_lock_irqsave(&g_global_lock, intr_flags);

		if (!list_empty((task->dead_tasks.head.next))) {
			BUG();
		}
	}

	spin_unlock_irqrestore(&g_global_lock, intr_flags);

	free_mm(task->mm);
	free_task_struct(task);
}

static void schedule_userSecondHalf() { /* user thread second Half: _schedule function function land here. */
	spin_unlock_irqrestore(&g_global_lock, g_current_task->flags);
	ar_updateCpuState(g_current_task, 0);
	clone_push_to_userland();
}
static void schedule_kernelSecondHalf() { /* kernel thread second half:_schedule function task can lands here. */
	spin_unlock_irqrestore(&g_global_lock, g_current_task->flags);
	g_current_task->thread.real_ip(g_current_task->thread.argv, 0);
}
/* NOT do not add any extra code in this function, if any register is used syscalls will not function properly */
void sc_before_syscall() {
	g_current_task->curr_syscall_id = g_cpu_state[getcpuid()].md_state.syscall_id;
	g_current_task->callstack_top = 0;
	g_current_task->stats.syscall_count++;
}

void sc_after_syscall() {
	/* Handle any pending signal */
	SYSCALL_DEBUG("syscall ret  state:%x\n",g_current_task->state);
	if (g_current_task->pending_signals == 0) {
		return;
	}
	if (is_kernelThread(g_current_task)) {
		ut_printf(" WARNING: kernel thread cannot be killed \n");
	} else {
		g_current_task->pending_signals = 0;
		if (g_current_task->killed == 1) {
			g_current_task->killed = 0;
			SYS_sc_exit(9);
		}
		return;
	}
	g_current_task->pending_signals = 0;
	g_current_task->callstack_top = 0;
	sc_schedule();
}
#define MAX_DEADTASKLIST_SIZE 100
static struct task_struct *deadtask_list[MAX_DEADTASKLIST_SIZE + 1];
static int deadlist_size = 0;
static void _add_to_deadlist(struct task_struct *task) {
	if (deadlist_size >= MAX_DEADTASKLIST_SIZE) {
		BUG();
	}
	deadtask_list[deadlist_size] = task;
	deadlist_size++;
	return;
}
static struct task_struct * _get_dead_task() {
	struct task_struct *task = 0;

	if (deadlist_size == 0)
		return 0;
	task = deadtask_list[deadlist_size - 1];
	deadtask_list[deadlist_size - 1] = 0;
	deadlist_size--;
	return task;
}
void sc_remove_dead_tasks() {
	unsigned long intr_flags;

	while (1) {
		spin_lock_irqsave(&g_global_lock, intr_flags);
		struct task_struct *task = 0;
		if (g_cpu_state[getcpuid()].idle_task != g_current_task) {
			task = _get_dead_task();
		}
		spin_unlock_irqrestore(&g_global_lock, intr_flags);

		if (task != 0) {
			if (task->state == TASK_DEAD) {
				sc_delete_task(task);
			} else {
				BUG();
			}
		} else {
			return;
		}
	}
}

void sc_schedule() { /* _schedule function task can land here. */
	unsigned long intr_flags;
	int cpuid = getcpuid();

	/*  Safe checks */
	if (!g_current_task) {
		BUG();
		return;
	}
	if (g_current_task->current_cpu
			!= cpuid|| g_current_task->magic_numbers[0] != MAGIC_LONG || g_current_task->magic_numbers[1] != MAGIC_LONG) /* safety check */
			{
		DEBUG(" Task Stack got CORRUPTED task:%x :%x :%x \n",g_current_task,g_current_task->magic_numbers[0],g_current_task->magic_numbers[1]);
		BUG();
			}

	/* schedule */
	spin_lock_irqsave(&g_global_lock, intr_flags);
	intr_flags = _schedule(intr_flags);
	spin_unlock_irqrestore(&g_global_lock, intr_flags);

	/* remove dead tasks */
	sc_remove_dead_tasks();
}

unsigned long _schedule(unsigned long flags) {
	struct task_struct *prev, *next;
	int cpuid = getcpuid();

	g_current_task->ticks++;
	prev = g_current_task;

	/* Only Active cpu will pickup the task, others runs idle task with external interrupts disable */
	if ((cpuid != 0) && (g_cpu_state[cpuid].active == 0)) { /* non boot cpu can sit idle without picking any tasks */
		next = g_cpu_state[cpuid].idle_task;
		apic_disable_partially();
		g_cpu_state[cpuid].intr_disabled = 1;
	} else {
		next = _del_from_runqueue(0, cpuid);
#if 1
		if (next != 0 && next->stick_to_cpu != 0xffff && next->stick_to_cpu != cpuid) {
			next->allocated_cpu = next->stick_to_cpu;
			_add_to_runqueue(next, -1);
			next = 0;
		}
		if ((g_cpu_state[cpuid].intr_disabled == 1) && (cpuid != 0) && (g_cpu_state[cpuid].active == 1)) {
			/* re-enable the apic */
			apic_reenable();
			g_cpu_state[cpuid].intr_disabled = 0;
		}
#endif
	}

	if (next == 0 && (prev->state != TASK_RUNNING)) { /* if there is no task in runqueue and current is not runnable */
		next = g_cpu_state[cpuid].idle_task;
	}

	if (next == 0) { /* by this point , we will always have some next */
		next = prev;
	}
#if 1 // SAFE Check
	if (next == g_cpu_state[cpuid].idle_task && g_cpu_state[cpuid].run_queue_length > 0) {
		BUG();
	}
#endif
#ifdef SMP   // SAFE Check
	if (g_cpu_state[0].idle_task==g_cpu_state[1].current_task || g_cpu_state[0].current_task==g_cpu_state[1].idle_task) {
		ut_printf("ERROR  cpuid :%d  %d\n",cpuid,getcpuid());
		while(1);
	}
	if (g_cpu_state[0].current_task==g_cpu_state[1].current_task) {
		while(1);
	}
#endif

	/* if prev and next are same then return */
	if (prev == next) {
		if ((prev->state != TASK_RUNNING)) {
			BUG();
		}
		return flags;
	} else {
		if (next != g_cpu_state[cpuid].idle_task) {
			g_cpu_state[cpuid].stat_nonidle_contexts++;
		}
		apic_set_task_priority(g_cpu_state[cpuid].cpu_priority);
	}
	/* if  prev and next are having same address space , then avoid tlb flush */
	next->counter = 5; /* 50 ms time slice */
	if (prev->mm->pgd != next->mm->pgd) /* TODO : need to make generic */
	{
		flush_tlb(next->mm->pgd);
	}
	if (prev->state == TASK_DEAD) {
		_add_to_deadlist(prev);
	} else if (prev != g_cpu_state[cpuid].idle_task && prev->state == TASK_RUNNING) { /* some other cpu  can pickup this task , running task and idle task should not be in a run equeue even though there state is running */
		if (prev->run_queue.next != 0) { /* Prev should not be on the runqueue */
			BUG();
		}
		prev->current_cpu = 0xffff;
		_add_to_runqueue(prev, -1);
	}
	next->current_cpu = cpuid; /* get cpuid based on this */
	next->cpu_contexts++;
	g_cpu_state[cpuid].current_task = next;
	g_cpu_state[cpuid].stat_total_contexts++;
	arch_spinlock_transfer(&g_global_lock, prev, next);
	/* update the cpu state  and tss state for system calls */
	ar_updateCpuState(next, prev);
	ar_setupTssStack((unsigned long) next + TASK_SIZE);

	prev->flags = flags;
	prev->current_cpu = 0xffff;
	/* finally switch the task */
	arch::switch_to(prev, next, prev);
	/* from the next statement onwards should not use any stack variables, new threads launched will not able see next statements*/
	return g_current_task->flags;
}
#if 1
int do_softirq() {
	static unsigned long house_keeper_count=0;
	int i;

	house_keeper_count++;
	g_cpu_state[getcpuid()].stat_idleticks++;

	/* collect cpu stats */
	if (getcpuid() == 0 && g_conf_cpu_stats != 0) {
		int cpuid = getcpuid();
		perf_stat_rip_hit(g_cpu_state[cpuid].stat_rip);
	}

	/* 2. Test of wait queues for any expiry. time queue is one of the wait queue  */
	if (getcpuid() == 0) {
		ipc_check_waitqueues();
	}

	/* TODO: Workaround for bug:  house keeping to check if the cpus stuck with error in gdt descriptors
	 * BAD: Looks like the problem fixed with IDT desccriptor correction
	 *  ES =0000 0000000000000000 ffffffff 00000000
CS =0008 0000000000000000 ffffffff 00a09900 DPL=0 CS64 [--A]
SS =0000 0000000000000000 ffffffff 00000000
DS =0000 0000000000000000 ffffffff 00000000
FS =0000 0000000000000000 ffffffff 00000000
GS =0000 ffffffff803bf2c0 ffffffff 00000000

GOOD:
ES =0000 0000000000000000 000fffff 00000000
CS =0008 0000000000000000 ffffffff 00a09900 DPL=0 CS64 [--A]
SS =0010 0000000000000000 ffffffff 00a09300 DPL=0 DS   [-WA]
DS =0000 0000000000000000 000fffff 00000000
FS =0000 0000000000000000 ffffffff 00c00000
GS =0000 ffffffff803bf200 000fffff 00000000
	 *
	 * */
#if 1
	if (getcpuid() == 0  && (house_keeper_count%100)==0){
		for (i=1; i< getmaxcpus(); i++){
			if (g_cpu_state[i].active == 0 || g_cpu_state[i].run_queue_length==0 ) continue;
			if (g_cpu_state[i].last_total_contexts == g_cpu_state[i].stat_total_contexts){
				struct task_struct *next;
				do {
					unsigned long intr_flags;

					spin_lock_irqsave(&g_global_lock, intr_flags);
					next = _del_from_runqueue(0, i);
					if (next != 0){
						next->allocated_cpu = 0;
						_add_to_runqueue(next, 0);
					}
					spin_unlock_irqrestore(&g_global_lock, intr_flags);
				} while (next != 0);
				g_cpu_state[i].active = 0;
				ut_log(" ERROR: deactive the cpu because cpu %d  is stuck \n",i);
			}
			g_cpu_state[i].last_total_contexts = g_cpu_state[i].stat_total_contexts;
		}
	}
#endif
	if (g_current_task->counter <= 0) {
		sc_schedule();
	}
}
#endif
//unsigned long kvm_ticks=0;
void timer_callback(void *unused_args) {
	int jiff_incremented = 0;

	/* 1. increment timestamp */
	if (getcpuid() == 0) {
		unsigned long kvm_ticks = get_kvm_time_fromboot();
		if ((kvm_ticks != 0) && (g_jiffie_tick > (kvm_ticks + 10))) {
			g_jiffie_errors++;
			g_jiffies++;
		} else {
			g_jiffie_tick++;
			g_jiffies++;
			jiff_incremented = 1;
		}
	}

	g_current_task->counter--;
	g_current_task->stats.ticks_consumed++;
}

/*********************************************8888   Sys calls and Jcmds ************************************/
void Jcmd_cpu_active(unsigned char *arg1, unsigned char *arg2) {
	int cpu, state;

	if (arg1 == 0 || arg2 == 0) {
		ut_printf(" cpu_active <cpu> <active=0/1>");
		return;
	}
	cpu = ut_atoi(arg1);
	state = ut_atoi(arg2);
	if (cpu > getmaxcpus() || state > 1 || cpu < 1 || state < 0) {
		ut_printf(" Invalid values cpu:%d state:%d valid cpus:1-%d state:0,1 \n", cpu, state, getmaxcpus());
		return;
	}
	if (state == 1 && g_cpu_state[cpu].active == 0) {
		g_cpu_state[cpu].active = state;
	} else {
		g_cpu_state[cpu].active = state;
	}
	ut_printf(" CPU %d changed state to %d \n", cpu, state);
	return;
}
void Jcmd_ipi(unsigned char *arg1, unsigned char *arg2) {
	int i;
	int ret;

	if (arg1 == 0)
		return;
	i = ut_atoi(arg1);
	ut_printf(" sending one IPI's from:%d  to cpu :%d\n", getcpuid(), i);
	ret = apic_send_ipi_vector(i, IPI_CLEARPAGETABLE);
	ut_printf(" return value of ipi :%d \n", ret);
}
void Jcmd_taskcpu(unsigned char *arg_pid, unsigned char *arg_cpuid) {
	int cpuid, pid;

	if (arg_pid == 0 || arg_cpuid == 0) {
		ut_printf(" taskcpu <pid> <cpuid>\n");
		return;
	}
	pid = ut_atoi(arg_pid);
	cpuid = ut_atoi(arg_cpuid);
	if (sc_task_stick_to_cpu(pid, cpuid) == SYSCALL_SUCCESS) {
		ut_printf(" Sucessfully assigned task %x(%d) to cpu %d\n", pid, pid, cpuid);
	} else {
		ut_printf(" Fail to  assigned task %x(%d) to cpu %d\n", pid, pid, cpuid);
	}
	return;
}
uint8_t apic_get_task_priority();
void Jcmd_prio(unsigned char *arg1, unsigned char *arg2) {
	int cpu, priority;

	if (arg1 == 0 || arg2 == 0) {
		ut_printf(" set cpu priority usage: prio <cpu> <priority>\n");
		return;
	}
	cpu = ut_atoi(arg1);
	priority = ut_atoi(arg2);
	if (cpu < MAX_CPUS && priority < 255) {
		ut_printf(" current priority : %d newpriority: %d \n",apic_get_task_priority(),priority);
		g_cpu_state[cpu].cpu_priority = priority;
	} else {
		ut_log("Error : Ivalid cpu :%d or priority:%d \n", cpu, priority);
	}
}
int Jcmd_kill(uint8_t *arg1, uint8_t *arg2) {
	int pid;
	if (arg1 != 0) {
		pid = ut_atoi(arg1);
	} else {
		ut_printf(" Error: kill pid is needed\n");
		return 0;
	}
	ut_printf(" Killing the process with pid : %x(%d)\n", pid, pid);
	SYS_sc_kill(pid, 9);
	return 1;
}
int Jcmd_ps(uint8_t *arg1, uint8_t *arg2) {
	unsigned long flags;
	struct list_head *pos;
	struct task_struct *task;
	int i, len, max_len;
	unsigned char *buf;
	int all = 0;

	ut_printf(" pid allocatedcpu: state generation ticks sleep_ticks mm mm_count name cpu\n");

	if (arg1 != 0 && ut_strcmp(arg1, (uint8_t *) "all") == 0) {
		all = 1;
	}
	len = PAGE_SIZE * 100;
	max_len = len;
	buf = (unsigned char *) vmalloc(len, 0);
	if (buf == 0) {
		ut_printf(" Unable to get vmalloc memory \n");
		return 0;
	}

	spin_lock_irqsave(&g_global_lock, flags);
	list_for_each(pos, &g_task_queue.head) {
		task = list_entry(pos, struct task_struct, task_queue);
		if (is_kernelThread(task))
			len = len - ut_snprintf(buf + max_len - len, len, "[%2x]", task->pid);
		else
			len = len - ut_snprintf(buf + max_len - len, len, "(%2x)", task->pid);

		len = len
				- ut_snprintf(buf + max_len - len, len,
						"cpu-%d: %3d %3d %5x  %5x %4d %7s sleeptick(%3d:%d) cpu:%3d :%s: count:%d status:%s stickcpu:%x\n",
						task->allocated_cpu, task->state, task->cpu_contexts, task->ticks, task->mm, task->mm->count.counter,
						task->name, task->sleep_ticks, task->stats.ticks_consumed, task->current_cpu, task->fs->cwd,
						task->count.counter, task->status_info, task->stick_to_cpu);
		temp_bt.count = 0;
		//if (task->state != TASK_RUNNING) {
		if (1) {
			if (all == 1) {
				ut_getBackTrace((unsigned long *) task->thread.rbp, (unsigned long) task, &temp_bt);
				for (i = 0; i < temp_bt.count; i++) {
					len = len
							- ut_snprintf(buf + max_len - len, len, "          %d: %9s - %x \n", i, temp_bt.entries[i].name,
									temp_bt.entries[i].ret_addr);
				}
			}
		}

	}
	len = len
			- ut_snprintf(buf + max_len - len, len,
					" CPU Processors:  cpuid contexts(user/total) <state 0=idle, intr-disabled > name pid\n");
	for (i = 0; i < getmaxcpus(); i++) {
		len = len
				- ut_snprintf(buf + max_len - len, len, "%2d:(%4d/%4d) <%d-%d> %7s(%d)\n", i,
						g_cpu_state[i].stat_nonidle_contexts, g_cpu_state[i].stat_total_contexts, g_cpu_state[i].active,
						g_cpu_state[i].intr_disabled, g_cpu_state[i].current_task->name, g_cpu_state[i].current_task->pid);
	}
	spin_unlock_irqrestore(&g_global_lock, flags);
	if (g_current_task->mm == g_kernel_mm)
		ut_printf("%s", buf);
	else
		SYS_fs_write(1, buf, len);

	vfree((unsigned long) buf);
	return 1;
}
static int get_free_cpu() {
	static int i = 0;
	int k;
	i++;
	k = i % getmaxcpus();
	if (k >= getmaxcpus()) {
		k = 0;
	}
#if 1
	if (g_cpu_state[k].active == 1) {
		return k;
	}
#endif
	return 0;

}
unsigned long SYS_sc_clone(int clone_flags, void *child_stack, void *pid, int (*fn)(void *, void *), void **args) {
	struct task_struct *p;
	struct mm_struct *mm;
	struct fs_struct *fs;
	unsigned long flags;
	//void *fn=fd_p;
	unsigned long ret_pid = 0;
	int i;

	SYSCALL_DEBUG("clone ctid:%x child_stack:%x flags:%x args:%x \n", fn, child_stack, clone_flags, args);

	/* Initialize the stack  */
	p = alloc_task_struct();
	if (p == 0)
		BUG();

	/* Initalize fs */
	if (clone_flags & CLONE_FS) {
		fs = g_current_task->fs;
		atomic_inc(&fs->count);
	} else {
		fs = create_fs();
		for (i = 0; i < g_current_task->fs->total && i < 3; i++) {
			fs->filep[i] = fs_dup(g_current_task->fs->filep[i], 0);
		}
		fs->total = g_current_task->fs->total;
		ut_strcpy(fs->cwd, g_current_task->fs->cwd);
	}

	/* Initialize mm */
	if (clone_flags & CLONE_VM){ /* parent and child run in the same vm */
		if (clone_flags & CLONE_KERNEL_THREAD) {
			mm = g_kernel_mm;
		} else {
			mm = g_current_task->mm;
		}
		atomic_inc(&mm->count);
		SYSCALL_DEBUG("clone  CLONE_VM the mm :%x counter:%x \n", mm, mm->count.counter);
	} else {
		mm = create_mm();
		DEBUG("BEFORE duplicating pagetables and vmaps \n");
		ar_dup_pageTable(g_current_task->mm, mm);
		vm_dup_vmaps(g_current_task->mm, mm);
		mm->brk_addr = g_current_task->mm->brk_addr;
		mm->brk_len = g_current_task->mm->brk_len;
		DEBUG("AFTER duplicating pagetables and vmaps\n");
		mm->exec_fp = 0; // TODO : need to be cloned
	}

	/* initialize task struct */
	init_task_struct(p, mm, fs);
	if (mm != g_kernel_mm) { /* user level thread */
		ut_memcpy((uint8_t *) &(p->thread.userland), (uint8_t *) &(g_current_task->thread.userland), sizeof(struct user_thread));
		ut_memcpy((uint8_t *) &(p->thread.user_regs),
				(uint8_t *) g_cpu_state[getcpuid()].md_state.kernel_stack - sizeof(struct user_regs), sizeof(struct user_regs));

		DEBUG(" userland  ip :%x \n", p->thread.userland.ip);
		p->thread.userland.sp = (unsigned long) child_stack;
		p->thread.userland.user_stack = (unsigned long) child_stack;

		DEBUG(" child ip:%x stack:%x \n", p->thread.userland.ip, p->thread.userland.sp);
		DEBUG("userspace rip:%x rsp:%x \n", p->thread.user_regs.isf.rip, p->thread.user_regs.isf.rsp);
		//p->thread.userland.argc = 0;/* TODO */
		//p->thread.userland.argv = 0; /* TODO */
		save_flags(p->flags);
		p->thread.ip = (void *) schedule_userSecondHalf;
	} else { /* kernel level thread */
		p->thread.ip = (void *) schedule_kernelSecondHalf;
		save_flags(p->flags);
		p->thread.argv = args;
		p->thread.real_ip = fn;
	}
	p->thread.sp = (void *) ((addr_t) p + (addr_t) TASK_SIZE - (addr_t) 160); /* 160 bytes are left at the bottom of the stack */
	p->state = TASK_RUNNING;

	p->allocated_cpu = get_free_cpu();
	ut_strncpy(p->name, g_current_task->name, MAX_TASK_NAME);

//	ut_log(" New thread is created:%d %s on %d\n",p->pid,p->name,p->allocated_cpu);
	ret_pid = p->pid;
	spin_lock_irqsave(&g_global_lock, flags);
	list_add_tail(&p->task_queue, &g_task_queue.head);
	_add_to_runqueue(p, -1);

	spin_unlock_irqrestore(&g_global_lock, flags);
	if (clone_flags & CLONE_VFORK) { //TODO : sys-vfork partially deone, need to use to signals suspend and continue the parent process

	}

	SYSCALL_DEBUG("clone return pid :%d \n", ret_pid);
	return ret_pid;
}
unsigned long SYS_sc_fork() {
	SYSCALL_DEBUG("fork \n");
	return SYS_sc_clone(0, 0, 0, 0, 0);
}
unsigned long SYS_sc_vfork() {
	SYSCALL_DEBUG("vfork \n");
	return SYS_sc_clone(CLONE_VFORK, 0, 0, 0, 0);
}
int SYS_sc_exit(int status) {
	unsigned long flags;
	SYSCALL_DEBUG("sys exit : status:%d \n", status);
	SYSCALL_DEBUG(" pid:%d existed cause:%d name:%s \n", g_current_task->pid, status, g_current_task->name);
	//ut_log(" pid:%d existed cause:%d name:%s \n",g_current_task->pid,status,g_current_task->name);
	ar_updateCpuState(g_current_task, 0);

	release_resources(g_current_task, 1);

	spin_lock_irqsave(&g_global_lock, flags);
	g_current_task->state = TASK_DEAD; /* this should be last statement before schedule */
	g_current_task->exit_code = status;
	spin_unlock_irqrestore(&g_global_lock, flags);

	sc_schedule();
	return 0;
}

void SYS_sc_execve(unsigned char *file, unsigned char **argv, unsigned char **env) {
	struct mm_struct *mm, *old_mm;
	struct fs_struct *fs;
	int i;
	unsigned long main_func;
	unsigned long t_argc, t_argv;
	unsigned long stack_len, tmp_stack, tmp_stack_top, tmp_aux;

	SYSCALL_DEBUG("execve file:%s argv:%x env:%x state:%x\n", file, argv, env,g_current_task->state);
	/* create the argc and env in a temporray stack before we destory the old stack */
	t_argc = 0;
	t_argv = 0;

	/* delete old vm and create a new one */
	mm = create_mm();
	fs = create_fs();

	/* these are for user level threads */
	for (i = 0; i < g_current_task->fs->total; i++) {
		fs->filep[i] = fs_dup(g_current_task->fs->filep[i], 0);
	}

	fs->total = g_current_task->fs->total;
	ut_strcpy(fs->cwd, g_current_task->fs->cwd);
	/* every process page table should have soft links to kernel page table */
	if (ar_dup_pageTable(g_kernel_mm, mm) != 1) {
		BUG();
	}

	mm->exec_fp = (struct file *) fs_open(file, 0, 0);
	if (mm->exec_fp != 0 && mm->exec_fp->vinode != 0)
		fs_set_flags(mm->exec_fp, INODE_EXECUTING);
	ut_strncpy(g_current_task->name, file, MAX_TASK_NAME);

	unsigned char *elf_interp = 0;
	tmp_stack_top = fs_elf_check_prepare(mm->exec_fp, argv, env, &t_argc, &t_argv, &stack_len, &tmp_aux, &elf_interp, &tmp_stack);
	if (tmp_stack_top == 0) {
		fs_close(mm->exec_fp);
		mm->exec_fp = 0;
		free_mm(mm);
		free_fs(fs);
		return;
	}
	if (elf_interp != 0) {
		fs_close(mm->exec_fp);
		mm->exec_fp = (struct file *) fs_open(elf_interp, 0, 0);
		if (mm->exec_fp != 0 && mm->exec_fp->vinode != 0)
			fs_set_flags(mm->exec_fp, INODE_EXECUTING);
	}
	release_resources(g_current_task, 0);

	old_mm = g_current_task->mm;
	g_current_task->mm = mm;
	g_current_task->fs = fs;
//	sc_set_fsdevice(DEVICE_SERIAL1, DEVICE_SERIAL1);

	/* from this point onwards new address space comes into picture, no memory belonging to previous address space like file etc should  be used */
	flush_tlb(mm->pgd);
	free_mm(old_mm);

	/* check for symbolic link */
	if (mm->exec_fp != 0 && (fs_get_type(mm->exec_fp) != REGULAR_FILE)) {
		struct fileStat file_stat;
		unsigned char newfilename[MAX_FILENAME];
		fs_stat(mm->exec_fp, &file_stat);
		if (fs_get_type(mm->exec_fp) == SYM_LINK_FILE) {
			if (fs_read(mm->exec_fp, newfilename, MAX_FILENAME) != 0) {
				fs_close(mm->exec_fp);
				mm->exec_fp = (struct file *) fs_open(newfilename, 0, 0);
			}
		}
	}

	/* populate vm with vmaps */
	if (mm->exec_fp == 0) {
		vfree(tmp_stack);
		ut_printf("Error execve : Failed to open the file \n");
		SYS_sc_exit(701);
		return;
	}

	main_func = fs_elf_load(mm->exec_fp, tmp_stack_top, stack_len, tmp_aux);
	vfree(tmp_stack);
	if (main_func == 0) {
		ut_printf("Error execve : ELF load Failed \n");
		SYS_sc_exit(703);
		return;
	}
ut_log(" execve : %s  pid :%d \n",g_current_task->name,g_current_task->pid);
	g_current_task->thread.userland.ip = main_func;
	g_current_task->thread.userland.sp = t_argv;
	g_current_task->thread.userland.argc = t_argc;
	g_current_task->thread.userland.argv = t_argv;

	g_current_task->thread.userland.user_stack = USERSTACK_ADDR + USERSTACK_LEN;
	g_current_task->thread.userland.user_ds = 0;
	g_current_task->thread.userland.user_es = 0;
	g_current_task->thread.userland.user_gs = 0;
	g_current_task->thread.userland.user_fs = 0;

	g_current_task->thread.userland.user_fs_base = 0;
	ar_updateCpuState(g_current_task, 0);

	push_to_userland();
}

int SYS_sc_kill(unsigned long pid, unsigned long signal) {
	unsigned long flags;
	struct list_head *pos;
	struct task_struct *task;
	int ret = SYSCALL_FAIL;

	SYSCALL_DEBUG("kill pid:%d signal:%d \n", pid, signal);

	spin_lock_irqsave(&g_global_lock, flags);
	list_for_each(pos, &g_task_queue.head) {
		task = list_entry(pos, struct task_struct, task_queue);
		if (task->pid == pid) {
			task->pending_signals = 1;
			ret = SYSCALL_SUCCESS;
			if (signal == 9) {
				task->killed = 1;
			}
			break;
		}
	}
	spin_unlock_irqrestore(&g_global_lock, flags);
	return ret;
}
extern int init_code_readonly(unsigned long arg1);
extern int g_fault_stop_allcpu;
void idleTask_func() {
	int k = 0;

	/* wait till initilization completed */
	while (g_boot_completed == 0)
		;
	//init_code_readonly(0); /* TODO:HARDCODED */

	ut_log("Idle Thread Started cpuid: %d stack addrss:%x \n", getcpuid(), &k);
	while (1) {
		if (g_fault_stop_allcpu ==1){
			asm volatile (" cli; hlt" : : : "memory");
		}
		arch::sti_hlt();
		sc_schedule();
	}
}
}
#endif
