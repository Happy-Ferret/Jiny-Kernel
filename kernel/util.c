/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *   kernel/util.c
 *   Naredula Janardhana Reddy  (naredula.jana@gmail.com, naredula.jana@yahoo.com)
 *
 */
#define DEBUG_ENABLE 1
#include <stdarg.h>
#include "common.h"
#include "interface.h"
extern unsigned long stack,placement_address;
static void print_symbol(addr_t addr)
{
	int i;
	for (i=0; i< g_total_symbols; i++)
	{
		if ((addr>=g_symbol_table[i].address) && (addr<g_symbol_table[i+1].address))
		{
			ut_printf("   :%s + %x\n",g_symbol_table[i].name,(addr-g_symbol_table[i].address));
			return ;
		}
	}
	ut_printf("   :%x \n",addr);
	return;
}
void ut_showTrace(unsigned long *stack_top)
{
	unsigned long addr;
	unsigned long  sz,stack_end,code_end;
	int i;


	sz=(long)stack_top;
	sz=sz/4;
	sz=sz*4;
	stack_top=(unsigned char *)sz;
	i = 0;
	sz=(TASK_SIZE-1);
	sz=~sz;
	stack_end = (unsigned long)stack_top & (sz);
	stack_end = stack_end+TASK_SIZE-10;
	code_end = &placement_address;
	ut_printf("\nCALL Trace: %x  code_end:%x  %x :%x  \n",stack,code_end,stack_top,stack_end);
	if (stack_end) {
		while ((stack_top < stack_end) && i<12) {
			addr = *stack_top;
			stack_top=(unsigned char *)stack_top+4;
			if ((addr > 0x103000) && (addr<code_end)) {
				print_symbol(addr);
				i++;
			}
		}
	}
}
unsigned long ut_atol(char *p)
{
	unsigned long a;
	int i,m,k;
	a=0;
	m=0;
	for (i=0; p[i]!='\0'; i++)
	{
		if (p[i] == '0' && m==0) continue;
		m++;
		if (p[i]<='9' && p[i]>='0') k=p[i]-'0';
		else k=p[i]-'a'+0xa;
		if (m>1) a=a*0x10;
		a=a+k;
	}
	return a;
}
unsigned int ut_atoi(char *p)
{         
	unsigned int a;
	int i,m,k;
	a=0;
	m=0;         
	for (i=0; p[i]!='\0'; i++)
	{
		if (p[i] == '0' && m==0) continue;
		m++;
		if (p[i]<='9' && p[i]>='0') k=p[i]-'0';
		else k=p[i]-'a'+0xa;
		if (m>1) a=a*0x10;
		a=a+k;                                                 
	}
	return a;
}
// Copy len bytes from src to dest.
void ut_memcpy(uint8_t *dest, uint8_t *src, long len)
{
	uint8_t *sp = (const uint8_t *)src;
	uint8_t *dp = (uint8_t *)dest;
	long i=0;

	DEBUG(" src:%x dest:%x len:%x \n",src,dest,len);
	for(i=len; i>0; i--) 
	{
		*dp = *sp;
		dp++;
		sp++;
	}
}
// Write len copies of val into dest.
void ut_memset(uint8_t *dest, uint8_t val, long len)
{
	uint8_t *temp = (uint8_t *)dest;
	long i;
	DEBUG("memset NEW dest :%x val :%x LEN addr:%x len:%x temp:%x \n",dest,val,&len,len,&temp);/* TODO */
	for ( i=len; i != 0; i--) *temp++ = val;
	return ;
}

// Compare two strings. Should return -1 if 
// str1 < str2, 0 if they are equal or 1 otherwise.
int ut_strcmp(char *str1, char *str2)
{
	int i = 0;
	int failed = 0;
	while(str1[i] != '\0' && str2[i] != '\0')
	{
		if(str1[i] != str2[i])
		{
			failed = 1;
			break;
		}
		i++;
	}
	// why did the loop exit?
	if( (str1[i] == '\0' && str2[i] != '\0') || (str1[i] != '\0' && str2[i] == '\0') )
		failed = 1;
	if (failed == 1 && str1[i] =='\0') failed=2;
	return failed;
}
int ut_memcmp(unsigned char *m1, unsigned char *m2,int len)
{
	int i = 0;
	while(i<len && m1[i] == m2[i])
	{
		i++;
	}
	if (i == len) return 0;
	return 1;
}

// Copy the NULL-terminated string src into dest, and
// return dest.
char *ut_strcpy(char *dest, const char *src)
{
	do
	{
		*dest++ = *src++;
	}
	while (*src != 0);
	*dest=0;
	return dest;
}
char *ut_strncpy(char *dest, const char *src,int n)
{
	int len=0;
        do
        {
                *dest++ = *src++;
		len++;
        }
        while (*src != 0 && len<n);
        *dest=0;
        return dest;
}
// Concatenate the NULL-terminated string src onto
// the end of dest, and return dest.
char *ut_strcat(char *dest, const char *src)
{
	while (*dest != 0)
	{
		*dest = *dest++;
	}

	do
	{
		*dest++ = *src++;
	}
	while (*src != 0);
	return dest;
}

