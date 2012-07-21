/*
 * mm/kmemleak.c
 *
 * Copyright (C) 2008 ARM Limited
 * Written by Catalin Marinas <catalin.marinas@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 *
 * For more information on the algorithm and kmemleak usage, please see
 * Documentation/kmemleak.txt.
 *
 * Notes on locking
 * ----------------
 *
 * The following locks and mutexes are used by kmemleak:
 *
 * - kmemleak_lock (rwlock): protects the object_list modifications and
 *   accesses to the object_tree_root. The object_list is the main list
 *   holding the metadata (struct kmemleak_object) for the allocated memory
 *   blocks. The object_tree_root is a priority search tree used to look-up
 *   metadata based on a pointer to the corresponding memory block.  The
 *   kmemleak_object structures are added to the object_list and
 *   object_tree_root in the create_object() function called from the
 *   kmemleak_alloc() callback and removed in delete_object() called from the
 *   kmemleak_free() callback
 * - kmemleak_object.lock (spinlock): protects a kmemleak_object. Accesses to
 *   the metadata (e.g. count) are protected by this lock. Note that some
 *   members of this structure may be protected by other means (atomic or
 *   kmemleak_lock). This lock is also held when scanning the corresponding
 *   memory block to avoid the kernel freeing it via the kmemleak_free()
 *   callback. This is less heavyweight than holding a global lock like
 *   kmemleak_lock during scanning
 * - scan_mutex (mutex): ensures that only one thread may scan the memory for
 *   unreferenced objects at a time. The gray_list contains the objects which
 *   are already referenced or marked as false positives and need to be
 *   scanned. This list is only modified during a scanning episode when the
 *   scan_mutex is held. At the end of a scan, the gray_list is always empty.
 *   Note that the kmemleak_object.use_count is incremented when an object is
 *   added to the gray_list and therefore cannot be freed. This mutex also
 *   prevents multiple users of the "kmemleak" debugfs file together with
 *   modifications to the memory scanning parameters including the scan_thread
 *   pointer
 *
 * The kmemleak_object structures have a use_count incremented or decremented
 * using the get_object()/put_object() functions. When the use_count becomes
 * 0, this count can no longer be incremented and put_object() schedules the
 * kmemleak_object freeing via an RCU callback. All calls to the get_object()
 * function must be protected by rcu_read_lock() to avoid accessing a freed
 * structure.
 */

/*
 *  Modified by Naredula Janardhana Reddy while porting the code to Jiny
 *  TODO :
 *    1) rcu_locks removed: need to  put back or  locking need to be taken care.
 *    2) enabling /disabling the module on fly without recompiling.
 *    3) scanning for mem leak need to be improved:
 *        a) scan areas nodes to use while scanning, currently they are not used.
 *        b) locking need to be taken care while scanning .
 *        c) traces need to be fine tune(removing kmemcheck functions in the trace)
 *        d) kmemcheck_is_obj_initialized needs architecture dependent code to check if the memory of initialized or not. .(http://lwn.net/Articles/260068/)
 *        e) using jiffies to record the timestamp of the object, and then avoiding scanning of certain objects.
 *        f) keep track of early allocs and free  till the module is up.
 *
 */
#include "os_dep.h"

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt


#include "misc.h"
#define pgoff_t unsigned long

#include "prio_tree.h"
typedef unsigned int u32;
typedef unsigned char u8;
#define bool char
#define true 1
#define false 0

#define pr_warning pr_debug
#define pr_notice pr_debug
#define pr_info pr_debug

#define gfp_t int
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})

#define ULONG_MAX       (~0UL)
/*
 * Kmemleak configuration and common defines.
 */
#define MAX_TRACE		8	/* stack trace length */
#define MSECS_MIN_AGE		5000	/* minimum object age for reporting */
#define SECS_FIRST_SCAN		60	/* delay before the first scan */
#define SECS_SCAN_WAIT		600	/* subsequent auto scanning delay */
#define MAX_SCAN_SIZE		4096	/* maximum size of a scanned block */

#define BYTES_PER_POINTER	sizeof(void *)
#define USE_STATIC_MEMORY 1
/* scanning area inside a memory block */
struct kmemleak_scan_area {
	struct hlist_node node;
	unsigned long start;
	size_t size;
#ifdef USE_STATIC_MEMORY
	struct kmemleak_scan_area *next_free;
#endif
};

