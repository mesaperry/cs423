#define LINUX

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/page-flags.h>
#include <linux/cdev.h>

#include "mp3_given.h"

#define PROC_FILENAME "status"
#define PROC_DIRNAME "mp3"
#define CDEV_NAME "mp3"
#define CDEV_COUNT 1
#define RW_PERMISSION 0666                      // allows read, write but not execute
#define BUFF_SIZE 128
#define DECIMAL_BASE 10
#define MP3_BUFF_SIZE 2 << 19					// 512 KB
#define SAMPLING_RATE_HZ 20
#define SAMPLING_PERIOD_MS 1000 / SAMPLING_RATE_HZ
#define SAMPLE_LENGTH 4
#define QUEUE_LENGTH 20 * 600

MODULE_LICENSE("GPL");
MODULE_AUTHOR("mesagp2");

#define DEBUG 1

struct aug_task_struct {
	struct task_struct *linux_task;
    struct list_head list;
    pid_t pid;
    unsigned long proc_util;
    unsigned long maj_fault_ct;
    unsigned long min_fault_ct;
};

static struct work_struct work;
static struct delayed_work dwork;

static struct aug_task_struct pcb_list;
static struct mutex list_mutex;

static struct proc_dir_entry *procfs_dir;
static struct proc_dir_entry *procfs_entry;

static struct cdev *mp3_cdev;

static unsigned long *mp3buf;

static unsigned long queue_ct;

/* work function handler */
static void work_handler(struct work_struct *arg) {
	struct aug_task_struct *this_pcb;
	struct list_head *this_node;
	unsigned long min_flt, maj_flt, utime, stime, cpu_util;
	int res;

    /* iterate through PCBs */
	mutex_lock(&list_mutex);
    list_for_each(this_node, &pcb_list.list) {
        /* get process values */
        this_pcb = list_entry(this_node, struct aug_task_struct, list);
		res = get_cpu_use(this_pcb->pid, &min_flt, &maj_flt, &utime, &stime);
		if (res != 0) {
			printk(KERN_ALERT "ERROR: Couldn't find process from PID\n");
		}
		cpu_util = (utime + stime) * 100 / msecs_to_jiffies(SAMPLING_PERIOD_MS);

		mp3buf[queue_ct*SAMPLE_LENGTH] = jiffies;
		mp3buf[queue_ct*SAMPLE_LENGTH+1] = min_flt;
		mp3buf[queue_ct*SAMPLE_LENGTH+2] = maj_flt;
		mp3buf[queue_ct*SAMPLE_LENGTH+3] = cpu_util;

		/* increment to next sample in queue */
		queue_ct = (queue_ct + 1) % QUEUE_LENGTH;
    }
	mutex_unlock(&list_mutex);

	/* repeat at constant rate */
	schedule_delayed_work(&dwork, msecs_to_jiffies(SAMPLING_PERIOD_MS));
}

/* places next arg into buffer and returns size */
static size_t get_next_arg(char const *buff, char *arg_buff, loff_t pos_init) {
	size_t arg_size;
	loff_t pos;

	/* start at input position */
	pos = pos_init;

	/* skip past whitespace and commas to argument */
	while (buff[pos] == ' ' || buff[pos] == ',') {
		pos++;
	}

	/* extract arg */
	for ( 	arg_size = 0;
		  	buff[pos] != '\0' && buff[pos] != '\n' && buff[pos] != ',';
		  	pos++, arg_size++ ) {
		arg_buff[arg_size] = buff[pos];
	}

	/* null terminate arg buffer */
	arg_buff[arg_size++] = '\0';

	return arg_size;
}

/* creates an augmented PCB */
static struct aug_task_struct* _init_aug_pcb(pid_t pid) {
	struct task_struct *pcb;
	struct aug_task_struct *aug_pcb;

	/* get the userapp's task_struct */
	pcb = find_task_by_pid(pid);

	/* allocate cache for PCB */
	aug_pcb = (struct aug_task_struct*) kmalloc( sizeof(struct aug_task_struct),
												 GFP_KERNEL );

