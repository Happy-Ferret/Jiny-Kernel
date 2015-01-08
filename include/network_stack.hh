#ifndef _JINYKERNEL_NETWORK_STACK_HH
#define _JINYKERNEL_NETWORK_STACK_HH
extern "C" {

extern void netstack_lock();
extern void netstack_unlock();
}
#define WRITE_BUF_CREATED 0x100
#define WRITE_SLEEP_TILL_SEND 0x1
enum _socket_type
{
  SOCK_STREAM = 1,      /* Sequenced,reliable andconnection-based byte streams.  */
  SOCK_DGRAM = 2,       /* Connectionless, unreliable datagrams.  */
  SOCK_RAW = 3,         /* Raw protocol type.  */
  SOCK_RDM = 4,
  SOCK_STREAM_CHILD=200
};
#define AF_INET 2  /* family */

#define IPPROTO_UDP 0x11 /* protocol type */
#define IPPROTO_TCP 0x06
class network_connection{
public:
	int family;
	int type; /* udp or tcp */
	uint32_t dest_ip,src_ip;
	uint16_t dest_port,src_port;
	uint8_t 	protocol; /* ip_protocol , tcp or udp */

	network_connection *child_connection; /* this for listening  tcp connection */
	void *proto_connection;  /* protocol connection */
};
class network_stack{
//	char temp_buff[8192];

public:
	unsigned char *name;

	int open(network_connection *conn, int flag);
    int write(network_connection *conn, uint8_t *app_data, int app_len);
    int read(network_connection *conn, uint8_t *raw_data, int raw_len, uint8_t *app_data, int app_maxlen);
	int close(network_connection *conn);
	int bind(network_connection *conn, uint16_t port);
	int connect(network_connection *conn);
};

#endif
