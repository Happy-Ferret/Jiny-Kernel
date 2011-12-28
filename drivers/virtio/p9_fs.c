#define DEBUG_ENABLE 1
#include "vfs.h"
#include "mm.h"
#include "common.h"
#include "task.h"
#include "interface.h"
#include "9p.h"

#define OFFSET_ALIGN(x) ((x/PC_PAGESIZE)*PC_PAGESIZE) /* the following 2 need to be removed */

static p9_client_t client;
static int p9ClientInit() {
	static int init = 0;
	uint32_t msg_size;
	int ret;
	unsigned char version[200];
	unsigned long addr,i;

	if (init != 0)
		return 1;

	client.pkt_buf = (unsigned char *) mm_getFreePages(MEM_CLEAR, 0);
	client.pkt_len = 4098;
	init = 1;

	client.type = P9_TYPE_TVERSION;
	client.tag = 0xffff;

	addr = p9_write_rpc(&client, "ds", 4090*2, "9P2000.u");
	if (addr != 0) {
		ret = p9_read_rpc(&client, "ds", &msg_size, version);
	}
	DEBUG("New cmd:%x size:%x version:%s  pkt_buf:%x\n",ret, msg_size,version,client.pkt_buf);

	client.type = P9_TYPE_TATTACH;
	client.tag = 0x13;
	client.root_fid = 132;
	addr = p9_write_rpc(&client, "ddss", client.root_fid, ~0, "jana", "");
	if (addr != 0) {
		ret = p9_read_rpc(&client, "");
	}
	for (i=0; i<MAX_P9_FILES; i++) {
		client.files[i].fid=0;
	}
	client.next_free_fid = client.root_fid+1;
	return 1;
}
static int p9_open(uint32_t fid, unsigned char *filename, int flags, int arg_mode) {
	unsigned long addr;
	uint8_t mode_b;
	uint32_t perm;
	int i,ret=-1;

	if (flags & O_RDONLY)
		mode_b = 0;
	else if (flags & O_WRONLY)
		mode_b = 1;
	else if (flags & O_RDWR)
		mode_b = 2;
	if (flags & O_CREAT) {
		client.type = P9_TYPE_TCREATE;
		perm = 0x666;
		addr = p9_write_rpc(&client, "dsdb", fid, filename, perm, mode_b);
	} else {
		client.type = P9_TYPE_TOPEN;
		addr = p9_write_rpc(&client, "db", fid, mode_b);
	}

	if (addr != 0) {
		ret = p9_read_rpc(&client, "");
		ret = 1;
	}
	return ret;;
}

static void *walkLock=0;
//TODO: handling locking in entire p9 using new mutex calls
static uint32_t p9_walk(unsigned char *filename, int flags, unsigned char **create_filename) {
	unsigned long addr;
	static unsigned char names[MAX_P9_FILEDEPTH][MAX_P9_FILELENGTH];
	int ret, len, levels;
	int i, j, empty_fd = -1;
	uint32_t parent_fid, ret_fd = 0;

	i = 0;
	levels = 0;
	j = 0;
	mutexLock(walkLock);
	while (i < MAX_P9_FILELENGTH && (filename[i] != '\0')) {
		if (filename[i] == '/') {
			if (j > 0) {
				names[levels][j] = '\0';
				levels++;
			}
			j = 0;
			i++;
			continue;
		}
		names[levels][j] = filename[i];
		j++;
		i++;
	}
	names[levels][j] = '\0';
	j = 0;
	parent_fid = client.root_fid;
	while (j <= levels) { // start to walk
		empty_fd = -1;
		ret_fd = 0;
		for (i = 0; i < MAX_P9_FILES; i++) {
			if (client.files[i].fid != 0) {
				if ((ut_strcmp(client.files[i].name, names[j]) == 0) && (client.files[i].parent_fid == parent_fid)) {
					parent_fid = client.files[i].fid;
					ret_fd = client.files[i].fid;
					if (j == levels) { // leaf level
						goto last;
					}
					break;
				}
				continue;
			}
			if (empty_fd == -1)
				empty_fd = i;
		}
		if (ret_fd == 0) { // walk only if it is not found in cache
			client.type = P9_TYPE_TWALK;
			addr = p9_write_rpc(&client, "ddws", parent_fid, client.next_free_fid, 1, names[j]);
			if (addr != 0) {
				ret = p9_read_rpc(&client, "");
				if ((empty_fd != -1) && (client.recv_type == P9_TYPE_RWALK)) {
					client.files[empty_fd].fid = client.next_free_fid;
					client.next_free_fid++; /* TODO : there will be collision , the logic need to replaced with better one */
					ut_strcpy(client.files[empty_fd].name, names[j]);
					client.files[empty_fd].parent_fid = parent_fid;
					parent_fid = client.files[empty_fd].fid; /* new fd becoms parent for next walk or search */
					if (j == levels) {
						ret_fd = client.files[empty_fd].fid;
						goto last;
					}
				} else {
					if ((flags&O_CREAT) && (j == levels) && (client.recv_type != P9_TYPE_RWALK))
					{ /* Retry it without name */
						addr = p9_write_rpc(&client, "ddw", parent_fid, client.next_free_fid, 0);
						if (addr != 0) {
							ret = p9_read_rpc(&client, "");
							if ((empty_fd != -1) &&  (client.recv_type == P9_TYPE_RWALK)) {
								ret_fd=client.next_free_fid;
								client.files[empty_fd].fid = client.next_free_fid;
								client.next_free_fid++; /* TODO : there will be collision , the logic need to replaced with better one */
								ut_strcpy(client.files[empty_fd].name, names[j]);
								client.files[empty_fd].parent_fid = parent_fid;
							}
							else ret_fd =0;
						}
                        len=ut_strlen(filename)-ut_strlen(names[j]);
                        *create_filename=filename+len;
						goto last;
					}
					ret_fd = 0;
					goto last;
				}
			}
		}
		j++;
	}

	last: mutexUnLock(walkLock);
	return ret_fd;
}

