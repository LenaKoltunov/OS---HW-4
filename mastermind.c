#include <linux/kernel.h>	/* We're doing kernel work */
#include <linux/module.h>	/* Specifically, a module */
#include <linux/fs.h>
#include <asm/uaccess.h>	/* for get_user and put_user */

#include "mastermind.h"
#define SUCCESS 0
#define DEVICE_NAME "mastermind"
#define BUF_LEN 4




/*
 * This function opens the module for reading/writing (you should always open the module with the
 * O_RDWR flag to support both operations).
 * If the module in question is that of the Codemaker you should make sure that no other Codemaker
 * exists, otherwise this function should close the module and return -EPERM.
 * Codebreakers should get 10 turns (each) by default when opening the module.
 */
int open(struct inode* inode, struct file* filp){

}

/*
 * ● For the Codemaker (minor number 0) -
 * Attempts to read the contents of the guess buffer.
 * If the buffer is empty and no Codebreakers with available turns exist this function should
 * return EOF.
 * If the buffer is empty but any Codebreaker exist that can still play (has turns available)
 * then the Codemaker should wait until something is written into it.
 * If the buffer is full then this function copies its contents into buf (make sure buf is of
 * adequate size).
 * Upon success this function return 1.
 * Note: This operation should NOT empty the guess buffer.
 * ● For the Codebreaker (minor number 1) -
 * Attempts to read the feedback buffer.
 * If the Codebreaker has no more turns available this function should immediately return
 * -EPERM.
 * If the buffer is empty and no Codemaker exists this function should return EOF.
 * If the buffer is empty but a Codemaker is present then the Codebreaker should wait until
 * the buffer is filled by the Codemaker.
 * If the buffer is full then this function copies its contents into buf. Additionally if the
 * feedback buffer’s value is “2222” then that means the Codebreaker’s guess was correct,
 * in which case he should earn a point and the round needs to end.
 * This operation should return 1 upon success, and empty both the guess buffer and the
 * feedback buffer
 */
ssize_t read(struct file *filp, char *buf, size_t count, loff_t *f_pos){

}

/*
 * ● For the Codemaker (minor number 0) -
 * If the round hasn’t started - this function writes the contents of buf into the
 * password buffer.
 * While a round is in progress - attempts to write the contents of buf into the feedback
 * buffer.
 * The contents of buf should be generated using the generateFeedback function prior to
 * writing.
 * If the feedback buffer is full then the function should immediately return -EBUSY.
 * Returns 1 upon success.
 * ● For the Codebreaker (minor number 1) -
 * Attempts to write the contents of buf into the guess buffer.
 * If buf contains an illegal character (one which exceeds the specified range of colors) then
 * this function should return -EINVAL.
 * If the guess buffer is full and no Codemaker exists then this function should return EOF.
 * If the guess buffer is full but a Codemaker exists then the Codebreaker should wait until
 * it is emptied (you may assume that the Codebreaker who filled the buffer will eventually
 * empty it).
 * Returns 1 upon success.
 */
ssize_t write(struct file *filp, const char *buf, size_t count, loff_t *f_pos){

}
/*
 * This function is not needed in this exercise, but to prevent the OS from generating a default
 * implementation you should write this function to always return -ENOSYS when invoked.
 */
loff_t llseek(struct file *filp, loff_t a, int num){

}
/*
 * The Device Driver should support the following commands, as defined in mastermind.h:
 * ● ROUND_START -
 * Starts a new round, with a colour-range specified in arg.
 * If 4>arg or 10<arg then this function should return -EINVAL.
 * If a round is already in progress this should return -EBUSY.
 * This command can only be initiated by the Codemaker, if a Codebreaker attempts to use
 * it this function should return -EPERM
 * ● GET_MY_SCORE -
 * Returns the score of the invoking process.
 * In case of any other command code this function should return -ENOTTY.
 */
int ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg){

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

}

