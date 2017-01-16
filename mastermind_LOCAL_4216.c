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

int colour_range;

bool in_round;

char codeBuf[BUF_LEN];
int codeLen;
char feedbackBuf[BUF_LEN];
int feedbackLen;
char guessBuf[BUF_LEN];
int guessLen;

char *MassagePtr;

struct file_operations fops;

typedef struct device_private_data  {
	int minor;
	int turns;
	int score;
}* Device_private_data;

int num_of_codemakers; //TODO
int num_of_codebrakers;

wait_queue_head_t wq_codemakers; //TODO
wait_queue_head_t wq_codebrakers;

spinlock_t codemaker_exits_lock; //TODO
spinlock_t counters_lock; //TODO

struct semaphore
read_lock,
write_lock,
	index_lock; //TODO

//-------------------------------- Functions ---------------------------------//

/*
 * This function opens the module for reading/writing (you should always open the module with the
 * O_RDWR flag to support both operations).
 * If the module in question is that of the Codemaker you should make sure that no other Codemaker
 * exists, otherwise this function should close the module and return -EPERM.
 * Codebreakers should get 10 turns (each) by default when opening the module.
 */
	int open(struct inode* inode, struct file* filp){
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

/*
 * This function should close the module for the invoker, freeing used data and updating global
 * variables accordingly.
 * The Codemaker may only release the module when the round is over, if the round is in progress
 * this function should return -EBUSY to an invoking Codemaker.
 * You may assume that Codebreakers only invoke this function after playing a full turn (I.E. -
 * writing to the guess buffer AND reading from the feedback buffer)
 */
	int release(struct inode* inode, struct file* filp){
		if( filp->f_mode & O_RDWR ){
			Device_private_data data = filp->private_data;

			if (data->minor == 0)
			{
				if (in_round){
					return -EBUSY;
				}
				
				spin_lock(counters_lock);
				num_of_codemakers--;
				spin_unlock(counters_lock);

				wake_up_interruptible(&wq_codebrakers);
			}
			if (data->minor == 1 && data->turns>0)
			{
				spin_lock(counters_lock);
				num_of_codebrakers--;
				spin_unlock(counters_lock);
				// wake_up_interruptible(&wq_codemakers);
			}

			kfree(filp->private_data);

			return 0;
		}
	}

	ssize_t read(struct file *filp, char *buf, size_t count, loff_t *f_pos){
/*
 * ● For the Codemaker (minor number 0) -
 *   Attempts to read the contents of the guess buffer.
 *   If the buffer is empty and no Codebreakers with available turns exist this function should
 *   return EOF.
 *   If the buffer is empty but any Codebreaker exist that can still play (has turns available)
 *   then the Codemaker should wait until something is written into it.
 *   If the buffer is full then this function copies its contents into buf (make sure buf is of
 *   adequate size).
 *   Upon success this function return 1.
 *   Note: This operation should NOT empty the guess buffer.
*/

		Device_private_data data = filp->private_data;
		if (data->minor == 0 ){
			if (guessLen = 0){
				spin_lock(counters_lock);
				if (num_of_codebrakers == 0){
					spin_unlock(counters_lock);
					return EOF;
				} else wait_event_interruptible(wq_codebrakers, guessLen > 0);
			}

			int bytes_read = 0;

			if (guessLen == 4){
				MassagePtr = guessBuf;
				if (*MassagePtr == 0)
					return EOF;

				while (count && *MassagePtr) {
					put_user(*(MassagePtr++), buf++);
					count--;
					bytes_read++;
				}
			}

			if (bytes_read == 4)
				return 1;

			return -1;
		}

 /* ● For the Codebreaker (minor number 1) -
 *   Attempts to read the feedback buffer.
 *   If the Codebreaker has no more turns available this function should immediately return
 *   -EPERM.
 *   If the buffer is empty and no Codemaker exists this function should return EOF.
 *   If the buffer is empty but a Codemaker is present then the Codebreaker should wait until
 *   the buffer is filled by the Codemaker.
 *   If the buffer is full then this function copies its contents into buf. Additionally if the
 *   feedback buffer’s value is “2222” then that means the Codebreaker’s guess was correct,
 *   in which case he should earn a point and the round needs to end.
 *   This operation should return 1 upon success, and empty both the guess buffer and the
 *   feedback buffer
 */
		if (data->minor == 1){
			if (data->turns = 0)
				return -EPERM;

			if (feedbackLen == 0){
				if (num_of_codemakers == 0)
					return EOF;

				wait_event_interruptible(wq_codemakers, feedbackLen > 0);
			}

			int bytes_read = 0;
			bool guessed = true;

			if (feedbackLen == 4){
				MassagePtr = guessBuf;
				if (*MassagePtr == 0)
					return EOF;

				while (count && *MassagePtr) {
					if (*MassagePtr != 2)
						guessed = false;

					put_user(*(MassagePtr++), buf++);
					count--;
					bytes_read++;
				}
			}
			// TODO: End the round.
			if (bytes_read == 4){
				feedbackLen = guessLen = 0;
				return 1;
			}

			return -1;
		}
	}

/*
 * ● For the Codemaker (minor number 0) -
 *   If the round hasn’t started - this function writes the contents of buf into the
 *   password buffer.
 *   While a round is in progress - attempts to write the contents of buf into the feedback
 *   buffer.
 *   The contents of buf should be generated using the generateFeedback function prior to
 *   writing.
 *   If the feedback buffer is full then the function should immediately return -EBUSY.
 *   Returns 1 upon success.
 * ● For the Codebreaker (minor number 1) -
 *   Attempts to write the contents of buf into the guess buffer.
 *   If buf contains an illegal character (one which exceeds the specified range of colors) then
 *   this function should return -EINVAL.
 *   If the guess buffer is full and no Codemaker exists then this function should return EOF.
 *   If the guess buffer is full but a Codemaker exists then the Codebreaker should wait until
 *   it is emptied (you may assume that the Codebreaker who filled the buffer will eventually
 *   empty it).
 *   Returns 1 upon success.
 */
	ssize_t write(struct file *filp, const char *buf, size_t count, loff_t *f_pos){
		Device_private_data data = filp->private_data;
		if (data->minor == 0 ){
			if(in_round==false){
				int i;
				for (i = 0; i < count && i < BUF_LEN; i++){
					get_user(codeBuf[i], buf + i);
					codeLen++;
				}
				return 1;
			}else{
				//TODO in case there is breakers or not
			}
		}

		if (data->minor == 1 ){
			if (guessLen != 0){

				if(codemaker_exits){
					wait_event_interruptible(wq_codebrakers, guessLen == 0);
				}else{
					return EOF;
				}
			}
			int i;
			for (i = 0; i < count && i < BUF_LEN; i++){
				get_user(guessBuf[i], buf + i);
				if(!checkInput(guessBuf[i])){
					clearBuf(guessBuf, BUF_LEN);
					return -EINVAL;
				}
				guessLen++;
			}
		}


		MassagePtr = buffer;

		return i;
	}

	/* 
	 * Auxillary func:
	 * clears buffer
	 */

	void clearBuf(char *buf, int size){
		int i;
		for (int i = 0; i < size; ++i)
		{
			buf[i]=0;
		}
	}

	/* 
	 * Auxillary func:
	 * checks inputs range
	 */
	bool checkInput(char car){
		if(car < '0' || car > ('0'+colour_range)){
			return false;
		}
		return true;
	}
/*
 * This function is not needed in this exercise, but to prevent the OS from generating a default
 * implementation you should write this function to always return -ENOSYS when invoked. - DONE
 */
	loff_t llseek(struct file *filp, loff_t a, int num){
		return -ENOSYS;
	}

/*
 * The Device Driver should support the following commands, as defined in mastermind.h:
 * ● ROUND_START -
 *   Starts a new round, with a colour-range specified in arg.
 *   If 4>arg or 10<arg then this function should return -EINVAL.
 *   If a round is already in progress this should return -EBUSY.
 *   This command can only be initiated by the Codemaker, if a Codebreaker attempts to use
 *   it this function should return -EPERM
 * ● GET_MY_SCORE -
 *   Returns the score of the invoking process.
 *   In case of any other command code this function should return -ENOTTY. - DONE
 */
	int ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg){
		switch( cmd ) {
			case ROUND_START:

			if (arg<4 || arg>10)
				return -EINVAL;
			if (in_round)
				return -EBUSY;
			Device_private_data data = filp->private_data;
			if (data->minor == 1 )
				return -EPERM;

			in_round = true;
			generateCode();

			break;

			case GET_MY_SCORE:

			Device_private_data data = filp->private_data;
			return data->score;

			break;

			default: return -ENOTTY;
		}
	}



