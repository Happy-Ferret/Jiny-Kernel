/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *   kernel/ipc.c
 *   Author: Naredula Janardhana Reddy  (naredula.jana@gmail.com, naredula.jana@yahoo.com)
 *
 */
#include "file.hh"
#include "ipc.hh"

extern "C"{
#include "interface.h"
static int ipc_init_done=0;
extern int _sc_task_assign_to_cpu(struct task_struct *task);
unsigned long _schedule(unsigned long flags);
void sc_remove_dead_tasks();
void *ipc_mutex_create(char *name) {
	semaphore *sem ;
	assert (ipc_init_done !=0);

	sem = jnew_obj(semaphore, 1, name);
	sem->waitqueue->used_for = sem;
	sem->stat_acquired_start_time =0;
	return sem;
}

int ipc_mutex_lock(void *p, int line) {
	semaphore *sem = p;
	int ret;

	return sem->lock(line);
}
int ipc_mutex_unlock(void *p, int line) {
	semaphore *sem = p;
	sem->unlock(line);
}

int ipc_mutex_destroy(void *p) {
	semaphore *sem = p;
	if (p == 0)
		return 0;
	sem->free();
	return 1;
}


int init_ipc(){
	int i;

	for (i = 0; i < MAX_WAIT_QUEUES; i++) {
		wait_queue::wait_queues[i] = 0;
	}
	ipc_init_done =1;
	return 1;
}

static int wakeup_cpus(int wakeup_cpu) {
	int i;
	int ret = 0;

	if (getmaxcpus() == 1) return 0; /* for single cpu */
	/* wake up specific cpu */
	if ((wakeup_cpu != -1)) {
		if (g_current_task->current_cpu == wakeup_cpu) return 0;
		if (g_cpu_state[wakeup_cpu].current_task == g_cpu_state[wakeup_cpu].idle_task) {
			apic_send_ipi_vector(wakeup_cpu, IPI_INTERRUPT);
			return 1;
		} else {
			return 0;
		}
	}

	/* wakeup all cpus */
	for (i = 0; i < getmaxcpus(); i++) {
		if (g_current_task->current_cpu != i
				&& g_cpu_state[i].current_task == g_cpu_state[i].idle_task
				&& g_cpu_state[i].active) {
			apic_send_ipi_vector(i, IPI_INTERRUPT);
			ret++;
			// return ret;
		}
	}
	return ret;
}

void ipc_del_from_waitqueues(struct task_struct *task) {
	int i;
	unsigned long flags;
	int assigned_to_running_cpu;

	if (task ==0) return;
	spin_lock_irqsave(&g_global_lock, flags);
	for (i = 0; i < MAX_WAIT_QUEUES; i++) {
		if (wait_queue::wait_queues[i] == 0)
			continue;
		if (wait_queue::wait_queues[i]->_del_from_me(task)==JFAIL){
			continue;
		}
		assigned_to_running_cpu = 0;
		if (task->run_queue.next == 0)
			assigned_to_running_cpu = _sc_task_assign_to_cpu(task);
		else
			BUG();
		if (assigned_to_running_cpu == 0) {
			spin_unlock_irqrestore(&g_global_lock, flags);
			wakeup_cpus(task->allocated_cpu);
			spin_lock_irqsave(&g_global_lock, flags);
		}
		break;
	}
	spin_unlock_irqrestore(&g_global_lock, flags);
}

void ipc_check_waitqueues() {
	int i;
	unsigned long flags;
	int assigned_to_running_cpu;

	spin_lock_irqsave(&g_global_lock, flags);
	for (i = 0; i < MAX_WAIT_QUEUES; i++) {
		if (wait_queue::wait_queues[i] == 0)
			continue;
		if (wait_queue::wait_queues[i]->head.next != &(wait_queue::wait_queues[i]->head)) {
			struct task_struct *task;
			task =list_entry(wait_queue::wait_queues[i]->head.next, struct task_struct, wait_queue);
			assert(task!=0);
			if ((task <  KADDRSPACE_START) || (task >KADDRSPACE_END)){
				BUG();
			}

			task->sleep_ticks--;
			if (task->sleep_ticks <= 0) {
				wait_queue::wait_queues[i]->_del_from_me(task);
				assigned_to_running_cpu = 0;
				if (task->run_queue.next == 0)
					assigned_to_running_cpu = _sc_task_assign_to_cpu(task);
				else
					BUG();
				if (assigned_to_running_cpu == 0) {
					spin_unlock_irqrestore(&g_global_lock, flags);
					wakeup_cpus(task->allocated_cpu);
					spin_lock_irqsave(&g_global_lock, flags);
				}
			}
		}
	}
	spin_unlock_irqrestore(&g_global_lock, flags);
}

void ipc_release_resources(struct task_struct *task){
	int i;
	unsigned long flags;

	spin_lock_irqsave(&g_global_lock, flags);
	for (i = 0; i < MAX_WAIT_QUEUES; i++) {
		if (wait_queue::wait_queues[i] == 0)
			continue;
		if (wait_queue::wait_queues[i]->used_for == 0) continue;
		semaphore *sem = wait_queue::wait_queues[i]->used_for;
		if (sem->owner_pid != task->pid) continue;

		spin_unlock_irqrestore(&g_global_lock, flags);
		sem->signal();
		spin_lock_irqsave(&g_global_lock, flags);
	}
	spin_unlock_irqrestore(&g_global_lock, flags);

}
/*********************************  end of Wait queue ***********************************/


#ifdef SPINLOCK_DEBUG
spinlock_t *g_spinlocks[MAX_SPINLOCKS];
int g_spinlock_count = 0;

int Jcmd_locks(char *arg1, char *arg2) {
	int i;
	unsigned long flags;
	struct list_head *pos;
	struct task_struct *task;
	int len,max_len;
	unsigned char *buf;

	ut_printf("SPIN LOCKS:  Name  pid count contention(rate) recursive# \n");
	for (i = 0; i < g_spinlock_count; i++) {
		if (g_spinlocks[i]==0) continue;
		ut_printf(" %9s %3x %5d %5d(%d) %3d\n", g_spinlocks[i]->name,
				g_spinlocks[i]->pid, g_spinlocks[i]->stat_locks,
				g_spinlocks[i]->contention,
				g_spinlocks[i]->contention / g_spinlocks[i]->stat_locks,
				g_spinlocks[i]->stat_recursive_locks);
	}

	ut_printf("Wait queue: name: [owner pid] (wait_ticks/count:recursive_count) : waiting pid(name-line_no)\n");

	len = PAGE_SIZE*100;
	max_len=len;
	buf = (unsigned char *) vmalloc(len,0);
	if (buf == 0) {
		ut_printf(" Unable to get vmalloc memory \n");
		return 0;
	}
	spin_lock_irqsave(&g_global_lock, flags);
	for (i = 0; i < MAX_WAIT_QUEUES; i++) {
		struct semaphore *sem=0;
		int recursive_count=0;
		int total_acquired_time=0;
		if (wait_queue::wait_queues[i] == 0)
			continue;
		if (wait_queue::wait_queues[i]->used_for == 0)
			len = len - ut_snprintf(buf+max_len-len,len," %9s : ", wait_queue::wait_queues[i]->name);
		else {
			len = len - ut_snprintf(buf+max_len-len,len,"[%9s]: ", wait_queue::wait_queues[i]->name);
			sem = wait_queue::wait_queues[i]->used_for;
			if (sem){
				recursive_count = sem->stat_recursive_count;
				total_acquired_time = sem->stat_total_acquired_time;
				//if (sem->owner_pid != 0)
					len = len - ut_snprintf(buf+max_len-len,len," [%x] ", sem->owner_pid);
			}
		}
		len = len - ut_snprintf(buf+max_len-len,len," (%d/%d:%d: AT:%d) ", wait_queue::wait_queues[i]->stat_wait_ticks,wait_queue::wait_queues[i]->stat_wait_count,recursive_count,total_acquired_time);

		if (len <= 0) break;
		list_for_each(pos, &wait_queue::wait_queues[i]->head) {
			int wait_line=0;
			task = list_entry(pos, struct task_struct, wait_queue);
			if (sem!=0) wait_line=task->stats.wait_line_no;
			len = len - ut_snprintf(buf+max_len-len,len,":%x(%s-%d),", task->pid, task->name,wait_line);
		}

		len = len - ut_snprintf(buf+max_len-len,len,"\n");
		if (len <= 0) break;
	}
	spin_unlock_irqrestore(&g_global_lock, flags);

	ut_printf("%s",buf);
	vfree(buf);
	return 1;
}

#endif

/**********************************************/
#if 0
void ipc_test1() {
	int i;
	struct semaphore sem;

	ipc_sem_new(&sem, 1);
	ipc_sem_signal(&sem);
	for (i = 0; i < 40; i++) {
		ut_printf("Iterations  :%d  jiffes:%x\n", i, g_jiffies);
		ipc_sem_wait(&sem, 1000);
	}
	ipc_sem_free(&sem);
}
#endif
} /* end of c */


