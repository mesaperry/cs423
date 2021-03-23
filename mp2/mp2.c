#define LINUX

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/jiffies.h>

#include "mp2_given.h"

#define FILENAME "status"
#define DIRECTORY "mp2"
#define RW_PERMISSION 0666                      // allows read, write but not execute
#define BUFF_SIZE 128
#define DECIMAL_BASE 10
#define LN2 693

MODULE_LICENSE("GPL");
MODULE_AUTHOR("mesagp2");
MODULE_DESCRIPTION("CS-423 MP2");

#define DEBUG 1

struct mp2_task_struct {
	struct task_struct *linux_task;
    struct timer_list wakeup_timer;
    struct list_head list;
    pid_t pid;
    unsigned long period;
    unsigned long runtime_ms;
    unsigned long deadline_jiff;
    enum task_state { READY, RUNNING, SLEEPING } state;
};

static unsigned acrs; // admission control ratio sum

static struct mp2_task_struct *running_task;
static struct task_struct *dispatch_thread;

static struct mutex list_mutex;
static struct mp2_task_struct proc_list;

static struct proc_dir_entry *procfs_dir;
static struct proc_dir_entry *procfs_entry;

/* set_proc_state - sets target process to target state */
static void set_proc_state(pid_t pid, enum task_state state) {
	struct mp2_task_struct *this_task;

	/* enter critical section */
	mutex_lock(&list_mutex);

	/* find process by PID and set state to READY */
	list_for_each_entry(this_task, &proc_list.list, list) {
		if (this_task->pid == pid) {
			this_task->state = state;
		}
	}

	/* exit critical section */
	mutex_unlock(&list_mutex);
}

/* dispatch_func - callback for kernel thread responsible for context switch */
static int dispatch_func(void *data) {
	struct mp2_task_struct *task, *highest_task;
	struct sched_param sparam;

	/* prime kthread to sleep */
	set_current_state(TASK_INTERRUPTIBLE);
	
	while (!kthread_should_stop()) {
		/* sleep and wait to wake */
		schedule();

		/* kernel thread wakes up here */

		/* enter critical section */
		mutex_lock(&list_mutex);

		/* search for READY task with highest priority */
		highest_task = NULL;
		list_for_each_entry(task, &proc_list.list, list) {
			if ((task->state == READY || task->state == RUNNING) &&
				(highest_task == NULL || task->period < highest_task->period)) {
				highest_task = task;
			}
		}

		/* context switch */

		/* switch out of preempted task */
		if (running_task != NULL) {
			/* if current task was preempted, return to READY */
			if (running_task->state == RUNNING) {
				running_task->state = READY;
			}

			sparam.sched_priority = 0;
			sched_setscheduler(running_task->linux_task, SCHED_NORMAL, &sparam);
			running_task = NULL;
		}

		/* if a READY task exists and is, switch to it */
		if (highest_task != NULL) {
			highest_task->state = RUNNING;
			wake_up_process(highest_task->linux_task);
			sparam.sched_priority = 99;
			sched_setscheduler(highest_task->linux_task, SCHED_FIFO, &sparam);
			running_task = highest_task;
		}

		/* exit critical section */
		mutex_unlock(&list_mutex);

		/* prime kthread to sleep */
		set_current_state(TASK_INTERRUPTIBLE);
	}

	/* idk some guide said to do this */
	set_current_state(TASK_RUNNING);

	return 0;
}

/* wakeup_timer_func - callback that READY's task and wakes dispatching thread */
static void wakeup_timer_func(unsigned long pid) {
	/* set process state to READY */
	set_proc_state((pid_t) pid, READY);

	/* wake dispatching thread */
	wake_up_process(dispatch_thread);
}