static uint32_t p9_read(uint32_t fid, uint64_t offset, unsigned char *data, uint32_t data_len) {
	unsigned long addr;
	int ret;
	uint32_t read_len;

	client.type = P9_TYPE_TREAD;
	client.user_data = data;
	client.userdata_len = data_len;

	addr = p9_write_rpc(&client, "dqd", fid, offset, data_len);
	if (addr != 0) {
		ret = p9_read_rpc(&client, "d", &read_len);
	}

	return read_len;
}

static uint32_t p9_write(uint32_t fid, uint64_t offset, unsigned char *data, uint32_t data_len) {
	unsigned long addr;
	int ret;
	uint32_t write_len;
	unsigned char *rd;

	client.type = P9_TYPE_TWRITE;
	client.user_data = data;
	client.userdata_len = data_len;

	addr = p9_write_rpc(&client, "dqd", fid, offset, data_len);
	if (addr != 0) {
		ret = p9_read_rpc(&client, "d", &write_len);
	}

	rd = client.pkt_buf+1024+10;
	rd[20]='\0';
	DEBUG("write len :%d  write_rpc ret:%d : \n",data_len,ret);
	return write_len;
}

struct filesystem p9_fs;
static int p9Request(unsigned char type, struct inode *inode, uint64_t offset, unsigned char *data, int data_len, int flags, int mode) {
	uint32_t fid;
	unsigned char *createFilename;

	if (inode == 0)
		return -1;
	if (type == REQUEST_OPEN) {
		fid = p9_walk(inode->filename, flags, &createFilename);
		if (fid > 0) {
			inode->fs_private = fid;
			return p9_open(fid, createFilename, flags, mode);
		} else {
            return -1;
		}
	} else if (type == REQUEST_READ) {
		fid = inode->fs_private;
		return p9_read(fid, offset, data, data_len);
	} else if (type == REQUEST_WRITE) {
		fid = inode->fs_private;
		return p9_write(fid, offset, data, data_len);
	}
}

static struct file *p9Open(unsigned char *filename, int flags, int mode) {
	struct file *filep;
	struct inode *inodep;
	int ret;

	p9ClientInit();
	filep = kmem_cache_alloc(g_slab_filep, 0);
	if (filep == 0)
		goto error;
	if (filename != 0) {
		ut_strcpy(filep->filename, filename);
	} else {
		goto error;
	}

	inodep = fs_getInode(filep->filename);
	if (inodep == 0)
		goto error;
	if (inodep->fs_private == 0) /* need to get info from host  irrespective the file present, REQUEST_OPEN checks the file modification and invalidated the pages*/
	{
		ret = p9Request(REQUEST_OPEN, inodep, 0, 0, 0, flags, mode);
		if (ret < 0)
			goto error;
		inodep->file_size = ret;
	}
	filep->inode = inodep;
	filep->offset = 0;
	return filep;

error:
    if (filep != NULL)
		kmem_cache_free(g_slab_filep, filep);
	if (inodep != NULL) {
		fs_putInode(inodep);
	}
	return 0;
}
static int p9Lseek(struct file *filep, unsigned long offset, int whence) {
	filep->offset = offset;
	return 1;
}

static int p9Fdatasync(struct file *filep) {

	return 1;
}
static int p9Write(struct inode *inode, uint64_t offset, unsigned char *data, unsigned long  data_len) {
	p9ClientInit();
    return  p9Request(REQUEST_WRITE, inode, offset, data, data_len, 0, 0);
}

static int p9Read(struct inode *inode, uint64_t offset, unsigned char *data, unsigned long  data_len) {
	p9ClientInit();
    return  p9Request(REQUEST_READ, inode, offset, data, data_len, 0, 0);
}

static int p9Close(struct file *filep) {

	return 1;
}

int p9_initFs() {
//	p9ClientInit(); /* TODO need to include here */
	p9_fs.open = p9Open;
	p9_fs.read = p9Read;
	p9_fs.close = p9Close; // TODO
	p9_fs.write = p9Write;
	p9_fs.fdatasync = p9Fdatasync; //TODO
	p9_fs.lseek = p9Lseek; //TODO

    walkLock=mutexCreate();
    if (walkLock==0) return 0;
	fs_registerFileSystem(&p9_fs);

	return 1;
}