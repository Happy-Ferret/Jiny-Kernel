/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
*   kernel/task.c
*   Naredula Janardhana Reddy  (naredula.jana@gmail.com, naredula.jana@yahoo.com)
*
*/
//#define DEBUG_ENABLE 1
#include "common.h"
#include "mm.h"


/********************* Data Structures *****************************/
#define NR_MEM_LISTS 8
#define memory_head(x) ((struct page *)(x))

/* The start of this MUST match the start of "struct page" */
struct free_mem_area_struct {
	struct page *next;
	struct page *prev;
	unsigned int *map;
	unsigned int stat_count; /* jana Added */
};
page_struct_t *g_mem_map = NULL;
unsigned long g_max_mapnr=0;
int g_nr_free_pages = 0;
static struct free_mem_area_struct free_mem_area[NR_MEM_LISTS];
static spinlock_t free_area_lock = SPIN_LOCK_UNLOCKED("memory_freearea");

extern unsigned long g_phy_mem_size;

/*********************** local function *******************************/

static inline void init_mem_queue(struct free_mem_area_struct * head)
{
	head->next = memory_head(head);
	head->prev = memory_head(head);
	head->stat_count=0;
}

static inline void add_mem_queue(struct free_mem_area_struct * head, struct page * entry)
{
	struct page * next = head->next;

	entry->prev = memory_head(head);
	entry->next = next;
	next->prev = entry;
	head->next = entry;
	head->stat_count++;
}

static inline void remove_mem_queue(struct free_mem_area_struct *area,struct page * entry)
{
	struct page * next = entry->next;
	struct page * prev = entry->prev;
	next->prev = prev;
	prev->next = next;
	area->stat_count--;
}

/*
 * Free_page() adds the page to the free lists. This is optimized for
 * fast normal cases (no error jumps taken normally).
 *
 * The way to optimize jumps for gcc-2.2.2 is to:
 *  - select the "normal" case and put it inside the if () { XXX }
 *  - no else-statements if you can avoid them
 *
 * With the above two rules, you get a straight-line execution path
 * for the normal case, giving better asm-code.
 */

/*
 * Buddy system. Hairy. You really aren't expected to understand this
 *
 * Hint: -mask = 1+~mask
 */

static inline void _free_pages_ok(unsigned long map_nr, unsigned long order)
{
	struct free_mem_area_struct *area = free_mem_area + order;
	unsigned long index = map_nr >> (1 + order);
	unsigned long mask = (~0UL) << order;


#define list(x) (g_mem_map+(x))

	map_nr &= mask;
	g_nr_free_pages -= mask;
	while (mask + (1 << (NR_MEM_LISTS-1))) {
		if (!test_and_change_bit(index, area->map))
			break;
		remove_mem_queue(area,list(map_nr ^ -mask));
		mask <<= 1;
		area++;
		index >>= 1;
		map_nr &= mask;
	}
	add_mem_queue(area, list(map_nr));

#undef list

}

/*
 * Some ugly macros to speed up mm_getFreePages()..
 */
#define MARK_USED(index, order, area) \
	change_bit((index) >> (1+(order)), (area)->map)
#define CAN_DMA(x) (PageDMA(x))
//#define ADDRESS(x) (KERNEL_CODE_START + ((x) << PAGE_SHIFT))
#define ADDRESS(x) (KADDRSPACE_START + ((x) << PAGE_SHIFT))

#define EXPAND(map,index,low,high,area) \
	do { unsigned long size = 1 << high; \
		while (high > low) { \
			area--; high--; size >>= 1; \
			add_mem_queue(area, map); \
			MARK_USED(index, high, area); \
			index += size; \
			map += size; \
		} \
		atomic_set(&map->count, 1); \
	} while (0)

int low_on_memory = 0;


#define LONG_ALIGN(x) (((x)+(sizeof(long))-1)&~((sizeof(long))-1))

/*
 * set up the free-area data structures:
 *   - mark all pages reserved
 *   - mark all memory queues empty
 *   - clear the memory bitmaps
 */
