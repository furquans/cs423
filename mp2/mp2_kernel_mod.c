/*
 * mp2_kernel_mod.c : Kernel module for mp2
 */
#include <linux/module.h>
#include <linux/kernel.h>

static int __init mp2_init_module(void)
{
  printk(KERN_INFO "MP2 module loaded\n");

  return 0;
}

static void __exit mp2_exit_module(void)
{
  printk(KERN_INFO "MP2 module unloaded\n");
}

module_init(mp2_init_module);
module_exit(mp2_exit_module);

MODULE_LICENSE("GPL");
