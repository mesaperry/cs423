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

#include "mp3_given.h"

#define FILENAME "status"
#define DIRECTORY "mp3"
#define RW_PERMISSION 0666                      // allows read, write but not execute
#define BUFF_SIZE 128
#define DECIMAL_BASE 10
#define MP3_BUFF_SIZE 2 << 19					// 512 KB

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

static struct workqueue_struct *wq;
static struct mutex wq_mutex;

static struct aug_task_struct pcb_list;
static struct mutex list_mutex;

static struct proc_dir_entry *procfs_dir;
static struct proc_dir_entry *procfs_entry;

static unsigned long *mp3buf;

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
static struct aug_task_struct * init_aug_pcb(pid_t pid) {
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
	mutex_lock(&list_mutex);
	list_add(&aug_pcb->list, &pcb_list.list);
	mutex_unlock(&list_mutex);

	return aug_pcb;
}

/* deletes an augmented PCB */
static void del_aug_pcb(pid_t pid) {
	struct aug_task_struct *this_pcb;
	struct list_head *this_node, *temp;

    /* enter critical section */
    mutex_lock(&list_mutex);

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

    /* exit critical section */
    mutex_unlock(&list_mutex);
}

/* helper function for proc_write */
static void register_pid(pid_t pid) {
	#ifdef DEBUG
	printk(KERN_ALERT "Registering PID: %d\n", pid);
	#endif

	/* create workqueue if list is empty */
    mutex_lock(&wq_mutex);
	if (list_empty(&pcb_list.list)) {
		wq = create_workqueue("mp3_workqueue");
	}
    mutex_unlock(&wq_mutex);

	/* create augmented PCB and add to list */
	init_aug_pcb(pid);
}

/* helper function for proc_write */
static void unregister_pid(pid_t pid) {
	#ifdef DEBUG
	printk(KERN_ALERT "Unregistering PID: %d\n", pid);
	#endif

	del_aug_pcb(pid);

	/* delete work queue */
    mutex_lock(&wq_mutex);
	if (list_empty(&pcb_list.list)) {
		destroy_workqueue(wq);
	}
    mutex_unlock(&wq_mutex);
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
static ssize_t mp3_read( struct file *file, char __user *buffer,
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
static ssize_t mp3_write( struct file *file, const char __user *buffer,
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
static const struct file_operations mp3_fops = {
	.owner   = THIS_MODULE,
	.read    = mp3_read,
	.write   = mp3_write,
};

/* called when module is loaded */
static int __init mp3_init(void) {
	int i;

	#ifdef DEBUG
	printk(KERN_ALERT "mp3 MODULE LOADING\n");
	#endif

	/* init mutex's */
	mutex_init(&list_mutex);
	mutex_init(&wq_mutex);

	/* init augmented PCB list */
	INIT_LIST_HEAD(&pcb_list.list);

	/* initialize shared memory buffer, set PG_reserved bit */
	mp3buf = vmalloc(MP3_BUFF_SIZE);
	for(i = 0; i < MP3_BUFF_SIZE; i += PAGE_SIZE) {
		SetPageReserved(vmalloc_to_page((void *)((unsigned long)mp3buf + i)));
	}

	/* make directory */
	procfs_dir = proc_mkdir(DIRECTORY, NULL);
	if (!procfs_dir) {
		return -ENOMEM;
	}

	/* make entry */
	procfs_entry = proc_create(FILENAME, RW_PERMISSION, procfs_dir, &mp3_fops);
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
	remove_proc_entry(FILENAME, procfs_dir);
	remove_proc_entry(DIRECTORY, NULL);

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