wait_queue *wait_queue::wait_queues[MAX_WAIT_QUEUES];
wait_queue::wait_queue( char *arg_name, unsigned long arg_flags) {
	int i;
	unsigned long irq_flags;

	spin_lock_irqsave(&g_global_lock, irq_flags);
	for (i = 0; i < MAX_WAIT_QUEUES; i++) {
		if (wait_queues[i] == 0) {
			INIT_LIST_HEAD(&(head));
			name = arg_name;

			wait_queue::wait_queues[i] = this;
			used_for = 0;
			flags = arg_flags;
			goto last;
		}
	}
last:
    spin_unlock_irqrestore(&g_global_lock, irq_flags);
}
/* TODO It should be called from all the places where sc_register_waitqueue is called, currently it is unregister is called only from few places*/
int wait_queue::unregister() {
	int i;
	unsigned long irq_flags;

	spin_lock_irqsave(&g_global_lock, irq_flags);
	for (i = 0; i < MAX_WAIT_QUEUES; i++) {
		if (wait_queue::wait_queues[i] == this) {
			/*TODO:  remove the tasks present in the queue */
			wait_queues[i] = 0;
			goto last;
		}
	}
last:
    spin_unlock_irqrestore(&g_global_lock, irq_flags);
    jfree_obj(this);
	return -1;
}
void wait_queue::_add_to_me( struct task_struct * p, long ticks) {
	long cum_ticks, prev_cum_ticks;
	struct list_head *pos, *prev_pos;
	struct task_struct *task;

	cum_ticks = 0;
	prev_cum_ticks = 0;

	assert (p != g_cpu_state[getcpuid()].idle_task ) ;

	if (head.next == &head) {
		p->sleep_ticks = ticks;
		list_add_tail(&p->wait_queue, &head);
	} else {
		prev_pos = &head;
		list_for_each(pos, &head) {
			task = list_entry(pos, struct task_struct, wait_queue);
			prev_cum_ticks = cum_ticks;
			cum_ticks = cum_ticks + task->sleep_ticks;

			if (cum_ticks > ticks) {
				p->sleep_ticks = (ticks - prev_cum_ticks);
				task->sleep_ticks = task->sleep_ticks - p->sleep_ticks;
				list_add(&p->wait_queue, prev_pos);
				goto last;
			}
			prev_pos = pos;
		}
		p->sleep_ticks = (ticks - cum_ticks);
		list_add_tail(&p->wait_queue, &head);
	}

last: ;

}
/* delete from the  wait queue */
int wait_queue::_del_from_me( struct task_struct *p) {
	struct list_head *pos;
	struct task_struct *task;
	int ret = JFAIL;

	if (p==0 ) return JFAIL;

	list_for_each(pos, &head) {
		task = list_entry(pos, struct task_struct, wait_queue);

		if (p == task) {
			if(pos == 0){
				BUG();
			}
			pos = pos->next;
			if (pos != &head) {
				if (p<0x100 || p->magic_numbers[0] != MAGIC_LONG){
					BUG();
				}
				task = list_entry(pos, struct task_struct, wait_queue);

				if ( task<0x100 || task>0xfffffffffffff000){
					BUG();
				}
				task->sleep_ticks = task->sleep_ticks + p->sleep_ticks;
			}
			p->sleep_ticks = 0;
			ret = JSUCCESS;
			list_del(&p->wait_queue);//TODO crash happening */
			return ret;
		}
	}

	return ret;
}
int wait_queue::wakeup() {
	int ret = 0;
	struct task_struct *task;
	int wakeup_cpu=-1;
	int assigned_to_running_cpu;
	unsigned long irq_flags;

	spin_lock_irqsave(&g_global_lock, irq_flags);
	while (head.next != &head) {
		task = list_entry(head.next, struct task_struct, wait_queue);
		if (_del_from_me(task) == JSUCCESS) {
			assigned_to_running_cpu = 0;
			if (task->run_queue.next == 0 ){
				assigned_to_running_cpu = _sc_task_assign_to_cpu(task);
			}else{
				BUG();
			}
			ret++;
			if (flags & WAIT_QUEUE_WAKEUP_ONE){
				wakeup_cpu = task->allocated_cpu;
				goto gotit ;
			}
		}
	}
gotit:
	spin_unlock_irqrestore(&g_global_lock, irq_flags);
	if (ret > 0 && assigned_to_running_cpu==0)
		wakeup_cpus(wakeup_cpu);

	return ret;
}
/* ticks in terms of 10 ms */
int wait_queue::wait(unsigned long ticks) {
	unsigned long irq_flags;

	spin_lock_irqsave(&g_global_lock, irq_flags);
	if (g_current_task->state == TASK_NONPREEMPTIVE){
		BUG();
	}
	g_current_task->state = TASK_INTERRUPTIBLE;
	ut_snprintf(g_current_task->status_info,MAX_TASK_STATUS_DATA ,"wait-%s: %d",name,ticks);
	g_current_task->stats.wait_start_tick_no = g_jiffies ;
	stat_wait_count++;
	_add_to_me(g_current_task, ticks);
	irq_flags=_schedule(irq_flags);
	spin_unlock_irqrestore(&g_global_lock, irq_flags);

	sc_remove_dead_tasks();

	g_current_task->status_info[0] = 0;
	stat_wait_ticks = stat_wait_ticks + (g_jiffies-g_current_task->stats.wait_start_tick_no);
	if (g_current_task->sleep_ticks <= 0)
		return 0;
	else
		return g_current_task->sleep_ticks;
}
void wait_queue::print_stats(){

}
/* this call consume system resources */
semaphore::semaphore(uint8_t arg_count, char *arg_name) {
	name=arg_name;
	if (name==NULL){
		name="mutex_semaphore";
	}
	owner_pid = 0;
	count = arg_count;
	sem_lock = SPIN_LOCK_UNLOCKED(name);
	valid_entry = 1;
	waitqueue = jnew_obj(wait_queue, "semaphore" ,WAIT_QUEUE_WAKEUP_ONE);
}
/* Signals a semaphore. */
void semaphore::signal() {
	unsigned long irq_flags;

	spin_lock_irqsave(&(sem_lock), irq_flags);
	count++;
	spin_unlock_irqrestore(&(sem_lock), irq_flags);
	waitqueue->wakeup();
}

