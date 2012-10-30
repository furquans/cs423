/*
 * mp3_kernel_mod.c : Kernel module for mp3
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <asm/uaccess.h>

#include "mp3_given.h"

struct mp3_task_struct {
	/* PID of the registered process */
	unsigned int pid;
	/* Pointer to the task_struct of the process */
	struct task_struct *task;
	/* Processor utilization */
	unsigned int proc_util;
	/* Major Fault Count */
	unsigned int major_fault;
	/* Minor Fault Count */
	unsigned int minor_fault;
        /* List head for maintaining list of all registered processes */
        struct list_head task_list;
};

/* Proc dir and proc entry to be added */
static struct proc_dir_entry *proc_dir, *proc_entry;

/* List for holding all the tasks registered with MP3 module */
static struct list_head mp3_task_struct_list;

/* Semaphore for synchronization on the list */
static struct semaphore mp3_sem;

/* Func: find_mp3_task_by_pid
 * Desc: Find the mp3 task struct of given pid
 *
 */
struct mp3_task_struct *find_mp3_task_by_pid(unsigned int pid)
{
        struct mp3_task_struct *tmp;

        /* Enter critical region */
        if (down_interruptible(&mp3_sem)) {
                printk(KERN_INFO "mp3:Unable to enter critical region\n");
                return 0;
        }

        /* Scan through the task list */
        list_for_each_entry(tmp, &mp3_task_struct_list, task_list) {
                if (tmp->pid == pid) {
                        break;
		}
        }

        /* Check if task is not present. Return NULL in this case */
        if (&tmp->task_list == &mp3_task_struct_list) {
                printk(KERN_INFO "mp3: Task not found on list\n");
                tmp = NULL;
        }

        /* Exit critical region */
        up(&mp3_sem);

        /* return the mp3 task struct */
        return tmp;
}

/* Func: mp3_register_process
 * Desc: Register the process with mp3 module
 *
 */
void mp3_register_process(unsigned int pid)
{
	struct mp3_task_struct *new_task;

	/* Create a new mp3_task_struct */
	new_task = kmalloc(sizeof(*new_task), GFP_KERNEL);

	/* Copy the pid */
	new_task->pid = pid;

	/* Find the task struct */
	new_task->task = find_task_by_pid(new_task->pid);

	/* Enter critical region */
        if (down_interruptible(&mp3_sem)) {
		printk(KERN_INFO "mp3:Unable to enter critical region\n");
                return;
        }

	new_task->proc_util =
		new_task->major_fault =
		new_task->minor_fault = 0;

        /* Add entry to the list */
	list_add_tail(&(new_task->task_list), &mp3_task_struct_list);

        /* Exit critical region */
	up(&mp3_sem);
}

/* Func: mp3_deregister_process
 * Desc: Deregister the process from mp3 module
 *
 */
void mp3_deregister_process(unsigned int pid)
{
	struct mp3_task_struct *tmp;

	tmp = find_mp3_task_by_pid(pid);

	if (tmp) {
		/* Enter critical region */
                if (down_interruptible(&mp3_sem)) {
                        printk(KERN_INFO "mp3:Unable to enter critical region\n");
                        return;
                }
                /* Delete the task from mp3 task struct list */
                list_del(&tmp->task_list);
                /* Exit critical region */
                up(&mp3_sem);
		kfree(tmp);
	} else {
		/* Deregister only registered processes */
                printk(KERN_INFO "mp3: No process with PID:%u registered\n", pid);
	}
}

/* Func: mp3_read_proc
 * Desc: Read the list and provide pid and cpu time of each registered
 *       process to the user
 *
 */
