/*
 * mp1_kernel_mod.c : Kernel module for mp1
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <asm/uaccess.h>

/* Proc dir and proc entry to be added */
static struct proc_dir_entry *proc_dir, *proc_entry;

/* Entry to be maintained for each process in a list */
typedef struct mp1_proc_entry{
	struct list_head list;
	unsigned int pid;
	unsigned int cpu_time;
}MP1_PROC_ENTRY;

/* List head */
MP1_PROC_ENTRY mp1_proc_list;

/* Func: mp1_read_proc
 * Desc: Read the list and provide pid and cpu time of each registered
 *       process to the user
 *
 */
int mp1_read_proc(char *page, char **start, off_t off,
		  int count, int *eof, void *data)
{
	int len = 0;
	MP1_PROC_ENTRY *tmp;

	/* Traverse the list and put values into page */
	list_for_each_entry(tmp, &mp1_proc_list.list, list) {
		len += sprintf(page+len, "%u:%u\n",tmp->pid, tmp->cpu_time);
	}

	/* Return length of data being sent */
	return len;
}

/* Func: mp1_write_proc
 * Desc: Copy the pid sent from user process and make a new entry in the
 *       list
 *
 */
int mp1_write_proc(struct file *filp, const char __user *buff,
		   unsigned long len, void *data)
{
#define PID_LEN 8
	char pid_str[PID_LEN];
	MP1_PROC_ENTRY *tmp;

	if (len > PID_LEN) {
		len = PID_LEN;
	}

	/* Copy the pid sent by the user process into kernel buffer */
	if (copy_from_user(pid_str, buff, len)) {
		return -EFAULT;
	}

	/* Allocate a new list entry */
	tmp = kmalloc(sizeof(*tmp), GFP_KERNEL);

	/* Store the pid */
	sscanf(pid_str,"%u", &tmp->pid);

	/* Initialize time to 0 */
	tmp->cpu_time = 0;

	/* Initialize list structure in the entry */
	INIT_LIST_HEAD(&tmp->list);

	/* Add the entry to the head */
	list_add_tail(&(tmp->list), &(mp1_proc_list.list));

	return len;
}

/* Func: mp1_init_module
 * Desc: Module initialization code
 *
 */
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
			proc_entry->read_proc = mp1_read_proc;
			proc_entry->write_proc = mp1_write_proc;

			printk(KERN_INFO "MP1 module loaded\n");

			/* Initialize a linked list for mp1 proc details */
			INIT_LIST_HEAD(&mp1_proc_list.list);
		}
	}

	return ret;
}

/* Func: mp1_exit_module
 * Desc: Module cleanup code
 *
 */
static void __exit mp1_exit_module(void)
{
	MP1_PROC_ENTRY *tmp,*swap;

	/* Remove the status entry first */
	remove_proc_entry("status", proc_dir);

	/* Remove the mp1 proc dir now */
	remove_proc_entry("mp1", NULL);

	printk(KERN_INFO "MP1 module unloaded\n");

	/* Delete each list entry and free the allocated structure */
	list_for_each_entry_safe(tmp, swap, &mp1_proc_list.list, list) {
		printk(KERN_INFO "freeing %u\n",tmp->pid);
		list_del(&tmp->list);
		kfree(tmp);
	}
}

module_init(mp1_init_module);
module_exit(mp1_exit_module);

MODULE_LICENSE("GPL");
