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
#include "interface.h"
static int ipc_init_done=0;
void *ipc_mutex_create(char *name) {
	struct semaphore *sem = mm_malloc(sizeof(struct semaphore), 0);
	assert (ipc_init_done !=0);

	if (sem == 0) {
		return 0;
	}
	sem->name =(unsigned char *) name;
	ipc_sem_new(sem, 1);
	sem->wait_queue.used_for = sem;
	sem->stat_acquired_start_time =0;
	return sem;
}

int ipc_mutex_lock(void *p, int line) {
	sys_sem_t *sem = p;
	int ret;

	if (p == 0)
		return 0;
	if ((sem->owner_pid == g_current_task->pid) && sem->recursive_count != 0){
		//ut_log("mutex_lock Recursive mutex: thread:%s  count:%d line:%d \n",g_current_task->name,sem->recursive_count,line);
		sem->recursive_count++;
		sem->stat_recursive_count++;
		return 1;
	}
	assert (g_current_task->locks_nonsleepable == 0);

	g_current_task->stats.wait_line_no = line;
	ret = 0;
	while (ret != 1){
		ret = ipc_sem_wait(p, 10000);
	}
	g_current_task->status_info[0] = 0;
	sem->owner_pid = g_current_task->pid;
	sem->recursive_count =1;
	g_current_task->locks_sleepable++;
	assert (g_current_task->locks_nonsleepable == 0);

	sem->stat_line = line;
	sem->stat_acquired_start_time = g_jiffies;
	return 1;
}
int ipc_mutex_unlock(void *p, int line) {
	sys_sem_t *sem = p;

	if (p == 0)
		return 0;
	if ((sem->owner_pid == g_current_task->pid) && sem->recursive_count > 1){
		//ut_log("mutex_unlock Recursive mutex: thread:%s  recursive count:%d line:%d \n",g_current_task->name,sem->recursive_count,line);
		sem->recursive_count--;
		return 1;
	}
	sem->owner_pid = 0;
	sem->recursive_count =0;
	g_current_task->locks_sleepable--;
	sem->stat_line = -line;
	sem->stat_total_acquired_time += (g_jiffies-sem->stat_acquired_start_time);
	ipc_sem_signal(p);

	return 1;
}

int ipc_mutex_destroy(void *p) {
	if (p == 0)
		return 0;
	ipc_sem_free(p);
	return 1;
}

/* this call consume system resources */
signed char ipc_sem_new(struct semaphore *sem, uint8_t count) {
	int ret;
	char *name;

	name=sem->name;
	if (name==NULL){
		name="mutex_semaphore";
	}
	sem->owner_pid = 0;
	sem->count = count;
	sem->sem_lock = SPIN_LOCK_UNLOCKED(name);
	sem->valid_entry = 1;
	if (sem->name == 0) {
		sem->name = "semaphore";
	}
	ret = ipc_register_waitqueue(&sem->wait_queue, sem->name,WAIT_QUEUE_WAKEUP_ONE);
	return 0;
}

int sys_sem_valid(sys_sem_t *sem) {
	return sem->valid_entry;
}

void sys_sem_set_invalid(sys_sem_t *sem) {

	sem->valid_entry = 0;
}
/* Deallocates a semaphore. */
void ipc_sem_free(sys_sem_t *sem) {

	arch_spinlock_unregister(&(sem->sem_lock));
	ipc_unregister_waitqueue(&(sem->wait_queue));
}

/* Signals a semaphore. */
void ipc_sem_signal(sys_sem_t *sem) {
	unsigned long flags;

	spin_lock_irqsave(&(sem->sem_lock), flags);
	sem->count++;
	spin_unlock_irqrestore(&(sem->sem_lock), flags);
	ipc_wakeup_waitqueue(&sem->wait_queue);
}

uint32_t ipc_sem_wait(sys_sem_t *sem, uint32_t timeout_arg){ /* timeout_arg in ms */
	unsigned long flags;
	unsigned long timeout;

	timeout = timeout_arg / 10;
	while (1) {
		if (sem->count <= 0) {
			timeout = ipc_waiton_waitqueue(&(sem->wait_queue), timeout);
		}
		if (timeout_arg == 0)
			timeout = 100;

		spin_lock_irqsave(&(sem->sem_lock), flags);
		/* Atomically check that we can proceed */
		if (sem->count > 0 || (timeout <= 0))
			break;
		spin_unlock_irqrestore(&(sem->sem_lock), flags);

		if (g_current_task->killed == 1){
			return IPC_TIMEOUT;
		}
	}

	if (sem->count > 0) {
		sem->count--;
		spin_unlock_irqrestore(&(sem->sem_lock), flags);
		return 1;
	}

	spin_unlock_irqrestore(&(sem->sem_lock), flags);
	return IPC_TIMEOUT;
}


