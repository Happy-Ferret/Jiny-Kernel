#ifndef IPC_H
#define IPC_H

#include "common.h"
#include "task.h"
#include "spinlock.h"

#define IPC_TIMEOUT 0xffffffffUL

#if 0
struct wait_struct {
	struct task_struct *queue;
	spinlock_t lock;
};
#endif

struct semaphore
{
	int count;
	spinlock_t sem_lock; /* this is to protect count */
	queue_t wait_queue;
};

typedef struct semaphore *sys_sem_t;
/* ipc */

void *mutexCreate();
int mutexLock(void *p);
int mutexUnLock(void *p);
int mutexDestroy(void *p);

int sem_alloc(struct semaphore *sem, uint8_t count);
int sem_free(struct semaphore *sem);

sys_sem_t sys_sem_new(uint8_t count);
void sys_sem_free(sys_sem_t sem);
void sys_sem_signal(sys_sem_t sem);
uint32_t sys_arch_sem_wait(sys_sem_t sem, uint32_t timeout);
#endif
