/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
*   fs/vfs.c
*   Naredula Janardhana Reddy  (naredula.jana@gmail.com, naredula.jana@yahoo.com)
*
*/
//#define DEBUG_ENABLE 1
#include "common.h"
#include "mm.h"
#include "vfs.h"
#include "interface.h"


static struct filesystem *vfs_fs = 0;
static kmem_cache_t *slab_inodep;
static LIST_HEAD(inode_list);
void *g_inode_lock = 0; /* protects inode_list */
//spinlock_t g_userspace_stdio_lock = SPIN_LOCK_UNLOCKED("userspace_stdio");

#define OFFSET_ALIGN(x) ((x/PC_PAGESIZE)*PC_PAGESIZE) /* the following 2 need to be removed */

kmem_cache_t *g_slab_filep;
static void inode_sync(struct inode *inode, unsigned long truncate);

static int fs_create_pipe(struct file *wfp, struct file *rfp);
static int fs_dup_pipe(struct file *fp);
static int fs_destroy_pipe(struct file *fp);
static int fs_recv_from_pipe(struct file *fp, uint8_t *buf, int len);
static int fs_send_to_pipe(struct file *fp,uint8_t *buf, int len);


static int inode_init(struct inode *inode, uint8_t *filename,
		struct filesystem *vfs) {
	int i;

	if (inode == NULL)
		return 0;
	inode->count.counter = 1;
	inode->nrpages = 0;
	inode->stat_locked_pages.counter = 0;

	inode->flags = INODE_LONGLIVED;
	inode->u.file.stat.st_size = 0;
	inode->fs_private = 0;
	inode->u.file.stat.inode_no = 0;
	inode->u.file.stat_insync = 0;
	inode->type = 0;
	inode->vfs = vfs;
	ut_strcpy(inode->filename, (uint8_t *) filename);
	for (i=0; i<PAGELIST_HASH_SIZE; i++)
		INIT_LIST_HEAD(&(inode->page_list[i]));
	INIT_LIST_HEAD(&(inode->vma_list));
	INIT_LIST_HEAD(&(inode->inode_link));
	DEBUG(" inode init filename:%s: :%x  :%x \n", filename, &inode->page_list, &(inode->page_list));

	mutexLock(g_inode_lock);
	list_add(&inode->inode_link, &inode_list);
	mutexUnLock(g_inode_lock);

	return 1;
}

/*************************** API functions ************************************************/
/* TODO :  inode lock need to used in all the places where inode is used */

int Jcmd_ls(uint8_t *arg1, uint8_t *arg2) {
	struct inode *tmp_inode;
	struct list_head *p;
	int i,len,max_len;
	uint8_t *buf;
	int total_pages = 0;
	const uint8_t *type;

	ut_printf("usagecount nrpages length  inode_no type/type  name (in/out/err)\n");
	len = PAGE_SIZE*100;
	max_len=len;
	buf = (uint8_t *) vmalloc(len,0);
	if (buf == 0) {
		ut_printf(" Unable to get vmalloc memory\n");
		return 0;
	}

	mutexLock(g_inode_lock);
	list_for_each(p, &inode_list) {
		tmp_inode = list_entry(p, struct inode, inode_link);
		if (tmp_inode->flags & INODE_EXECUTING) type=(uint8_t *)"*";
		else type=(uint8_t *)" ";
		if (tmp_inode->type == NETWORK_FILE) {
			len = len
					- ut_snprintf(buf + max_len - len, len,
							"%d: %4d %4d(%2d) %8d %8d %2x/%x %s", i,
							tmp_inode->count, tmp_inode->nrpages,
							tmp_inode->stat_locked_pages.counter, 0,
							tmp_inode->u.file.stat.inode_no, tmp_inode->type,
							tmp_inode->flags, type);
		} else {
			len = len
					- ut_snprintf(buf + max_len - len, len,
							"%d: %4d %4d(%2d) %8d %8d %2x/%x %s", i,
							tmp_inode->count, tmp_inode->nrpages,
							tmp_inode->stat_locked_pages.counter,
							tmp_inode->u.file.stat.st_size,
							tmp_inode->u.file.stat.inode_no, tmp_inode->type,
							tmp_inode->flags, type );
		}
		len = len - ut_snprintf(buf + max_len - len, len,"(%d/%d/%d-%s\n",tmp_inode->stat_in,tmp_inode->stat_out,tmp_inode->stat_err,tmp_inode->filename);
		total_pages = total_pages + tmp_inode->nrpages;
		i++;
		if (len < 500) break;
	}
	mutexUnLock(g_inode_lock);
	len = len - ut_snprintf(buf+max_len-len, len,"Total pages :%d\n", total_pages);

	len = ut_strlen(buf);
	SYS_fs_write(1,buf,len);

	vfree(buf);
	return 1;
}
int Jcmd_clear_pagecache() {
	struct inode *tmp_inode;
	struct list_head *p;

	mutexLock(g_inode_lock);  // Need to implement Recursive lock
	list_for_each(p, &inode_list) {
		tmp_inode = list_entry(p, struct inode, inode_link);
		ut_printf("%s %d %d %d \n", tmp_inode->filename, tmp_inode->count,
				tmp_inode->nrpages, tmp_inode->u.file.stat.st_size);
		fs_fadvise(tmp_inode, 0, 0, POSIX_FADV_DONTNEED);
	}
	mutexUnLock(g_inode_lock);

	return 1;
}
int fs_data_sync(int num_pages) {
	int ret;
	page_struct_t *page;
	int write_count=0;

	while (1) {
		page = pc_get_dirty_page();
		if (page == 0){
			goto last;
		}
		struct inode *inode = page->inode;
		uint64_t len = inode->u.file.stat.st_size;
		if (len < (page->offset + PC_PAGESIZE)) {
			len = len - page->offset;
		} else {
			len = PC_PAGESIZE;
		}
		if ((len>PC_PAGESIZE) || (len <0)){
			ut_log(" Error in data_sync len:%x  offset:%x inode_len:%x \n",len,page->offset,inode->u.file.stat.st_size);
			len = PC_PAGESIZE;
		}
		assert (page->magic_number == PAGE_MAGIC);
		ret = vfs_fs->write(page->inode, page->offset, pcPageToPtr(page), len);
		if (ret == len) {
			pc_pagecleaned(page);
			write_count++;
		}
		pc_put_page(page);
		if (num_pages>0 && write_count>num_pages){
			goto last;
		}
	}
last:
	return write_count;
}