static unsigned long init_free_area(unsigned long start_mem, unsigned long end_mem)
{
	page_struct_t *p;
	unsigned long mask = PAGE_MASK;
	unsigned long i;
	unsigned long start_addrspace = KADDRSPACE_START;
	int size;

	/*
	 * Select nr of pages we try to keep free for important stuff
	 * with a minimum of 10 pages and a maximum of 256 pages, so
	 * that we don't waste too much memory on large systems.
	 * This is fairly arbitrary, but based on some behaviour
	 * analysis.
	 */
	INIT_LOG("	init_free_area start_mem: %x endmem:%x   \n",start_mem,end_mem);
	i = (end_mem - start_addrspace) >> (PAGE_SHIFT+7);
	if (i < 10)
		i = 10;
	if (i > 256)
		i = 256;
	/*TODO freepages.min = i;
	  freepages.low = i * 2;
	  freepages.high = i * 3;*/
	g_mem_map = (page_struct_t *) LONG_ALIGN(start_mem+8);
	INIT_LOG("	g_mem_map :%x  size:%x  \n",g_mem_map,MAP_NR(end_mem));
	p = g_mem_map + MAP_NR(end_mem);
	start_mem = LONG_ALIGN((unsigned long) p);
	size=(start_mem -(unsigned long) g_mem_map);
	INIT_LOG(" freearemap setup map: %x diff:%x(%dM)   \n",g_mem_map,(start_mem -(unsigned long) g_mem_map),size/(1024*1024));
	//while(1);
	ut_memset((unsigned char *)g_mem_map, 0, start_mem -(unsigned long) g_mem_map);
	do {
		--p;
		atomic_set(&p->count, 0);
		p->flags = (1 << PG_DMA) | (1 << PG_reserved) ;
	} while (p > g_mem_map);

	for (i = 0 ; i < NR_MEM_LISTS ; i++) {
		unsigned long bitmap_size;
		init_mem_queue(free_mem_area+i);
		mask += mask;
		end_mem = (end_mem + ~mask) & mask;
		bitmap_size = (end_mem - start_addrspace) >> (PAGE_SHIFT + i);
		bitmap_size = (bitmap_size + 7) >> 3;
		bitmap_size = LONG_ALIGN(bitmap_size);
		free_mem_area[i].map = (unsigned int *) start_mem;
		ut_memset((void *) start_mem, 0, bitmap_size);
		start_mem += bitmap_size;
		INIT_LOG("		%d : bitmapsize:%x end_mem:%x \n",i,bitmap_size,end_mem);
	}
	return start_mem;
}
static int init_done=0;
static unsigned long stat_mem_size=0;
static unsigned long stat_allocs=0;
static unsigned long stat_frees=0;
static void init_mem(unsigned long start_mem, unsigned long end_mem, unsigned long virt_start_addr){
	int reservedpages = 0;
	unsigned long tmp;

	end_mem &= PAGE_MASK;
	g_max_mapnr  = MAP_NR(end_mem);

	INIT_LOG("	first page : %x :%x :%x\n",MAP_NR(start_mem),MAP_NR(start_mem+PAGE_SIZE),MAP_NR(virt_start_addr));
	start_mem = PAGE_ALIGN(start_mem);
	stat_mem_size = end_mem -start_mem;
	while (start_mem < end_mem) {
		clear_bit(PG_reserved, &g_mem_map[MAP_NR(start_mem)].flags);
		start_mem += PAGE_SIZE;
	}
	for (tmp = virt_start_addr ; tmp < (end_mem - 0x2000) ; tmp += PAGE_SIZE) {
		/*if (tmp >= MAX_DMA_ADDRESS)
		  clear_bit(PG_DMA, &g_mem_map[MAP_NR(tmp)].flags);*/
		if (PageReserved(g_mem_map+MAP_NR(tmp))) {
			reservedpages++;
			continue;
		}
		atomic_set(&g_mem_map[MAP_NR(tmp)].count, 1);
		PageSetReferenced(g_mem_map+MAP_NR(tmp));
		mm_putFreePages(tmp,0);
	}
	stat_allocs=0;
	stat_frees =0;
	INIT_LOG("	Reserved pages : %x(%d) \n",reservedpages,reservedpages);
	init_done=1;
	INIT_LOG("	Release to FREEMEM : %x \n",(end_mem - 0x2000));
	return;
}
/*****************************************************************  API functions */
#if 0
int mm_check_debug_data(unsigned long addr, unsigned long data){
#ifdef MEMORY_DEBUG
	unsigned long flags;
	spin_lock_irqsave(&free_area_lock, flags);

	unsigned long map_nr = MAP_NR(addr);
	page_struct_t *page = g_mem_map + map_nr;
	if ( page->option_data!=data){
		BUG();
	}

	spin_unlock_irqrestore(&free_area_lock, flags);
#endif
}
int mm_set_debug_data(unsigned long addr, unsigned long data){
#ifdef MEMORY_DEBUG
	unsigned long flags;
	spin_lock_irqsave(&free_area_lock, flags);

	unsigned long map_nr = MAP_NR(addr);
	page_struct_t *page = g_mem_map + map_nr;
	if (data==0xffff && page->option_data==0x0){
		BUG();
	}
	if (data==0x0 && page->option_data==0x0){
		BUG();
	}
	page->option_data = data;
	spin_unlock_irqrestore(&free_area_lock, flags);
#endif
}
#endif
int mm_putFreePages(unsigned long addr, unsigned long order) {
	unsigned long map_nr = MAP_NR(addr);
	int ret = 0;
	int page_order = order;
	unsigned long flags;

	stat_frees++;
#ifdef MEMLEAK_TOOL
	memleakHook_free(addr,0);
#endif
	spin_lock_irqsave(&free_area_lock, flags);
	if (map_nr < g_max_mapnr) {
		page_struct_t * map = g_mem_map + map_nr;
		if (PageReserved(map)) {
			BUG();
		}
#ifdef MEMORY_DEBUG
		if (map->option_data != 0){
			BUG();
		}
#endif
		if (atomic_dec_and_test(&map->count)) {
			if (PageSwapCache(map)){
				ut_log("PANIC Freeing swap cache pages");
				BUG();
			}
		//	map->flags &= ~(1 << PG_referenced);
			_free_pages_ok(map_nr, order);
			if (init_done == 1) {
				DEBUG(" Freeing memory addr:%x order:%d \n", addr, order);
			}else{
			//	BUG();
			}
			ret = 1;
		}
	}else{
		BUG();
	}
last:
	if (ret){
		unsigned long i = (1 << page_order);
		struct page *page = virt_to_page(addr);

		while (i--) {
#ifdef MEMORY_DEBUG
			if (!PageReferenced(page)){
				ut_printf("Page Backtrace in Free Page :\n");
				ut_printBackTrace(page->bt_addr_list,MAX_BACKTRACE_LENGTH);
			}
#endif
			assert(PageReferenced(page));
			PageClearReferenced(page);
#ifdef MEMORY_DEBUG
			ut_storeBackTrace(page->bt_addr_list,MAX_BACKTRACE_LENGTH);
#endif
			page++;
		}
	}else{
		BUG();
	}
	spin_unlock_irqrestore(&free_area_lock, flags);
	return ret;
}

