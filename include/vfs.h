#ifndef __VFS_H__
#define __VFS_H__
#include "common.h"
#include "mm.h"
#include "task.h"

#define MAX_FILENAME 200
#define HOST_SHM_ADDR 0xd0000000
#define HOST_SHM_CTL_ADDR 0xd1000000

#define O_ACCMODE       00000003
#define O_RDONLY        00000000
#define O_WRONLY        00000001
#define O_RDWR          00000002
#ifndef O_CREAT
#define O_CREAT         00000100        /* not fcntl */
#endif
#ifndef O_EXCL
#define O_EXCL          00000200        /* not fcntl */
#endif
#ifndef O_NOCTTY
#define O_NOCTTY        00000400        /* not fcntl */
#endif
#ifndef O_TRUNC
#define O_TRUNC         00001000        /* not fcntl */
#endif
#ifndef O_APPEND
#define O_APPEND        00002000
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK      00004000
#endif
#ifndef O_DSYNC
#define O_DSYNC         00010000        /* used to be O_SYNC, see below */
#endif
#ifndef FASYNC
#define FASYNC          00020000        /* fcntl, for BSD compatibility */
#endif
#ifndef O_DIRECT
#define O_DIRECT        00040000        /* direct disk access hint */
#endif
#ifndef O_LARGEFILE
#define O_LARGEFILE     00100000
#endif
#ifndef O_DIRECTORY
#define O_DIRECTORY     00200000        /* must be a directory */
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW      00400000        /* don't follow links */
#endif
#ifndef O_NOATIME
#define O_NOATIME       01000000
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC       02000000        /* set close_on_exec */
#endif


#define F_DUPFD         0       /* dup */

enum {
POSIX_FADV_DONTNEED=4
};

enum {
TYPE_SHORTLIVED=1,
TYPE_LONGLIVED=2
};
extern unsigned long g_hostShmLen;

struct file;
struct inode;

enum {
	NETWORK_FILE=2,

	OUT_FILE=3,  /* special in/out files */
	IN_FILE=4,
	OUT_PIPE_FILE=5,
	IN_PIPE_FILE=6,

	REGULAR_FILE=0x8000,
	DIRECTORY_FILE=0x4000,
	SYM_LINK_FILE=0xA000
};
enum {
	DEVICE_SERIAL=1,
	DEVICE_KEYBOARD=2,
	DEVICE_DISPLAY_VGI=3
};

struct file {
	unsigned char filename[MAX_FILENAME];
	int type;
	uint64_t offset;

	struct inode *inode;
	void *private;
};

struct inode {
	atomic_t count; /* usage count */
	int nrpages;	
	int type; /* short leaved (MRU) or long leaved (LRU) */
	time_t mtime; /* last modified time */
	unsigned long fs_private;
	struct filesystem *vfs;

	int file_type;
	uint64_t file_size; /* file length */
	uint64_t inode_no;

	unsigned char filename[MAX_FILENAME];
	struct list_head page_list;	
	struct list_head vma_list;	
	struct list_head inode_link;	
};

struct fileStat {
	uint32_t mode;
	uint32_t atime,mtime;
	uint64_t st_size;
	uint64_t inode_no;
	uint32_t type;
	uint32_t blk_size;
};

struct dirEntry {
	unsigned char filename[MAX_FILENAME];
	int file_type;
	uint64_t inode_no;
};

typedef struct fileStat fileStat_t;
struct filesystem {
	int (*open)(struct inode *inode, int flags, int mode);
	int (*lseek)(struct file *file,  unsigned long offset, int whence);
	long (*write)(struct inode *inode, uint64_t offset, unsigned char *buff, unsigned long len);
	long (*read)(struct inode *inode, uint64_t offset,  unsigned char *buff, unsigned long len);
	long (*readDir)(struct inode *inode, struct dirEntry *dir_ptr, unsigned long dir_max);
	int (*remove)(struct inode *inode);
	int (*stat)(struct inode *inode, struct fileStat *stat);
	int (*close)(struct inode *inodep);
	int (*fdatasync)(struct inode *inodep);
};

#define fd_to_file(fd) (fd >= 0 && g_current_task->mm->fs.total > fd) ? (g_current_task->mm->fs.filep[fd]) : ((struct file *)0)


#endif
