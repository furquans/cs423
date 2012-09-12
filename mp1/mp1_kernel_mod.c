/*
 * mp1_kernel_mod.c : Kernel module for mp1
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>

static struct proc_dir_entry *proc_dir, *proc_entry;

static int __init mp1_init_module(void)
{
	int ret = 0;

	/* Create a proc directory entry mp1 */
	proc_dir = proc_mkdir("mp1", NULL);

	/* Check if directory was created */
	if (proc_dir == NULL) {
		printk(KERN_INFO "mp1: Couldn't create proc dir\n");
		ret = -ENOMEM;
	} else {
		/* Create an entry status under proc dir mp1 */
		proc_entry = create_proc_entry( "status", 0666, proc_dir);

		/*Check if entry was created */
		if (proc_entry == NULL) {
			printk(KERN_INFO "mp1: Couldn't create proc entry\n");
			ret = -ENOMEM;
		} else {

			/* proc_entry->owner = THIS_MODULE; */
			/* proc_entry->read_proc = mp1_read_proc; */
			/* proc_entry->write_proc = mp1_write_proc; */

			printk(KERN_INFO "MP1 module loaded\n");
		}
	}

	return ret;
}

static void __exit mp1_exit_module(void)
{
	/* Remove the status entry first */
	remove_proc_entry("status", proc_dir);

	/* Remove the mp1 proc dir now */
	remove_proc_entry("mp1", NULL);

	printk(KERN_INFO "MP1 module unloaded\n");
}

module_init(mp1_init_module);
module_exit(mp1_exit_module);

MODULE_LICENSE("GPL");