/* get_proc_params - reads proc params into buff, returns bytes read */
static int get_proc_params(char *buff, size_t count) {
	struct mp2_task_struct *pcb;
	loff_t pos;
	int res;
	int i;

	/* initialize position to beginning of buffer */
	pos = 0;
	res = 0;

	/* enter critical section */
	mutex_lock(&list_mutex);

	/* iterate through each process */
	i = 0;
	list_for_each_entry(pcb, &proc_list.list, list) {
		i++;
		/* convert PID to string and insert into buffer */
		res = snprintf(buff + pos, count - pos, "%d", pcb->pid);
		if (res < 0) {
			/* return with error */
			return res;
		}
		else {
			/* increment position by number of written characters */
			pos += res;
		}
		
		/* insert formatting */
		buff[pos++] = ':';
		buff[pos++] = ' ';

		/* convert period to string and insert into buffer */
		res = snprintf(buff + pos, count - pos, "%lu", pcb->period);
		if (res < 0) {
			/* return with error */
			return res;
		}
		else {
			/* increment position by number of written characters */
			pos += res;
		}
		
		/* insert formatting */
		buff[pos++] = ',';
		buff[pos++] = ' ';

		/* convert processing time to string and insert into buffer */
		res = snprintf(buff + pos, count - pos, "%lu", pcb->runtime_ms);
		if (res < 0) {
			/* return with error */
			return res;
		}
		else {
			/* increment position by number of written characters */
			pos += res;
		}

		/* insert newline */
		buff[pos++] = '\n';
	}

	#ifdef DEBUG
	// printk(KERN_ALERT "number of elements in list: %d\n", i);
	#endif

	/* exit critical section */
	mutex_unlock(&list_mutex);

	/* null terminate string */
	buff[pos++] = '\0';

	return pos;
}

