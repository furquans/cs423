/*
 * mp1_kernel_mod.c : Kernel module for mp1
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <asm/uaccess.h>
#include <linux/kthread.h>
#include <linux/sched.h>

#include "mp1_given.h"

/* Proc dir and proc entry to be added */
static struct proc_dir_entry *proc_dir, *proc_entry;

/* Entry to be maintained for each process in a list */
typedef struct mp1_proc_entry{
	struct list_head list;
	unsigned int pid;
	unsigned long cpu_time;
}MP1_PROC_ENTRY;

/* List head */
static MP1_PROC_ENTRY mp1_proc_list;

/* Kernel Thread */
static struct task_struct *mp1_kernel_thread;

/* Wait queue for kernel thread to wait on */
static DECLARE_WAIT_QUEUE_HEAD (mp1_waitqueue);

/* Timer for waking kernel thread periodically */
static struct timer_list mp1_timer;

/* Semaphore for synchronization on the list */
static struct semaphore mp1_sem;

/* Func: mp1_timer_callback
 * Desc: Timer callback to just wake up the kernel thread
 *
 */
void mp1_timer_callback(unsigned long data)
{
	/* Put the kernel thread into running state */
	wake_up_interruptible(&mp1_waitqueue);
}

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

	/* Enter critical region */
	if (down_interruptible(&mp1_sem)) {
		printk(KERN_INFO "mp1:unable to obtain sem\n");
		return 0;
	}

	/* Traverse the list and put values into page */
	list_for_each_entry(tmp, &mp1_proc_list.list, list) {
		len += sprintf(page+len, "%u:%lu\n",tmp->pid, tmp->cpu_time);
	}

	/* Exit critical region */
	up(&mp1_sem);

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
	int ret;

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

	/* Enter critical region */
	if (down_interruptible(&mp1_sem)) {
		printk(KERN_INFO "mp1:Unable to enter critical region\n");
		return 0;
	}

	/* For the first entry, start the timer */
	if (list_empty(&mp1_proc_list.list)) {
		printk(KERN_INFO "mp1:list is empty..starting timer\n");
		/* Starting timer for 5 secs from now */
		ret = mod_timer(&mp1_timer, jiffies + msecs_to_jiffies(5000));
		/* Not able to start the timer? */
		if (ret) {
			printk(KERN_INFO "mp1:Error in mod_timer\n");
		}
	}

	/* Add the entry to the head */
	list_add_tail(&(tmp->list), &(mp1_proc_list.list));

	/* Exit critical region */
	up(&mp1_sem);

	return len;
}

/* Func: mp1_kernel_thread_fn
 * Desc: Kernel thread to update the linked list of
 *       registered processes
 *
 */
int mp1_kernel_thread_fn(void *unused)
{
	MP1_PROC_ENTRY *tmp, *swap;
	int ret;

	/* Declare a waitqueue */
	DECLARE_WAITQUEUE(wait,current);

	/* Add wait queue to the head */
	add_wait_queue(&mp1_waitqueue,&wait);

	while(1) {
		/* Set current state to interruptible */
		set_current_state(TASK_INTERRUPTIBLE);

		/* give up the control */
		schedule();

		/* coming back to running state, check if it needs to stop */
		if (kthread_should_stop()) {
			printk(KERN_INFO "mp1:thread needs to stop\n");
			break;
		}

		printk(KERN_INFO "mp1:updating cpu time of processes\n");

		/* Enter critical region */
		if (down_interruptible(&mp1_sem)) {
			printk(KERN_INFO "mp1: Cannot enter critical region\n");
		}

		/* Traverse the list and update the cpu time for each registered
		   process */
		list_for_each_entry_safe(tmp, swap, &mp1_proc_list.list, list) {
			/* check for return value and update link list accordingly */
			if (get_cpu_use(tmp->pid, &tmp->cpu_time) == -1) {
				printk(KERN_INFO "mp1:deleting %u\n",tmp->pid);
				list_del(&tmp->list);
				kfree(tmp);
			}
		}

		if (list_empty(&mp1_proc_list.list)) {
			/* If list is now empty, we need not start the timer */
			printk(KERN_INFO "mp1:All entries removed. Not starting timer\n");
		} else {
			/* Start the timer here */
			ret = mod_timer(&mp1_timer, jiffies + msecs_to_jiffies(5000));

			/* Not able to start the timer? */
			if (ret) {
				printk(KERN_INFO "mp1:Error in mod_timer\n");
			}
		}

		/* Exit critical region */
		up(&mp1_sem);
	}

	/* exiting thread, set it to running state */
	set_current_state(TASK_RUNNING);
	/* remove the waitqueue */
	remove_wait_queue(&mp1_waitqueue, &wait);

	printk(KERN_INFO "mp1:thread killed\n");
	return 0;
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

			printk(KERN_INFO "mp1:MP1 module loaded\n");

			/* Initialize a linked list for mp1 proc details */
			INIT_LIST_HEAD(&mp1_proc_list.list);

			/* Initialize semaphore */
			sema_init(&mp1_sem,1);

			/* Create a kernel thread */
			mp1_kernel_thread = kthread_run(mp1_kernel_thread_fn,
							NULL,
							"mp1kt");

			/* If thread creation failed for some reason, cleanup */
			if (mp1_kernel_thread == NULL) {
				printk(KERN_INFO "mp1:thread not created\n");
				remove_proc_entry("status", proc_dir);
				remove_proc_entry("mp1", NULL);
				ret = -ENOMEM;
			} else {
				/* Setup the timer */
				setup_timer(&mp1_timer, mp1_timer_callback, 0);
			}
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

	/* Delete the timer */
	del_timer_sync(&mp1_timer);

	/* Remove the status entry first */
	remove_proc_entry("status", proc_dir);

	/* Remove the mp1 proc dir now */
	remove_proc_entry("mp1", NULL);

	printk(KERN_INFO "mp1:MP1 module unloaded\n");

	/* Delete each list entry and free the allocated structure */
	list_for_each_entry_safe(tmp, swap, &mp1_proc_list.list, list) {
		printk(KERN_INFO "mp1:freeing %u\n",tmp->pid);
		list_del(&tmp->list);
		kfree(tmp);
	}

	/* Before stopping the thread, put it into running state */
	wake_up_interruptible(&mp1_waitqueue);

	/* now stop the thread */
	kthread_stop(mp1_kernel_thread);
}

module_init(mp1_init_module);
module_exit(mp1_exit_module);

MODULE_LICENSE("GPL");
