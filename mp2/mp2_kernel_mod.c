/*
 * mp2_kernel_mod.c : Kernel module for mp2
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/list.h>

#include "mp2_given.h"

#define MP2_TASK_RUNNING  0
#define MP2_TASK_READY    1
#define MP2_TASK_SLEEPING 2

/* MP2 task struct */
struct mp2_task_struct {
	/* PID of the registered process */
	unsigned int pid;
	/* Pointer to the task_struct of the process */
	struct task_struct *task;
	/* Timer to wake up this process at the end of period */
	struct timer_list wakeup_timer;
	/* Computation time in milliseconds */
	unsigned int C;
	/* Period of the process */
	unsigned int P;
	/* List head for maintaining list of all registered processes */
	struct list_head task_list;
	/* List head for run queue */
	struct list_head mp2_rq_list;
	/* Time of next period in jiffies */
	u64 next_period;
	/* MP2 state of the task */
	unsigned int state;
};

/* Entries in procfs */
static struct proc_dir_entry *proc_dir, *proc_entry;

/* List for holding all the tasks registered with MP2 module */
static struct list_head mp2_task_struct_list;

/* Run queue list for the scheduler */
static struct list_head mp2_rq;

/* Currently running process */
static struct mp2_task_struct *mp2_current;

/* Wait queue for kernel thread to wait on */
static DECLARE_WAIT_QUEUE_HEAD (mp2_waitqueue);

/* Kernel Thread */
static struct task_struct *mp2_sched_kthread;

/* Semaphore for synchronization on the list */
static struct semaphore mp2_sem;

static unsigned long flags;

int mp2_read_proc(char *page, char **start, off_t off,
		  int count, int *eof, void *data)
{
	int len = 0, i=1;
	struct mp2_task_struct *tmp;

	/* Enter critical region */
        if (down_interruptible(&mp2_sem)) {
                printk(KERN_INFO "mp2:Unable to enter critical region\n");
                return 0;
        }

	/* Traverse the list and put values into page */
        list_for_each_entry(tmp, &mp2_task_struct_list, task_list) {
		len += sprintf(page+len, "Process # %d details:\n",i);
		len += sprintf(page+len, "PID:%u\n",tmp->pid);
		len += sprintf(page+len, "P:%u\n",tmp->P);
		len += sprintf(page+len, "C:%u\n",tmp->C);
		i++;
        }

        /* Exit critical region */
        up(&mp2_sem);

	return len;
}

void mp2_add_task_to_rq(struct mp2_task_struct *tmp)
{
	struct list_head *prev, *curr;
	struct mp2_task_struct *curr_task;

	tmp->state = MP2_TASK_READY;

	prev = &mp2_rq;
	curr = prev->next;
	list_for_each(curr, &mp2_rq) {
		curr_task = list_entry(curr, typeof(*tmp), mp2_rq_list);

		if (curr_task->P > tmp->P) {
			break;
		}
		prev = curr;
	}

	__list_add(&(tmp->mp2_rq_list),prev,curr);
}

void mp2_remove_task_from_rq(struct mp2_task_struct *tmp)
{
	list_del(&(tmp->mp2_rq_list));
}

struct mp2_task_struct *find_mp2_task_by_pid(unsigned int pid)
{
	struct mp2_task_struct *tmp;

	/* Enter critical region */
        if (down_interruptible(&mp2_sem)) {
                printk(KERN_INFO "mp2:Unable to enter critical region\n");
                return 0;
        }

	list_for_each_entry(tmp, &mp2_task_struct_list, task_list) {
		if (tmp->pid == pid) {
			break;
		}
	}

	if (&tmp->task_list == &mp2_task_struct_list) {
		printk(KERN_INFO "mp2: Task not found on list\n");
		tmp = NULL;
	}

	/* Exit critical region */
	up(&mp2_sem);

	return tmp;
}

