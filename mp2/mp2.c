#define LINUX

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include "mp2_given.h"

#define FILENAME "status"
#define DIRECTORY "mp2"
#define RW_PERMISSION 0666                      // allows read, write but not execute
#define BUFF_SIZE 128
#define DECIMAL_BASE 10

MODULE_LICENSE("GPL");
MODULE_AUTHOR("mesagp2");
MODULE_DESCRIPTION("CS-423 MP2");

#define DEBUG 1

enum task_state { READY, RUNNING, SLEEPING };

struct mp2_task_struct {
	struct task_struct *linux_task;
    struct timer_list wakeup_timer;
    struct list_head list;
    pid_t pid;
    unsigned long period;
    unsigned long runtime_ms;
    unsigned long deadline_jiff;
    enum task_state state;
};

static struct proc_dir_entry *proc_dir;
static struct proc_dir_entry *proc_entry;

/* mp2_read - outputs a list of processes and their scheduling parameters */
static ssize_t mp2_read( struct file *file, char __user *buffer,
									size_t count, loff_t *data ) {
   	char *procfs_buffer;
   	ssize_t res;

	/* allocate procfs_buffer to heap */
	procfs_buffer = (char *) kmalloc(count, GFP_KERNEL);
	


	/* copy buffer to user space */
	res = simple_read_from_buffer(buffer, count, data, procfs_buffer, count);

	/* free buffer from heap */
	kfree(procfs_buffer);

	return res;
}

/* get_next_arg - places next arg into buffer and returns size */
static size_t get_next_arg(char *buff, char *arg_buff, loff_t *pos) {
	size_t arg_size;

	/* skip past whitespace and commas to argument */
	while (buff[*pos] == ' ' || buff[*pos] == ',') {
		(*pos)++;
	}

	/* extract arg */
	for ( 	arg_size = 0;
		  	buff[*pos] != '\0' && buff[*pos] != '\n' && buff[*pos] != ',';
		  	(*pos)++, arg_size++ ) {
		arg_buff[arg_size] = buff[*pos];
	}

	/* null terminate arg buffer */
	arg_buff[arg_size++] = '\0';

	return arg_size;
}

/* init_pcb - creates an augmented PCB */
static void init_pcb( struct mp2_task_struct *aug_pcb, pid_t pid,
					  unsigned long period, unsigned long processing_time ) {
	struct task_struct *pcb;

	/* get the userapp's task_struct */
	pcb = find_task_by_pid(pid);

	/* allocate cache for PCB */
	aug_pcb = (struct mp2_task_struct*) kmalloc( sizeof(struct mp2_task_struct),
												 GFP_KERNEL );

	/* init task members */
	aug_pcb->state = SLEEPING;
	aug_pcb->period = period;
	aug_pcb->runtime_ms = processing_time;
}

/* mp2_write - interface for userapps to register, yield, or de-register */
static ssize_t mp2_write( struct file *file, const char __user *buffer,
									size_t count, loff_t *data ) {
	char procfs_buff[BUFF_SIZE];
	ssize_t procfs_size;
	char arg_buff[BUFF_SIZE];
	loff_t pos;
	char operation;
	pid_t pid;
    unsigned long period;
    unsigned long processing_time;
	int error;

	/* copy buffer into kernel space */
	procfs_size = simple_write_to_buffer( procfs_buff,
                                  BUFF_SIZE,
                                  data,
                                  buffer,
                                  count );
	if (procfs_size < 0) {
		return procfs_size;
	}

	/* setup to parse args */
	pos = 0;

	/* get operation arg */
	operation = procfs_buff[pos++];

	/* get PID arg */
	get_next_arg(procfs_buff, arg_buff, &pos);
	error = kstrtoint(arg_buff, DECIMAL_BASE, &pid);
	if (error) {
		return error;
	}

	switch (operation) {
		case 'R':
			#ifdef DEBUG
			printk( KERN_ALERT "Registering PID: %d, Period: %lu, ProcTime %lu\n",
					pid, period, processing_time );
			#endif

			/* get period arg */
			get_next_arg(procfs_buff, arg_buff, &pos);
			error = kstrtoul(arg_buff, DECIMAL_BASE, &period);
			if (error) {
				return error;
			}

			/* get processing time arg */
			get_next_arg(procfs_buff, arg_buff, &pos);
			error = kstrtoul(arg_buff, DECIMAL_BASE, &processing_time);
			if (error) {
				return error;
			}

			/* initialize augmented PCB */
			

			break;

		case 'Y':
			#ifdef DEBUG
			printk(KERN_ALERT "Yielding PID: %d\n", pid);
			#endif

			break;

		case 'D':
			#ifdef DEBUG
			printk(KERN_ALERT "De-registering PID: %d\n", pid);
			#endif

			break;
	}

	return procfs_size;
}

/* mp2_fops - stores links read and write functions to mp2 file */
static const struct file_operations mp2_fops = {
	.owner   = THIS_MODULE,
	.read    = mp2_read,
	.write   = mp2_write,
};

/* mp2_init - called when module is loaded */
static int __init mp2_init(void) {
	#ifdef DEBUG
	printk(KERN_ALERT "MP2 MODULE LOADING\n");
	#endif

	/* make directory */
	proc_dir = proc_mkdir(DIRECTORY, NULL);
	if (!proc_dir) {
		return -ENOMEM;
	}

	/* make entry */
	proc_entry = proc_create(FILENAME, RW_PERMISSION, proc_dir, &mp2_fops);
	if (!proc_entry) {
		return -ENOMEM;
	}
	
	#ifdef DEBUG
	printk(KERN_ALERT "MP2 MODULE LOADED\n");
	#endif
	
	return 0;
}

/* mp2_exit - called when module is unloaded */
static void __exit mp2_exit(void) {
	#ifdef DEBUG
	printk(KERN_ALERT "MP2 MODULE UNLOADING\n");
	#endif

	/* remove proc files */
	remove_proc_entry(FILENAME, proc_dir);
	remove_proc_entry(DIRECTORY, NULL);

	#ifdef DEBUG
	printk(KERN_ALERT "MP2 MODULE UNLOADED\n");
	#endif
}

/* register init and exit funtions */
module_init(mp2_init);
module_exit(mp2_exit);
