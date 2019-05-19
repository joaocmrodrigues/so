/*                                                     
 * $Id: serp.c,v 1.5 2004/10/26 03:32:21 corbet Exp $ 
 */                                                    
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/ioctl.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/delay.h>

#include "serial_reg.h"

#define PORT 0x3f8 // first of range
#define UART_DIV_1200   96	/* Divisor for 1200 bps */

//MODULE_LICENSE("Dual BSD/GPL");

int serp_major        =   0;
int serp_devs         =   1;
int serp_buffer_size  =   4000;
int result;

struct cdev *serp_device; 


int serp_open (struct inode *inodep, struct file *filep)
{
	filep->private_data = serp_device;

	printk(KERN_ALERT "POINTER AVAILABLE IN PRIVATE_DATA\n");

	return nonseekable_open(inodep, filep);

}

int serp_release (struct inode *inodep, struct file *filep)
{
	printk(KERN_ALERT "serp_RELEASE INVOKED\n");
	return 0;
}

ssize_t serp_read(struct file *filep, char __user *buff, size_t count, loff_t *offp)
{
    unsigned int LSR = PORT + UART_LSR;

	char *buffer=kmalloc(sizeof(char)*(count+1), GFP_KERNEL);

	if (!buffer) {
		printk(KERN_NOTICE "READ FUNC:  BUFFER ALLOCATION FAILED\n");
		return -1;
	}

	while( !( inb(LSR) & UART_LSR_DR ) )
	{
		set_current_state(TASK_INTERRUPTIBLE);
		msleep_interruptible(5);
	}

	if( inb(LSR) & UART_LSR_OE )
	{
		printk(KERN_ALERT "at least one character in the Buffer Receiver Register was overwritten by another character\n");
		goto error;
	}
	
	if( inb(LSR) & UART_LSR_PE )
	{
		printk(KERN_ALERT "received character does not have the expected parity\n");
		goto error;
	}

	if( inb(LSR) & UART_LSR_FE )
	{
		printk(KERN_ALERT "received character did not have a valid stop bit\n");
		goto error;
	}
	

	*buffer=inb(PORT + UART_RX);
	
	if(copy_to_user(buff,buffer, count))
	{
		printk(KERN_ALERT "READ FUNC: chars not read from copy_to_user (BUFFER -> BUFF)\n");
		goto error;
	}

    kfree(buffer);
    return 1;


  error:
	kfree(buffer);
    return -EIO;
}

ssize_t serp_write(struct file *filep, const char __user *buff, size_t count, loff_t *offp)
{
	char *buffer = kmalloc(sizeof(char)*(count+1), GFP_KERNEL);
	int i, err=0;

    if (!buffer) {
		printk(KERN_NOTICE "WRITE FUNC:  BUFFER ALLOCATION FAILED\n");
		return -1;
	}

	
    err=copy_from_user(buffer, buff, count);
    

    if(err)
    {
        printk(KERN_ALERT "WRITE FUNC: %d chars not read from copy_from_user (BUFF -> BUFFER)\n", err);
        return -1;
    }

    buffer[count - err] = '\0';


      for(i=0; i<=(count - err) ;i++){	
  
            while(! (inb(PORT + UART_LSR)  &  UART_LSR_THRE)){  // if THRE bit not empty
		      schedule();
		 }
	      outb(buffer[i],PORT+UART_TX);
	}
	
	kfree(buffer);
	return (count - err);
}



//////////////////////////////////////
struct file_operations fops = {
	.owner =     THIS_MODULE,
	.open =	     serp_open,
	.release =   serp_release,
    .read =      serp_read,
    .write =     serp_write,
	};
//////////////////////////////////////



static int serp_init(void)
{
	int result;
	dev_t dev = MKDEV(serp_major, 0);
    unsigned char LCR;


	if (serp_major)
		result = register_chrdev_region(dev, serp_devs, "serp");
	else {
		result = alloc_chrdev_region(&dev, 0, serp_devs, "serp");
		serp_major = MAJOR(dev);
	}
	if (result < 0){
        return result;
    }
    
	serp_device=cdev_alloc();
	serp_device->ops = &fops;
	serp_device->owner = THIS_MODULE;

	if (cdev_add(serp_device, dev, 1)<0) {
		unregister_chrdev_region(dev, serp_devs);
		printk(KERN_NOTICE "CDEV_ADD ERROR\n");
			
		return -1;
	}

    if (!request_region(PORT, 1, "serp")) {
        printk(KERN_NOTICE "REQUEST_REGION ERROR\n");

        return -1;
    }
    
    LCR = UART_LCR_DLAB;

	outb(LCR , PORT + UART_LCR); // DLAB 1
	
	outb(UART_DIV_1200 , PORT + UART_DLL); // 1200 bps
	outb(0x0 , PORT + UART_DLM);

	LCR &= ~UART_LCR_DLAB;
	outb(LCR , PORT + UART_LCR); // DLAB 0
	
	LCR = UART_LCR_WLEN8 | UART_LCR_STOP | UART_LCR_PARITY | UART_LCR_EPAR; // Word Length: 8 bits, Parity: Evan, Stop Bits: 2

	outb(LCR , PORT + UART_LCR);
	outb(0 , PORT + UART_IER);

	return 0;
}

static void serp_cleanup(void)
{
    dev_t dev = MKDEV(serp_major, 0);

    release_region(PORT, 0x7);
	cdev_del(serp_device);
	unregister_chrdev_region(dev, serp_devs);


	printk(KERN_ALERT "Goodbye, cruel world\n");
}

module_init(serp_init);
module_exit(serp_cleanup);
