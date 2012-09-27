/*
 * mp2_kernel_mod.c : Kernel module for mp2
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>

static struct proc_dir_entry *proc_dir, *proc_entry;

int mp2_read_proc(char *page, char **start, off_t off,
		  int count, int *eof, void *data)
{
	printk(KERN_INFO "mp2: In read proc\n");

	return 0;
}

void mp2_register_process(char *user_data)
{
	printk(KERN_INFO "mp2:Registration\n");
}

void mp2_yield_process(char *user_data)
{
	printk(KERN_INFO "mp2: Yield\n");
}

void mp2_deregister_process(char *user_data)
{
	printk(KERN_INFO "mp2: De-registration\n");
}

int mp2_write_proc(struct file *filp, const char __user *buff,
		   unsigned long len, void *data)
{
#define MAX_USER_DATA_LEN 50

	char user_data[MAX_USER_DATA_LEN];

	if (len > MAX_USER_DATA_LEN) {
		printk(KERN_WARNING "mp2: truncating user data\n");
		len = MAX_USER_DATA_LEN;
	}

	if (copy_from_user(user_data,
			   buff,
			   len)) {
		return -EFAULT;
	}

	switch (user_data[0]) {
	case 'R':
		mp2_register_process(user_data);
		break;

	case 'Y':
		mp2_yield_process(user_data);
		break;

	case 'D':
		mp2_deregister_process(user_data);
		break;

	default:
		printk(KERN_WARNING "mp2: Incorrect option\n");
		break;
	}

	return len;
}

static int __init mp2_init_module(void)
{
	int ret = 0;

	/* Create a proc directory entry mp2 */
	proc_dir = proc_mkdir("mp2", NULL);

	/* Check if directory was created */
	if (proc_dir == NULL) {
		printk(KERN_INFO "mp2: Couldn't create proc dir\n");
		ret = -ENOMEM;
	} else {
		/* Create an entry status under proc dir mp2 */
		proc_entry = create_proc_entry( "status", 0666, proc_dir);

		/*Check if entry was created */
		if (proc_entry == NULL) {
			printk(KERN_INFO "mp2: Couldn't create proc entry\n");
			ret = -ENOMEM;
		} else {

			/* proc_entry->owner = THIS_MODULE; */
			proc_entry->read_proc = mp2_read_proc;
			proc_entry->write_proc = mp2_write_proc;

			printk(KERN_INFO "MP2 module loaded\n");
		}
	}

	return ret;
}

static void __exit mp2_exit_module(void)
{
	/* Remove the status entry first */
	remove_proc_entry("status", proc_dir);

	/* Remove the mp1 proc dir now */
	remove_proc_entry("mp2", NULL);

	printk(KERN_INFO "MP2 module unloaded\n");
}

module_init(mp2_init_module);
module_exit(mp2_exit_module);

MODULE_LICENSE("GPL");
