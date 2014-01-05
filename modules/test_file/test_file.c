/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *   test_file.c
 *   Author: Naredula Janardhana Reddy  (naredula.jana@gmail.com, naredula.jana@yahoo.com)
 */

int file_test();
#ifdef _KERNEL
/*  TODO : this is temporary solution, In permanent solution app is not required any changes only libc will have changes  */
#include "libc_highpriority.h"
#else  /* for noraml priority */
#include<stdio.h>
int file_test();
main(int argc, char *argv[]) {
	file_test();
}
#endif

unsigned char buf[4096];

int loops = 200000000;
typedef struct {
	unsigned long tv_sec;
	unsigned long tv_usec;
} time_t;
static unsigned char *tmp_arg[100];
void create_thread(void (*func)(void *arg)) {
#ifdef _KERNEL
	int ret;

	tmp_arg[0] = 0;
	tmp_arg[1] = 0;
	tmp_arg[2] = 0;

	ret = sc_createKernelThread(func, &tmp_arg, "test_file_thread");
#else
	int i = fork();

	if (i == 0) {
		func(0);
	} else {
		return i;
	}
#endif
}

int file_test() {
	int i, ret, tret;
	time_t start_time, end_time;
	unsigned long fd = open("/dev/null", 0, 0);
	printf(" fd :%d \n", fd);
	gettimeofday(&start_time, 0);
	ret = 0;
	for (i = 0; i < loops; i++) {
		write(fd, buf, 40);
		tret = read(fd, buf, 40);
		if (tret > 10)
			ret++;
	}
	gettimeofday(&end_time, 0);
	printf(" sucessfully read : %d/%d duration:sec %x:%x(%d)  usec:%x:%x  \n",
			ret, loops, end_time.tv_sec, start_time.tv_sec,
			(end_time.tv_sec - start_time.tv_sec), end_time.tv_usec,
			start_time.tv_usec);
	if (fd > 0) {
		printf(" before closing the file\n");
		close(fd);
		printf(" after closing the file\n");
	}
#ifdef _KERNEL
	Jcmd_lsmod("stat",0);
#endif
	exit(0);
}




