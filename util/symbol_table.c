#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#define MAX_SYMBOLLEN 40
#define MAX_FILELEN 50
#define MAX_SYMBOLS 3000
#define TYPE_GTEXT 0
#define TYPE_LTEXT 2
#define TYPE_DATA 1
typedef struct {
	unsigned long address;
	char type;
	char name[MAX_SYMBOLLEN];
	char file_lineno[MAX_FILELEN];
	unsigned long subsystem_type;
} symb_table_t;
symb_table_t stable[MAX_SYMBOLS];
#define MAX_DATA 900001
char data[MAX_DATA];
unsigned long ut_atoi(char *p) {
	unsigned long a;
	int i, m, k;
	a = 0;
	m = 0;
	for (i = 0; p[i] != '\0'; i++) {
		if (p[i] == '0' && m == 0)
			continue;
		m++;
		if (p[i] <= '9' && p[i] >= '0')
			k = p[i] - '0';
		else
			k = p[i] - 'a' + 0xa;
		if (m > 1)
			a = a * 0x10;
		a = a + k;
	}
	return a;
}
static tokenise(char *p, symb_table_t *t) {
	int i, k = 0;
	char *q;
	char s[300];
	q = p;
	i = 0;
	t->name[0] = '\0';
	while (p[i] != '\n' && p[i] != '\0' && p[i] != '\r' && k < 4) {
		if (p[i] == ' ' || p[i] == '\t') {

			p[i] = '\0';
			if (k == 0) {
				t->address = ut_atoi(p);
//printf(" %s : %x \n",p,t->address);
			} else if (k == 1) {
				if (q[0] == 'T') {
					t->type = TYPE_GTEXT;
				} else if (q[0] == 't') {
					t->type = TYPE_LTEXT;
				}else{
					//printf("  :%c:    :%c: \n",q[0],q[1]);
					t->type = TYPE_DATA;
				}

			} else if (k == 2) {
				snprintf(t->name, MAX_SYMBOLLEN, "%s", q);
			} else if (k == 3) {
				snprintf(t->file_lineno, MAX_FILELEN, "%s", q);
			}
			k++;
			q = p + i + 1;
			if (k > 3)
				return 1;
		}
		i++;
	}
	if (k == 3) {
		snprintf(t->file_lineno, MAX_FILELEN, "%s", q);
		return 1;
	}else if (k==2){
		return 1;
	}
	return 0;
}
main(int argc, char *argv[]) {
	int fd, wfd, i, j, ret;
	char *p;

	fd = open(argv[1], 0);
	wfd = open(argv[2], O_WRONLY | O_CREAT);
	if (fd < 0 || wfd < 0) {
		printf(" NO FILE EXISTS \n");
		return 0;
	}
	ret = read(fd, data, MAX_DATA);
	j = 0;
	i = 0;
	p = &data[0];
	while (i < ret) {
		if (data[i] == '\n') {
			data[i] = '\0';
			if (tokenise(p, &stable[j]) == 1)
				j++;
			p = &data[i + 1];
		}
		i++;
	}
	write(wfd, &stable[0], j * sizeof(symb_table_t));
	close(wfd);

#if 0
	for (i = 0; i < j; i++)
		printf(":%d :%s: %d %x \n", i, stable[i].name, stable[i].type,
				stable[i].address);
	printf(" j :%d  size:%d total size:%d \n", j, sizeof(symb_table_t),
			j * sizeof(symb_table_t));
#endif
}