int ut_strlen(const char * s)
{
	const char *sc;

	for (sc = s; *sc != '\0'; ++sc)
		/* nothing */;
	return sc - s;
}
#define ZEROPAD 1               /* pad with zero */
#define SIGN    2               /* unsigned/signed long */
#define PLUS    4               /* show plus */
#define SPACE   8               /* space if plus */
#define LEFT    16              /* left justified */
#define SPECIAL 32              /* 0x */
#define LARGE   64              /* use 'ABCDEF' instead of 'abcdef' */
static int skip_atoi(const char **s)
{
    int i=0;

    while (isdigit(**s))
        i = i*10 + *((*s)++) - '0';
    return i;
}
static char * number(char * buf, char * end, long long num, int base, int size, int precision, int type)
{
    char c,sign,tmp[66];
    const char *digits;
    const char small_digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    const char large_digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int i;

    digits = (type & LARGE) ? large_digits : small_digits;
    if (type & LEFT)
        type &= ~ZEROPAD;
    if (base < 2 || base > 36)
        return buf;
    c = (type & ZEROPAD) ? '0' : ' ';
    sign = 0;
    if (type & SIGN) {
        if (num < 0) {
            sign = '-';
            num = -num;
            size--;
        } else if (type & PLUS) {
            sign = '+';
            size--;
        } else if (type & SPACE) {
            sign = ' ';
            size--;
        }
    }
    if (type & SPECIAL) {
        if (base == 16)
            size -= 2;
        else if (base == 8)
            size--;
    }
    i = 0;
    if (num == 0)
        tmp[i++]='0';
    else
    {
        /* XXX KAF: force unsigned mod and div. */
        unsigned long long num2=(unsigned long long)num;
        unsigned int base2=(unsigned int)base;
        while (num2 != 0) { tmp[i++] = digits[num2%base2]; num2 /= base2; }
    }
    if (i > precision)
        precision = i;
    size -= precision;
    if (!(type&(ZEROPAD+LEFT))) {
        while(size-->0) {
            if (buf <= end)
                *buf = ' ';
            ++buf;
        }
    }
    if (sign) {
        if (buf <= end)
            *buf = sign;
        ++buf;
    }
    if (type & SPECIAL) {
        if (base==8) {
            if (buf <= end)
                *buf = '0';
            ++buf;
        } else if (base==16) {
            if (buf <= end)
                *buf = '0';
            ++buf;
            if (buf <= end)
                *buf = digits[33];
            ++buf;
        }
    }
    if (!(type & LEFT)) {
        while (size-- > 0) {
            if (buf <= end)
                *buf = c;
            ++buf;
        }
    }
    while (i < precision--) {
        if (buf <= end)
            *buf = '0';
        ++buf;
    }
    while (i-- > 0) {
        if (buf <= end)
            *buf = tmp[i];
        ++buf;
    }
    while (size-- > 0) {
        if (buf <= end)
            *buf = ' ';
        ++buf;
    }
    return buf;
}
static int vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
    int len;
    unsigned long long num;
    int i, base;
    char *str, *end, c;
    const char *s;

    int flags;          /* flags to number() */

    int field_width;    /* width of output field */
    int precision;              /* min. # of digits for integers; max
                                   number of chars for from string */
    int qualifier;              /* 'h', 'l', or 'L' for integer fields */
                                /* 'z' support added 23/7/1999 S.H.    */
                                /* 'z' changed to 'Z' --davidm 1/25/99 */

    str = buf;
    end = buf + size - 1;

    if (end < buf - 1) {
        end = ((void *) -1);
        size = end - buf + 1;
    }

    for (; *fmt ; ++fmt) {
        if (*fmt != '%') {
            if (str <= end)
                *str = *fmt;
            ++str;
            continue;
        }

        /* process flags */
        flags = 0;
    repeat:
        ++fmt;          /* this also skips first '%' */
        switch (*fmt) {
        case '-': flags |= LEFT; goto repeat;
        case '+': flags |= PLUS; goto repeat;
        case ' ': flags |= SPACE; goto repeat;
        case '#': flags |= SPECIAL; goto repeat;
        case '0': flags |= ZEROPAD; goto repeat;
        }

        /* get field width */
        field_width = -1;
        if (isdigit(*fmt))
            field_width = skip_atoi(&fmt);
        else if (*fmt == '*') {
            ++fmt;
            /* it's the next argument */
            field_width = va_arg(args, int);
            if (field_width < 0) {
                field_width = -field_width;
                flags |= LEFT;
            }
        }

        /* get the precision */
        precision = -1;
        if (*fmt == '.') {
            ++fmt;
            if (isdigit(*fmt))
                precision = skip_atoi(&fmt);
            else if (*fmt == '*') {
                ++fmt;
                          /* it's the next argument */
                precision = va_arg(args, int);
            }
            if (precision < 0)
                precision = 0;
        }

        /* get the conversion qualifier */
        qualifier = -1;
        if (*fmt == 'h' || *fmt == 'l' || *fmt == 'L' || *fmt =='Z') {
            qualifier = *fmt;
            ++fmt;
            if (qualifier == 'l' && *fmt == 'l') {
                qualifier = 'L';
                ++fmt;
            }
        }
        if (*fmt == 'q') {
            qualifier = 'L';
            ++fmt;
        }

        /* default base */
        base = 10;

        switch (*fmt) {
        case 'c':
            if (!(flags & LEFT)) {
                while (--field_width > 0) {
                    if (str <= end)
                        *str = ' ';
                    ++str;
                }
            }
            c = (unsigned char) va_arg(args, int);
            if (str <= end)
                *str = c;
            ++str;
            while (--field_width > 0) {
                if (str <= end)
                    *str = ' ';
                ++str;
            }
            continue;

        case 's':
            s = va_arg(args, char *);
            if (!s)
                s = "<NULL>";
            //len = strlen(s, precision);

            len = ut_strlen(s);

            if (!(flags & LEFT)) {
                while (len < field_width--) {
                    if (str <= end)
                        *str = ' ';
                    ++str;
                }
            }
            for (i = 0; i < len; ++i) {
                if (str <= end)
                    *str = *s;
                ++str; ++s;
            }
            while (len < field_width--) {
                if (str <= end)
                    *str = ' ';
                ++str;
            }
            continue;

        case 'p':
            if (field_width == -1) {
                field_width = 2*sizeof(void *);
                flags |= ZEROPAD;
            }
            str = number(str, end,
                         (unsigned long) va_arg(args, void *),
                         16, field_width, precision, flags);
            continue;


        case 'n':
            if (qualifier == 'l') {
                long * ip = va_arg(args, long *);
                *ip = (str - buf);
            } else if (qualifier == 'Z') {
                size_t * ip = va_arg(args, size_t *);
                *ip = (str - buf);
            } else {
                int * ip = va_arg(args, int *);
                *ip = (str - buf);
            }
            continue;

        case '%':
            if (str <= end)
                *str = '%';
            ++str;
            continue;

            /* integer number formats - set up the flags and "break" */
        case 'o':
            base = 8;
            break;

        case 'X':
            flags |= LARGE;
        case 'x':
            base = 16;
            break;

        case 'd':
        case 'i':
            flags |= SIGN;
        case 'u':
            break;

        default:
            if (str <= end)
                *str = '%';
            ++str;
            if (*fmt) {
                if (str <= end)
                    *str = *fmt;
                ++str;
            } else {
                --fmt;
            }
            continue;
        }
        if (qualifier == 'L')
            num = va_arg(args, long long);
        else if (qualifier == 'l') {
            num = va_arg(args, unsigned long);
            if (flags & SIGN)
                num = (signed long) num;
        } else if (qualifier == 'Z') {
            num = va_arg(args, size_t);
        } else if (qualifier == 'h') {
            num = (unsigned short) va_arg(args, int);
            if (flags & SIGN)
                num = (signed short) num;
        } else {
            num = va_arg(args, unsigned int);
            if (flags & SIGN)
                num = (signed int) num;
        }

        str = number(str, end, num, base,
                     field_width, precision, flags);
    }
    if (str <= end)
        *str = '\0';
    else if (size > 0)
        /* don't write out a null byte if the buf size is zero */
        *end = '\0';
    /* the trailing null byte doesn't count towards the total
     * ++str;
     */
    return str-buf;
}

/**
 * snprintf - Format a string and place it in a buffer
 * @buf: The buffer to place the result into
 * @size: The size of the buffer, including the trailing null space
 * @fmt: The format string to use
 * @...: Arguments for the format string
 */
int ut_snprintf(char * buf, size_t size, const char *fmt, ...)
{
    va_list args;
    int i;

    va_start(args, fmt);
    i=vsnprintf(buf,size,fmt,args);
    va_end(args);
    return i;
}

/**
 * vsprintf - Format a string and place it in a buffer
 * @buf: The buffer to place the result into
 * @fmt: The format string to use
 * @args: Arguments for the format string
 *
 * Call this function if you are already dealing with a va_list.
 * You probably want sprintf instead.
 */
static int vsprintf(char *buf, const char *fmt, va_list args)
{
    return vsnprintf(buf, 0xFFFFFFFFUL, fmt, args);
}


/**
 * sprintf - Format a string and place it in a buffer
 * @buf: The buffer to place the result into
 * @fmt: The format string to use
 * @...: Arguments for the format string
 */
int ut_sprintf(char * buf, const char *fmt, ...)
{
    va_list args;
    int i;

    va_start(args, fmt);
    i=vsprintf(buf,fmt,args);
    va_end(args);
    return i;
}