#define KMEMLEAK_GREY	0
#define KMEMLEAK_BLACK	-1
extern int memleakHook_disable();
/*
 * Structure holding the metadata for each allocated memory block.
 * Modifications to such objects should be made while holding the
 * object->lock. Insertions or deletions from object_list, gray_list or
 * tree_node are already protected by the corresponding locks or mutex (see
 * the notes on locking above). These objects are reference-counted
 * (use_count) and freed using the RCU mechanism.
 */
struct kmemleak_object {
	struct list_head object_list;
	struct list_head gray_list;
	struct prio_tree_node tree_node;

	unsigned long pointer;
	size_t size;
	unsigned char flags; /* object status flags */
	unsigned long type;
	/* the total number of pointers found pointing to this object */
	int count;

#ifdef USE_STATIC_MEMORY
	struct kmemleak_object *next_free;
#endif
	unsigned long *trace[MAX_TRACE];
};
struct stack_trace {
	unsigned int nr_entries, max_entries;
	unsigned long *entries;
	int skip; /* input argument: How many entries to skip */
};

/* flag representing the memory block allocation status */
#define OBJECT_ALLOCATED	(1 << 0)
/* flag set after the first reporting of an unreference object */
#define OBJECT_REPORTED		(1 << 1)
/* flag set to not scan the object */
#define OBJECT_NO_SCAN		(1 << 2)

/* number of bytes to print per line; must be 16 or 32 */
#define HEX_ROW_SIZE		16
/* number of bytes to print at a time (1, 2, 4, 8) */
#define HEX_GROUP_SIZE		1
/* include ASCII after the hex output */
#define HEX_ASCII		1
/* max number of lines to be printed */
#define HEX_MAX_LINES		2



static void kmemleak_disable(void);

/*
 * Print a warning and dump the stack trace.
 */
#define kmemleak_warn(x...)	do {		\
	pr_warning(x);				\
	dump_stack();				\
	atomic_set(&kmemleak_warning, 1);	\
} while (0)

/*
 * Macro invoked when a serious kmemleak condition occurred and cannot be
 * recovered from. Kmemleak will be disabled and further allocation/freeing
 * tracing no longer available.
 */
#define kmemleak_stop(x...)	do {	\
	kmemleak_warn(x);		\
	kmemleak_disable();		\
} while (0)
static int dump_stack() {
	pr_debug("Dumping stack \n");
	return 1;
}

static inline long IS_ERR(const void *ptr) {/* TODO */
	return 0;
}
#define WARN_ON(x)


/************************   Start of any global variables should be declaired here */
static unsigned long dummy_start_uninitilized_var; /* some unitialized variable */
/* the list of all allocated objects */
static LIST_HEAD(object_list);
/* the list of gray-colored objects (see color_gray comment below) */
static LIST_HEAD(gray_list);
/* prio search tree for object boundaries */
static struct prio_tree_root object_tree_root;
/* rw_lock protecting the access to object_list and prio_tree_root */
//static spinlock_t kmemleak_lock = SPIN_LOCK_UNLOCKED;
static spinlock_t kmemleak_lock = {0};


/* set if tracing memory operations is enabled */
static atomic_t kmemleak_enabled = ATOMIC_INIT(0);
/* set in the late_initcall if there were no errors */
//static atomic_t kmemleak_initialized = ATOMIC_INIT(0);
/* set if a kmemleak warning was issued */
static atomic_t kmemleak_warning = ATOMIC_INIT(0);
/* set if a fatal kmemleak error has occurred */
static atomic_t kmemleak_error = ATOMIC_INIT(0);
static int stat_obj_count,stat_errors;
/* minimum and maximum address that may be valid pointers */
static unsigned long min_addr = ULONG_MAX;
static unsigned long max_addr;


#ifdef USE_STATIC_MEMORY

static struct kmemleak_object objs[MAX_STATIC_OBJS];
static struct kmemleak_scan_area scan_areas[MAX_STATIC_SCAN_AREAS];
static void *object_cache,*scan_area_cache;
#else
/* allocation caches for kmemleak internal data */
static kmem_cache_t *object_cache;
static kmem_cache_t *scan_area_cache;
#endif

