#include "keyboard.h"
#include "common.h"
#include "task.h"
#include "mach_dep.h"
char codes[128] = {
        /*0    1    2    3    4    5    6    7    8    9    A    B    C    D    E    F*/  
  /*0*/   0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '_', '=','\b', '\t',
  /*1*/ 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']',  10,   0, 'a', 's',
  /*2*/ 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';','\'', '`',   0, '\\', 'z', 'x', 'c', 'v',
  /*3*/ 'b', 'n', 'm', ',', '.', '/',   0,   0,   0, ' ',   0,   0,   0,   0,   0,   0,
  /*4*/   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
  /*5*/   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
  /*6*/   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
  /*7*/   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
};

char s_codes[128] = {
        /*0    1    2    3    4    5    6    7    8    9    A    B    C    D    E    F*/  
  /*0*/   0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+','\b', '\t',
  /*1*/ 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}',  10,   0, 'A', 'S',
  /*2*/ 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',   0, '|', 'Z', 'X', 'C', 'V',
  /*3*/ 'B', 'N', 'M', '<', '>', '?',   0,   0,   0, ' ',   0,   0,   0,   0,   0,   0,
  /*4*/   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
  /*5*/   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
  /*6*/   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
  /*7*/   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
};
static unsigned char lastkey=0;
static int en_scan_to_ascii(unsigned char *buf, uint8_t scancode, uint8_t comb_flags, uint8_t state_flags)
{

	if (scancode ==75 ) /* leftArrow */
	{
		*buf=3;
		return 1;
	}
	if (scancode ==77 ) /* RightArrow */
	{
		*buf=4;
		return 1;
	}
	if (scancode ==72 ) /* uppArrow */
	{
		*buf=1;
		return 1;
	}
	if (scancode==80 ) /* lowArrow */
	{
		*buf=2;
		return 1;
	}

	if(codes[scancode]) {
		*buf = (comb_flags & SHIFT_MASK) ? s_codes[scancode] : codes[scancode];
		/*  if(state_flags & CAPS_MASK && isalpha(*buf)) {
		    if(isupper(*buf)) *buf = tolower(*buf);
		    else *buf = toupper(*buf);
		    } */
		return 1;
	}
	return 0;
}

#define MAX_BUF 100
#define MAX_INPUT_DEVICES 2
static struct {
	int device_id;
	unsigned char kb_buffer[MAX_BUF+1];
	int current_pos;
	int read_pos;
	wait_queue_t kb_waitq;
}input_devices[MAX_INPUT_DEVICES];


unsigned char dr_kbGetchar(int input_id) {
	int i,device_id;
	unsigned char c;

	if (input_id==-1)
		device_id = g_current_task->mm->fs.input_device;
	else
		device_id = input_id;
	for (i = 0; i < MAX_INPUT_DEVICES; i++) {
		if (input_devices[i].device_id == device_id)
			break;
	}
	if (i >= MAX_INPUT_DEVICES)
		return -1;

	while (input_devices[i].current_pos == 0) {
		ipc_waiton_waitqueue(&input_devices[i].kb_waitq, 100);
	}
//TODO: the below code need to be protected by spin lock if multiple reader are there
	if (input_devices[i].read_pos < input_devices[i].current_pos) {
		c = input_devices[i].kb_buffer[input_devices[i].read_pos];
		input_devices[i].read_pos++;
		if (input_devices[i].read_pos == input_devices[i].current_pos) {
			input_devices[i].current_pos = 0;
			input_devices[i].read_pos = 0;
		}
#if 0
	//	ut_log(" key value :%x: \n",c);
#define CTRL_C 3
#define CTRL_D 4
		if (c == 'C') {
			ut_log("sending the CTRL_C \n");
			return CTRL_C;
		}
		if (c == 'D')
			return CTRL_D;
#endif

		return c;
	}

	return 0;
}
int ar_addInputKey(int device_id, unsigned char c) {
	int i;
	for (i = 0; i < MAX_INPUT_DEVICES; i++) {
		if (input_devices[i].device_id != device_id)
			continue;
		if (input_devices[i].current_pos < MAX_BUF) {
			int pos = input_devices[i].current_pos;
			input_devices[i].kb_buffer[pos] = c;
			input_devices[i].current_pos++;
			break;
		}
	}
	if (i >= MAX_INPUT_DEVICES)
		return -1;
	ipc_wakeup_waitqueue(&input_devices[i].kb_waitq);

	return 1;
}
static void keyboard_handler(registers_t regs) {
	unsigned char c, control_kbd, lastKey;
	int ret;

	lastKey = inb(0x60);
//	printf(" Key pressed : %x curr_pos:%x read:%x\n",LastKey,current_pos,read_pos);
	if ( lastKey < 128) {
		ret = en_scan_to_ascii(&c, lastKey, 0, 0);
		//     printf(" character :%c: ret:%d \n",kb_buffer[current_pos],ret);
		if (ret == 1)
			ar_addInputKey(DEVICE_KEYBOARD, c);
	}
	/* Tell the keyboard hat the key is processed */
	control_kbd = inb(0x61);
	outb(0x61, control_kbd | 0x80); /* set the "enable kbd" bit */
	outb(0x61, control_kbd); /* write back the original control value */

}
int init_driver_keyboard(unsigned long unused) {
	int i;

	for (i = 0; i < MAX_INPUT_DEVICES; i++) {
		input_devices[i].current_pos = 0;
		input_devices[i].read_pos = 0;
		if (i > 1)
			BUG(); //TODO: currently only two input devices

		if (i == 0) {
			input_devices[i].device_id = DEVICE_SERIAL;
			ipc_register_waitqueue(&input_devices[i].kb_waitq, "kb-serial",0);
		} else {
			input_devices[i].device_id = DEVICE_KEYBOARD;
			ipc_register_waitqueue(&input_devices[i].kb_waitq, "kb-key",0);
		}
	}

	ar_registerInterrupt(33, keyboard_handler, "keyboard", NULL);
	return 0;
}