int fs_sync(){
	struct inode *tmp_inode;
	struct list_head *p;
	struct fileStat stat;

	fs_data_sync(0);

restart:

   /* sync the file system with host */
	mutexLock(g_inode_lock);
	list_for_each(p, &inode_list) {
		tmp_inode = list_entry(p, struct inode, inode_link);
		if (vfs_fs->stat(tmp_inode, &stat) != JSUCCESS) {
			if (fs_putInode(tmp_inode)) {
				mutexUnLock(g_inode_lock);
				goto restart;
			}
		}
	}
	mutexUnLock(g_inode_lock);

	return SYSCALL_SUCCESS;
}

int SYS_sync(){
	return fs_sync();
}
int Jcmd_sync() {
	return fs_sync();
}
/************************************************************************************************/
static void transform_filename(uint8_t *arg_filename, char *filename) {
	int i, len;

	filename[0] = 0;
	if (arg_filename[0] != '/') {
		ut_strcpy((uint8_t *)filename, (const uint8_t *)g_current_task->mm->fs.cwd);
	}
	i = 0;

	/* remove "./" from input file */
	if (arg_filename[0] == '.' && arg_filename[1] == '/')
		i = 2;

	len = ut_strlen((const uint8_t *)filename);
	if (filename[len - 1] != '/') {
		ut_strcat((const uint8_t *)filename, (uint8_t *)"/");
	}
	ut_strcat((uint8_t *)filename, &arg_filename[i]); //TODO use strncat

	len = ut_strlen((const uint8_t *)filename);

	/* remove "/" as last character */
	if (filename[len - 1] == '/') {
		filename[len - 1] = 0;
	}

	DEBUG(" opening in :%s:  transformfile :%s: \n", arg_filename, filename);

}
static struct inode *fs_getInode(uint8_t *arg_filename, int flags, int mode) {
	struct inode *ret_inode, *tmp_inode;
	struct list_head *p;
	int ret;
	uint8_t filename[MAX_FILENAME];

	ret_inode = 0;
	transform_filename(arg_filename, filename);

	mutexLock(g_inode_lock);
	list_for_each(p, &inode_list) {
		ret_inode = list_entry(p, struct inode, inode_link);
		if (ut_strcmp((uint8_t *) filename, ret_inode->filename) == 0) {
			atomic_inc(&ret_inode->count);
			mutexUnLock(g_inode_lock);
			goto last;
		}
	}
	mutexUnLock(g_inode_lock);

	ret_inode = mm_slab_cache_alloc(slab_inodep, 0);
	if (ret_inode != 0) {
		struct fileStat stat;

		inode_init(ret_inode, filename, vfs_fs);
		ret_inode->u.file.open_mode = mode & 0x3;
#if 1
		ret = vfs_fs->open(ret_inode, flags, ret_inode->u.file.open_mode);
		if (ret == JFAIL) {
			ret_inode->count.counter = 0;
			if (fs_putInode(ret_inode) == 0) {
				BUG();
			}
			ret_inode = 0;
			goto last;
		}
		DEBUG(" filename:%s: %x\n", ret_inode->filename, ret_inode);
		if (vfs_fs->stat(ret_inode, &stat) == JSUCCESS) { /* this is to get inode number */
			ret_inode->u.file.stat.inode_no = stat.inode_no;



			mutexLock(g_inode_lock);
			list_for_each(p, &inode_list) {
				tmp_inode = list_entry(p, struct inode, inode_link);
				if (tmp_inode == ret_inode)
					continue;
				if (ret_inode->u.file.stat.inode_no == tmp_inode->u.file.stat.inode_no
						&& ut_strcmp(ret_inode->filename, tmp_inode->filename)
								== 0) {
					ret_inode->count.counter = 0;
					if (fs_putInode(ret_inode) == 0) {
						BUG();
					}
					ret_inode = tmp_inode;
				}
			}
			mutexUnLock(g_inode_lock);
		} else {
			ut_printf("ERROR: cannot able to get STAT\n");
		}
#endif

		atomic_inc(&ret_inode->count);
	}

	last: return ret_inode;
}