static unsigned long dummy_end_uninitilized_var; /* end of uninitialized variable */
/************************   END of any global variables should be declaired here */
#ifdef USE_STATIC_MEMORY
static void *kmem_cache_create(char *type, int unused1 , int unused2 , int unused3, int unused4, int unused5)
{
	int i;

	if (strcmp(type, "kmemleak_objects") == 0) {
		for (i = 0; i < (MAX_STATIC_OBJS - 1); i++) {
			objs[i].next_free = &objs[i + 1];
		}
		objs[MAX_STATIC_OBJS - 1].next_free = 0;
		return (void *)&objs[0];
	} else if (strcmp(type, "kmemleak_scan_area") == 0) {
		for (i = 0; i < (MAX_STATIC_SCAN_AREAS - 1); i++) {
			scan_areas[i].next_free = &scan_areas[i + 1];
		}
		scan_areas[MAX_STATIC_SCAN_AREAS - 1].next_free = 0;
		return (void *)&scan_areas[0];
	} else {
		BUG();
	}
	return 0;
}
static void kmem_cache_free(void *cache, void *object) {
	if (cache == object_cache) {
		struct kmemleak_object *obj = object;
		obj->next_free = object_cache;
		object_cache = obj;
	} else if (cache == scan_area_cache) {
		struct kmemleak_scan_area *obj = object;
		obj->next_free = scan_area_cache;
		scan_area_cache = obj;
	} else {
		BUG();
	}
	return;
}
static void *kmem_cache_alloc(void *cache, void *flag) {
	if (cache == object_cache) {
		struct kmemleak_object *obj = object_cache;
		if (object_cache == 0)
			return 0;
		object_cache = obj->next_free;
		return obj;
	} else if (cache == scan_area_cache) {
		struct kmemleak_scan_area *obj = scan_area_cache;
		if (scan_area_cache == 0)
			return 0;
		scan_area_cache = obj->next_free;
		return obj;
	} else {
		BUG();
	}
	return 0;
}
#endif


extern void *data_region_start,*data_region_end;
/*
 * Object colors, encoded with count and min_count:
 * - white - orphan object, not enough references to it (count < min_count)
 * - gray  - not orphan, not marked as false positive (min_count == 0) or
 *		sufficient references to it (count >= min_count)
 * - black - ignore, it doesn't contain references (e.g. text section)
 *		(min_count == -1). No function defined for this color.
 * Newly created objects don't have any color assigned (object->count == -1)
 * before the next memory scan when they become white.
 */
static bool color_white(const struct kmemleak_object *object) {
	return  object->count < 1;
}

static bool color_gray(const struct kmemleak_object *object) {
	return  object->count >= 1;
}
#define typecheck(type,x) \
  ({      type __dummy; \
          typeof(x) __dummy2; \
          (void)(&__dummy == &__dummy2); \
          1; \
  })
#define time_after_eq(a,b)      \
         (typecheck(unsigned long, a) && \
          typecheck(unsigned long, b) && \
          ((long)(a) - (long)(b) >= 0))
#define time_before_eq(a,b)     time_after_eq(b,a)

/*
 * Print the kmemleak_object information. This function is used mainly for
 * debugging special cases when kmemleak operations. It must be called with
 * the object->lock held.
 */
static void dump_object_info(struct kmemleak_object *object) {
#if 0
	struct stack_trace trace;

	trace.nr_entries = object->trace_len;
	trace.entries = object->trace;
#endif
	pr_notice("Object 0x%08lx (size %zu):\n", object->tree_node.start,
			object->size);
	//pr_notice("ptr:%x   comm \"%s\", pid %d, jiffies %lu\n",object->pointer, object->comm, object->pid,
	//		object->jiffies);
	pr_notice("  count = %d\n", object->count);
	pr_notice("  flags = 0x%lx\n", object->flags);

	//pr_notice("  backtrace:\n");

	//print_stack_trace(&trace, 4);
}

/*
 * Look-up a memory block metadata (kmemleak_object) in the priority search
 * tree based on a pointer value. If alias is 0, only values pointing to the
 * beginning of the memory block are allowed. The kmemleak_lock must be held
 * when calling this function.
 */
