/*
 * mp2_kernel_mod.c : Kernel module for mp2
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/sched.h>

/* Entries in procfs */
static struct proc_dir_entry *proc_dir, *proc_entry;

/* List for holding all the tasks registered with MP2 module */
struct list_head mp2_task_struct_list;

/* MP2 task struct */
struct mp2_task_struct {
	/* PID of the registered process */
	unsigned int pid;
	/* Pointer to the task_struct of the process */
	struct task_struct *linux_task;
	/* Timer to wake up this process at the end of period */
	struct timer_list wakeup_timer;
	/* Computation time in milliseconds */
	unsigned int C;
	/* Period of the process */
	unsigned int P;
	/* List head for maintaining list of all registered processes */
	struct list_head list;
};


int mp2_read_proc(char *page, char **start, off_t off,
		  int count, int *eof, void *data)
{
	int len = 0, i=1;
	struct mp2_task_struct *tmp;

	/* Traverse the list and put values into page */
        list_for_each_entry(tmp, &mp2_task_struct_list, list) {
		len += sprintf(page+len, "Process # %d details:\n",i);
		len += sprintf(page+len, "PID:%u\n",tmp->pid);
		len += sprintf(page+len, "P:%u\n",tmp->P);
		len += sprintf(page+len, "C:%u\n",tmp->C);
		i++;
        }

	return len;
}

void mp2_register_process(char *user_data)
{
	char *tmp;
	struct mp2_task_struct *new_task;

	/* Create a new mp2_task_struct entry */
	new_task = kmalloc(sizeof(*new_task), GFP_KERNEL);

	/* Advance to PID in the string */
	user_data += 3;

	/* Extract the PID */
	tmp = strchr(user_data, ',');
	*tmp = '\0';
	sscanf(user_data, "%u", &new_task->pid);

	/* Advance to Period in the string */
	user_data = tmp + 2;

	/* Extract the period */
	tmp = strchr(user_data, ',');
	*tmp = '\0';
	sscanf(user_data, "%u", &new_task->P);

	/* Advance to Computation time in the string */
	user_data = tmp + 2;
	sscanf(user_data, "%u", &new_task->C);

	printk(KERN_INFO "mp2: Registration for PID:%u with P:%u and C:%u\n",
	       new_task->pid,
	       new_task->P,
	       new_task->C);

	/* Add entry to the list */
	list_add_tail(&(new_task->list), &mp2_task_struct_list);
}

void mp2_deregister_process(char *user_data)
{
	unsigned int pid;
	struct mp2_task_struct *tmp, *swap;
	char done = 0;

	/* Extract PID */
	sscanf(user_data+3, "%u", &pid);

	/* Check if PID exists and remove from the list */
	list_for_each_entry_safe(tmp, swap, &mp2_task_struct_list, list) {
		if (tmp->pid == pid) {
			printk(KERN_INFO "mp2: De-registration for PID:%u\n", pid);
			list_del(&tmp->list);
			kfree(tmp);
			done = 1;
			break;
		}
        }

	/* If process not found, report back to user */
	if (done == 0) {
		printk(KERN_INFO "mp2: No process with P:%u registered\n", pid);
	}

}

void mp2_yield_process(char *user_data)
{
	printk(KERN_INFO "mp2: Yield\n");
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

			/* Initialize list head for MP2 task struct */
			INIT_LIST_HEAD(&mp2_task_struct_list);

			/* MP2 module is now loaded */
			printk(KERN_INFO "mp2: Module loaded\n");
		}
	}

	return ret;
}

static void __exit mp2_exit_module(void)
{
	struct mp2_task_struct *tmp, *swap;

	/* Remove the status entry first */
	remove_proc_entry("status", proc_dir);

	/* Remove the mp1 proc dir now */
	remove_proc_entry("mp2", NULL);

	/* Delete each list entry and free the allocated structure */
        list_for_each_entry_safe(tmp, swap, &mp2_task_struct_list, list) {
		printk(KERN_INFO "mp2: freeing %u\n",tmp->pid);
		list_del(&tmp->list);
		kfree(tmp);
        }

	printk(KERN_INFO "mp2: Module unloaded\n");
}

module_init(mp2_init_module);
module_exit(mp2_exit_module);

MODULE_LICENSE("GPL");