unsigned long mm_getFreePages(int gfp_mask, unsigned long order) {
	unsigned long flags;
	unsigned long ret_address;
	unsigned long page_order ;

	stat_allocs++;
	ret_address = 0;
	page_order = order;
	if (order >= NR_MEM_LISTS)
		return ret_address;

	spin_lock_irqsave(&free_area_lock, flags);
	do {
		struct free_mem_area_struct * area = free_mem_area+order;
		unsigned long new_order = order;
		do { struct page *prev = memory_head(area), *ret = prev->next;
			while (memory_head(area) != ret) {
				if ( CAN_DMA(ret)) {
					unsigned long map_nr;
					(prev->next = ret->next)->prev = prev;
					map_nr = ret - g_mem_map;
					MARK_USED(map_nr, new_order, area);
					area->stat_count--;
					g_nr_free_pages -= 1 << order;
					EXPAND(ret, map_nr, order, new_order, area);
					DEBUG(" Page alloc return address: %x mask:%x order:%d \n",ADDRESS(map_nr),gfp_mask,order);
					if (gfp_mask & MEM_CLEAR) ut_memset(ADDRESS(map_nr),0,PAGE_SIZE<<order);
					if (!(gfp_mask & MEM_FOR_CACHE)) memleakHook_alloc(ADDRESS(map_nr),PAGE_SIZE<<order,0,0);
					ret_address = ADDRESS(map_nr);
					goto last;
				}
				prev = ret;
				ret = ret->next;
			}
			new_order++; area++;
		} while (new_order < NR_MEM_LISTS);
	} while (0);


last:
	if (ret_address > 0) {
		unsigned long i = (1 << page_order);
		struct page *page = virt_to_page(ret_address);

		while (i--) {
#ifdef MEMORY_DEBUG
			if (PageReferenced(page)){
				ut_log("Page Backtrace in Alloc page :\n");
				ut_printBackTrace(page->bt_addr_list,MAX_BACKTRACE_LENGTH);
			}
#endif
			assert(!PageReferenced(page));
			PageSetReferenced(page);
#ifdef MEMORY_DEBUG
			ut_storeBackTrace(page->bt_addr_list,MAX_BACKTRACE_LENGTH);
#endif
			page++;
		}
	}
	spin_unlock_irqrestore(&free_area_lock, flags);

	if (ret_address ==0) return ret_address;
	if ((ret_address >= (KADDRSPACE_START+g_phy_mem_size)) || (ret_address < KADDRSPACE_START)){
		ut_log(" ERROR:  frames execeeding the max frames :%x\n",ret_address);
		BUG();
	}

	return ret_address;
}


extern unsigned long _start,_end;
extern addr_t end; // end of code and data region
unsigned long 	g_pagecache_size = 0x10000000 /4 ;
extern unsigned long symbols_end;

#if 1
unsigned long test_mem_start = 0;
unsigned long test_mem_end = 0;
#endif