static struct kmemleak_object *lookup_object(unsigned long ptr, int alias) {
	struct prio_tree_node *node;
	struct prio_tree_iter iter;
	struct kmemleak_object *object;

	prio_tree_iter_init(&iter, &object_tree_root, ptr, ptr);
	node = prio_tree_next(&iter);
	if (node) {
		object = prio_tree_entry(node, struct kmemleak_object,
				tree_node);
		if (!alias && object->pointer != ptr) {
			//kmemleak_warn("Found object by alias at 0x%08lx\n",ptr);
			dump_object_info(object);
			object = NULL;
		}
	} else
		object = NULL;

	return object;
}

#if 0

/*
 * RCU callback to free a kmemleak_object.
 */
static void free_object(struct kmemleak_object *object) {
	kmem_cache_free(object_cache, object);
}


/*
 * Decrement the object use_count. Once the count is 0, free the object using
 * an RCU callback. Since put_object() may be called via the kmemleak_free() ->
 * delete_object() path, the delayed RCU freeing ensures that there is no
 * recursive call to the kernel allocator. Lock-less RCU object_list traversal
 * is also possible.
 */
static void put_object(struct kmemleak_object *object) {
	free_object(object);
}
#endif

/*
 * Look up an object in the prio search tree and increase its use_count.
 */
static struct kmemleak_object *find_and_get_object(unsigned long ptr, int alias) {
	unsigned long flags;
	struct kmemleak_object *object = NULL;

	spin_lock_irqsave(&kmemleak_lock, flags);
	if (ptr >= min_addr && ptr < max_addr)
		object = lookup_object(ptr, alias);
	spin_unlock_irqrestore(&kmemleak_lock, flags);

	return object;
}

static void save_stack_trace(struct stack_trace *trace) {
	unsigned long addr;
	unsigned long *stack_top = &addr;
	unsigned long sz, stack_end, code_end;
	int i;

	sz = (long) stack_top;
	sz = sz / 4;
	sz = sz * 4;
	stack_top = (unsigned long *) sz;
	i = 0;
	sz = (TASK_SIZE - 1);
	sz = ~sz;
	stack_end = (unsigned long) stack_top & (sz);
	stack_end = stack_end + TASK_SIZE - 10;
	code_end = (unsigned long)&data_region_start;
	//ut_printf("\nCALL Trace: %x  code_end:%x  %x :%x  \n",stack,code_end,stack_top,stack_end);

	if (stack_end) {
		while (((unsigned long)stack_top < stack_end) && i < MAX_TRACE) {
			addr = *stack_top;
			stack_top =  stack_top + 1;
			if ((addr > 0x103000) && (addr < code_end)) {
				trace->entries[i] = addr;
				i++;
			}
		}
		trace->nr_entries = i;
	}
}
/*
 * Save stack trace to the given array of MAX_TRACE size.
 */
static unsigned int __save_stack_trace(unsigned long **trace) {
	struct stack_trace stack_trace;

	stack_trace.max_entries = MAX_TRACE;
	stack_trace.nr_entries = 0;
	stack_trace.entries = (unsigned long *)trace;
	stack_trace.skip = 2;
	save_stack_trace(&stack_trace);

	return stack_trace.nr_entries;
}

/*
 * Create the metadata (struct kmemleak_object) corresponding to an allocated
 * memory block and add it to the object_list and object_tree_root.
 */
