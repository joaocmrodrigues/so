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

#include <linux/interrupt.h>
#include <linux/kfifo.h>

#define PORT 0x3f8 // first of range
#define PORT_LEN 8
#define UART_DIV_1200   96	/* Divisor for 1200 bps */

#define SERI_DEVS       1
#define MAJOR_AUX       0
#define MINOR_AUX       0
#define USERS           1

#define TIMEOUT					500
#define IRQ             4
#define KFIFO_SIZE      1024 // 2^10

//MODULE_LICENSE("Dual BSD/GPL");

typedef struct {
	struct cdev cdev;

	spinlock_t fifo_rx_lock;
	spinlock_t fifo_tx_lock;    // lock transmiter
	spinlock_t open_lock; // lock users
	struct kfifo *fifo_rx; // receiver fifo
	struct kfifo *fifo_tx; // transmiter fifo

	wait_queue_head_t wq_rx;	// for IH synchron - receive
    wait_queue_head_t wq_tx;	// for IH synchron - transmit

} seri_dev_t;

//VARI INIT

dev_t dev;
seri_dev_t *seri_device;      // FOR ALLOCATION IN SERI_INIT

int users  =   USERS;
int major  =   MAJOR_AUX;
int minor  =   MINOR_AUX;

irqreturn_t int_handler(int irq, void *dev_id)
{
    unsigned char IIR = UART_IIR;
    unsigned char buffer;

        if (inb(PORT+IIR) & UART_IIR_RDI)
        {
            buffer = inb(PORT + UART_RX);
            kfifo_put(seri_device->fifo_rx, &buffer, 1);

            wake_up_interruptible(&(seri_device->wq_rx));
        }

        if ((inb(PORT+IIR) & UART_IER_THRI) && kfifo_len(seri_device->fifo_tx)>0)
        {
            kfifo_get(seri_device->fifo_tx, &buffer, 1);
            outb(buffer, PORT + UART_TX);

            wake_up_interruptible(&(seri_device->wq_tx));
        }

    return 0;
}


int seri_open (struct inode *inodep, struct file *filep)
{
	filep->private_data = seri_device;

	printk(KERN_ALERT "POINTER AVAILABLE IN PRIVATE_DATA\n");

	return nonseekable_open(inodep, filep);

}

int seri_release (struct inode *inodep, struct file *filep)
{
    if (users)
    {
        users--;
        printk(KERN_ALERT "REALEASE INVOKED\n");
    }
	return 0;
}

ssize_t seri_read(struct file *filep, char __user *buff, size_t count, loff_t *offp)
{
    int count_aux=0, output_aux;
		char *buffer = kmalloc(sizeof(char)*(count+1), GFP_KERNEL);


    while(count_aux < count)
    {
        count_aux= kfifo_len(seri_device->fifo_rx);

        output_aux= wait_event_interruptible_timeout(seri_device->wq_rx, kfifo_len(seri_device->fifo_rx)>count_aux, TIMEOUT);
        // The condition ( kfifo_len(seri_d->rxfifo) > count_aux ) is checked each time the waitqueue wq_rx is woken up
        // If new data available (new data on fifo) it wokes up. (kfifo_len > old kfifo_len)
        // If interrupted by a signal it wokes up
        // If the TIMEOUT defined time passes and the condition evaluated to false returns 0

        if(output_aux == 0)                 // NO MORE DATA AFTER TIMEOUT
        {
            printk("READ FUNC:  NO MORE DATA AFTER TIMEOUT\n");
            break;
        }

         if(output_aux == -ERESTARTSYS)     // INTERRUPTED BY A SIGNAL
        {
            printk("READ FUNC:  ERROR: WAIT_EVENT_INTERRUPTIBLE_TIMOUT: INTERRUPTED BY A SIGNAL\n");

            return output_aux;
        }
    }


    if (!buffer) {
		printk(KERN_NOTICE "READ FUNC:  ERROR: BUFFER ALLOCATION FAILED\n");
		return -1;
	}


    if( kfifo_get(seri_device->fifo_rx, buffer, count_aux) != count_aux )           // FIFO -> BUFFER
    {
        printk("READ FUNC:  ERROR: NOT ALL DATA TRANSFERED TO THE BUFFER\n");
        goto error;
    }


    buffer[count_aux] = '\0';


    ////////////////////  PROTECTIONS  ////////////////////


    // OVERRUN ERROR
	if( inb(PORT+UART_LSR) & UART_LSR_OE )
	{
		printk(KERN_ALERT "READ FUNC:  ERROR: at least one character in the Buffer Receiver Register was overwritten by another character\n");
		goto error;
	}

    // PARITY ERROR
	if( inb(PORT+UART_LSR) & UART_LSR_PE )
	{
		printk(KERN_ALERT "READ FUNC:  ERROR: received character does not have the expected parity\n");
		goto error;
	}

    // FRAMING ERROR
	if( inb(PORT+UART_LSR) & UART_LSR_FE )
	{
		printk(KERN_ALERT "READ FUNC:  ERROR: received character did not have a valid stop bit\n");
		goto error;
	}

    ///////////////////////////////////////////////////////



    if( copy_to_user(buff, buffer, count_aux) )             // BUFFER -> BUFF
    {
        printk("READ FUNC:  ERROR: NOT ALL DATA TRANSFERED TO BUFF\n");
        goto error;
    }

    kfree(buffer);


    return count_aux;



  error:
	kfree(buffer);
    return -1;
}



