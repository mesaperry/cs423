#define LINUX

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>

#include "mp2_given.h"

#define FILENAME "status"
#define DIRECTORY "mp2"
#define RW_PERMISSION 0666                      // allows read, write but not execute
#define LONG_BUFF_SIZE 21                       // number of digits in ULLONG_MAX + 1

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
static ssize_t mp2_read ( struct file *file, char __user *buffer,
									size_t count, loff_t *data ) {
	return 0;
}

/* mp2_write - interface for userapps to register, yield, or de-register */
static ssize_t mp2_write ( struct file *file, const char __user *buffer,
									size_t count, loff_t *data ) {
	char procfs_buffer[LONG_BUFF_SIZE];
	size_t procfs_buffer_size;
	ssize_t res;

	/* copy buffer into kernel space */
	procfs_buffer_size = count;
	if (procfs_buffer_size > LONG_BUFF_SIZE) {
		procfs_buffer_size = LONG_BUFF_SIZE;
	}
	res = simple_write_to_buffer( procfs_buffer,
                                  LONG_BUFF_SIZE,
                                  data,
                                  buffer,
                                  procfs_buffer_size );
	if (res < 0) {
		return res;
	}

	switch (procfs_buffer[0]) {
		case 'R':
			printk(KERN_ALERT "write case R");
			break;

		case 'Y':
			printk(KERN_ALERT "write case Y");
			break;

		case 'D':
			printk(KERN_ALERT "write case D");
			break;
	}

	/* fix string to terminate */
	procfs_buffer[procfs_buffer_size] = '\0';

	return res;
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