static struct kmemleak_object *create_object(unsigned long ptr, size_t size, int type,
		int min_count) {
	unsigned long flags;
	struct kmemleak_object *object;
	struct prio_tree_node *node;

	object = kmem_cache_alloc(object_cache, 0);
	if (!object) {
		pr_warning("Cannot allocate a kmemleak_object structure\n");
		kmemleak_disable();
		return NULL;
	}

	stat_obj_count++;
	INIT_LIST_HEAD(&object->object_list);
	INIT_LIST_HEAD(&object->gray_list);
	object->flags = OBJECT_ALLOCATED;
	object->pointer = ptr;
	object->size = size;
	object->type = (unsigned char)type;
	object->count = 0; /* white color initially */

	/* kernel backtrace */
   __save_stack_trace(&object->trace[0]);

	INIT_PRIO_TREE_NODE(&object->tree_node);
	object->tree_node.start = ptr;
	object->tree_node.last = ptr + size - 1;

	spin_lock_irqsave(&kmemleak_lock, flags); /* global lock */

	min_addr = min(min_addr, ptr);
	max_addr = max(max_addr, ptr + size);
	node = prio_tree_insert(&object_tree_root, &object->tree_node);
	/*
	 * The code calling the kernel does not yet have the pointer to the
	 * memory block to be able to free it.  However, we still hold the
	 * kmemleak_lock here in case parts of the kernel started freeing
	 * random memory blocks.
	 */

	if (node != &object->tree_node)  /* Faile to insert in to tree */
	{
#if 0
		kmemleak_stop("Cannot insert %x  len:%d into the object search tree "
		"(already existing)\n", ptr, size);
		object = lookup_object(ptr, 1);
		spin_lock(&object->lock);
		dump_object_info(object);
		spin_unlock(&object->lock);
#else
		stat_errors++;
		stat_obj_count--;
		kmem_cache_free(object_cache, object);
#endif
		goto out;
	}

	list_add_tail(&object->object_list, &object_list);
out:
	spin_unlock_irqrestore(&kmemleak_lock, flags);
	return object;
}

/*
 * Look up the metadata (struct kmemleak_object) corresponding to ptr and
 * delete it.
 */
static void delete_object(unsigned long ptr) {
	struct kmemleak_object *object;

	object = find_and_get_object(ptr, 0);
	if (!object) {
//#ifdef DEBUG
		pr_debug("ERROR Freeing unknown object at %x \n", ptr);
//#endif
		return;
	}
	unsigned long flags;

	spin_lock_irqsave(&kmemleak_lock, flags);
	prio_tree_remove(&object_tree_root, &object->tree_node);
	list_del(&object->object_list);
	spin_unlock_irqrestore(&kmemleak_lock, flags);

	object->flags &= ~OBJECT_ALLOCATED;
    object->pointer = 0;
	kmem_cache_free(object_cache, object);
	stat_obj_count--;
}

/*
 * Add a scanning area to the object. If at least one such area is added,
 * kmemleak will only scan these ranges rather than the whole memory block.
 */
void add_scan_area(unsigned long ptr, size_t size) {
	struct kmemleak_object *object;
	struct kmemleak_scan_area *area;

	object = find_and_get_object(ptr, 1);
	if (!object) {
		kmemleak_warn("Adding scan area to unknown object at 0x%08lx\n", ptr);
		return;
	}

	area = kmem_cache_alloc(scan_area_cache, 0);
	if (!area) {
		pr_warning("Cannot allocate a scan area\n");
		goto out;
	}


	if (ptr + size > object->pointer + object->size) {
		kmemleak_warn("Scan area larger than object 0x%08lx\n", ptr);
		dump_object_info(object);
		kmem_cache_free(scan_area_cache, area);
		goto out_unlock;
	}

	INIT_HLIST_NODE(&area->node);
	area->start = ptr;
	area->size = size;

	out_unlock:

	out:
	return;
}

/**
 * kmemleak_alloc - register a newly allocated object
 * @ptr:	pointer to beginning of the object
 * @size:	size of the object
 * @min_count:	minimum number of references to this object. If during memory
 *		scanning a number of references less than @min_count is found,
 *		the object is reported as a memory leak. If @min_count is 0,
 *		the object is never reported as a leak. If @min_count is -1,
 *		the object is ignored (not scanned and not reported as a leak)
 * @gfp:	kmalloc() flags used for kmemleak internal memory allocations
 *
 * This function is called from the kernel allocators when a new object
 * (memory block) is allocated (kmem_cache_alloc, kmalloc, vmalloc etc.).
 */
static void kmemleak_alloc(const void *ptr, int size, int type, void *cachep) {
	int min_count = 1;
	//pr_debug("%s(0x%p, %zu, %d)\n", __func__, ptr, size, min_count);
	if ((cachep == scan_area_cache) || (cachep == object_cache)) { /* this is to avoid infinite recursion */
		return;
	}
	if (atomic_read(&kmemleak_enabled) && ptr && !IS_ERR(ptr)){
		create_object((unsigned long) ptr, size, type, min_count);
	}
}

