#include <linux/ioctl.h>
#include <linux/cdev.h>

#define ECHO_MAJOR 0
#define ECHO_DEVS 4
#define ECHO_BUFFER_SIZE 4000
#define ECHO_QUANTUM  4000
#define ECHO_QSET     500

struct echo_dev {
	void **data;
	struct scullc_dev *next;  /* next listitem */
	int vmas;                 /* active mappings */
	int quantum;              /* the current allocation size */
	int qset;                 /* the current array size */
	size_t size;              /* 32-bit will suffice */
	struct semaphore sem;     /* Mutual exclusion */
	struct cdev cdev;
};
