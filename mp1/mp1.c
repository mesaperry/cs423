#define LINUX

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <asm/uaccess.h>

#include "mp1_given.h"

#define FILENAME "status"
#define DIRECTORY "mp1"
#define RW_PERMISSION 0666                      // allows read, write but not execute
#define LONG_BUFF_SIZE 21                       // number of digits in ULLONG_MAX + 1
#define TIME_GRANULARITY 100                    // in jiffies

MODULE_LICENSE("GPL");
MODULE_AUTHOR("mesagp2");
MODULE_DESCRIPTION("CS-423 MP1");

#define DEBUG 1

static unsigned long time;

struct time_data {
   int pid;
   unsigned long creation_time;
   struct list_head node;
};

/* init process time data list */
static struct time_data time_list;

static void inc_time_callback(unsigned long data) {
   time += TIME_GRANULARITY;
}

static struct proc_dir_entry *proc_dir;
static struct proc_dir_entry *proc_entry;

static ssize_t mp1_read ( struct file *file, char __user *buffer,
                           size_t count, loff_t *data ) {
   struct time_data *this_entry;
   size_t procfs_buffer_size;
   char temp_buffer[LONG_BUFF_SIZE];
   int i;
   char *procfs_buffer;
   ssize_t res;

   /* allocate procfs_buffer to heap */
   procfs_buffer = (char *) kmalloc(count, GFP_KERNEL);

   /* load process time data into buffer */
   procfs_buffer_size = 0;
   list_for_each_entry(this_entry, &time_list.node, node) {
      /* convert PID from unsigned to string */
      sprintf(temp_buffer, "%d", this_entry->pid);
      // printk(KERN_ALERT "%d\n", this_entry->pid);

      /* copy PID string into buffer */
      i = 0;
      while (temp_buffer[i] != '\0') {
         procfs_buffer[procfs_buffer_size] = temp_buffer[i];
         procfs_buffer_size++;
         i++;
      }

      /* add seperator between PID and CPU time of PID */
      procfs_buffer[procfs_buffer_size++] = ':';
      procfs_buffer[procfs_buffer_size++] = ' ';

      /* convert CPU time from unsigned long to string */
      sprintf(temp_buffer, "%d", (int)(time - this_entry->creation_time));

      /* copy CPU time into buffer */
      i = 0;
      while (temp_buffer[i] != '\0') {
         procfs_buffer[procfs_buffer_size] = temp_buffer[i];
         procfs_buffer_size++;
         i++;
      }

      /* add new line */
      procfs_buffer[procfs_buffer_size++] = '\n';
   }

   /* copy buffer to user space */
   res = simple_read_from_buffer(buffer, count, data, procfs_buffer, procfs_buffer_size);

   /* free buffer from heap */
   kfree(procfs_buffer);

   return res;
}

static ssize_t mp1_write ( struct file *file, const char __user *buffer,
                           size_t count, loff_t *data ) {
   struct time_data *this_entry;
   char procfs_buffer[LONG_BUFF_SIZE];
   size_t procfs_buffer_size;
   int error;
   ssize_t res;

   /* create struct to track process's uptime */
   this_entry = (struct time_data*) kmalloc(sizeof(struct time_data), GFP_KERNEL);

   /* add to list */
   list_add(&(this_entry->node), &(time_list.node));

   /* set creation time */
   this_entry->creation_time = time;

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

   /* fix string to terminate */
   procfs_buffer[procfs_buffer_size] = '\0';

   /* set pid */
   error = kstrtol(procfs_buffer, 10, (long*) &this_entry->pid);
   if (error) {
      return error;
   }

   return res;
}

static const struct file_operations mp1_fops = {
   .owner   = THIS_MODULE,
   .read    = mp1_read,
   .write   = mp1_write,
};

// mp1_init - Called when module is loaded
static int __init mp1_init(void)
{
   #ifdef DEBUG
   printk(KERN_ALERT "MP1 MODULE LOADING\n");
   #endif

   /* make directory */
   proc_dir = proc_mkdir(DIRECTORY, NULL);
   if (!proc_dir) {
      return -ENOMEM;
   }

   /* make entry */
   proc_entry = proc_create(FILENAME, RW_PERMISSION, proc_dir, &mp1_fops);
   if (!proc_entry) {
      return -ENOMEM;
   }

   /* module existance time starts at 0 */
   time = 0;

   /* init process time list */
   INIT_LIST_HEAD(&time_list.node);
   
   printk(KERN_ALERT "MP1 MODULE LOADED\n");
   return 0;
}

// mp1_exit - Called when module is unloaded
static void __exit mp1_exit(void)
{
   struct time_data *this_entry;
   struct list_head *this_node, *temp;

   #ifdef DEBUG
   printk(KERN_ALERT "MP1 MODULE UNLOADING\n");
   #endif
   
   /* remove proc files */
   remove_proc_entry(FILENAME, proc_dir);
   remove_proc_entry(DIRECTORY, NULL);

   /* clear linked list of process time data */
   list_for_each_safe(this_node, temp, &time_list.node) {
      this_entry = list_entry(this_node, struct time_data, node);
      list_del(this_node);
      kfree(this_entry);
   }

   printk(KERN_ALERT "MP1 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp1_init);
module_exit(mp1_exit);