	/* populate PCB members */
	aug_pcb->linux_task = pcb;
	aug_pcb->pid = pid;

	/* add PCB to list */
	list_add(&aug_pcb->list, &pcb_list.list);

	return aug_pcb;
}

/* deletes an augmented PCB */
static void _del_aug_pcb(pid_t pid) {
	struct aug_task_struct *this_pcb;
	struct list_head *this_node, *temp;

    /* iterate through PCBs */
    list_for_each_safe(this_node, temp, &pcb_list.list) {
        /* get PCB */
        this_pcb = list_entry(this_node, struct aug_task_struct, list);

        /* target PCB with matching pid */
        if (this_pcb->pid == pid) {
            list_del(this_node);
            kfree(this_pcb);
        }
    }
}

/* helper function for proc_write */
static void register_pid(pid_t pid) {
	#ifdef DEBUG
	printk(KERN_ALERT "Registering PID: %d\n", pid);
	#endif

    mutex_lock(&list_mutex);

	/* create workqueue job if this is the first process */
	if (list_empty(&pcb_list.list)) {
		schedule_work(&work);
	}

	/* create augmented PCB and add to list */
	_init_aug_pcb(pid);

    mutex_unlock(&list_mutex);
}

/* helper function for proc_write */
static void unregister_pid(pid_t pid) {
	#ifdef DEBUG
	printk(KERN_ALERT "Unregistering PID: %d\n", pid);
	#endif

    mutex_lock(&list_mutex);

	/* delete augmented PCB from list and memory */
	_del_aug_pcb(pid);

	/* delete work queue if there's no more processes */
	if (list_empty(&pcb_list.list)) {
		cancel_delayed_work(&dwork);
		flush_scheduled_work();
	}

    mutex_unlock(&list_mutex);
}

/* loads PIDS into the buffer */
static int print_pids(char *buff, size_t count) {
	struct aug_task_struct *pcb;
	loff_t pos;
	int res;

	pos = 0;
	res = 0;

	/* enter critical section */
	mutex_lock(&list_mutex);

	/* iterate through each process */
	list_for_each_entry(pcb, &pcb_list.list, list) {

		/* convert PID to string and insert into buffer */
		res = snprintf(buff + pos, count - pos, "%d", pcb->pid);
		if (res < 0) {
			/* return if error */
			return res;
		}

		/* increment position by number of written characters */
		pos += res;

		/* insert newline */
		buff[pos++] = '\n';

	}

	/* exit critical section */
	mutex_unlock(&list_mutex);

	/* null terminate string */
	buff[pos++] = '\0';

	return pos;
}

/* outputs a list of processes and their scheduling parameters */
static ssize_t procfs_read( struct file *file, char __user *buffer,
									size_t count, loff_t *data ) {
   	char procfs_buffer[BUFF_SIZE];
   	int res;

	/* should only read from pos 0, handle read termination */
	if (*data > 0) {
		return 0;
	}
	
	/* generate output */
	res = print_pids(procfs_buffer, count);
	if (res < 0) {
		/* error handling */
		return res;
	}

	/* copy buffer to user space */
	return simple_read_from_buffer(buffer, count, data, procfs_buffer, res);
}

/* interface for userapps to register, yield, or de-register */
static ssize_t procfs_write( struct file *file, const char __user *buffer,
									size_t count, loff_t *data ) {
	char procfs_buff[BUFF_SIZE];
	ssize_t procfs_size;
	char arg_buff[BUFF_SIZE];
	pid_t pid;
	int error;

	/* should only write to pos 0 */
	if (*data > 0) {
		return -EFAULT;
	}

	/* copy buffer into kernel space */
	procfs_size = simple_write_to_buffer( procfs_buff,
										  BUFF_SIZE,
										  data,
										  buffer,
										  count );
	if (procfs_size < 0) {
		/* error handling */
		return procfs_size;
	}

	/* get PID from args */
	get_next_arg(procfs_buff, arg_buff, 1);
	error = kstrtoint(arg_buff, DECIMAL_BASE, &pid);
	if (error) {
		return error;
	}

	/* handle registration or unregistration */
	switch (procfs_buff[0]) {
		case 'R':
			register_pid(pid);
			break;

		case 'U':
			unregister_pid(pid);
			break;
	}

    return procfs_size;
}