void wakeup_timer_handler(unsigned long pid)
{
	struct mp2_task_struct *tmp = find_mp2_task_by_pid(pid);

	if (tmp == NULL) {
		printk(KERN_WARNING "mp2: task not found..strange!\n");
		return;
	}

	tmp->state = MP2_TASK_READY;

	mp2_add_task_to_rq(tmp);

	printk(KERN_INFO "mp2: Task added to rq:%d\n",tmp->pid);

	/* Wake up kernel scheduler thread */
	wake_up_interruptible(&mp2_waitqueue);
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

	/* Find the task struct */
	new_task->task = find_task_by_pid(new_task->pid);

	if (new_task->task == NULL) {
		printk(KERN_WARNING "mp2: Task not found\n");
		kfree(new_task);
		return;
	}

	new_task->next_period = jiffies + msecs_to_jiffies(new_task->P);

	/* Enter critical region */
        if (down_interruptible(&mp2_sem)) {
                printk(KERN_INFO "mp2:Unable to enter critical region\n");
                return;
        }

	/* Add entry to the list */
	list_add_tail(&(new_task->task_list), &mp2_task_struct_list);

	/* Exit critical region */
	up(&mp2_sem);

	/* Setup the timer for this task */
	setup_timer(&new_task->wakeup_timer, wakeup_timer_handler, new_task->pid);

	/* Mark mp2 task state as sleeping */
	new_task->state = MP2_TASK_SLEEPING;
}

void mp2_set_sched_priority(struct mp2_task_struct *tmp,
			    int policy,
			    int priority)
{
	struct sched_param sparam;

	sparam.sched_priority = priority;
	sched_setscheduler(tmp->task, policy, &sparam);
}

void mp2_deregister_process(char *user_data)
{
	unsigned int pid;
	struct mp2_task_struct *tmp;

	/* Extract PID */
	sscanf(user_data+3, "%u", &pid);

	/* Find the mp2 task struct for this pid */
	tmp = find_mp2_task_by_pid(pid);

	if (tmp) {
		printk(KERN_INFO "mp2: De-registration for PID:%u\n", pid);
		/* Remove the task from run queue */
		local_irq_save(flags);
		local_irq_disable();
		mp2_remove_task_from_rq(tmp);
		local_irq_restore(flags);
		local_irq_enable();
		/* Enter critical region */
		if (down_interruptible(&mp2_sem)) {
			printk(KERN_INFO "mp2:Unable to enter critical region\n");
			return;
		}
		/* Delete the task from mp2 task struct list */
		list_del(&tmp->task_list);
		/* Exit critical region */
		up(&mp2_sem);
		/* Delete the timer */
		del_timer_sync(&tmp->wakeup_timer);
		if (tmp == mp2_current) {
			/* Reset the priority to normal */
			mp2_set_sched_priority(tmp, SCHED_NORMAL, 0);
			mp2_current = NULL;
		}
		/* Free the structure */
		kfree(tmp);
		wake_up_interruptible(&mp2_waitqueue);
	} else {
		/* Deregister only registered processes */
		printk(KERN_INFO "mp2: No process with P:%u registered\n", pid);
	}
}