/**
 * kmemleak_free - unregister a previously registered object
 * @ptr:	pointer to beginning of the object
 *
 * This function is called from the kernel allocators when an object (memory
 * block) is freed (kmem_cache_free, kfree, vfree etc.).
 */
static void kmemleak_free(const void *ptr, void *cachep) {
	//pr_debug("%s(0x%p)\n", __func__, ptr);
	if ((cachep == scan_area_cache) || (cachep == object_cache)) { /* this is to avoid infinite recursion */
		return;
	}
	if (atomic_read(&kmemleak_enabled) && ptr && !IS_ERR(ptr))
		delete_object((unsigned long) ptr);
}
static void kmemleak_update(const void *ptr,unsigned long type) {
	struct kmemleak_object *object;

	object = find_and_get_object((unsigned long)ptr, 0);
	if (object!= 0) {
       object->type =type;
	}

	return;
}

/*
 * Scan a memory block (exclusive range) for valid pointers and add those
 * found to the gray list.
 */
static void _scan_block(void *_start, void *_end,
		struct kmemleak_object *scanned, int allow_resched) {
	unsigned char *ptr;
//	unsigned long *start = PTR_ALIGN(_start, BYTES_PER_POINTER);
	unsigned long *start,*p_long;
	unsigned long *end = _end - (BYTES_PER_POINTER - 1);

	start = (unsigned long *)(((long) _start / 8) * 8);
	for (ptr = (unsigned char*)start; ptr < (unsigned char *)end; ptr=ptr+4) {
		struct kmemleak_object *object;
		unsigned long pointer;

#if 0
		/* don't scan uninitialized memory */
		if (!kmemcheck_is_obj_initialized((unsigned long)ptr,
						BYTES_PER_POINTER))
		continue;
#endif
		p_long=(unsigned long*)ptr;
		pointer = *p_long;

		object = find_and_get_object(pointer, 1);
		if (!object)
			continue;
		if (object == scanned) {
			continue;
		}

		/*
		 * Avoid the lockdep recursive warning on object->lock being
		 * previously acquired in scan_object(). These locks are
		 * enclosed by scan_mutex.
		 */

		if (!color_white(object)) {
			continue;
		}

		/*
		 * Increase the object's reference count (number of pointers
		 * to the memory block). If this count reaches the required
		 * minimum, the object's color will become gray and it will be
		 * added to the gray_list.
		 */
		object->count++;
		if (color_gray(object)) {
			list_add_tail(&object->gray_list, &gray_list);
			continue;
		}

	}
}
static void scan_block(void *_start, void *_end,
		struct kmemleak_object *scanned, int allow_resched) {

	if ((_start < (void *)&dummy_start_uninitilized_var) && ((void *)&dummy_end_uninitilized_var < _end)){
		_scan_block(_start, &dummy_start_uninitilized_var, scanned, 1);
		_scan_block(&dummy_end_uninitilized_var, _end, scanned, 1);
	}else{
	    _scan_block(_start, _end, scanned, 1);
	}
	return;
}
/*
 * Scan a memory block corresponding to a kmemleak_object. A condition is
 * that object->use_count >= 1.
 */
static void scan_object(struct kmemleak_object *object) {
	/*
	 * Once the object->lock is acquired, the corresponding memory block
	 * cannot be freed (the same lock is acquired in delete_object).
	 */
	if (object->flags & OBJECT_NO_SCAN)
		goto out;
	if (!(object->flags & OBJECT_ALLOCATED))
		/* already freed object */
		goto out;
	if (1) {
		void *start = (void *) object->pointer;
		void *end = (void *) (object->pointer + object->size);

		while (start < end && (object->flags & OBJECT_ALLOCATED)
				&& !(object->flags & OBJECT_NO_SCAN)) {
			scan_block(start, min(start + MAX_SCAN_SIZE, end), object, 0);
			start += MAX_SCAN_SIZE;

		}
	}
	out:
	return;
}

/*
 * Scan the objects already referenced (gray objects). More objects will be
 * referenced and, if there are no memory leaks, all the objects are scanned.
 */