/* Module Declarations */

/* 
 * This structure will hold the functions to be called
 * when a process does something to the device we
 * created. Since a pointer to this structure is kept in
 * the devices table, it can't be local to
 * init_module. NULL is for unimplemented functions. 
 */
	struct file_operations fops = {
		.open=		open,
		.release=	release,
		.read=		read,
		.write=		write,
		.llseek=	llseek,
		.ioctl=		ioctl,
		.owner=		THIS_MODULE,
	};

/* 
 * Initialize the module - Register the character device 
 */
	int init_module()
	{
		major_num = register_chrdev(ZERO, DEVICE_NAME, &fops);

		if (major_num < 0)
		{
			printk(KERN_ALERT "Registering char device failed with %d\n",
				major_num);
			return major_num;
		}

		codeBuf = guessBuf = feedbackBuf = 0;
		memset(codeBuf, 0, BUF_LEN);
		memset(feedbackBuf, 0, BUF_LEN);
		memset(guessBuf, 0, BUF_LEN);

		num_of_codemakers = 0;
		num_of_codebrakers = 0;
		spin_lock_init(&codemaker_exits_lock);
		spin_lock_init(&counters_lock);
		init_waitqueue_head(&wq_codemakers);
		init_waitqueue_head(&wq_codebrakers);
		sema_init(&read_lock, 1);
		sema_init(&write_lock, 1);
		sema_init(&index_lock, 1);


		printk("simple device registered with %d major\n",major_num);

		return 0;
	}

/* 
 * Cleanup - unregister the appropriate file from /proc 
 */
	void cleanup_module()
	{
		int ret;

		ret = unregister_chrdev(major_num, DEVICE_NAME);

		if (ret < 0)
			printk(KERN_ALERT "Error: unregister_chrdev: %d\n", ret);

		printk("releasing module with %d major\n",major_num);
	}

	void generateCode(){
		srand((unsigned)time(&t));

		for( i = 0 ; i < BUF_LEN ; i++ ) 
		{
			codeBuf[i] = 4 + rand() % 6;
		}
	}