unsigned long fs_putInode(struct inode *inode) {
	int ret = 0;

	if (inode == 0)
		return ret;
	mutexLock(g_inode_lock);
	if (inode != 0)
		atomic_dec(&inode->count);
	if (inode != 0 && inode->nrpages == 0 && inode->count.counter <= 0) {
		list_del(&inode->inode_link);
		ret = 1;
	} else {
		if (inode->count.counter == 1) {
			inode->flags = inode->flags & (~INODE_EXECUTING);
		}
	}
	mutexUnLock(g_inode_lock);

	if (ret == 1)
		mm_slab_cache_free(slab_inodep, inode);

	return ret;
}

struct file *fs_open(uint8_t *filename, int flags, int mode) {
	struct file *filep;
	struct inode *inodep;

	if (vfs_fs == 0 || filename == 0)
		return 0;

	filep = mm_slab_cache_alloc(g_slab_filep, 0);
	if (filep == 0)
		goto error;
	if (filename != 0) {
		ut_strcpy(filep->filename, filename);
	} else {
		goto error;
	}
	/* special files handle here before going to regular files */
	if (ut_strcmp((uint8_t *) filename, (uint8_t *) "/dev/sockets")
			== 0) {
		filep->inode = mm_slab_cache_alloc(slab_inodep, 0);
		if (filep->inode == NULL) goto error;
		inode_init(filep->inode, (uint8_t *)"/dev/sockets", 0);
		filep->offset = 0;
		filep->type = NETWORK_FILE;
		filep->inode->type = NETWORK_FILE;
		return filep;
	} else {
		filep->type = REGULAR_FILE;
	}

	inodep = fs_getInode((uint8_t *) filep->filename, mode, flags);
	if (inodep == 0)
		goto error;

	filep->inode = inodep;
	if (flags & O_APPEND) {
		filep->offset = inodep->u.file.stat.st_size;
	} else {
		filep->offset = 0;
	}
	if (flags & O_CREAT) {
		if ((inodep->u.file.stat.st_size >0) && (filep->offset==0)){
			inode_sync(inodep,1); /* truncate the file */
		}
	}
	return filep;

error:
	if (filep != NULL)
		mm_slab_cache_free(g_slab_filep, filep);
	if (inodep != NULL) {
		fs_putInode(inodep);
	}
	return 0;
}

unsigned long SYS_fs_open(char *filename, int mode, int flags) {
	struct file *filep;
	int total;
	int i;
	int ret= SYSCALL_FAIL;

	SYSCALL_DEBUG("open: filename :%s: mode:%x flags:%x \n", filename, mode, flags);
	if (ut_strcmp((uint8_t *) filename, (uint8_t *) "/dev/tty") == 0) {
		ret = 1;
		goto last;
	}
	if (ut_strcmp((uint8_t *) filename, (uint8_t *) "/dev/null") == 0) {
		filep  = mm_slab_cache_alloc(g_slab_filep, 0);
		filep->type = DEV_NULL_FILE;
	}else{
		filep = (struct file *) fs_open((uint8_t *) filename, flags, mode);
	}
	total = g_current_task->mm->fs.total;
	if (filep != 0 && total < MAX_FDS) {
		for (i = 3; i < MAX_FDS; i++) { /* fds: 0,1,2 are for in/out/error */
			if (g_current_task->mm->fs.filep[i] == 0) {
				break;
			}
		}
		if (i == MAX_FDS){
			goto fail;
		}
		g_current_task->mm->fs.filep[i] = filep;
		if (i >= total)
			g_current_task->mm->fs.total = i + 1;

		ret = i;
		goto last;
	}
fail:
	if (filep != 0)
		fs_close(filep);

last:
	SYSCALL_DEBUG("open return value: %d \n", ret);
	return ret;
}
/*********************************************************************************************/
static void inode_sync(struct inode *inode, unsigned long truncate) {
	struct page *page;
	int ret;
	unsigned long offset;

	if (truncate){
		ret = vfs_fs->setattr(inode, 0);
		fs_fadvise(inode,0,0,POSIX_FADV_DONTNEED);
		return;
	}

	for (offset = 0; offset < inode->u.file.stat.st_size; offset = offset + PAGE_SIZE) {
		page = pc_getInodePage(inode, offset);
		if (page == NULL)
			continue;
		if (PageDirty(page)) {
			uint64_t len = inode->u.file.stat.st_size;
			if (len < (page->offset + PC_PAGESIZE)) {
				len = len - page->offset;
			} else {
				len = PC_PAGESIZE;
			}
			if (len > 0) {
				assert (page->magic_number == PAGE_MAGIC);
				ret = vfs_fs->write(inode, page->offset, pcPageToPtr(page),
						len);
				if (ret == len) {
					pc_pagecleaned(page);
				}
			}
		}
		pc_put_page(page);
	}
}

unsigned long fs_fdatasync(struct file *filep) {
	struct inode *inode;

	if (vfs_fs == 0 || filep == 0)
		return 0;

	inode = filep->inode;
	inode_sync(inode,0);

	return 0;
}
unsigned long SYS_fs_fdatasync(unsigned long fd) {
	struct file *file;

	SYSCALL_DEBUG("fdatasync fd:%d \n", fd);
	file = fd_to_file(fd);
	return fs_fdatasync(file);
}
/*****************************************************************************************************/
unsigned long fs_lseek(struct file *file, unsigned long offset, int whence) {
	if (vfs_fs == 0)
		return 0;
	if (file == 0)
		return 0;
	return vfs_fs->lseek(file, offset, whence);
}

