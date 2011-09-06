#ifndef IPC_H
#define IPC_H

#include "common.h"
#include "task.h"
#include "spinlock.h"

#define IPC_TIMEOUT 0xffffffffUL

struct wait_struct {
	struct task_struct *queue;
	spinlock_t lock;
};

struct semaphore
{
	int count;
	struct wait_struct wait_queue;
};

typedef struct semaphore *sys_sem_t;
/* ipc */
sys_sem_t sys_sem_new(uint8_t count);
void sys_sem_init(struct semaphore *sem, uint8_t count);
void sys_sem_free(sys_sem_t sem);
void sys_sem_signal(sys_sem_t sem);
uint32_t sys_arch_sem_wait(sys_sem_t sem, uint32_t timeout);
#endif
