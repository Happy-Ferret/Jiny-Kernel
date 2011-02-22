#define FS_VERSION 3
#define FS_MAGIC 0x12345678
#define MAX_FILENAMELEN 200
#define MAX_REQUESTS 100
#define PC_PAGESIZE 4096

enum {
	STATE_INVALID =0,
	STATE_VALID=1,
	STATE_UPDATE_INPROGRESS=2
};

enum {
	RESPONSE_NONE=0,
	RESPONSE_FAILED=1,
	RESPONSE_DONE=2
};
enum {
	REQUEST_OPEN=0,
	REQUEST_READ=1,
	REQUEST_WRITE=2
};
	
typedef struct {
	unsigned char state;
	unsigned char type;
	unsigned char filename[MAX_FILENAMELEN];
	unsigned long offset;
	unsigned long request_len;
	unsigned long shm_offset ; /* offset from the begining of shared memory */

	unsigned char response;	
	unsigned long response_len;
}Request_t;
	
typedef struct {
	unsigned long magic_number;
	unsigned char version;
	unsigned char state;
	int request_highindex;
	Request_t requests[MAX_REQUESTS];
}fileCache_t ;

