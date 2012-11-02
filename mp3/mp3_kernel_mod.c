/*
 * mp3_kernel_mod.c : Kernel module for mp3
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <asm/uaccess.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>

#include "mp3_given.h"

#define NPAGES 130

struct mp3_task_struct {
	/* PID of the registered process */
	unsigned int pid;
	/* Pointer to the task_struct of the process */
	struct task_struct *task;
	/* Processor utilization */
	unsigned long proc_util;
	/* Major Fault Count */
	unsigned long major_fault;
	/* Minor Fault Count */
	unsigned long minor_fault;
        /* List head for maintaining list of all registered processes */
        struct list_head task_list;
};

/* Proc dir and proc entry to be added */
static struct proc_dir_entry *proc_dir, *proc_entry;

/* List for holding all the tasks registered with MP3 module */
static struct list_head mp3_task_struct_list;

/* Semaphore for synchronization on the list */
static struct semaphore mp3_sem;

/* Handler function for mp3 work queue */
static void mp3_timer_handler(struct work_struct *);

/* Delay for work queue */
static unsigned long delay;

/* Work queue for mp3 bottom half handling */
static struct workqueue_struct *mp3_wq = 0;
static DECLARE_DELAYED_WORK(mp3_work, &mp3_timer_handler);

/* Buffer to be shared with user space process */
static unsigned long *vmalloc_buffer;
static int buff_ptr = 0;

static int mp3_dev_major, mp3_dev_minor = 0;
static int mp3_nr_devs = 1;
static dev_t mp3_dev;
static struct cdev *mp3_cdev;

int mp3_dev_mmap(struct file *, struct vm_area_struct *);

static struct file_operations mp3_dev_fops = {
	.owner = THIS_MODULE,
	.open = NULL,
	.release = NULL,
	.mmap = mp3_dev_mmap,
};

int mp3_dev_mmap(struct file *fp, struct vm_area_struct *vma)
{
	int ret,i;
	unsigned long length = vma->vm_end - vma->vm_start;

	if (length > NPAGES * PAGE_SIZE) {
		return -EIO;
	}

	for (i=0; i < length; i+=PAGE_SIZE) {

		if ((ret = remap_pfn_range(vma,
					   vma->vm_start + i,
					   vmalloc_to_pfn((void*)(((unsigned long)vmalloc_buffer)
								  + i)),
					   PAGE_SIZE,
					   vma->vm_page_prot)) < 0) {
		printk(KERN_INFO "mp3:mmap failed");
		return ret;
		}
	}
	printk(KERN_INFO "mp3:mmap successful");
	return 0;
}

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

/* Func: mp3_timer_handler
 * Desc: Timer handler for work queue
 *
 */
static void mp3_timer_handler(struct work_struct *dummy)
{
	struct mp3_task_struct *tmp;
	unsigned long maj, min, cpu;
	unsigned long total_maj = 0, total_min = 0, total_cpu = 0;

	printk(KERN_INFO "mp3:timer handler for work queue");

        if (down_interruptible(&mp3_sem)) {
		printk(KERN_INFO "mp3:Unable to enter critical region\n");
                return;
        }

	/* Scan through the list to update params for all processes */
	list_for_each_entry(tmp, &mp3_task_struct_list, task_list) {
		if (get_cpu_use(tmp->pid,
				&min,
				&maj,
				&cpu) == -1) {
			printk(KERN_INFO "mp3:Task Not found %u",tmp->pid);
			continue;
		}

		total_min += min;
		total_maj += maj;
		total_cpu += cpu;

		tmp->minor_fault = min;
		tmp->major_fault = maj;
		tmp->proc_util = cpu;
        }

	up(&mp3_sem);

	vmalloc_buffer[buff_ptr++] = jiffies;
	vmalloc_buffer[buff_ptr++] = total_min;
	vmalloc_buffer[buff_ptr++] = total_maj;
	vmalloc_buffer[buff_ptr++] = total_cpu;

	printk(KERN_INFO "\n\nmp3:Jiffies:%lu",jiffies);
	printk(KERN_INFO "Min:%lu",total_min);
	printk(KERN_INFO "Maj:%lu",total_maj);
	printk(KERN_INFO "Cpu:%lu",total_cpu);

	if (mp3_wq) {
		queue_delayed_work(mp3_wq, &mp3_work, delay);
	}
}

/* Func: mp3_create_wq
 * Desc: Create a work queue
 *
 */
void mp3_create_wq(void)
{
	printk(KERN_INFO "mp3:Creating work queue");
	if (!mp3_wq) {
		mp3_wq = create_singlethread_workqueue("mp3_work");
	}
	if (mp3_wq) {
		queue_delayed_work(mp3_wq, &mp3_work, delay);
	}
}

