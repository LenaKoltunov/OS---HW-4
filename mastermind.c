//-------------------------------- Include's ---------------------------------//
#include <linux/errno.h>
#include <linux/module.h>
#include <asm/semaphore.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <asm/uaccess.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <stdio.h>
#include <stdbool.h>
#include "hw4.h"
#include "encryptor.h"
#include "mastermind.h"

MODULE_LICENSE("GPL");

//--------------------------------- Defines ---------------------------------//
#define BUF_LEN 5
#define ZERO 0
#define DEVICE_NAME "mastermind"
#define NUM_TURNS 10

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

bool codemaker_exists;
spinlock_t codemaker_exists_lock;

int colour_range;

bool in_round;
int round_id;
spinlock_t in_round_lock;

char passwordBuf[BUF_LEN];
char feedbackBuf[BUF_LEN];
char guessBuf[BUF_LEN];

bool passwordReady;
bool feedbackReady;
bool guessReady;

char *MassagePtr;

struct file_operations fops;

typedef struct device_private_data  {

	int minor;
	int turns;
	int score;
	int round_id;
}* Device_private_data;

int num_of_codemakers; //TODO
int num_of_codebrakers;

wait_queue_head_t wq_guess;


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
 	printk("trying to open file\n");

 	int minor = get_minor_from_inode(inode);

 	spin_lock(codemaker_exists_lock);
 	if (codemaker_exists && (minor == 0))
 		return -EPERM;

 	if (minor == 0)
 		codemaker_exists = true;

 	spin_unlock(codemaker_exists_lock);

 	filp->private_data = kmalloc(sizeof(Device_private_data), GFP_KERNEL);
 	if (filp->private_data == NULL)
 		return -ENOMEM;

 	Device_private_data data = filp->private_data;
 	data->minor = minor;
 	filp->f_op = &fops;

 	if (minor == 1) {
 		data->turns = NUM_TURNS;
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

 	printk("open file successfully\n");
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

			wake_up_interruptible(&wq_guess);
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

	return -1;
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
 	if (!guessReady){
 		spin_lock(counters_lock);
 		if (num_of_codebrakers == 0){
 			spin_unlock(counters_lock);
 			return EOF;
 		} else wait_event_interruptible(wq_guess, guessReady);
 	}

 	int bytes_read = 0;

 	if (guessReady){
 		MassagePtr = guessBuf;

 		if (*MassagePtr == 0)
 			return EOF;

 		while (count && *MassagePtr) {
 			if (!put_user(*(MassagePtr++), buf++)) 
 				return -ENOMEM;
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
 	if(round_id != data.round_id){
 		data->turns =NUM_TURNS;
 		data->round_id = round_id;
 	}
 	if (data->turns == 0)
 		return -EPERM;

 	if (!feedbackReady){
 		if (num_of_codemakers == 0)
 			return EOF;

 		wait_event_interruptible(wq_guess, feedbackReady > 0);
 	}

 	int bytes_read = 0;
 	bool guessed = true;

 	if (feedbackReady){
 		MassagePtr = guessBuf;
 		if (*MassagePtr == 0)
 			return EOF;

 		while (count && *MassagePtr) {
 			if (*MassagePtr != 2)
 				guessed = false;

 			if (!put_user(*(MassagePtr++), buf++))
 				return -ENOMEM;
 			count--;
 			bytes_read++;
 		}
 	}
 	if (guessed){
 		in_round = false;
 		data->score++;
 	}

 	if (bytes_read == 4){
 		feedbackReady = guessReady = false;
 		return 1;
 	}
 }
 return -1;
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
 * ● For the Codemaker (minor number 0) -
 *   If the round hasn’t started - this function writes the contents of buf into the
 *   password buffer.
 *   While a round is in progress - attempts to write the contents of buf into the feedback
 *   buffer.
 *   The contents of buf should be generated using the generateFeedback function prior to
 *   writing.
 *   If the feedback buffer is full then the function should immediately return -EBUSY.
 *   Returns 1 upon success.
*/
 ssize_t write(struct file *filp, const char *buf, size_t count, loff_t *f_pos){
 	Device_private_data data = filp->private_data;
 	if (data->minor == 0 ){
		//feedback buffer is full
 		if(feedbackReady){
 			return -EBUSY;
 		}

		//round didnt started yet
 		if(in_round==false){
 			int i;
 			for (i = 0; i < count && i < BUF_LEN; i++){
 				if (!get_user(passwordBuf[i], buf + i))
 					return -ENOMEM;
 			}
 			passwordReady = true;
 			wake_up_interruptible(wq_guess);
 			return 1;
 		}
			//round started
 		int retval = generateFeedback(feedbackBuf, guessBuf, passwordBuf);
			//we have a winner
 		if (retval){
				//TODO win case
 			return 1;
 		}
 		feedbackReady = true;
 		guessReady = false;

			//TODO in case there is breakers or not

 	}


	/*
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
	 if (data->minor == 1 ){
	 	if (!in_round) return -EIO;
	 	if(!data.turns) return -EPERM;
	 	//the guess Buffer is busy- wait
	 	if (guessReady){
	 		if(codemaker_exists){
	 			wait_event_interruptible(wq_guess, !guessReady);
	 		}else{
	 			return EOF;
	 		}
	 	}

	 	i = 0;
	 	for (; i < count && i < BUF_LEN; i++){
	 		if (!get_user(guessBuf[i], buf + i))
	 			return -ENOMEM;
	 		if(!checkInput(guessBuf[i])){
	 			guessReady = false;
	 			return -EINVAL;
	 		}
	 		guessReady = true;
	 		data.turns--;
	 	}
	 }
	 return i;
	}
/*
 * This function is not needed in this exercise, but to prevent the OS from generating a default
 * implementation you should write this function to always return -ENOSYS when invoked. - DONE
 */
	loff_t llseek(struct file *filp, loff_t a, int num){
		printk("llseek\n");
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
			printk("ioctl with ROUND_START\n");

			if (arg<4 || arg>10)
				return -EINVAL;
			colour_range = arg;

			if (in_round)
				return -EBUSY;

			Device_private_data data = filp->private_data;
			if (data->minor == 1)
				return -EPERM;

			if (passwordReady && num_of_codebrakers > 0){
				printk("Round Started\n");
				in_round = true;
				return 1;
			}

			return 0;

			case GET_MY_SCORE:
			printk("ioctl with GET_MY_SCORE\n");
			return data->score;
		}
		return -ENOTTY;
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
 		printk(KERN_ALERT "Registering mastermind device failed with %d\n",
 			major_num);
 		return major_num;
 	}

		passwordReady = guessReady = feedbackReady = false;
		memset(passwordBuf, 0, BUF_LEN);
		memset(feedbackBuf, 0, BUF_LEN);
		memset(guessBuf, 0, BUF_LEN);

 	num_of_codemakers = 0;
 	num_of_codebrakers = 0;
 	spin_lock_init(&codemaker_exists_lock);
 	spin_lock_init(&counters_lock);
 	init_waitqueue_head(&wq_guess);
 	sema_init(&read_lock, 1);
 	sema_init(&write_lock, 1);
 	sema_init(&index_lock, 1);

 	printk("mastermind device registered with %d major\n",major_num);
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

 	printk("releasing mastermind module with %d major\n",major_num);
 }