addr_t initialise_paging_new(addr_t physical_mem_size, unsigned long virt_image_end, unsigned long *virt_addr_start, unsigned long *virt_addr_end) ;
int init_memory(unsigned long arg1){
	unsigned long virt_real_start_addr,virt_start_addr,virt_end_addr;
	unsigned long pc_size;
	unsigned long current_end_memused=&end; /* till this point memory is used*/

	unsigned long phy_end_addr = g_phy_mem_size;
	ut_log("	Initializing memory phy_endaddr : %x  current end:%x symbols_end:%x\n",phy_end_addr,current_end_memused,symbols_end);
	if (symbols_end != 0){
		current_end_memused = symbols_end;
	}

	virt_start_addr=initialise_paging_new(phy_end_addr, current_end_memused,&virt_real_start_addr,&virt_end_addr);
	INIT_LOG("	After Paging initialized Virtual start_addr: %x virtual endaddr: %x  current end:%x virtualreal_start:%x\n",virt_start_addr,virt_end_addr,current_end_memused,virt_real_start_addr);
	INIT_LOG("	code+data  : %x  -%x size:%dK",&_start,&_end);
	INIT_LOG("	free area  : %x - %x size:%dM\n",virt_start_addr,virt_end_addr,(virt_end_addr-virt_start_addr)/1000000);
	virt_start_addr=init_free_area( virt_start_addr, virt_end_addr);

	pc_size = g_pagecache_size ;
	pc_init((unsigned char *)virt_start_addr,pc_size);
	ut_log("	pagecache  : %x - %x size:%dM",virt_start_addr,virt_start_addr+pc_size,pc_size/1000000);

	virt_start_addr=virt_start_addr+pc_size;

#if 1 /* reserve the test memory for testing */
	if (virt_end_addr > (virt_start_addr + (600*1024*1024))){
	test_mem_start = virt_start_addr;
	test_mem_end  = virt_start_addr + 512 *1024 *1204 ; /* 512M */
	virt_start_addr =  test_mem_end + 4096;
	}
#endif

	init_mem(virt_start_addr, virt_end_addr, virt_real_start_addr);
	INIT_LOG("		buddy pages: %x - %x size:%dM\n",virt_start_addr, virt_end_addr,(virt_end_addr-virt_start_addr)/1000000);
	return JSUCCESS;
}
/*
 * Show free area list (used inside shift_scroll-lock stuff)
 * We also calculate the percentage fragmentation. We do this by counting the
 * memory on each free list with the exception of the first item on the list.
 */
int Jcmd_mem(char *arg1, char *arg2) {
	unsigned long order, flags;
	unsigned long total = 0;

	spin_lock_irqsave(&free_area_lock, flags);
	for (order = 0; order < NR_MEM_LISTS; order++) {
		struct page * tmp;
		unsigned long nr = 0;
		for (tmp = free_mem_area[order].next;
				tmp != memory_head(free_mem_area+order); tmp = tmp->next) {
			nr++;
		}
		total += nr << order;
		ut_printf("%d(%d): count:%d  static count:%d total:%d (%dM)\n", order,1<<order, nr,
				free_mem_area[order].stat_count, (nr << order), ((nr << order)*PAGE_SIZE)/(1024*1024));
	}
	spin_unlock_irqrestore(&free_area_lock, flags);
	ut_printf("total Free pages = %d (%dM) Actual pages: %d (%dM) pagecachesize: %dM \n", total, (total * 4) / 1024,stat_mem_size/PAGE_SIZE,stat_mem_size/(1024*1024),g_pagecache_size/(1024*1024));

	int slab=0;
	int referenced=0;
	int reserved=0;
//	int free=0;
	int dma=0;
	unsigned long va_end=(unsigned long)__va(g_phy_mem_size);

	page_struct_t *p;
	p = g_mem_map + MAP_NR(va_end);
	do {
		--p;
		if (PageReserved(p)) reserved++;
		if (PageDMA(p)) dma++;
		if (PageReferenced(p))referenced++;
		if (PageSlab(p)) slab++;
	} while (p > g_mem_map);
	ut_printf(" reserved :%d(%dM) referenced:%d dma:%d slab:%d  stat_allocs:%d stat_frees: %d\n\n",reserved,(reserved*PAGE_SIZE)/(1024*1024),referenced,dma,slab,stat_allocs,stat_frees);
	if ((arg1 != 0) && (ut_strcmp(arg1,"all")==0))
		Jcmd_jslab(0,0);
	return 1;
}
int Jcmd_testmem(char *arg1, char *arg2){
	int i;
	int size=40000;
	if (arg1){
		size=ut_atoi(arg1,FORMAT_DECIMAL);
	}
	for (i=0; i<size; i++){
		mm_getFreePages(0, 0);
	}
	ut_printf(" Test created (size=%d): %dM \n",size,size*PAGE_SIZE/(1024*1024));
	Jcmd_mem(0,0);
	return 1;
}