ssize_t seri_write(struct file *filep, const char __user *buff, size_t count, loff_t *offp)
{
	char *buffer = kmalloc(sizeof(char)*(count+1), GFP_KERNEL);
  unsigned char first_char;
	int output_aux, count_aux=0;

    if (!buffer) {
		printk(KERN_NOTICE "WRITE FUNC:  BUFFER ALLOCATION FAILED\n");
		return -1;
	}

		output_aux=copy_from_user(buffer, buff, count);
    if(output_aux)
    {
        printk(KERN_ALERT "WRITE FUNC: %d chars not read from copy_from_user (BUFF -> BUFFER)\n", output_aux);
        return output_aux;
    }

    buffer[count] = '\0';


    while (count_aux < count)
    {
        output_aux = wait_event_interruptible_timeout(seri_device->wq_tx, kfifo_len(seri_device->fifo_rx) < KFIFO_SIZE, TIMEOUT);
        // by this way we assure that all chars are read in case of a full FIFO
        // IH frees it and wakes up so the remaining chars can be read (count - count_aux)

        if(output_aux == 0)                 // NO MORE DATA AFTER TIMEOUT
        {
            printk("READ FUNC:  NO MORE DATA AFTER TIMEOUT\n");
            break;
        }

        if(output_aux == -ERESTARTSYS)     // INTERRUPTED BY A SIGNAL
        {
            printk("READ FUNC:  ERROR: WAIT_EVENT_INTERRUPTIBLE_TIMOUT: INTERRUPTED BY A SIGNAL\n");

            return output_aux;
        }


        count_aux += kfifo_put(seri_device->fifo_tx, buffer+count_aux, count-count_aux);
        // count_aux saves the number of chars read till the moment
        // the process continues from the last char read ( buffer+count_aux )



        // the first char after an "idle" period of UART transmitter must be sent by DD code other than the IH
        if( (inb(PORT + UART_LSR)  &  UART_LSR_THRE) )
        {
            kfifo_get(seri_device->fifo_tx , &first_char , 1);
            outb(first_char , PORT + UART_TX);
        }
    }

	kfree(buffer);
	return count_aux;
}



//////////////////////////////////////
struct file_operations fops = {
	.owner =     THIS_MODULE,
	.open =	     seri_open,
	.release =   seri_release,
    .read =      seri_read,
    .write =     seri_write,
	};
//////////////////////////////////////



static int seri_init(void)
{
	int output_aux;
    unsigned char LCR;

	dev_t dev = MKDEV(major, 0);



	if (major) {
		output_aux = register_chrdev_region(dev, SERI_DEVS, "seri");
    }

	else {
		output_aux = alloc_chrdev_region(&dev, 0, SERI_DEVS, "seri");
		major = MAJOR(dev);
	}

	if (output_aux){
        return output_aux;
    }

	seri_device = kmalloc(sizeof(seri_dev_t), GFP_KERNEL);
	cdev_init(&(seri_device->cdev), &fops);
	seri_device->cdev.owner=THIS_MODULE;
	seri_device->cdev.ops=&fops;

	if (cdev_add(&(seri_device->cdev), dev, SERI_DEVS)<0) {
		unregister_chrdev_region(dev, SERI_DEVS);
		printk(KERN_ALERT "CDEV_ADD ERROR\n");

		return -1;
	}

    if (!request_region(PORT, PORT_LEN, "seri")) {
        printk(KERN_ALERT "REQUEST_REGION ERROR\n");

        return -1;
    }

		output_aux = request_irq(IRQ, int_handler, SA_INTERRUPT, "seri", seri_device);

    if(output_aux)
    {
        printk(KERN_ALERT "REQUEST_IRQ ERROR\n");

        return output_aux;
    }


    // FIFO INI AND ASSOCIATED LOCKS //

    spin_lock_init(&(seri_device->fifo_tx_lock));
	spin_lock_init(&(seri_device->fifo_rx_lock));

    kfifo_alloc(KFIFO_SIZE, GFP_KERNEL, &(seri_device->fifo_tx_lock));  // Transmitter FIFO allocation
    kfifo_alloc(KFIFO_SIZE, GFP_KERNEL, &(seri_device->fifo_rx_lock));  // Receiver FIFO allocation

    ///////////////////////////////////





    LCR = UART_LCR_DLAB;

	outb(LCR , PORT + UART_LCR); // DLAB 1

	outb(UART_DIV_1200 , PORT + UART_DLL); // 1200 bps
	outb(0 , PORT + UART_DLM);

	LCR &= ~UART_LCR_DLAB;
	outb(LCR , PORT + UART_LCR); // DLAB 0

	LCR = UART_LCR_WLEN8 | UART_LCR_STOP | UART_LCR_PARITY | UART_LCR_EPAR; // Word Length: 8 bits, Parity: Even, Stop Bits: 2

	outb(LCR , PORT + UART_LCR);
	outb(0 , PORT + UART_IER);


    printk("SERI_INIT: SUCCESS\n");

	return 0;
}

static void seri_cleanup(void)
{
    dev_t dev = MKDEV(major, 0);


    kfifo_free(seri_device->fifo_tx);
    kfifo_free(seri_device->fifo_rx);

    free_irq(IRQ, seri_device);

    release_region(PORT, PORT_LEN);
	cdev_del(&(seri_device->cdev));
	unregister_chrdev_region(dev, SERI_DEVS);


	printk(KERN_ALERT "Goodbye, cruel world\n");
}

module_init(seri_init);
module_exit(seri_cleanup);
