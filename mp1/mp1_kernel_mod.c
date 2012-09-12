/*
 * mp1_kernel_mod.c : Kernel module for mp1
 */
#include <linux/module.h>
#include <linux/kernel.h>

static int __init mp1_init_module(void)
{
	printk(KERN_INFO "MP1 module loaded\n");

	return 0;
}

static void __exit mp1_exit_module(void)
{
	printk(KERN_INFO "MP1 module unloaded\n");
}

module_init(mp1_init_module);
module_exit(mp1_exit_module);

MODULE_LICENSE("GPL");
