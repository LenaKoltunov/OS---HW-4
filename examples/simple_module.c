//-------------------------------- Include's ---------------------------------//
#include <linux/errno.h>
#include <linux/module.h>
#include <asm/semaphore.h>
#include <linux/fs.h> 			//for struct file_operations
#include <linux/kernel.h> 		//for printk()
#include <asm/uaccess.h> 		//for copy_from_user()
#include <linux/sched.h>
#include <linux/slab.h>			// for kmalloc
#include <linux/spinlock.h>
#include <stdbool.h>
#include "hw4.h"
#include "encryptor.h"
#include "mastermind.h"
#include "simple_module.h"


MODULE_LICENSE("GPL");
MODULE_AUTHOR( "Lena Koltanov & Alexander Khvolis" );

//--------------------------------- Defines ---------------------------------//
#define BUF_LEN 5
#define ZERO 0

//--------------------------------- In Lines ---------------------------------//
static inline int get_mahor_from_inode(struct inode *inode)
{
  return MAJOR(inode->i_rdev);
}

static inline int get_minor_from_inode(struct inode *inode)
{
  return MINOR(inode->i_rdev);
}

//-------------------------------- Variables ---------------------------------//
int major_num;
bool codemaker_exits;

char buffer[BUF_LEN];
char *MassagePtr;

struct file_operations fops;

typedef struct device_private_data  {
	int minor;
	int private_key;
	int turns;
	bool in_round;
}* Device_private_data;

int num_of_codemakers;
int num_of_codebrakers;

wait_queue_head_t wq_codemakers;
wait_queue_head_t wq_codebrakers;

spinlock_t codemaker_exits_lock;
spinlock_t counters_lock;

struct semaphore
	read_lock,
	write_lock,
	index_lock;

//-------------------------------- Functions ---------------------------------//
int open(struct inode* inode, struct file* filp)
{

	int minor = get_minor_from_inode(inode);

	spin_lock(codemaker_exits_lock);
	if (codemaker_exits && (minor == 0))
		return -EPERM;

	if (minor == 0)
		codemaker_exits = true;

	spin_unlock(codemaker_exits_lock);

	filp->private_data = kmalloc(sizeof(Device_private_data), GFP_KERNEL);
	if (filp->private_data == NULL)
		return -ENOMEM;

	Device_private_data data = filp->private_data;
	data->minor = minor;
	filp->f_op = &fops;

	if (minor == 1) {
		data->turns = 10;
		data->in_round = false;
	}

	spin_lock(counters_lock);
	if (minor == 0)
	{
		num_of_codemakers++;
	}
	if (minor == 1)
	{
		num_of_codebrakers++;
	}
	spin_unlock(counters_lock);

	return 0;
}

int release(struct inode* inode, struct file* filp)
{
	Device_private_data data = filp->private_data;

	if (data->in_round)
		return -EBUSY;

	kfree(filp->private_data);
	int minor = get_minor_from_inode(inode);

	spin_lock(counters_lock);
	if (minor == 0)
	{
		num_of_codemakers--;
		wake_up_interruptible(&wq_codebrakers);
	}
	if (minor == 1)
	{
		num_of_codebrakers--;
		wake_up_interruptible(&wq_codemakers);
	}
	spin_unlock(counters_lock);

	printk("simple device released\n");
	return 0;
}

ssize_t read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	int bytes_read = 0;

	if (*MassagePtr == 0)
		return 0;

	while (count && *MassagePtr) {
		put_user(*(MassagePtr++), buf++);
		count--;
		bytes_read++;
	}

	return bytes_read;
}

/* 
 * This function is called when somebody tries to
 * write into our device file. 
 */
ssize_t write(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
	int i;

	for (i = 0; i < count && i < BUF_LEN; i++)
		get_user(buffer[i], buf + i);

	MassagePtr = buffer;

	return i;
}


struct file_operations fops = {		//Alon: FOPS for minor=0
	.open=		open,
	.release=	release,
	.read=		read,
	.write=		write,
	// .llseek=	my_llseek,
	// .ioctl=		my_ioctl,
	.owner=		THIS_MODULE,
};

int init_module(void)
{


	major_num = register_chrdev(ZERO, DEVICE_NAME, &fops);

	if (major_num < 0)
	{
		printk(KERN_ALERT "Registering char device failed with %d\n",
			major_num);
		return major_num;
	}

	num_of_codemakers = 0;
	num_of_codebrakers = 0;
	spin_lock_init(&codemaker_exits_lock);
	spin_lock_init(&counters_lock);
	init_waitqueue_head(&wq_codemakers);
	init_waitqueue_head(&wq_codebrakers);
	sema_init(&read_lock, 1);
	sema_init(&write_lock, 1);
	sema_init(&index_lock, 1);
	//memset(buffer[i], 0, BUF_LEN);

	printk("simple device registered with %d major\n",major_num);

	return 0;
}

void cleanup_module(void)
{
	int ret;

	ret = unregister_chrdev(major_num, DEVICE_NAME);

	if (ret < 0)
		printk(KERN_ALERT "Error: unregister_chrdev: %d\n", ret);

	printk("releasing module with %d major\n",major_num);
}
