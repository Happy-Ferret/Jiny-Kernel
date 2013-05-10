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
void *ipc_mutex_create(unsigned char *name) {
	struct semaphore *sem = mm_malloc(sizeof(struct semaphore), 0);

	if (sem == 0) {
		return 0;
	}
	sem->name = name;
	ipc_sem_new(sem, 1);
	sem->wait_queue.used_for = sem;
	return sem;
}
int ipc_mutex_lock(void *p, int line) {
	if (p == 0)
		return 0;
	while (ipc_sem_wait(p, 100) != 1)
		;

	sys_sem_t *sem = p;
	sem->owner_pid = g_current_task->pid;
	g_current_task->locks_held++;
	sem->stat_line = line;
	return 1;
}
int ipc_mutex_unlock(void *p, int line) {
	sys_sem_t *sem = p;

	if (p == 0)
		return 0;
	sem->owner_pid = 0;
	g_current_task->locks_held--;
	sem->stat_line = -line;
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

	sem->owner_pid = 0;
	sem->count = count;
	sem->sem_lock = SPIN_LOCK_UNLOCKED("mutex_semaphore");
	sem->valid_entry = 1;
	if (sem->name == 0) {
		sem->name = "semaphore";
	}
	ret = ipc_register_waitqueue(&sem->wait_queue, sem->name);
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
	ipc_wakeup_waitqueue(&sem->wait_queue);
	spin_unlock_irqrestore(&(sem->sem_lock), flags);
}

uint32_t ipc_sem_wait(sys_sem_t *sem, uint32_t timeout_arg) /* timeout_arg in ms */
{
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
	return 1;
}
static void _add_to_waitqueue(wait_queue_t *waitqueue, struct task_struct * p, long ticks) {
	long cum_ticks, prev_cum_ticks;
	struct list_head *pos, *prev_pos;
	struct task_struct *task;

	cum_ticks = 0;
	prev_cum_ticks = 0;
	if (p == g_cpu_state[0].idle_task  || p == g_cpu_state[1].idle_task ){
		BUG();
	}
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
	int ret = 0;

	if (p==0 || waitqueue==0) return 0;

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
			ret = 1;
			list_del(&p->wait_queue);//TODO crash happening */
			return 1;
		}
	}

	return ret;
}


int ipc_register_waitqueue(wait_queue_t *waitqueue, char *name) {
	int i;
	unsigned long flags;

	spin_lock_irqsave(&g_global_lock, flags);
	for (i = 0; i < MAX_WAIT_QUEUES; i++) {
		if (wait_queues[i] == 0) {
			INIT_LIST_HEAD(&(waitqueue->head));
			waitqueue->name = name;

			wait_queues[i] = waitqueue;
			waitqueue->used_for = 0;
			goto last;
		}
	}
last:
    spin_unlock_irqrestore(&g_global_lock, flags);
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
int ipc_wakeup_waitqueue(wait_queue_t *waitqueue ) {
	int ret = 0;
	struct task_struct *task;

	unsigned long flags;
	if (waitqueue == NULL)
		return ret;

	spin_lock_irqsave(&g_global_lock, flags);
	while (waitqueue->head.next != &waitqueue->head) {
		task = list_entry(waitqueue->head.next, struct task_struct, wait_queue);
		if (_del_from_waitqueue(waitqueue, task) == 1) {
			task->state = TASK_RUNNING;
			if (task->run_queue.next == 0 )
				sc_add_to_runqueue(task);
			else
				BUG();
			ret++;
		}
	}
	spin_unlock_irqrestore(&g_global_lock, flags);

	if (ret>0){/* wakeup any other idle and active cpus to serve the runqueue  */
		int i;
		for (i=0; i<getmaxcpus(); i++){
			if (g_current_task->cpu != i && g_cpu_state[i].current_task==g_cpu_state[i].idle_task && g_cpu_state[i].active){
				apic_send_ipi_vector(i,IPI_INTERRUPT);
				return ret;
			}
		}
	}

	return ret;
}
int ipc_waiton_waitqueue(wait_queue_t *waitqueue, unsigned long ticks) {
	unsigned long flags;

	spin_lock_irqsave(&g_global_lock, flags);
	g_current_task->state = TASK_INTERRUPTIBLE;
	_add_to_waitqueue(waitqueue, g_current_task, ticks);
	spin_unlock_irqrestore(&g_global_lock, flags);

	sc_schedule();
	if (g_current_task->sleep_ticks <= 0)
		return 0;
	else
		return g_current_task->sleep_ticks;
}
void ipc_check_waitqueues() {
	int i;
	unsigned long flags;

	spin_lock_irqsave(&g_global_lock, flags);
	for (i = 0; i < MAX_WAIT_QUEUES; i++) {
		if (wait_queues[i] == 0)
			continue;
		if (wait_queues[i]->head.next != &(wait_queues[i]->head)) {
			struct task_struct *task;
			task =
					list_entry(wait_queues[i]->head.next, struct task_struct, wait_queue);

			task->sleep_ticks--;
			if (task->sleep_ticks <= 0) {
				_del_from_waitqueue(wait_queues[i], task);
				task->state = TASK_RUNNING;
				if (task->run_queue.next == 0)
					sc_add_to_runqueue(task);
				else
					BUG();
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
#if 1
int Jcmd_locks(char *arg1, char *arg2) {
	int i;
	unsigned long flags;
	struct list_head *pos;
	struct task_struct *task;

	ut_printf("SPIN LOCKS:  Name  pid count contention(rate) recursive# \n");
	for (i = 0; i < g_spinlock_count; i++) {
		ut_printf(" %9s %3x %5d %5d(%d) %3d\n", g_spinlocks[i]->name,
				g_spinlocks[i]->pid, g_spinlocks[i]->stat_locks,
				g_spinlocks[i]->contention,
				g_spinlocks[i]->contention / g_spinlocks[i]->stat_locks,
				g_spinlocks[i]->stat_recursive_locks);
	}

	ut_printf("Wait queue: \n");
	spin_lock_irqsave(&g_global_lock, flags);
	for (i = 0; i < MAX_WAIT_QUEUES; i++) {
		if (wait_queues[i] == 0)
			continue;
		if (wait_queues[i]->used_for == 0)
			ut_printf(" %8s : ", wait_queues[i]->name);
		else
			ut_printf("[%8s]: ", wait_queues[i]->name);
		list_for_each(pos, &wait_queues[i]->head) {
			task = list_entry(pos, struct task_struct, wait_queue);
			ut_printf(" %x(%s),", task->pid, task->name);
		}
		ut_printf("\n");
	}
	spin_unlock_irqrestore(&g_global_lock, flags);

	return 1;
}
#endif
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
