#ifndef _MY_MODULE_H_
#define _MY_MODULE_H_

#include <linux/ioctl.h>

#define MY_MAGIC 'r'
#define MY_OP1 _IOW(MY_MAGIC,0,int)
#define MY_OP2 _IO(MY_MAGIC,1)

#endif