static void scan_gray_list(void) {
	struct kmemleak_object *object, *tmp;
	/*
	 * The list traversal is safe for both tail additions and removals
	 * from inside the loop. The kmemleak objects cannot be freed from
	 * outside the loop because their use_count was incremented.
	 */
	object = list_entry(gray_list.next, typeof(*object), gray_list);
	while (&object->gray_list != &gray_list) {
		//cond_resched();

		scan_object(object);

		tmp = list_entry(object->gray_list.next, typeof(*object),
				gray_list);

		/* remove the object from the list and release it */
		list_del(&object->gray_list);

		object = tmp;
	}
	WARN_ON(!list_empty(&gray_list));
}

/*
 * Scan data sections and all the referenced memory blocks allocated via the
 * kernel's standard allocators. This function must be called with the
 * scan_mutex held.
 */

void kmemleak_scan(void) {
	struct kmemleak_object *object;
	unsigned long flags;
	int new_leaks = 0;
	int total_obj = 0;

	atomic_set(&kmemleak_enabled, 0);
	memleakHook_disable();

	spin_lock_irqsave(&kmemleak_lock, flags); /* global lock , this is to freeze memory while scanning */
	/* prepare the kmemleak_object's */
	list_for_each_entry(object, &object_list, object_list) {
		/* reset the reference count (whiten the object) */
		object->count = 0;
		total_obj++;
	}

	dummy_start_uninitilized_var=0;
	dummy_end_uninitilized_var=0;
	/* data/bss scanning */
	scan_block(data_region_start, data_region_end + 8, NULL, 1);

	/*
	 * Scan the objects already referenced from the sections scanned
	 * above.
	 */
	scan_gray_list();

	/*
	 * Re-scan the gray list for modified unreferenced objects.
	 */
	scan_gray_list();
	spin_unlock_irqrestore(&kmemleak_lock, flags);

	/*
	 * Scanning result reporting.
	 */

	list_for_each_entry(object, &object_list, object_list) {
#if 1
		if (object->count==0) {
			if (object->type !=0) {
				char *p=(char *)object->type;
				pr_debug(" object addr:%x  size:%d type:%s\n",object->pointer,object->size,p);
			}
		}
#endif
		if ((object->count==0) && !(object->flags & OBJECT_REPORTED)) {
			object->flags |= OBJECT_REPORTED;
			new_leaks++;
		}
	}

	pr_debug("New3.0 Total Leaks : %d  total obj:%d stat_total:%d error:%d\n", new_leaks, total_obj,stat_obj_count,stat_errors);

}

/*
 * Disable kmemleak. No memory allocation/freeing will be traced once this
 * function is called. Disabling kmemleak is an irreversible operation.
 */
static void kmemleak_disable(void) {
	/* stop any memory operation tracing */
	atomic_set(&kmemleak_enabled, 0);
	pr_info("Kernel memory leak detector disabled\n");
}

extern int memleakHook_copyFromEarlylog(void (*palloc)(const void *ptr, int size, int type, void *cachep), void (*pfree)(const void *ptr, void *cachep),void (*pupdate)(const void *ptr, unsigned long type));
/*
 * Kmemleak initialization.
 */
void kmemleak_init(void) {
	unsigned long flags;

	prio_tree_init();
	stat_obj_count=0;
	stat_errors=0;
	object_cache = kmem_cache_create("kmemleak_objects",
			sizeof(struct kmemleak_object), 0, 0, 0, 0);
	scan_area_cache = kmem_cache_create("kmemleak_scan_area",
			sizeof(struct kmemleak_scan_area), 0, 0, 0, 0);
	INIT_PRIO_TREE_ROOT(&object_tree_root);

	/* the kernel is still in UP mode, so disabling the IRQs is enough */
	local_irq_save(flags);
	if (atomic_read(&kmemleak_error)) {
		local_irq_restore(flags);
		return;
	} else
		atomic_set(&kmemleak_enabled, 1);
	local_irq_restore(flags);

	if (memleakHook_copyFromEarlylog(kmemleak_alloc, kmemleak_free, kmemleak_update)==0){
		atomic_set(&kmemleak_enabled, 0);
	}

	pr_debug("1.0 Initializing KmemLEAK total objects:%d  object size:%d\n",stat_obj_count,sizeof(struct kmemleak_object));
}

