#include "keyboard.h"
#include "common.h"
#include "task.h"
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
static unsigned char kb_buffer[MAX_BUF+1];
static int current_pos=0;
static int read_pos=0;
int keyboard_int=0;
static queue_t kb_waitq;
static void dr_keyBoardBH() /* bottom half */
{
		sc_wakeUp(&kb_waitq);
}
unsigned char dr_kbGetchar() {

	unsigned char c;

	while (current_pos == 0) {
		sc_wait(&kb_waitq, 100);
	}
//TODO: the below code need to be protected by spin lock if multiple reader are there
	if (read_pos < current_pos) {
		c = kb_buffer[read_pos];
		read_pos++;
		if (read_pos == current_pos) {
			current_pos = 0;
			read_pos = 0;
		}

		return c;
	}

	return 0;
}
int ar_addInputKey(unsigned char c)
{
	if (current_pos < MAX_BUF)
	{
		kb_buffer[current_pos]=c;
		current_pos++;
	}
	dr_keyBoardBH();
	return 1;
}
static void keyboard_handler(registers_t regs)
{
	unsigned char c,control_kbd,LastKey;
	int ret;

	LastKey = inb(0x60);
//	printf(" Key pressed : %x curr_pos:%x read:%x\n",LastKey,current_pos,read_pos);
	if (current_pos < MAX_BUF && LastKey<128)
	{
		ret=en_scan_to_ascii(&c,LastKey,0,0);
		//     printf(" character :%c: ret:%d \n",kb_buffer[current_pos],ret);
		if (ret == 1) ar_addInputKey(c);
	}
	/* Tell the keyboard hat the key is processed */
	control_kbd = inb(0x61);
	outb(0x61, control_kbd | 0x80);  /* set the "enable kbd" bit */
	outb(0x61, control_kbd);  /* write back the original control value */
	keyboard_int=1;
	//dr_keyBoardBH();

}
int init_driver_keyboard()
{
	sc_register_waitqueue(&kb_waitq);
	ar_registerInterrupt(33,keyboard_handler,"keyboard");
	return 1;
}