int mp3_read_proc(char *page, char **start, off_t off,
		  int count, int *eof, void *data)
{
        int len = 0, i=1;
        struct mp3_task_struct *tmp;

        /* Enter critical region */
        if (down_interruptible(&mp3_sem)) {
                printk(KERN_INFO "mp3:Unable to enter critical region\n");
                return 0;
        }

        /* Traverse the list and put values into page */
        list_for_each_entry(tmp, &mp3_task_struct_list, task_list) {
                len += sprintf(page+len, "Process # %d details:\n",i);
                len += sprintf(page+len, "PID:%u\n",tmp->pid);
                len += sprintf(page+len, "Util:%u\n",tmp->proc_util);
                len += sprintf(page+len, "major fault:%u\n",tmp->major_fault);
                len += sprintf(page+len, "minor fault:%u\n",tmp->minor_fault);
                i++;
        }

        /* Exit critical region */
        up(&mp3_sem);

	/* Return length of data being sent */
	return len;
}

/* Func: mp3_write_proc
 * Desc: Copy the pid sent from user process and make a new entry in the
 *       list
 *
 */
int mp3_write_proc(struct file *filp, const char __user *buff,
		   unsigned long len, void *data)
{
#define MAX_USER_DATA_LEN 50
	char user_data[MAX_USER_DATA_LEN];
	unsigned int pid;

	if (len > MAX_USER_DATA_LEN) {
		len = MAX_USER_DATA_LEN;
		printk("mp3: Truncating user data\n");
	}

	/* Copy data from user */
	if (copy_from_user(user_data,
                           buff,
                           len)) {
                return -EFAULT;
        }

	/* Obtain PID */
	sscanf(user_data + 2, "%u", &pid);

	/* Switch according to action */
	switch (user_data[0]) {
	case 'R':
		printk(KERN_INFO "mp3: Registration:%u\r\n",pid);
		mp3_register_process(pid);
		break;
	case 'U':
		printk(KERN_INFO "mp3: Deregistration:%u\r\n",pid);
		mp3_deregister_process(pid);
		break;
	default:
		printk(KERN_INFO "mp3: Invalid action\r");
	}

	return len;
}

/* Func: mp3_init_module
 * Desc: Module initialization code
 *
 */
static int __init mp3_init_module(void)
{
	int ret = 0;

	/* Create a proc directory entry mp3 */
	proc_dir = proc_mkdir("mp3", NULL);

	/* Check if directory was created */
	if (proc_dir == NULL) {
		printk(KERN_INFO "mp3: Couldn't create proc dir\n");
		ret = -ENOMEM;
	} else {
		/* Create an entry status under proc dir mp3 */
		proc_entry = create_proc_entry( "status", 0666, proc_dir);

		/*Check if entry was created */
		if (proc_entry == NULL) {
			printk(KERN_INFO "mp3: Couldn't create proc entry\n");
			ret = -ENOMEM;
		} else {

			/* proc_entry->owner = THIS_MODULE; */
			proc_entry->read_proc = mp3_read_proc;
			proc_entry->write_proc = mp3_write_proc;

			/* Initialize list head for MP3 task struct */
			INIT_LIST_HEAD(&mp3_task_struct_list);

                        /* Initialize semaphore */
                        sema_init(&mp3_sem,1);

			printk(KERN_INFO "MP3 module loaded\n");
		}
	}

	return ret;
}

/* Func: mp3_exit_module
 * Desc: Module cleanup code
 *
 */
static void __exit mp3_exit_module(void)
{
	struct mp3_task_struct *tmp, *swap;

	/* Remove the status entry first */
	remove_proc_entry("status", proc_dir);

	/* Remove the mp3 proc dir now */
	remove_proc_entry("mp3", NULL);

	/* Enter critical region */
        if (down_interruptible(&mp3_sem)) {
                printk(KERN_INFO "mp3:Unable to enter critical region\n");
		return;
	}

	/* Delete each list entry and free the allocated structure */
        list_for_each_entry_safe(tmp, swap, &mp3_task_struct_list, task_list) {
                printk(KERN_INFO "mp3: freeing %u\n",tmp->pid);
		list_del(&tmp->task_list);
                kfree(tmp);
        }

        /* Exit critical region */
	up(&mp3_sem);

	printk(KERN_INFO "MP3 module unloaded\n");
}

module_init(mp3_init_module);
module_exit(mp3_exit_module);

MODULE_LICENSE("GPL");