/******************************************  WAIT QUEUE *******************/
#define MAX_WAIT_QUEUES 500
static wait_queue_t *wait_queues[MAX_WAIT_QUEUES]; /* TODO : this need to be locked */

int init_ipc(){
	int i;

	for (i = 0; i < MAX_WAIT_QUEUES; i++) {
		wait_queues[i] = 0;
	}
	ipc_init_done =1;
	return 1;
}
static void _add_to_waitqueue(wait_queue_t *waitqueue, struct task_struct * p, long ticks) {
	long cum_ticks, prev_cum_ticks;
	struct list_head *pos, *prev_pos;
	struct task_struct *task;

	cum_ticks = 0;
	prev_cum_ticks = 0;

	assert (p != g_cpu_state[getcpuid()].idle_task ) ;

	if (waitqueue->head.next == &waitqueue->head) {
		p->sleep_ticks = ticks;
		list_add_tail(&p->wait_queue, &waitqueue->head);
	} else {
		prev_pos = &waitqueue->head;
		list_for_each(pos, &waitqueue->head) {
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
		list_add_tail(&p->wait_queue, &waitqueue->head);
	}

last: ;

}

/* delete from the  wait queue */
static int _del_from_waitqueue(wait_queue_t *waitqueue, struct task_struct *p) {
	struct list_head *pos;
	struct task_struct *task;
	int ret = JFAIL;

	if (p==0 || waitqueue==0) return JFAIL;

	list_for_each(pos, &waitqueue->head) {
		task = list_entry(pos, struct task_struct, wait_queue);

		if (p == task) {
			if(pos == 0){
				BUG();
			}
			pos = pos->next;
			if (pos != &waitqueue->head) {
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


int ipc_register_waitqueue(wait_queue_t *waitqueue, char *name, unsigned long flags) {
	int i;
	unsigned long irq_flags;

	spin_lock_irqsave(&g_global_lock, irq_flags);
	for (i = 0; i < MAX_WAIT_QUEUES; i++) {
		if (wait_queues[i] == 0) {
			INIT_LIST_HEAD(&(waitqueue->head));
			waitqueue->name = name;

			wait_queues[i] = waitqueue;
			waitqueue->used_for = 0;
			waitqueue->flags = flags;
			goto last;
		}
	}
last:
    spin_unlock_irqrestore(&g_global_lock, irq_flags);
	return -1;
}
/* TODO It should be called from all the places where sc_register_waitqueue is called, currently it is unregister is called only from few places*/
int ipc_unregister_waitqueue(wait_queue_t *waitqueue) {
	int i;
	unsigned long flags;

	spin_lock_irqsave(&g_global_lock, flags);
	for (i = 0; i < MAX_WAIT_QUEUES; i++) {
		if (wait_queues[i] == waitqueue) {
			/*TODO:  remove the tasks present in the queue */
			wait_queues[i] = 0;
			goto last;
		}
	}
last:
    spin_unlock_irqrestore(&g_global_lock, flags);
	return -1;
}
static int wakeup_cpus(int wakeup_cpu) {
	int i;
	int ret = 0;

	/* wake up specific cpu */
	if ((wakeup_cpu != -1)) {
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
int ipc_wakeup_waitqueue(wait_queue_t *waitqueue ) {
	int ret = 0;
	struct task_struct *task;
	int wakeup_cpu=-1;
	int assigned_to_running_cpu;

	unsigned long flags;
	if (waitqueue == NULL)
		return ret;

	spin_lock_irqsave(&g_global_lock, flags);
	while (waitqueue->head.next != &waitqueue->head) {
		task = list_entry(waitqueue->head.next, struct task_struct, wait_queue);
		if (_del_from_waitqueue(waitqueue, task) == JSUCCESS) {
			task->state = TASK_RUNNING;
			assigned_to_running_cpu = 0;
			if (task->run_queue.next == 0 ){
				assigned_to_running_cpu = _sc_task_assign_to_cpu(task);
			}else{
				BUG();
			}
			ret++;
			if (waitqueue->flags & WAIT_QUEUE_WAKEUP_ONE){
				wakeup_cpu = task->allocated_cpu;
				goto gotit ;
			}
		}
	}
gotit:
	spin_unlock_irqrestore(&g_global_lock, flags);
	if (ret > 0 && assigned_to_running_cpu==0)
		wakeup_cpus(wakeup_cpu);

#if 0
	if ((waitqueue->flags & WAIT_QUEUE_WAKEUP_ONE) && active_cpu == 1){
		return 1;
	}


	if (ret>0){/* wakeup relevent cpus  */
		int i;

		/* wake up specific cpu */
		if ((wakeup_cpu != -1) && g_cpu_state[wakeup_cpu].current_task==g_cpu_state[wakeup_cpu].idle_task){
			apic_send_ipi_vector(wakeup_cpu,IPI_INTERRUPT);
			return ret;
		}

		/* wakeup all cpus */
		for (i=0; i<getmaxcpus(); i++){
			if (g_current_task->current_cpu != i && g_cpu_state[i].current_task==g_cpu_state[i].idle_task && g_cpu_state[i].active){
				apic_send_ipi_vector(i,IPI_INTERRUPT);
				return ret;
			}
		}
	}
#endif
	return ret;
}
/* ticks in terms of 10 ms */
int ipc_waiton_waitqueue(wait_queue_t *waitqueue, unsigned long ticks) {
	unsigned long flags;

	spin_lock_irqsave(&g_global_lock, flags);
	g_current_task->state = TASK_INTERRUPTIBLE;
	ut_snprintf(g_current_task->status_info,MAX_TASK_STATUS_DATA ,"wait-%s: %d",waitqueue->name,ticks);
	g_current_task->stats.wait_start_tick_no = g_jiffies ;
	waitqueue->stat_wait_count++;
	_add_to_waitqueue(waitqueue, g_current_task, ticks);
	flags=_schedule(flags);
	spin_unlock_irqrestore(&g_global_lock, flags);

	sc_remove_dead_tasks();

	g_current_task->status_info[0] = 0;
	waitqueue->stat_wait_ticks = waitqueue->stat_wait_ticks + (g_jiffies-g_current_task->stats.wait_start_tick_no);
	if (g_current_task->sleep_ticks <= 0)
		return 0;
	else
		return g_current_task->sleep_ticks;
}
void ipc_del_from_waitqueues(struct task_struct *task) {
	int i;
	unsigned long flags;
	int assigned_to_running_cpu;

	if (task ==0) return;
	spin_lock_irqsave(&g_global_lock, flags);
	for (i = 0; i < MAX_WAIT_QUEUES; i++) {
		if (wait_queues[i] == 0)
			continue;
		if (_del_from_waitqueue(wait_queues[i], task)==JFAIL){
			continue;
		}
		task->state = TASK_RUNNING;
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
		if (wait_queues[i] == 0)
			continue;
		if (wait_queues[i]->head.next != &(wait_queues[i]->head)) {
			struct task_struct *task;
			task =list_entry(wait_queues[i]->head.next, struct task_struct, wait_queue);
			assert(task!=0);

			task->sleep_ticks--;
			if (task->sleep_ticks <= 0) {
				_del_from_waitqueue(wait_queues[i], task);
				task->state = TASK_RUNNING;
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
		if (wait_queues[i] == 0)
			continue;
		if (wait_queues[i]->used_for == 0) continue;
		sys_sem_t *sem = wait_queues[i]->used_for;
		if (sem->owner_pid != task->pid) continue;

		spin_unlock_irqrestore(&g_global_lock, flags);
		ipc_sem_signal(sem);
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
		if (wait_queues[i] == 0)
			continue;
		if (wait_queues[i]->used_for == 0)
			len = len - ut_snprintf(buf+max_len-len,len," %9s : ", wait_queues[i]->name);
		else {
			len = len - ut_snprintf(buf+max_len-len,len,"[%9s]: ", wait_queues[i]->name);
			sem = wait_queues[i]->used_for;
			if (sem){
				recursive_count = sem->stat_recursive_count;
				total_acquired_time = sem->stat_total_acquired_time;
				if (sem->owner_pid != 0)
					len = len - ut_snprintf(buf+max_len-len,len," [%x] ", sem->owner_pid);
			}
		}
		len = len - ut_snprintf(buf+max_len-len,len," (%d/%d:%d: AT:%d) ", wait_queues[i]->stat_wait_ticks,wait_queues[i]->stat_wait_count,recursive_count,total_acquired_time);

		if (len <= 0) break;
		list_for_each(pos, &wait_queues[i]->head) {
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
#if 1
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
