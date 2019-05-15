/*                                                     
 * $Id: echo.c,v 1.5 2004/10/26 03:32:21 corbet Exp $ 
 */                                                    
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/ioctl.h>

//MODULE_LICENSE("Dual BSD/GPL");

#define ECHO_MAJOR 0
#define ECHO_MINOR 0
#define ECHO_DEVS 1

struct echo_dev {
	void **data;
	struct echo_dev *next; 	  /* next listitem */
	int vmas;                 /* active mappings */
	int quantum;              /* the current allocation size */
	int qset;                 /* the current array size */
	size_t size;              /* 32-bit will suffice */
	struct semaphore sem;     /* Mutual exclusion */
	struct cdev cdev;
};

int echo_major=ECHO_MAJOR;
int result;
static struct cdev* cdp;

int echo_open (struct inode *inodep, struct file *filep)
{
	struct echo_dev *dev; /* device information */

	/*  Find the device */
	dev = container_of(inodep->i_cdev, struct echo_dev, cdev);

	/* and use filp->private_data to point to the device data */
	filep->private_data = dev;

	printk(KERN_ALERT "POINTER AVAILABLE IN PRIVATE_DATA\n");

	return 0;          /* success */
}

int echo_release (struct inode *inodep, struct file *filep)
{
	printk(KERN_ALERT "ECHO_RELEASE INVOKED\n");
	return 0;
}



//////////////////////////////////////
struct file_operations fops = {
	.owner =     THIS_MODULE,
	.open =	     echo_open,
	.release =   echo_release,
	};
//////////////////////////////////////



static int echo_init(void)
{
	dev_t dev = MKDEV(ECHO_DEVS, 0);
	cdp = cdev_alloc();
	if (cdp != NULL){
	cdp->ops = &fops; 			 /* The file_operations structure */
	cdp->owner = THIS_MODULE;
	}
   
	else {
	printk(KERN_ALERT "CDEV_ALLOC FAIL\n");
	}


	if (cdev_add(cdp, dev, 1)<0) {
		result = -ENOMEM;
		unregister_chrdev_region(dev, ECHO_DEVS);
		printk("CDEV_ADD ERROR\n");
		return result;
	}

	if (echo_major){
		result = register_chrdev_region(dev, ECHO_DEVS, "echo");
	}

	else {
		result = alloc_chrdev_region(&dev, 0, ECHO_DEVS, "echo");
		echo_major = MAJOR(dev);
	}

	if (result < 0){
		return result;
	}

	printk(KERN_ALERT "MAJOR: %d\n", echo_major);
	
	return 0;
}

static void echo_cleanup(void)
{
	dev_t dev = MKDEV(echo_major, 0);
	unregister_chrdev_region(dev, ECHO_DEVS);
	cdev_del (cdp);

	printk(KERN_ALERT "Goodbye, cruel world\n");
	
}

module_init(echo_init);
module_exit(echo_cleanup);