unsigned long SYS_fs_lseek(unsigned long fd, unsigned long offset, int whence) {
	struct file *file;

	SYSCALL_DEBUG("lseek fd:%d offset:%x whence:%x \n", fd, offset, whence);
	file = fd_to_file(fd);
	if (vfs_fs == 0 || file == 0)
		return -1;
	if (whence == SEEK_SET) {
		file->offset = offset;
	} else if (whence == SEEK_CUR) {
		file->offset = file->offset + offset;
	} else
		return -1;

	return file->offset;
}
unsigned long fs_readdir(struct file *file, struct dirEntry *dir_ent,
		int len, int *offset) {
	if (vfs_fs == 0)
		return 0;
	if (file == 0)
		return 0;
	if (file->inode && file->inode->type != DIRECTORY_FILE) {
		ut_log("ERROR: SYS_getdents  file type error\n");
		return -1;
		//BUG();
	}
	return vfs_fs->readDir(file->inode, dir_ent, len,  offset);
}
/************************************************************************************************/

int fs_write(struct file *filep, uint8_t *buff, unsigned long len) {
	int i, ret;
	int tmp_len, size, page_offset;
	struct page *page;

	ret = 0;
	if (vfs_fs == 0 || (vfs_fs->write == 0) || (filep == 0))
		return 0;

	if (ar_check_valid_address((unsigned long )buff,len)==JFAIL){
		BUG();
	}

	if (filep->type == DEV_NULL_FILE){
		return len;
	}
	if (filep->type == OUT_FILE) {
	//	spin_lock_irqsave(&g_userspace_stdio_lock, flags);
		for (i = 0; i < len; i++) {
			ut_putchar((int) buff[i]);
		}
	//	spin_unlock_irqrestore(&g_userspace_stdio_lock, flags);
		return len;
	}else if (filep->type == OUT_PIPE_FILE) {
		return fs_send_to_pipe(filep, buff, len);
	}else if (filep->type == IN_PIPE_FILE ){
		ut_log(" ERROR: Using INPIPE instead of OUTPIPE\n");
		return -1;
	}

	DEBUG("Write  filename from hs  :%s: offset:%d inode:%x \n", filep->filename, filep->offset, filep->inode);
	tmp_len = 0;

	if (filep->inode && filep->inode->type == DIRECTORY_FILE) { //TODO : check for testing
		BUG();
	}
	if (filep->offset > (filep->inode->u.file.stat.st_size)) return -1;
#if 0
	if (filep->inode->u.file.open_mode == O_RDONLY){
		return -1;
	}
#endif
	while (tmp_len < len) {
		try_again:
		page = pc_getInodePage(filep->inode, filep->offset);
		if (page == NULL) {
			page = pc_getFreePage();
			if (page == NULL) {
				ret = -3;
				goto error;
			}
			page->offset = OFFSET_ALIGN(filep->offset);
			if (pc_insertPage(filep->inode, page) == JFAIL) {
				pc_putFreePage(page);
				ut_log("Fail to insert page \n");
				ret = -1;
				goto error;
			}else{
				goto try_again;
			}
		}
		size = PC_PAGESIZE;
		if (size > (len - tmp_len))
			size = len - tmp_len;
		page_offset = filep->offset - page->offset;
		if ((page_offset<0 || page_offset>PC_PAGESIZE) || size < 0 || size> PC_PAGESIZE) {
			ut_log("ERROR in write: offset:%x page_offset:%x diff:%x \n",filep->offset,page->offset,page_offset);
			while(1);
		}
		ut_memcpy(pcPageToPtr(page) + page_offset, buff + tmp_len, size);
		pc_pageDirted(page);
		pc_put_page(page);

		filep->offset = filep->offset + size;
		struct inode *inode = filep->inode;
		if (inode->u.file.stat.st_size < filep->offset)
			inode->u.file.stat.st_size = filep->offset;
		tmp_len = tmp_len + size;
		if (pc_is_freepages_available()==0){
			pc_housekeep();
			//inode_sync(inode,0);
		}
		DEBUG("write memcpy :%x %x  %d \n", buff, pcPageToPtr(page), size);
	}
	ret = tmp_len;
error:
	if (ret < 0){
		ut_log(" fs_write fails error:%x pid:%d \n",ret,g_current_task->pid);
		return 0;
	}
	return ret;
}

int SYS_fs_write(unsigned long fd, uint8_t *buff, unsigned long len) {
	struct file *file;
	int ret;

	//SYSCALL_DEBUG("write fd:%d buff:%x len:%x data:%s:\n", fd, buff, len,buff);
	file = fd_to_file(fd);

	if (file == 0){
		ut_log("ERROR write fd:%d buff:%x len:%x \n", fd, buff, len);
		return -1; // TODO : temporary
	}
	if (file->type == NETWORK_FILE) {
		return socket_write(file, buff, len);
	}
	ret = fs_write(file, buff, len);
	SYSCALL_DEBUG("write return : fd:%d ret:%d \n",fd,ret);
	return ret;
}
long SYS_fs_writev(int fd, const struct iovec *iov, int iovcnt) {
	int i;
	long ret, tret;
	struct file *file;

	SYSCALL_DEBUG("writev: fd:%d iovec:%x count:%d\n", fd, iov, iovcnt);

	file = fd_to_file(fd);
	ret = 0;
	for (i = 0; i < iovcnt; i++) {
		if (fd == 1 || fd == 2) {
			ut_printf("write %s\n", iov[i].iov_base);/* TODO need to terminate the buf with \0  */
			ret = ret + iov[i].iov_len;
			continue;
		}
		tret = fs_write(file, iov[i].iov_base, iov[i].iov_len);
		if (tret > 0)
			ret = ret + tret;
		else
			goto last;
	}
	last: return ret;
}
/*******************************************************************************************************/
struct page *fs_genericRead(struct inode *inode, unsigned long offset) {
	struct page *page;
	int tret;
	int err = 0;
	if (inode ==0){
		BUG();
	}
retry_again:  /* the purpose to retry is to get again from the page cache with page count incr */
	page = pc_getInodePage(inode, offset);
	if (page == NULL) {
		page = pc_getFreePage();
		if (page == NULL) {
			err = -3;
			goto error;
		}
		page->offset = OFFSET_ALIGN(offset);
		assert (page->magic_number == PAGE_MAGIC);
		tret = inode->vfs->read(inode, page->offset, pcPageToPtr(page),
				PC_PAGESIZE);

		if (tret > 0) {
			if (pc_insertPage(inode, page) == JFAIL) {
				pc_putFreePage(page);
				err = -5;
				goto error;
			}
			if ((tret + offset) > inode->u.file.stat.st_size)
				inode->u.file.stat.st_size = offset + tret;
		} else {
			pc_putFreePage(page);
			err = -4;
			goto error;
		}
		goto retry_again;
	}

