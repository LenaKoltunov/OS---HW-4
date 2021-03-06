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
#include "mastermind.h"

MODULE_LICENSE("GPL");

//--------------------------------- Defines ---------------------------------//
#define BUF_LEN 5
#define PASS_LEN BUF_LEN-1
#define NUM_TURNS 10
#define ZERO 0
#define DEVICE_NAME "mastermind"


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

int colour_range;

bool in_round;
int round_id;

bool maker_won;
bool breaker_won;

char passwordBuf[BUF_LEN];
char guessBuf[BUF_LEN];

bool guessedCorrect;

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

int num_of_codemakers;
int num_of_codebrakers;
int num_of_codebrakers_playing;

wait_queue_head_t wq;

spinlock_t spin_lock;
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

	spin_lock(spin_lock);
	if (num_of_codemakers && (minor == 0))
		return -EPERM;

	spin_unlock(spin_lock);
	filp->private_data = kmalloc(sizeof(Device_private_data), GFP_KERNEL);
	if (filp->private_data == NULL)
		return -ENOMEM;

	Device_private_data data = filp->private_data;
	data->minor = minor;
	data->score = 0;

	spin_lock(spin_lock);
	data->round_id = round_id;
	spin_unlock(spin_lock);

	filp->f_op = &fops;
	if (minor == 1) {
		data->turns = NUM_TURNS;
	}
	spin_lock(spin_lock);
	if (minor == 0)
	{
		num_of_codemakers++;
	}
	if (minor == 1)
	{
		num_of_codebrakers++;
		num_of_codebrakers_playing++;
	}
	spin_unlock(spin_lock);

	return 0;

}

/*
 * This function should close the module for the invoker, freeing used data and updating global
 * variables accordingly.
 * The Codemaker may only release the module when the round is over, if the round is in progretvals
 * this function should return -EBUSY to an invoking Codemaker.
 * You may assume that Codebreakers only invoke this function after playing a full turn (I.E. -
 * writing to the guess buffer AND reading from the feedback buffer)
 */
