

unsigned long ut_printf (const char *format, ...);
unsigned char buf[2048];

main(int argc ,char *argv[])
{
	void *fp,*wp;
	int i;
	unsigned long ret=0;
	i=1;
	ut_printf(" argc:%x   argv:%x \n",argc,argv);
 	while (i<5)
	{
		ut_printf("  LATEST SYSCALLs loop count: %x stackaddr:%x \n",i,&ret);
		i++;
	}
	ut_printf("before Exiting from test code \n");
	exit(1);
	ut_printf("after Exiting from test code \n");
}