	error: if (err < 0) {
		DEBUG(" Error in reading the file :%i \n", -err);
		page = 0;
	}
	return page;
}

#define CTRL_C 3
#define CTRL_D 4
long fs_read(struct file *filep, uint8_t *buff, unsigned long len) {
	int ret;
	struct page *page;
	struct inode *inode;


	ret = 0;
	if ((vfs_fs == 0) || (filep == 0))
		return 0;

	if (ar_check_valid_address((addr_t)buff,len)==JFAIL){
		BUG();
	}
	if (filep->type == OUT_FILE) {
		ut_log(" ERROR: read on OUT_FILE : name: %s\n",g_current_task->name);
		//return -1;
		BUG();
	}
	if (filep->type == IN_FILE) {
		if (buff == 0 || len == 0)
			return 0;
		buff[0] = dr_kbGetchar(g_current_task->mm->fs.input_device);

		if (buff[0]==CTRL_D || buff[0]==CTRL_C){
			ut_log(" RECVIED special character :%x thread name :%s:\n",buff[0],g_current_task->name);
		}

		if (buff[0]==CTRL_D || buff[0]==CTRL_C){/* for CTRL-D */
			return 0;
		}
		if (buff[0] == 0xd)
			buff[0] = '\n';
		buff[1] = '\0';
		ut_printf("%s", buff);
		//	SYSCALL_DEBUG("read char :%s: %x:\n",buff,buff[0]);
		return 1;
	}else if (filep->type == IN_PIPE_FILE) {
		return fs_recv_from_pipe(filep, buff, len);
	}if (filep->type == OUT_PIPE_FILE) {
		ut_log(" ERROR: looks like using OUT_PIPE_FILE instead of IN_PIPE_FILE \n");
		return -1;
	}

	DEBUG("Read filename from hs  :%s: offset:%d inode:%x buff:%x len:%x \n", filep->filename, filep->offset, filep->inode, buff, len);
	inode = filep->inode;
	//TODO 	if (inode->length <= filep->offset) return 0;

	page = fs_genericRead(filep->inode, filep->offset);
	if (page == 0){
		goto last;
	}

	ret = PC_PAGESIZE;
	ret = ret - (filep->offset - OFFSET_ALIGN(filep->offset));
	if (ret > len)
		ret = len;
	if ((filep->offset + ret) > inode->u.file.stat.st_size) {
		int r;
		r = inode->u.file.stat.st_size - filep->offset;
		if (r < ret)
			ret = r;
	}
	if (page > 0 && ret > 0) {
		ut_memcpy(buff, pcPageToPtr(page)
						+ (filep->offset - OFFSET_ALIGN(filep->offset)), ret);
		filep->offset = filep->offset + ret;
		DEBUG(" memcpy :%x %x  %d \n", buff, pcPageToPtr(page), ret);
	}
	pc_put_page(page);

last:
	return ret;
}

long SYS_fs_readv(int fd, const struct iovec *iov, int iovcnt) {
	int i;
	long ret, tret;
	struct file *file;

	SYSCALL_DEBUG("readv: fd:%d iovec:%x count:%d\n", fd, iov, iovcnt);
	file = fd_to_file(fd);
	ret = 0;
	for (i = 0; i < iovcnt; i++) {
		tret = fs_read(file, iov[i].iov_base, iov[i].iov_len);
		if (tret > 0)
			ret = ret + tret;
		else
			goto last;
	}
	last: return ret;
}
int SYS_fs_read(unsigned long fd, uint8_t *buff, unsigned long len) {
	struct file *file;
	int ret;

	SYSCALL_DEBUG("read fd:%d buff:%x len:%x \n",fd,buff,len);

	file = fd_to_file(fd);
	if (file == 0){
		ut_log("ERRO read fd:%d buff:%x len:%x \n", fd, buff, len);
		return 0;
	}

	if (file->type == NETWORK_FILE) {
		return socket_read(file, buff, len);
	} else if (file->type == DEV_NULL_FILE) {
		//ut_memset(buff,0,len-1);
		return len;
	}
	if ((vfs_fs == 0) || (vfs_fs->read == 0))
		return 0;

	ret = fs_read(file, buff, len);
	SYSCALL_DEBUG("read ret :%d\n",ret);
	return ret;
}
/***************************************************************************************************/
struct file *fs_dup(struct file *old_filep, struct file *new_filep) {

