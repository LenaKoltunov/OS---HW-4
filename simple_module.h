/*
 *  chardev.h - the header file with the ioctl definitions.
 *
 *  The declarations here have to be in a header file, because
 *  they need to be known both to the kernel module
 *  (in chardev.c) and the process calling ioctl (ioctl.c)
 */

#ifndef SIMPLE_MODULE_H
#define SIMPLE_MODULE_H

#include <linux/ioctl.h>

#define DEVICE_NAME "simple_dev"

#endif