uint32_t semaphore::wait(uint32_t timeout_arg){ /* timeout_arg in ms */
	unsigned long flags;
	unsigned long timeout;

	timeout = timeout_arg / 10;
	while (1) {
		if (count <= 0) {
			timeout = waitqueue->wait(timeout);
		}
		if (timeout_arg == 0)
			timeout = 100;

		spin_lock_irqsave(&(sem_lock), flags);
		/* Atomically check that we can proceed */
		if (count > 0 || (timeout <= 0))
			break;
		spin_unlock_irqrestore(&(sem_lock), flags);

		if (g_current_task->killed == 1){
			return IPC_TIMEOUT;
		}
	}

	if (count > 0) {
		count--;
		spin_unlock_irqrestore(&(sem_lock), flags);
		return 1;
	}

	spin_unlock_irqrestore(&(sem_lock), flags);
	return IPC_TIMEOUT;
}
int semaphore::lock(int line) {

	int ret;
	if ((owner_pid == g_current_task->pid) && recursive_count != 0){
		//ut_log("mutex_lock Recursive mutex: thread:%s  count:%d line:%d \n",g_current_task->name,sem->recursive_count,line);
		recursive_count++;
		stat_recursive_count++;
		return 1;
	}
	assert (g_current_task->locks_nonsleepable == 0);

	g_current_task->stats.wait_line_no = line;
	ret = 0;
	while (ret != 1){
		ret = wait(10000);
	}
	g_current_task->status_info[0] = 0;
	owner_pid = g_current_task->pid;
	recursive_count =1;
	g_current_task->locks_sleepable++;
	assert (g_current_task->locks_nonsleepable == 0);

	stat_line = line;
	stat_acquired_start_time = g_jiffies;
	return 1;
}
int semaphore::unlock(int line) {

	if ((owner_pid == g_current_task->pid) && recursive_count > 1){
		//ut_log("mutex_unlock Recursive mutex: thread:%s  recursive count:%d line:%d \n",g_current_task->name,sem->recursive_count,line);
		recursive_count--;
		return 1;
	}
	owner_pid = 0;
	recursive_count =0;
	g_current_task->locks_sleepable--;
	stat_line = -line;
	stat_total_acquired_time += (g_jiffies-stat_acquired_start_time);
	signal();

	return 1;
}
/* Deallocates a semaphore. */
void semaphore::free() {
	arch_spinlock_unregister(&(sem_lock));
	waitqueue->unregister();
    jfree_obj(this);
}
void semaphore::print_stats(){

}

