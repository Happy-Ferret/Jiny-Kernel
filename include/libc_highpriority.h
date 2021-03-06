/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *   include/libc_highpriority.h  // This is for high priority tasks
 *   Author: Naredula Janardhana Reddy  (naredula.jana@gmail.com, naredula.jana@yahoo.com)
 */
#include "types.h"

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3
struct epoll_event {
	uint32_t events;
	uint64_t data;
} __attribute__((packed));

#define socket SYS_socket
#define sendto SYS_sendto
#define gettimeofday SYS_gettimeofday
#define bind SYS_bind
#define recvfrom SYS_recvfrom
#define printf ut_printf
#define close SYS_fs_close
#define write SYS_fs_write
#define read SYS_fs_read
#define open SYS_fs_open
#define exit SYS_sc_exit

#define epoll_create SYS_epoll_create
#define epoll_ctl SYS_epoll_ctl
#define epoll_wait SYS_epoll_wait

/* Internet address. */
struct in_addr {
uint32_t       s_addr;     /* address in network byte order */
};
struct sockaddr_in {
	uint16_t    sin_family; /* address family: AF_INET */
	uint16_t      sin_port;   /* port in network byte order */
    struct in_addr sin_addr;   /* internet address */
 };




#define socklen_t int