	if (old_filep == 0)
		return 0;
	if (new_filep == 0) {
		new_filep = mm_slab_cache_alloc(g_slab_filep, 0);
	}

	/* 1. close the resources of  new */
	if (new_filep->type == REGULAR_FILE || new_filep->type == NETWORK_FILE) {
		fs_putInode(new_filep->inode);
	}else if (new_filep->type == OUT_PIPE_FILE || new_filep->type == IN_PIPE_FILE) {
		fs_destroy_pipe(new_filep);
	}

	/* 2. copy from old */
	ut_memcpy((uint8_t *)new_filep, (uint8_t *)old_filep, sizeof(struct file));
	if ((new_filep->type == REGULAR_FILE || new_filep->type == NETWORK_FILE) && new_filep->inode != 0) {
		atomic_inc(&new_filep->inode->count);
	}else if (new_filep->type == OUT_PIPE_FILE || new_filep->type == IN_PIPE_FILE) {
		fs_dup_pipe(new_filep);
	}
	return new_filep;
}
int SYS_fs_dup2(int fd_old, int fd_new);
int SYS_fs_dup(int fd_old) {
	int i;
	for (i = 3; i < MAX_FDS; i++) { /* fds: 0,1,2 are for in/out/error */
		if (g_current_task->mm->fs.filep[i] == 0) {
			break;
		}
	}
	if (i==MAX_FDS) return SYSCALL_FAIL;
	return SYS_fs_dup2(fd_old, i);
}
int SYS_fs_dup2(int fd_old, int fd_new) {
	int ret = fd_new; /* on success */
	struct file *fp_new, *fp_old;
	SYSCALL_DEBUG("dup2  fd_new:%x fd_old:%x \n", fd_new, fd_old);
	fp_new = fd_to_file(fd_new);
	fp_old = fd_to_file(fd_old);
	if ( fp_old == 0){
		ret = SYSCALL_FAIL;
		goto last;
	}
	fp_new = fs_dup(fp_old, fp_new);
	if (fp_new ==0){
		ret = SYSCALL_FAIL;
	}
	g_current_task->mm->fs.filep[fd_new] = fp_new;

last:
	SYSCALL_DEBUG("dup2 Return ret:%d new_fd:%d \n",ret,fd_new);
	return ret;
}
/*************************************************************************************************/
int fs_close(struct file *filep) {
	if (filep == 0 || vfs_fs == 0)
		return 0;
	if (filep->type == REGULAR_FILE) {
		if (filep->inode != 0) {
			//ret = vfs_fs->close(filep->inode);
			fs_putInode(filep->inode);
		}
	} else if (filep->type == NETWORK_FILE) {
		socket_close(filep);
	} else if ((filep->type == OUT_FILE) || (filep->type == IN_FILE) || (filep->type == DEV_NULL_FILE)) { /* nothng todo */
		//ut_log("Closing the IO file :%x name :%s: \n",filep,g_current_task->name);
	} else if (filep->type == OUT_PIPE_FILE || filep->type == IN_PIPE_FILE) {
		fs_destroy_pipe(filep);
// TODO : need to release kernel resource
	}
	filep->inode = 0;
	mm_slab_cache_free(g_slab_filep, filep);
	return SYSCALL_SUCCESS;
}

unsigned long SYS_fs_close(unsigned long fd) {
	struct file *file;

	SYSCALL_DEBUG("close fd:%d \n", fd);
	if (fd == 1) {
		SYSCALL_DEBUG("close fd:%d NOT CLOSING Temporary fix \n", fd);
		return 0; //TODO : temporary
	}
	file = fd_to_file(fd);
	if (file == 0)
		return SYSCALL_FAIL;
	g_current_task->mm->fs.filep[fd] = 0;

	return fs_close(file);
}
/************************************************************************************************/
static int vfsRemove(struct file *filep) {
	int ret;

	ret = vfs_fs->remove(filep->inode);
	if (ret == 1) {
		if (filep->inode != 0)
			fs_putInode(filep->inode);
		filep->inode = 0;
		mm_slab_cache_free(g_slab_filep, filep);
	}
	return ret;
}

int fs_remove(struct file *file) {
	if (vfs_fs == 0)
		return 0;
	return vfsRemove(file);
}
/**************************************************************************************************/
int fs_stat(struct file *filep, struct fileStat *stat) {
	int ret;
	if (vfs_fs == 0 || filep == 0)
		return 0;
	if (filep->inode && filep->inode->u.file.stat_insync == 1 ) {
		DEBUG(" stat filename :%s: \n",filep->filename);
		ret = JSUCCESS;
		ut_memcpy(stat,&(filep->inode->u.file.stat),sizeof(struct fileStat));
	} else {
		//BRK;
		ret = vfs_fs->stat(filep->inode, stat);
		if (ret == JSUCCESS) {
			stat->st_size = filep->inode->u.file.stat.st_size;
//TODO
		}
	}
	return ret;
}

