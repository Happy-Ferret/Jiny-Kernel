/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
*   kshell.cc
*   Naredula Janardhana Reddy  (naredula.jana@gmail.com, naredula.jana@yahoo.com)
*
*/

extern "C" {
#include "common.h"
}

#define MAX_LINE_LENGTH 200
#define CMD_PROMPT "->>>"
#define MAX_CMD_HISTORY 50
#define MAX_COMMANDS 500

class kshell {
	unsigned char cmd_history[MAX_CMD_HISTORY][MAX_LINE_LENGTH];
	int curr_line_no;
	int his_line_no;
	unsigned char curr_line[MAX_LINE_LENGTH];

	void tokenise(unsigned char *p, unsigned char *tokens[]);
	int get_cmd(unsigned char *line);
	int process_command(int cmd, unsigned char *p);
	int cmd_func(unsigned char *arg1, unsigned char *arg2);

public:
	int main(void *arg);
};
kshell ksh;

enum {
	CMD_GETVAR = 1, CMD_FILLVAR, CMD_UPARROW, CMD_DOWNARROW, CMD_LEFTARROW
};
int putchar(unsigned char c){
	unsigned char buf[10];
	buf[0]=c;
	return SYS_fs_write(1,buf,1);
}
void kshell::tokenise(unsigned char *p, unsigned char *tokens[]) {
	int i, k, j;

	i = 0;
	k = 1;
	j = 1;
	tokens[0] = p;
	while (p[i] != '\0') {
		if (p[i] == ' ') {
			p[i] = '\0';
			k = 0;
			i++;
			continue;
		}
		if (k == 0) {
			tokens[j] = p + i;
			j++;
			k = 1;
		}
		i++;
		if (j > 2)
			return;
	}
	return;
}
int kshell::cmd_func(unsigned char *arg1, unsigned char *arg2) {
	int ret;
#if 1
	if (arg1 == 0) {
		ut_printf("Command list:\n");
		ret = ut_symbol_show(SYMBOL_CMD);
	} else {
		ret = ut_symbol_execute(SYMBOL_CMD, (char *) arg1, arg2, 0);
	}
	if (ret == 0)
		ut_printf("Not Found: %s\n", arg1);
#endif
	return ret;
}

int kshell::process_command(int cmd, unsigned char *p) {
	int i, symbls;
	unsigned char *token[5];

	if (cmd == CMD_UPARROW) {
		his_line_no--;
		if (his_line_no < 0)
			his_line_no = MAX_CMD_HISTORY - 1;
		ut_strcpy(p, cmd_history[his_line_no]);
		putchar((int) '\n');
		return 0;
	} else if (cmd == CMD_DOWNARROW) {
		his_line_no++;
		if (his_line_no >= MAX_CMD_HISTORY)
			his_line_no = 0;
		ut_strcpy(p, cmd_history[his_line_no]);
		putchar((int) '\n');
		return 0;
	} else if (cmd == CMD_LEFTARROW) {
		curr_line[0] = '\0';
		putchar((int) '\n');
		return 0;
	}
	if (p[0] != '\0') {
		ut_strcpy(cmd_history[curr_line_no], p);
		curr_line_no++;
		if (curr_line_no >= MAX_CMD_HISTORY)
			curr_line_no = 0;
		his_line_no = curr_line_no;
	}
	for (i = 0; i < 3; i++)
		token[i] = 0;
	tokenise(p, token);
	symbls = 0;

	cmd_func(token[0], token[1]);
	if (cmd == CMD_FILLVAR && symbls > 0) {
		ut_printf("\n");
		return 1;
	} else
		ut_printf("Not found :%s: \n", p);
	p[0] = '\0';
	return 0;
}

