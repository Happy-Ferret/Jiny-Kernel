#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H



#define __sti() __asm__ __volatile__ ("sti": : :"memory")
#define __cli() __asm__ __volatile__ ("cli": : :"memory")

#define __LOCK_PREFIX "lock "

#if 1
#define BITS_PER_LONG 64
#define warn_if_not_ulong(x) do { unsigned long foo; (void) (&(x) == &foo); } while (0)
#define __save_flags(x)         do { warn_if_not_ulong(x); __asm__ __volatile__("# save_flags \n\t pushfq ; popq %q0":"=g" (x): /* no input */ :"memory"); } while (0)
#define __restore_flags(x)      __asm__ __volatile__("# restore_flags \n\t pushq %0 ; popfq": /* no output */ :"g" (x):"memory", "cc")
#endif


#define cli() __cli()
#define sti() __sti()
#define save_flags(x) __save_flags(x)
#define restore_flags(x) __restore_flags(x)


#define __SPINLOCK_LOCKED   1
#define __SPINLOCK_UNLOCKED 0

#ifdef SPINLOCK_DEBUG
#define SPIN_LOCK_UNLOCKED(x) (spinlock_t) { __SPINLOCK_UNLOCKED,0,x,0,0,0,0,-1}
#define MAX_SPINLOCKS 100
extern spinlock_t *g_spinlocks[MAX_SPINLOCKS];
extern int g_spinlock_count;
#else
#define SPIN_LOCK_UNLOCKED (spinlock_t) { __SPINLOCK_UNLOCKED }
#endif


static inline void arch_spinlock_lock(spinlock_t *lock, int line) {
#ifdef SPINLOCK_DEBUG
#ifdef RECURSIVE_SPINLOCK
	if (lock->pid == g_current_task->pid  && g_current_task->pid!=0) {
		lock->recursive_count++;
		lock->stat_recursive_locks++;
	}else{
#else
	if (1){
#endif
#endif
	__asm__ __volatile__( "movq $0,%%rbx\n"
			"mov  %0,%%edx\n"
			"spin:addq $1,%%rbx\n"
			"mov   %1, %%eax\n"
			"test %%eax, %%eax\n"
			"jnz  spin\n"
			"lock cmpxchg %%edx, %1 \n"
			"test  %%eax, %%eax\n"
			"jnz  spin\n"
			"mov %%rbx, %3\n"
			:: "r"(__SPINLOCK_LOCKED),"m"(lock->lock), "rI"(__SPINLOCK_UNLOCKED),"m"(lock->stat_count)
			: "%rax","%rbx", "memory" );
#ifdef SPINLOCK_DEBUG
	lock->stat_locks++;

	if (lock->name!=0 && lock->linked == -1 && g_spinlock_count<MAX_SPINLOCKS) {
		lock->linked = 0;
		g_spinlocks[g_spinlock_count]=lock;
		g_spinlock_count++;
	}

	if (lock->log_length >= MAX_SPIN_LOG) lock->log_length=0;
	lock->log[lock->log_length].line = line;
	lock->log[lock->log_length].pid = g_current_task->pid;
	lock->log[lock->log_length].cpuid = g_current_task->cpu;
	lock->log[lock->log_length].spins = 1 + (lock->stat_count/10);
	lock->log_length++;
	lock->pid = g_current_task->pid;
	lock->contention=lock->contention+(lock->stat_count/10);
  }/* toplevel if */
#endif
}
static inline void arch_spinlock_unregister(spinlock_t *lock){

#ifdef SPINLOCK_DEBUG
	int i;

	for (i=0; i<MAX_SPINLOCKS; i++) {
		if (g_spinlocks[i]==lock) {
			g_spinlocks[i]=0;
			return;
		}
	}
#endif

}
static inline void arch_spinlock_unlock(spinlock_t *lock, int line) {
#ifdef SPINLOCK_DEBUG
#ifdef RECURSIVE_SPINLOCK
	if (lock->pid == g_current_task->pid && lock->recursive_count>0) {
		lock->recursive_count--;
	} else {
#else
	if (1){
#endif

		lock->stat_unlocks++;
		if (lock->log_length >= MAX_SPIN_LOG) lock->log_length=0;
		lock->log[lock->log_length].line = line;
		lock->log[lock->log_length].pid = g_current_task->pid;
		lock->log[lock->log_length].cpuid = g_current_task->cpu;
		lock->log[lock->log_length].spins = 0;
		lock->log_length++;
		lock->pid = 0;
		lock->recursive_count = 0;
#endif

	__asm__ __volatile__( __LOCK_PREFIX "xchgl %0, %1\n"
			:: "r"(__SPINLOCK_UNLOCKED), "m"( lock->lock )
			: "memory" );
#ifdef SPINLOCK_DEBUG
     } /* top level if */
#endif

}


#define spin_lock_init(x)       do { (x)->lock = __SPINLOCK_UNLOCKED; } while (0)
#define spin_trylock(lock)      (!test_and_set_bit(0,(lock)))

#define spin_lock(x)            do { arch_spinlock_lock(x,__LINE__); } while (0)
#define spin_unlock(x)          do { arch_spinlock_unlock(x,__LINE__); } while (0)


#define spin_lock_irq(x)        do { cli(); spin_lock(x); } while (0)
#define spin_unlock_irq(x)      do { spin_unlock(x); sti(); } while (0)

#define local_irq_save(flags) \
        do { save_flags(flags); cli(); } while (0)
#define local_irq_restore( flags) \
        do {  restore_flags(flags); } while (0)

#define spin_lock_irqsave(x, flags) \
        do { save_flags(flags); spin_lock_irq(x); } while (0)
#define spin_unlock_irqrestore(x, flags) \
        do { spin_unlock(x); restore_flags(flags); } while (0)

#endif