/*************************************************************/
unsigned long fs_fadvise(struct inode *inode, unsigned long offset,
		unsigned long len, int advise) {
	struct page *page;
	int ret;
	int count=0;

	//TODO : implementing other advise types
	if (advise == POSIX_FADV_DONTNEED && len == 0) {
		while (offset < inode->u.file.stat.st_size) /* delete all the pages in the inode */
		{
			ret  = JFAIL;

			page = pc_getInodePage(inode,offset);
			offset=offset+PAGE_SIZE;
			if (page == 0) continue;

			mutexLock(g_inode_lock);
			pc_put_page(page);
			if (page->count.counter > 0){//TODO: need to handle this case, this will happen for paralle write into same file
				//ut_log(" ERROR in Truncate the file: page is in Use: %d \n",page->count.counter);
				while(1);
			}
			ret = pc_deletePage(page);
			mutexUnLock(g_inode_lock);

			if (ret==JSUCCESS){
				count++;
			}
		}
	}
	return count;
}

unsigned long SYS_fs_fadvise(unsigned long fd, unsigned long offset,
		unsigned long len, int advise) {
	struct file *file;
	struct inode *inode;

	SYSCALL_DEBUG(
			"fadvise fd:%d offset:%d len:%d advise:%x \n", fd, offset, len, advise);
	file = fd_to_file(fd);
	if (file == 0 || file->inode == 0)
		return 0;
	inode = file->inode;
	return fs_fadvise(inode, offset, len, advise);
}
/******************************************************************************/
unsigned long fs_getVmaPage(struct vm_area_struct *vma, unsigned long offset) {
	struct page *page;
	struct inode *inode;
	unsigned long ret;

	inode = vma->vm_inode;
	page = fs_genericRead(inode, offset);
	if (page == NULL)
		return 0;
	pc_put_page(page); /* The file will be marked as executable, the pages will be locked at the file level */

	ret = (unsigned long )pc_page_to_ptr(page);
	ret = __pa(ret);
	//DEBUG(
	//		" mapInodepage phy addr :%x  hostphyaddr:%x offset:%x diff:%x \n", ret, g_hostShmPhyAddr, offset, (to_ptr(page)-pc_startaddr));
	return ret; /* return physical address */
}
/***************************************** pipe ****************************************/
#define MAX_PIPES 100
struct {
#define MAX_PIPE_BUFS 20
	struct {
		uint8_t *data;
		int size;
		int start_offset;
	} bufs[MAX_PIPE_BUFS];
	int in_index, out_index;
	int count;
} pipes[MAX_PIPES];
static int init_pipe_done = 0;
/* TODO : locking need to added */
struct file *fs_create_filep(int *fd) {
	int i;
	struct file *fp;

