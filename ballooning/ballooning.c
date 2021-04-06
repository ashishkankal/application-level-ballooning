#include <linux/kernel.h>
#include <linux/syscalls.h>

SYSCALL_DEFINE0(reg_balloon)
{
	printk("Registered to Ballooning \n");
	return 0;
}
