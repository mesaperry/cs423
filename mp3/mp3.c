#define LINUX

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>

#define FILENAME "status"
#define DIRECTORY "mp3"
#define RW_PERMISSION 0666                      // allows read, write but not execute
#define BUFF_SIZE 128
#define DECIMAL_BASE 10

MODULE_LICENSE("GPL");
MODULE_AUTHOR("mesagp2");

#define DEBUG 1

static struct proc_dir_entry *procfs_dir;
static struct proc_dir_entry *procfs_entry;

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

/* helper function for proc_write */
static void register_pid(pid_t pid) {
	return;
}

/* helper function for proc_write */
static void unregister_pid(pid_t pid) {
	return;
}

/* outputs a list of processes and their scheduling parameters */
static ssize_t mp3_read( struct file *file, char __user *buffer,
									size_t count, loff_t *data ) {
    return 0;
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
	#ifdef DEBUG
	printk(KERN_ALERT "mp3 MODULE LOADING\n");
	#endif

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
	#ifdef DEBUG
	printk(KERN_ALERT "MP3 MODULE UNLOADING\n");
	#endif

	/* remove proc files */
	remove_proc_entry(FILENAME, procfs_dir);
	remove_proc_entry(DIRECTORY, NULL);

	#ifdef DEBUG
	printk(KERN_ALERT "MP3 MODULE UNLOADED\n");
	#endif
}

/* register init and exit funtions */
module_init(mp3_init);
module_exit(mp3_exit);