	for (i = 3; i < MAX_FDS; i++) {
		if (g_current_task->mm->fs.filep[i] == 0) {
			fp = mm_slab_cache_alloc(g_slab_filep, 0);
			if (fp == 0)
				return 0;
			if (i >= g_current_task->mm->fs.total) {
				g_current_task->mm->fs.total = i + 1;
			}
			g_current_task->mm->fs.filep[i] = fp;
			*fd = i;
			return fp;
		}
	}
	return 0;
}
int fs_destroy_filep(int fd) {
	struct file *fp;
	if (fd < 0 || fd > MAX_FDS) {
		return -1;
	}
	fp = g_current_task->mm->fs.filep[fd];
	g_current_task->mm->fs.filep[fd] = 0;
	mm_slab_cache_free(g_slab_filep, fp);
	return 0;
}
unsigned long SYS_pipe(int *fds) {
	struct file *wfp, *rfp;
	int ret;

	SYSCALL_DEBUG("pipe  fds:%x \n", fds);
	if (fds == 0)
		return -1;

	wfp = fs_create_filep(&fds[1]);
	if (wfp != 0) {
		rfp = fs_create_filep(&fds[0]);
		if (rfp == 0) {
			mm_slab_cache_free(g_slab_filep, wfp);
			return -1;
		}
	} else {
		return -1;
	}
	wfp->type = OUT_PIPE_FILE;
	rfp->type = IN_PIPE_FILE;
	ret = fs_create_pipe(wfp, rfp);
	if (ret != SYSCALL_SUCCESS) {
		fs_destroy_filep(fds[0]);
		fs_destroy_filep(fds[1]);
		fds[0] = -1;
		fds[1] = -1;
	}
	if (ret == SYSCALL_SUCCESS){
		SYSCALL_DEBUG("pipe  fds: fd1: %d fd2 :%d  \n", fds[0],fds[1]);
	}

	return ret;
}
static void init_pipes(){
	int i,j;
	for (i = 0; i < MAX_PIPES; i++) {
		pipes[i].in_index = 0;
		pipes[i].out_index = 0;
		pipes[i].count = 0;

		for (j = 0; j < MAX_PIPE_BUFS; j++) {
			pipes[i].bufs[j].data = 0;
			pipes[i].bufs[j].size = 0;
			pipes[i].bufs[j].start_offset = 0;
		}
	}
	init_pipe_done = 1;
	return ;
}
static int fs_create_pipe(struct file *wfp, struct file *rfp) {
	long i ;
	int j, found;
	int ret=-1;

	if (init_pipe_done == 0) {
		init_pipes();
	}
	if (wfp==0 || rfp==0) return ret;

	mutexLock(g_inode_lock);
	for (i = 0; i < MAX_PIPES; i++) {
		if (pipes[i].count == 0) {
			found = 1;
			pipes[i].in_index = 0;
			pipes[i].out_index = 0;
			pipes[i].count = 2;
			rfp->private_pipe = (void *)i;
			wfp->private_pipe = (void *)i;
			for (j = 0; j < MAX_PIPE_BUFS; j++) {
				pipes[i].bufs[j].data = 0;
				pipes[i].bufs[j].size = 0;
				pipes[i].bufs[j].start_offset = 0;
			}
			ret = SYSCALL_SUCCESS;
			goto last;
		}
	}
last:
	mutexUnLock(g_inode_lock);

	return ret;
}
static int fs_dup_pipe(struct file *fp) {
	long i = (long)fp->private_pipe;

	if (i < 0 || i >= MAX_PIPES)
		return -1;

	if (pipes[i].count==0){
		return -1;
	}
	mutexLock(g_inode_lock);
	pipes[i].count++;
	mutexUnLock(g_inode_lock);
	return SYSCALL_SUCCESS;
}
static int fs_destroy_pipe(struct file *fp) {
	int ret = -1;
	int j;
	long i = (long)fp->private_pipe;

	if (i < 0 || i >= MAX_PIPES)
		return -1;

	mutexLock(g_inode_lock);
	pipes[i].count--;
	ret = SYSCALL_SUCCESS;
	if ( pipes[i].count == 0 ) {
		for (j = 0; j < MAX_PIPE_BUFS; j++) {
			if (pipes[i].bufs[j].data != 0)
				mm_putFreePages((unsigned long) pipes[i].bufs[j].data, 0);
			pipes[i].bufs[j].data = 0;
			pipes[i].bufs[j].size = 0;
			pipes[i].bufs[j].start_offset = 0;
		}
	}
	mutexUnLock(g_inode_lock);

	return ret;
}
#define ENOSPC          28      /* No space left on device */
static int fs_send_to_pipe(struct file *fp, uint8_t *buf, int len) {
	int max_size = PAGE_SIZE ;
	int ret;
	int consumed_len=0;
	int left_len=len;

	long i = (long)fp->private_pipe;
	if (i < 0 || i >= MAX_PIPES)
		return -1;

restart:
	mutexLock(g_inode_lock);
	int out = pipes[i].out_index;
	if (pipes[i].bufs[out].data == 0) {
		pipes[i].bufs[out].data = (uint8_t *)alloc_page(0);
		pipes[i].bufs[out].size = 0;
		pipes[i].bufs[out].start_offset = 0;
	} else {
#if 0
		ret = -ENOSPC;
		goto last;
#else
		mutexUnLock(g_inode_lock);
		if (pipes[i].count > 1) { /* wait if there is more then one thread operating pipe */
			sc_sleep(2);
			goto restart;
		}
		return 0;
#endif
	}

	if (left_len < max_size)
		max_size = left_len;
	ut_memcpy(pipes[i].bufs[out].data, buf+consumed_len, max_size);
	pipes[i].bufs[out].size = max_size;
	pipes[i].bufs[out].start_offset = 0;
	pipes[i].out_index = (pipes[i].out_index + 1) % MAX_PIPE_BUFS;
	ret=max_size;
	consumed_len = consumed_len+max_size;
	left_len = left_len-max_size;

//last:
	mutexUnLock(g_inode_lock);
	if (left_len>0){
		goto restart;
	}
	//ut_printf(" sending in :%s: to pipe-%d : %d\n",g_current_task->name, i,ret);

	return consumed_len;
}
#define EAGAIN          11      /* Try again */
static int fs_recv_from_pipe(struct file *fp, uint8_t *buf, int len) {
	long i = (long )fp->private_pipe;
	int in, max_size;
	int ret = -1;

	if (i < 0 || i >= MAX_PIPES)
		return -1;
restart:
	mutexLock(g_inode_lock);
	in = pipes[i].in_index;
	if (pipes[i].bufs[in].data != 0) {
		max_size = pipes[i].bufs[in].size - pipes[i].bufs[in].start_offset;
		if (len < max_size)
			max_size = len;
		ut_memcpy(buf, pipes[i].bufs[in].data + pipes[i].bufs[in].start_offset,
				max_size);
		pipes[i].bufs[in].start_offset = pipes[i].bufs[in].start_offset
				+ max_size;
		if (pipes[i].bufs[in].start_offset == pipes[i].bufs[in].size) {
			mm_putFreePages((unsigned long) pipes[i].bufs[in].data, 0);
			pipes[i].bufs[in].data = 0;
			pipes[i].bufs[in].start_offset = 0;
			pipes[i].bufs[in].size = 0;
			pipes[i].in_index = (pipes[i].in_index + 1) % MAX_PIPE_BUFS;
		}
		ret = max_size;
	} else {
		ret=0;
		//ret = -EAGAIN;
		mutexUnLock(g_inode_lock);
		if (pipes[i].count > 1) { /* wait if there is more then thread operating pipe */
			sc_sleep(2);
			goto restart;
		}
		return ret;
	}
	mutexUnLock(g_inode_lock);

	//ut_printf(" recv from :%s: pipe-%d : %d\n",g_current_task->name, i,ret);
	return ret;
}

/***************************************************************************************/
unsigned long fs_registerFileSystem(struct filesystem *fs) {
	vfs_fs = fs; // TODO : currently only one lowelevel filsystem is hardwired to vfs.
	return 1;
}

int init_vfs() {
	g_slab_filep = kmem_cache_create("file_struct", sizeof(struct file), 0, 0,
			NULL, NULL);
	slab_inodep = kmem_cache_create("inode_struct", sizeof(struct inode), 0, JSLAB_FLAGS_DEBUG,
			NULL, NULL);

	return 0;
}