/* stores links read and write functions to file */
static const struct file_operations procfs_fops = {
	.owner  	= THIS_MODULE,
	.read   	= procfs_read,
	.write  	= procfs_write
};

static int cdev_open(struct inode *inode, struct file *file) {
    return 0;
}

static int cdev_release(struct inode *inode, struct file *file) {
    return 0;
}

static int cdev_mmap(struct file *file, struct vm_area_struct *vm_area) {
    return 0;
}

/* file operations for the character device driver */
static const struct file_operations cdev_fops = {
	.owner		= THIS_MODULE,
	.open		= cdev_open,
	.release    = cdev_release,
    .mmap       = cdev_mmap
};

/* called when module is loaded */
static int __init mp3_init(void) {
	int i;
    int res;
    dev_t device_num;

	#ifdef DEBUG
	printk(KERN_ALERT "mp3 MODULE LOADING\n");
	#endif

	/* init mutex's */
	mutex_init(&list_mutex);

	/* init augmented PCB list */
	INIT_LIST_HEAD(&pcb_list.list);

	/* init work structs */
	INIT_WORK(&work, work_handler);
	INIT_DELAYED_WORK(&dwork, work_handler);

	/* initialize shared memory buffer, set PG_reserved bit */
	mp3buf = vmalloc(MP3_BUFF_SIZE);
	for(i = 0; i < MP3_BUFF_SIZE; i += PAGE_SIZE) {
		SetPageReserved(vmalloc_to_page((void *)((unsigned long)mp3buf + i)));
	}
	queue_ct = 0;

    /* init and add character device to kernel */
    res = alloc_chrdev_region(&device_num, 0, CDEV_COUNT, CDEV_NAME);
    if (res != 0) {
        return res;
    }
    cdev_init(mp3_cdev, &cdev_fops);
    res = cdev_add(mp3_cdev, device_num, CDEV_COUNT);
    if (res != 0) {
        return res;
    }

	/* make procfs entry */
	procfs_dir = proc_mkdir(PROC_DIRNAME, NULL);
	if (!procfs_dir) {
		return -ENOMEM;
	}
	procfs_entry = proc_create(PROC_FILENAME, RW_PERMISSION, procfs_dir, &procfs_fops);
	if (!procfs_entry) {
		return -ENOMEM;
	}
	
	#ifdef DEBUG
	printk(KERN_ALERT "mp3 MODULE LOADED\n");
	#endif
	
	return 0;
}

/* called when module is unloaded */
static void __exit mp3_exit(void) {
	struct aug_task_struct *this_pcb;
	struct list_head *this_node, *temp;
	int i;

	#ifdef DEBUG
	printk(KERN_ALERT "MP3 MODULE UNLOADING\n");
	#endif

	/* remove proc files */
	remove_proc_entry(PROC_FILENAME, procfs_dir);
	remove_proc_entry(PROC_DIRNAME, NULL);

    /* remove character device from kernel */
    cdev_del(mp3_cdev);

	/* clear augmented PCB list */
	list_for_each_safe(this_node, temp, &pcb_list.list) {
		this_pcb = list_entry(this_node, struct aug_task_struct, list);
		list_del(this_node);
		kfree(this_pcb);
	}

	/* unlock pages in shared memory buffer and free */
	for(i = 0; i < MP3_BUFF_SIZE; i += PAGE_SIZE) {
		ClearPageReserved(vmalloc_to_page((void *)((unsigned long)mp3buf + i)));
	}
	vfree(mp3buf);

	#ifdef DEBUG
	printk(KERN_ALERT "MP3 MODULE UNLOADED\n");
	#endif
}

/* register init and exit funtions */
module_init(mp3_init);
module_exit(mp3_exit);