/* Func: mp3_destroy_wq
 * Desc: Destroy the work queue
 *
 */
void mp3_destroy_wq(void)
{
	if (mp3_wq) {
		cancel_delayed_work(&mp3_work);
		flush_workqueue(mp3_wq);
		destroy_workqueue(mp3_wq);
		printk(KERN_INFO "mp3:Deleted work queue");
		printk(KERN_INFO "bufctr: %d",buff_ptr);
	}
	mp3_wq = NULL;
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

	if (list_empty(&mp3_task_struct_list)) {
		mp3_create_wq();
	}

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
		return;
	}

	if (list_empty(&mp3_task_struct_list)) {
		mp3_destroy_wq();
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
                len += sprintf(page+len, "Util:%lu\n",tmp->proc_util);
                len += sprintf(page+len, "major fault:%lu\n",tmp->major_fault);
                len += sprintf(page+len, "minor fault:%lu\n",tmp->minor_fault);
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

/* Func: allocate_buffer
 * Desc: Allocate buffer to share with user
 *
 */
static int allocate_buffer(void)
{
	int i;

	if ((vmalloc_buffer = vzalloc(NPAGES * PAGE_SIZE)) == NULL) {
		return -ENOMEM;
	}

	buff_ptr = 0;

	for (i = 0;i < NPAGES*PAGE_SIZE;i += PAGE_SIZE) {
		SetPageReserved(vmalloc_to_page((void*)(((unsigned long)vmalloc_buffer)
							+ i)));
	}
	return 0;
}

static int mp3_create_char_dev(void)
{
	int result = 0;

	result = alloc_chrdev_region(&mp3_dev,
				     mp3_dev_minor,
				     mp3_nr_devs,
				     "mp3_char_dev");
	mp3_dev_major = result;
	if (result < 0) {
		printk(KERN_INFO "mp3: Char dev cannot get major\n");
		return result;
	}

	mp3_cdev = cdev_alloc();
	mp3_cdev->ops = &mp3_dev_fops;
	mp3_cdev->owner = THIS_MODULE;

	result = cdev_add(mp3_cdev, mp3_dev, 1);
	if (result) {
		printk(KERN_INFO "mp3: Error adding device\n");
		return result;
	}
	return result;
}

static void mp3_delete_char_dev(void)
{
	cdev_del(mp3_cdev);
	unregister_chrdev_region(mp3_dev, mp3_nr_devs);
}

/* Func: mp3_init_module
 * Desc: Module initialization code
 *
 */
static int __init mp3_init_module(void)
{
	int ret = 0;

	/* Initialize delay */
	delay = msecs_to_jiffies(50);

	/* Create a proc directory entry mp3 */
	proc_dir = proc_mkdir("mp3", NULL);

	/* Check if directory was created */
	if (proc_dir == NULL) {
		printk(KERN_INFO "mp3: Couldn't create proc dir\n");
		ret = -ENOMEM;
		goto clear_alloc;
	}
	/* Create an entry status under proc dir mp3 */
	proc_entry = create_proc_entry( "status", 0666, proc_dir);

	/*Check if entry was created */
	if (proc_entry == NULL) {
		printk(KERN_INFO "mp3: Couldn't create proc entry\n");
		ret = -ENOMEM;
		goto clear_alloc;
	}

	/* proc_entry->owner = THIS_MODULE; */
	proc_entry->read_proc = mp3_read_proc;
	proc_entry->write_proc = mp3_write_proc;

	/* Initialize list head for MP3 task struct */
	INIT_LIST_HEAD(&mp3_task_struct_list);

	/* Initialize semaphore */
	sema_init(&mp3_sem,1);

	/* Allocate buffer to share with user */
	if (allocate_buffer() == -ENOMEM) {
		ret = -ENOMEM;
		goto clear_alloc;
	}

	if ((ret = mp3_create_char_dev()) != 0) {
		goto clear_alloc;
	}

	printk(KERN_INFO "MP3 module loaded\n");

	return ret;
 clear_alloc:
	if (proc_entry) {
		remove_proc_entry("status", proc_dir);
	}
	if (proc_dir) {
		remove_proc_entry("mp3", NULL);
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
	int i;

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

	mp3_destroy_wq();
	mp3_delete_char_dev();

	for (i = 0;i < NPAGES*PAGE_SIZE;i += PAGE_SIZE) {
		ClearPageReserved(vmalloc_to_page((void*)(((unsigned long)vmalloc_buffer)
							  + i)));
	}
	vfree(vmalloc_buffer);

	printk(KERN_INFO "MP3 module unloaded\n");
}

module_init(mp3_init_module);
module_exit(mp3_exit_module);

MODULE_LICENSE("GPL");