int kshell::get_cmd(unsigned char *line) {
	int i;
	int cmd;

	i = 0;
	cmd = CMD_FILLVAR;
	for (i = 0; line[i] != '\0' && i < MAX_LINE_LENGTH; i++) {
		putchar((int) line[i]);
	}
	while (i < MAX_LINE_LENGTH) {
		int c;

		while ((line[i] = dr_kbGetchar(DEVICE_KEYBOARD)) == 0)
			;

		c = line[i];
		if (line[i] == 1) /* upArrow */
		{
			cmd = CMD_UPARROW;
			line[i] = '\0';
			break;
			//	ut_printf(" Upp Arrow \n");
		}
		if (line[i] == 2) /* upArrow */
		{
			cmd = CMD_DOWNARROW;
			line[i] = '\0';
			break;
			//	ut_printf(" DOWNArrow \n");
		}
		if (line[i] == 3) /* leftArrow */
		{
			cmd = CMD_LEFTARROW;
			line[i] = '\0';
			break;
			//	ut_printf(" LeftArrow \n");
		}
		if (line[i] == '\t') {
			line[i] = '\0';
			break;
		}
		if (line[i] == '\n' || line[i] == '\r') {
			cmd = CMD_GETVAR;
			putchar((int) '\n');
			line[i] = '\0';
			break;
		}
		putchar(line[i]);
		i++;
	}
	line[i] = '\0';
	return cmd;
}
unsigned char *envs[] = { (unsigned char *) "HOSTNAME=jana",
		(unsigned char *) "USER=jana", (unsigned char *) "HOME=/",
		(unsigned char *) "PWD=/", 0 };
static int thread_launch_user(void *arg1, void *arg2) {
	void **argv = sc_get_thread_argv();
	if (argv == 0) {
		unsigned char *arg[5];
		arg[0] = (unsigned char *) arg1;
		arg[1] = (unsigned char *) arg2;
		arg[2] = 0;

		SYS_sc_execve((unsigned char *) arg1, arg, envs);
	} else {
		//unsigned char name[100];
		void *arg[5];

		//ut_strcpy(name, (const unsigned char *)argv[0]);
		arg[0] = argv[0];
		arg[1] = argv[1];
		arg[2] = argv[2];
		arg[3] = 0;
		//	BRK;
		SYS_sc_execve((unsigned char *)argv[0], (unsigned char **)arg, envs);
	}
	ut_printf(" ERROR: COntrol Never Reaches\n");
	return 1;
}
void *tmp_arg[5];
static int sh_create(unsigned char *bin_file, unsigned char *name, unsigned char *arg) {
	int ret;

	tmp_arg[0] =(void *) bin_file;
	tmp_arg[1] =(void *) name;
	tmp_arg[2] =(void *) arg;
	sc_set_fsdevice(DEVICE_SERIAL, DEVICE_SERIAL);  /* all user level thread on serial line */
	ret = sc_createKernelThread(thread_launch_user, (void **) &tmp_arg,
			name);

	return ret;
}
#define USERLEVEL_SHELL "./busybox"
int kshell::main(void *arg) {
	int i, cmd_type;
	int ret = 1;

//	ret = fs_open(USERLEVEL_SHELL,0,0);
	if (ret != 0) {
//		fs_close(ret);
#if 1
	//	ret = sh_create((unsigned char *) USERLEVEL_SHELL,
	//			(unsigned char *) "sh", (unsigned char *)"start"); // start the user level shell
		ret = sh_create((unsigned char *) USERLEVEL_SHELL,
				(unsigned char *) "sh", 0); // start the user level shell
#endif
	} else {
		/* attach kernel shell to serial line since user level shell fails */
//		sc_set_fsdevice(DEVICE_KEYBOARD, DEVICE_KEYBOARD);
	}

	ut_log(" user shell thread creation ret :%x\n", ret);
	//ut_strncpy(g_current_task->name, "shell", MAX_TASK_NAME);
	for (i = 0; i < MAX_CMD_HISTORY; i++)
		cmd_history[i][0] = '\0';

	sc_sleep(500); /* this sleep is to make sure the user level thread come up, other wise we force it to use the keyboard */
	sc_set_fsdevice(DEVICE_KEYBOARD, DEVICE_KEYBOARD); /* kshell on vga console */
	curr_line[0] = '\0';
	while (1) {
		ut_printf(CMD_PROMPT);
		cmd_type = get_cmd(curr_line);
		process_command(cmd_type, curr_line);
	}
	return 1;
}
extern "C" {
void shell_main(){
    ksh.main(0);
}
}