int release(struct inode* inode, struct file* filp){
	if( filp->f_mode & O_RDWR ){
		Device_private_data data = filp->private_data;

		if (data->minor == 0)
		{
			spin_lock(spin_lock);
			if (in_round && !breaker_won && !maker_won){
				spin_unlock(spin_lock);
				return -EBUSY;
			}

			num_of_codemakers--;
			in_round = passwordReady = false;
			maker_won = breaker_won = false;
			spin_unlock(spin_lock);

			wake_up_interruptible(&wq);
		}

		if (data->minor == 1){
			if (data->turns>0)
			{
				spin_lock(spin_lock);
				num_of_codebrakers_playing--;
				if (in_round && !num_of_codebrakers_playing)
					maker_won = true;
			}
			num_of_codebrakers--;
		}
		spin_unlock(spin_lock);
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
	int retval;

	Device_private_data data = filp->private_data;
	if (data->minor == 0 ){
		spin_lock(spin_lock);
		if (!in_round){
			spin_unlock(spin_lock);
			return -EIO;
		}

		if (!guessReady){
			if (num_of_codebrakers_playing == 0){
				spin_unlock(spin_lock);
				return EOF;
			}
			spin_unlock(spin_lock);
			retval = wait_event_interruptible(wq, guessReady);
			if (retval){
				return -EINTR;
			}
			spin_lock(spin_lock);
			if (!in_round){
				spin_unlock(spin_lock);
				return -EIO;
			}
		}

		char feedbackBuf[BUF_LEN];
		feedbackBuf[BUF_LEN-1] = '\0';
		generateFeedback(feedbackBuf, guessBuf, passwordBuf);

		spin_lock(spin_lock);
		if (guessReady){
			spin_unlock(spin_lock);

			int i;
			for (i = 0; i < count && i < PASS_LEN;i++){
				retval = put_user(feedbackBuf[i], buf++);
				if (retval)
					return retval;
			}
			retval = put_user(NULL, buf++);
			if (retval)
				return retval;

		} else spin_unlock(spin_lock);
		return 1;
	}

 /* ● For the Codebreaker (minor number 1) -
 *   Attempts to read the feedback buffer.
 *   If the Codebreaker has no more turns available this function should immediately return
 *   -EPERM.
 *   If the buffer is empty and no Codemaker exists this function should return EOF.
 *   If the buffer is empty but a Codemaker is pretvalent then the Codebreaker should wait until
 *   the buffer is filled by the Codemaker.
 *   If the buffer is full then this function copies its contents into buf. Additionally if the
 *   feedback buffer’s value is “2222” then that means the Codebreaker’s guess was correct,
 *   in which case he should earn a point and the round needs to end.
 *   This operation should return 1 upon success, and empty both the guess buffer and the
 *   feedback buffer
 */
	if (data->minor == 1){
		spin_lock(spin_lock);
		if (!in_round){
			spin_unlock(spin_lock);
			return -EIO;
		}

		if(round_id != data->round_id){
			data->turns = NUM_TURNS;
			data->round_id = round_id;
		}

		if (data->turns < 0)
			return -EPERM;

		if (!feedbackReady){
			if (num_of_codemakers == 0){
				spin_unlock(spin_lock);
				return EOF;
			}
			spin_unlock(spin_lock);

			retval = wait_event_interruptible(wq, passwordReady && feedbackReady);
			if (retval){
				return -EINTR;
			}
			spin_lock(spin_lock);
			if (!in_round){
				spin_unlock(spin_lock);
				return -EIO;
			}
			spin_lock(spin_lock);
		}


		char feedbackBuf[BUF_LEN];
		feedbackBuf[BUF_LEN-1] = '\0';
		generateFeedback(feedbackBuf, guessBuf, passwordBuf);

		int i;
		for (i = 0;i < count && i < PASS_LEN;i++){
			retval = put_user(feedbackBuf[i], buf++);

			if (retval){
				return retval;
			}
		}
		retval = put_user(NULL, buf++);
		if (retval)
			return retval;
/*
* If the executing Breaker didn't win, exhausted his last turn, and was the last breaker with any turns left - then the round also must end. This time, however, the Codemaker needs to win and earn a point (and the round needs to end)
* In any case - don't forget to empty both buffers
*/
		spin_lock(spin_lock);

		feedbackReady = false;
		guessReady = false;
		wake_up_interruptible(&wq);

		if (!num_of_codebrakers_playing){
			in_round = false;
		}
		spin_unlock(spin_lock);
		return 1;
	}
	return 0;
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
 *   While a round is in progretvals - attempts to write the contents of buf into the feedback
 *   buffer.
 *   The contents of buf should be generated using the generateFeedback function prior to
 *   writing.
 *   If the feedback buffer is full then the function should immediately return -EBUSY.
 *   Returns 1 upon success.
*/
ssize_t write(struct file *filp, const char *buf, size_t count, loff_t *f_pos){
	int retval;

	Device_private_data data = filp->private_data;

	if (data->minor == 0 ){
		spin_lock(spin_lock);
		if(!in_round){
			spin_unlock(spin_lock);

			int i;
			for (i = 0;i < count && i < PASS_LEN;i++){
				retval = get_user(passwordBuf[i], buf + i);
				if (retval)
					return retval;
			}

			spin_lock(spin_lock);
			passwordReady = true;
			guessReady = false;
			feedbackReady = false;

			spin_unlock(spin_lock);
			wake_up_interruptible(&wq);
			return 1;
		}

		spin_lock(spin_lock);
		if(feedbackReady){
			spin_unlock(spin_lock);
			return -EBUSY;
		}
		char feedbackBuf[BUF_LEN];
		feedbackBuf[BUF_LEN-1] = '\0';
		generateFeedback(feedbackBuf, guessBuf, passwordBuf);

		int i;
		for (i = 0;i < count && i < PASS_LEN;i++){
			retval = get_user(feedbackBuf[i], buf + i);
			if (retval)
				return retval;
		}

		feedbackReady = true;
		wake_up_interruptible(&wq);

		if (maker_won){
			data->score++;
			maker_won = false;
			in_round = false;
		}

		spin_unlock(spin_lock);

		return 1;
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
		spin_lock(spin_lock);
		if (!in_round || breaker_won || maker_won){
			spin_unlock(spin_lock);
			return -EIO;
		}

		if (data->round_id != round_id){
			data->round_id = round_id;
			data->turns = NUM_TURNS;
		}

		if(!data->turns){
			spin_unlock(spin_lock);
			return -EPERM;
		}

		if (guessReady){
			if(!num_of_codemakers){
				spin_unlock(spin_lock);
				return EOF;
			}

			spin_unlock(spin_lock);
			retval = wait_event_interruptible(wq, !guessReady);
			if (retval)
				return -EINTR;
			spin_lock(spin_lock);
			if (!in_round || breaker_won || maker_won){
				spin_unlock(spin_lock);
				return -EIO;
			}
			spin_unlock(spin_lock);
		}

		int i;
		for (i = 0;i < count && i < PASS_LEN;i++){
			retval = get_user(guessBuf[i], buf + i);
			if (retval){
				return retval;
			}
			if(!checkInput(guessBuf[i])){
				return -EINVAL;
			}
		}

		spin_lock(spin_lock);
		guessReady = true;
		data->turns--;

		if (!data->turns){
			num_of_codebrakers_playing--;
		}

		char feedbackBuf[BUF_LEN];
		feedbackBuf[BUF_LEN-1] = '\0';
		int guessed = generateFeedback(feedbackBuf, guessBuf, passwordBuf);

		if (guessed){
			data->score++;
			breaker_won = true;
		}

		if (!num_of_codebrakers_playing && !guessed){
			maker_won = true;
		}

		spin_unlock(spin_lock);
		wake_up_interruptible(&wq);

		return 1;
	}

	return 0;
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
 *   If a round is already in progretvals this should return -EBUSY.
 *   This command can only be initiated by the Codemaker, if a Codebreaker attempts to use
 *   it this function should return -EPERM
 * ● GET_MY_SCORE -
 *   Returns the score of the invoking process.
 *   In case of any other command code this function should return -ENOTTY. - DONE
 */
int ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg){
	Device_private_data data = filp->private_data;
	switch( cmd ) {
		case ROUND_START:
		if (data->minor == 1)
			return -EPERM;

		if (arg<4 || arg>10)
			return -EINVAL;

		colour_range = arg;

		spin_lock(spin_lock);
		if (in_round){
			spin_unlock(spin_lock);
			return -EBUSY;
		}

		if (!num_of_codebrakers){
			spin_unlock(spin_lock);
			return -EPERM;
		}

		if (passwordReady){
			in_round = true;
			guessReady = feedbackReady = false;
			round_id++;
			spin_unlock(spin_lock);
			return 1;
		}
		spin_unlock(spin_lock);
		return -1;

		case GET_MY_SCORE:
		if (!data->minor && maker_won){
			maker_won = false;
			in_round = false;
			data->score++;
		}
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
		return major_num;
	}

	in_round = maker_won = breaker_won = guessedCorrect = passwordReady = guessReady = feedbackReady = false;
	memset(passwordBuf, 0, BUF_LEN);
	memset(guessBuf, 0, BUF_LEN);

	num_of_codemakers = 0;
	num_of_codebrakers = 0;
	num_of_codebrakers_playing = 0;
	spin_lock_init(&spin_lock);
	init_waitqueue_head(&wq);

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
}