void mp2_yield_process(char *user_data)
{
	unsigned int pid;
	struct mp2_task_struct *tmp;
	u64 release_time;

	/* Extract PID */
	sscanf(user_data+3, "%u", &pid);

	/* Check if this is the current running process */
	if (mp2_current && (mp2_current->pid == pid)) {
		tmp = mp2_current;
	} else {
		/* Else check on the mp2_task_struct_list */
		tmp = find_mp2_task_by_pid(pid);
	}

	/* If process is not found on the list, something is wrong */
	if (tmp == NULL) {
		printk(KERN_WARNING "mp2: Task not found for yield:%u\n",pid);
		return;
	}

	/* Check if we still have time for next release */
	if (jiffies < tmp->next_period) {
		/* If yes, put this task in sleep state
		   remove it from rq(if present there,
		   and start the timer */
		release_time = tmp->next_period - jiffies;
		printk(KERN_INFO "mp2: release_time:%llu,%d\n", release_time,tmp->pid);

		/* Change the task state to SLEEPING */
		tmp->state = MP2_TASK_SLEEPING;

		/* Start the timer according to release time */
		mod_timer(&tmp->wakeup_timer, jiffies + release_time);

		/* If this task was currently executing,
		   remove it from run queue and wake up
		   scheduler thread */
		if (mp2_current && (mp2_current->pid == tmp->pid)) {
			local_irq_save(flags);
			local_irq_disable();
			mp2_remove_task_from_rq(tmp);
			local_irq_restore(flags);
			local_irq_enable();

			printk(KERN_INFO "removed from rq\n");
			mp2_current = NULL;
			wake_up_interruptible(&mp2_waitqueue);
		}
		/* Lower the priority of the task */
		mp2_set_sched_priority(tmp, SCHED_NORMAL, 0);

		set_task_state(tmp->task, TASK_UNINTERRUPTIBLE);
	} else {
		/* Process needs to be on run queue
		   If in sleeping state, move it to run queue
		*/
		if (tmp->state == MP2_TASK_SLEEPING) {
			tmp->state = MP2_TASK_READY;
			local_irq_save(flags);
			local_irq_disable();
			mp2_add_task_to_rq(tmp);
			local_irq_restore(flags);
			local_irq_enable();
		}
		wake_up_interruptible(&mp2_waitqueue);
	}

	schedule();
	printk(KERN_INFO "mp2: Yield for %u\n",pid);
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

int mp2_sched_kthread_fn(void *unused)
{
	struct mp2_task_struct *tmp;

	/* Declare a wait queue */
	DECLARE_WAITQUEUE(wait,current);

	/* Add wait queue to the head */
	add_wait_queue(&mp2_waitqueue,&wait);

	printk(KERN_INFO "mp2: Schedule Thread created\n");

	while (1) {
		printk(KERN_INFO "mp2: Schedule thread sleeping\n");
		/* Set current state to interruptible */
		set_current_state(TASK_INTERRUPTIBLE);

		/* give up the control */
		schedule();

		/* coming back to running state, check if it needs to stop */
		if (kthread_should_stop()) {
                        printk(KERN_INFO "mp2: Thread needs to stop\n");
                        break;
                }

		printk(KERN_INFO "mp2: Schedule function running\n");

		/* Check if we have anything on the runqueue */
		if (!list_empty(&mp2_rq)) {
			/* If there is a task waiting on the run queue,
			   and has a higher priority than current running task if any
			   schedule it */
			local_irq_save(flags);
			local_irq_disable();
			tmp = list_first_entry(&mp2_rq, typeof(*tmp), mp2_rq_list);
			local_irq_restore(flags);
			local_irq_enable();
			/* If there is some task running currenly,
			   put it into ready state */
			if (mp2_current) {
				if (mp2_current->P > tmp->P) {
					printk(KERN_INFO "mp2: Scheduling out current process\n");
					mp2_set_sched_priority(mp2_current, SCHED_NORMAL, 0);
					set_task_state(mp2_current->task, TASK_UNINTERRUPTIBLE);
					mp2_current->state = MP2_TASK_READY;
					mp2_current = NULL;
				}
				else {
					printk(KERN_INFO "mp2: currently running process has higher prio\n");
					continue;
				}
			}

			wake_up_process(tmp->task);
			mp2_set_sched_priority(tmp, SCHED_FIFO, MAX_USER_RT_PRIO - 1);
			mp2_current = tmp;
			mp2_current->state = MP2_TASK_RUNNING;
			printk(KERN_INFO "next task running:%d\n",tmp->pid);
			do {
				mp2_current->next_period += msecs_to_jiffies(mp2_current->P);
			} while(mp2_current->next_period < jiffies);
		} else {
			printk(KERN_INFO "nothing on runqueue\n");
		}
	}

	/* exiting thread, set it to running state */
        set_current_state(TASK_RUNNING);

        /* remove the waitqueue */
        remove_wait_queue(&mp2_waitqueue, &wait);

        printk(KERN_INFO "mp2: Schedule thread killed\n");
        return 0;
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

			/* Initialize list head for MP2 run queue */
			INIT_LIST_HEAD(&mp2_rq);

			/* Initialize semaphore */
                        sema_init(&mp2_sem,1);

			/* Initialize current running mp2 task as NULL */
			mp2_current = NULL;

                        /* Create a kernel thread */
                        mp2_sched_kthread = kthread_run(mp2_sched_kthread_fn,
                                                        NULL,
                                                        "mp2_sched_kthread");

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

	/* Remove the mp2 proc dir now */
	remove_proc_entry("mp2", NULL);

	/* Enter critical region */
        if (down_interruptible(&mp2_sem)) {
                printk(KERN_INFO "mp2:Unable to enter critical region\n");
                return;
        }

	/* Delete each list entry and free the allocated structure */
        list_for_each_entry_safe(tmp, swap, &mp2_task_struct_list, task_list) {
		printk(KERN_INFO "mp2: freeing %u\n",tmp->pid);
		list_del(&tmp->task_list);
		kfree(tmp);
        }

	/* Exit critical region */
	up(&mp2_sem);

	/* Before stopping the thread, put it into running state */
        wake_up_interruptible(&mp2_waitqueue);

        /* now stop the thread */
        kthread_stop(mp2_sched_kthread);

 	printk(KERN_INFO "mp2: Module unloaded\n");
}

module_init(mp2_init_module);
module_exit(mp2_exit_module);

MODULE_LICENSE("GPL");