/* mp2_read - outputs a list of processes and their scheduling parameters */
static ssize_t mp2_read( struct file *file, char __user *buffer,
									size_t count, loff_t *data ) {
   	char procfs_buffer[BUFF_SIZE];
   	int res;

	/* should only read from pos 0, handle read termination */
	if (*data > 0) {
		return 0;
	}
	
	/* generate output */
	res = get_proc_params(procfs_buffer, count);
	if (res < 0) {
		/* error handling */
		return res;
	}

	/* copy buffer to user space */
	return simple_read_from_buffer(buffer, count, data, procfs_buffer, res);
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
static struct mp2_task_struct* init_pcb( pid_t pid, unsigned long period,
										 unsigned long processing_time ) {
	struct task_struct *pcb;
	struct mp2_task_struct *aug_pcb;

	/* get the userapp's task_struct */
	pcb = find_task_by_pid(pid);

	/* allocate cache for PCB */
	aug_pcb = (struct mp2_task_struct*) kmalloc( sizeof(struct mp2_task_struct),
												 GFP_KERNEL );

	/* init task members */
	aug_pcb->linux_task = pcb;
	aug_pcb->pid = pid;
	aug_pcb->period = period;
	aug_pcb->runtime_ms = processing_time;
	aug_pcb->state = SLEEPING;
	aug_pcb->deadline_jiff = 0;
	setup_timer(&(aug_pcb->wakeup_timer), wakeup_timer_func, pid);

	return aug_pcb;
}

/* dereg_task - de-registers task by PID */
static void dereg_task(pid_t pid) {
	struct mp2_task_struct *this_task;
	struct list_head *this_node, *temp;

    /* enter critical section */
    mutex_lock(&list_mutex);

    /* iterate through tasks */
    list_for_each_safe(this_node, temp, &proc_list.list) {

        /* get task */
        this_task = list_entry(this_node, struct mp2_task_struct, list);

        /* delete task with matching pid */
        if (this_task->pid == pid) {
			/* subtract this task from cumulative sum */
			acrs -= (1000 * this_task->runtime_ms) / this_task->period;

			del_timer(&(this_task->wakeup_timer));
            list_del(this_node);
            kfree(this_task);

			/* clear global current task pointer */
			if (running_task != NULL && running_task->pid == pid) {
				running_task = NULL;
			}
        }
        
    }

    /* exit critical section */
    mutex_unlock(&list_mutex);
}

/* mp2_yield - put calling task to sleep and set wakeup timer */
static void mp2_yield(pid_t pid) {
	struct mp2_task_struct *this_task;

	/* enter critical section */
	mutex_lock(&list_mutex);

	/* find process by PID */
	list_for_each_entry(this_task, &proc_list.list, list) {
		if (this_task->pid == pid) {
			break;
		}
	}

	/* set deadline to now + period, if first yield */
	if (this_task->deadline_jiff == 0) {
		this_task->deadline_jiff = jiffies +
									msecs_to_jiffies(this_task->period);
	}
	/* set deadline to next deadline, if not first yield */
	else {
		this_task->deadline_jiff += msecs_to_jiffies(this_task->period);
	}

	/* only set timer and put task to sleep if yield is on time */
	if (jiffies < this_task->deadline_jiff) {
		/* change state of calling task to SLEEPING */
		this_task->state = SLEEPING;

		/* set timer */
		mod_timer(&(this_task->wakeup_timer), this_task->deadline_jiff);

		/* put task to sleep */
		set_task_state(this_task->linux_task, TASK_UNINTERRUPTIBLE);
	}

	/* exit critical section */
	mutex_unlock(&list_mutex);

	/* wake dispatching thread */
	wake_up_process(dispatch_thread);

	schedule();
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
	struct mp2_task_struct *pcb;

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

			#ifdef DEBUG
			printk( KERN_ALERT "Registering PID: %d, Period: %lu, ProcTime %lu\n",
					pid, period, processing_time );
			#endif

			/* check admission control */
			if (acrs + (1000 * processing_time) / period <= LN2) {
				/* add this task to cumulative sum */
				acrs += (1000 * processing_time) / period;

				/* initialize augmented PCB */
				pcb = init_pcb(pid, period, processing_time);

				/* add PCB to list */
				mutex_lock(&list_mutex);
				list_add(&(pcb->list), &(proc_list.list));
				mutex_unlock(&list_mutex);
			}
			else {
				/* failed admission control */
				#ifdef DEBUG
				printk(KERN_ALERT "Failed admission control\n");
				#endif
				return -EINVAL;
			}


			break;

		case 'Y':
			#ifdef DEBUG
			printk(KERN_ALERT "Yielding PID: %d\n", pid);
			#endif

			/* yield process */
			mp2_yield(pid);

			break;

		case 'D':
			#ifdef DEBUG
			printk(KERN_ALERT "De-registering PID: %d\n", pid);
			#endif

            /* de-register task */
            dereg_task(pid);

			#ifdef DEBUG
			printk(KERN_ALERT "Sucessfully de-registered PID: %d\n", pid);
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

	/* init admission control ratio */
	acrs = 0;

    /* init curr process to none */
    running_task = NULL;

	/* init process list */
	INIT_LIST_HEAD(&proc_list.list);

	/* init list mutex */
	mutex_init(&list_mutex);

	/* init kernel/dispatching thread daemon */
	dispatch_thread = kthread_run(dispatch_func, NULL, "dispatcher");

	/* make directory */
	procfs_dir = proc_mkdir(DIRECTORY, NULL);
	if (!procfs_dir) {
		return -ENOMEM;
	}

	/* make entry */
	procfs_entry = proc_create(FILENAME, RW_PERMISSION, procfs_dir, &mp2_fops);
	if (!procfs_entry) {
		return -ENOMEM;
	}
	
	#ifdef DEBUG
	printk(KERN_ALERT "MP2 MODULE LOADED\n");
	#endif
	
	return 0;
}

/* mp2_exit - called when module is unloaded */
static void __exit mp2_exit(void) {
	struct mp2_task_struct *this_task;
	struct list_head *this_node, *temp;

	#ifdef DEBUG
	printk(KERN_ALERT "MP2 MODULE UNLOADING\n");
	#endif

	/* remove proc files */
	remove_proc_entry(FILENAME, procfs_dir);
	remove_proc_entry(DIRECTORY, NULL);

	/* clear process list */
	list_for_each_safe(this_node, temp, &proc_list.list) {
		this_task = list_entry(this_node, struct mp2_task_struct, list);
		list_del(this_node);
		kfree(this_task);
	}

	/* stop kernel/dispatch thread */
	kthread_stop(dispatch_thread);

	#ifdef DEBUG
	printk(KERN_ALERT "MP2 MODULE UNLOADED\n");
	#endif
}

/* register init and exit funtions */
module_init(mp2_init);
module_exit(mp2_exit);
