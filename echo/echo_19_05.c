/*                                                     
 * $Id: echo.c,v 1.5 2004/10/26 03:32:21 corbet Exp $ 
 */                                                    
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/ioctl.h>
#include "echo.h"

//MODULE_LICENSE("Dual BSD/GPL");

int echo_major        =   ECHO_MAJOR;
int echo_devs         =   ECHO_DEVS;
int echo_qset         =   ECHO_QSET;
int echo_quantum      =   ECHO_QUANTUM;
int echo_buffer_size  =   ECHO_BUFFER_SIZE;
int result;

struct echo_dev *echo_devices;  // TO ALLOCATE IN ECHO_INIT


int echo_open (struct inode *inodep, struct file *filep)
{
	struct echo_dev *dev; /* device information */

	/*  Find the device */
	dev = container_of(inodep->i_cdev, struct echo_dev, cdev);

	/* and use filp->private_data to point to the device data */
	filep->private_data = dev;

	printk(KERN_ALERT "POINTER AVAILABLE IN PRIVATE_DATA\n");

	return nonseekable_open(inodep, filep);

}

int echo_release (struct inode *inodep, struct file *filep)
{
	printk(KERN_ALERT "ECHO_RELEASE INVOKED\n");
	return 0;
}

////// POR FAZER:

// ssize_t read(struct file *filep, char __user *buff, size_t count, loff_t *offp);

// ssize_t write(struct file *filep, const char __user *buff, size_t count, loff_t *offp);

/////////////////



//////////////////////////////////////
struct file_operations fops = {
	.owner =     THIS_MODULE,
	.open =	     echo_open,
	.release =   echo_release,
	};
//////////////////////////////////////



static int echo_init(void)
{
	int result, i;
	dev_t dev = MKDEV(echo_major, 0);
	
	/*
	 * Register your major, and accept a dynamic number.
	 */
	if (echo_major)
		result = register_chrdev_region(dev, echo_devs, "echo");
	else {
		result = alloc_chrdev_region(&dev, 0, echo_devs, "echo");
		echo_major = MAJOR(dev);
	}
	if (result < 0)
		return result;

	
	/* 
	 * allocate the devices -- we can't have them static, as the number
	 * can be specified at load time
	 */
	echo_devices = kmalloc(echo_devs*sizeof (struct echo_dev), GFP_KERNEL);
	if (!echo_devices) {
		result = -ENOMEM;
		unregister_chrdev_region(dev, echo_devs);

		return result;
	}


	for (i = 0; i < echo_devs; i++) {

		int devaux = MKDEV(echo_major, i);
    
		cdev_init(&echo_devices[i].cdev, &fops);
		echo_devices[i].cdev.owner = THIS_MODULE;
		echo_devices[i].cdev.ops = &fops;

		if (cdev_add(&echo_devices[i].cdev, devaux, 1)<0) {
			result = -ENOMEM;
			unregister_chrdev_region(devaux, echo_devs);
			printk(KERN_NOTICE "CDEV_ADD ERROR (DEVICE %d)\n", i+1);
			
			return result;
		}
	}
	return 0;
}

static void echo_cleanup(void)
{
	int i;
	dev_t dev = MKDEV(echo_major, 0);

	for (i = 0; i < echo_devs; i++) {
		cdev_del(&echo_devices[i].cdev);
	}

	kfree(echo_devices);
	unregister_chrdev_region(dev, echo_devs);
	printk(KERN_ALERT "Goodbye, cruel world\n");
	
}

module_init(echo_init);
module_exit(echo_cleanup);
