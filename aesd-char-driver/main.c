/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/slab.h>
#include <asm/bug.h>
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

#define COMBUF_INITCAP 1024

#define LOCK_DEV(d) do {} while(down_interruptible(&d.sem))

MODULE_AUTHOR("George Hodgkins");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev the_dev;
struct file_operations aesd_fops;

int aesd_open(struct inode *inode, struct file *filp)
{
	PDEBUG("open");
	if (filp->f_op != &aesd_fops) {
	   PDEBUG("aesdchar: f_ops seems wrong: is %p, should be %p", filp->f_op, &aesd_fops);	
	}
	return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
	PDEBUG("release");
	return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
	if (!access_ok(buf, count)) return -EFAULT;
	LOCK_DEV(the_dev);
	PDEBUG("request %zu bytes with offset %lld",count,*f_pos);
	struct aesd_buffer_entry *ent, *first_ent = NULL;
	size_t ent_off = 0;
	size_t rd_off = *f_pos;
	ssize_t rd_count = 0;
	char __user *bufpos = buf;
	do {
		PDEBUG("looking for %zu more bytes starting at offset %zu", count-rd_count, rd_off);
		ent = aesd_circular_buffer_find_entry_offset_for_fpos(&the_dev.buf, rd_off, &ent_off);
	   	if (ent && (!first_ent || ent != first_ent)) {
			if (!first_ent) first_ent = ent;
			size_t copy = ent->size - ent_off;
			if (copy > count) copy = count;
			PDEBUG("found %zu bytes in buffer %p starting at offset %zu", copy, ent->buffptr, ent_off);
			print_hex_dump(KERN_ERR, "", DUMP_PREFIX_OFFSET, 8, 1, &ent->buffptr[ent_off], copy, true);
			size_t bad = __copy_to_user(bufpos, &ent->buffptr[ent_off], copy);
			if (bad)
				printk(KERN_ERR "aesdchar: %zu of %zu bytes not copied to user!", bad, copy);
			bufpos += copy;
			rd_off += copy;
			rd_count += copy;
		}
	} while (ent && rd_count < count);
	if (rd_count < count)
		PDEBUG("did not find all requested bytes: found %zu of %zu", rd_count, count);
	up(&the_dev.sem);
	return rd_count;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
	ssize_t retval = -ENOMEM;
	if (!access_ok(buf, count)) return -EFAULT;
	LOCK_DEV(the_dev);
	PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
	
	if (!the_dev.ccom) {
		the_dev.ccom = kmalloc(count, GFP_KERNEL);
		if (!the_dev.ccom) {
			printk(KERN_ERR "aesdchar: error allocating new command buffer");
			goto out;
		}
		the_dev.csz = ksize(the_dev.ccom);
		the_dev.cpos = 0;
		the_dev.ccom[count] = 0;
	} else if (the_dev.csz < the_dev.cpos + count) {
		char* ncom = krealloc(the_dev.ccom, the_dev.cpos + count, GFP_KERNEL);
		if (!ncom) {
			printk(KERN_ERR "aesdchar: error expanding command buffer");
			goto out;
		}
		the_dev.ccom = ncom;
		the_dev.csz = ksize(the_dev.ccom);
	}
	size_t bad = __copy_from_user(&the_dev.ccom[the_dev.cpos], buf, count);
	if (bad)
		printk(KERN_ERR "aesdchar: %zu of %zu bytes not copied from user!", bad, count);
	retval = count - bad;
	char* delim = memchr(&the_dev.ccom[the_dev.cpos], '\n', count);
	the_dev.cpos += count;

	if (delim) { // give entry to buffer
		struct aesd_buffer_entry ent = {
			.buffptr = the_dev.ccom,
			.size = the_dev.cpos
		};
		PDEBUG("Found delimiter, giving buffer %p with length %zu to queue", ent.buffptr, ent.size);
		// add new entry, freeing oldest entry if buffer is full
		const char* rem = aesd_circular_buffer_add_entry(&the_dev.buf, &ent);
		if (rem) {
			PDEBUG("Entry %p evicted by insertion, freeing", rem);
			kfree(rem);
		}	
		the_dev.ccom = NULL;
		the_dev.csz = 0;
		the_dev.cpos = 0;
	} else { // keep appending
		PDEBUG("no delimiter in this write");
	}

out:
	up(&the_dev.sem);
	return retval;
}

struct file_operations aesd_fops = {
	.owner =    THIS_MODULE,
	.read =     aesd_read,
	.write =    aesd_write,
	.open =     aesd_open,
	.release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
	int err, devno = MKDEV(aesd_major, aesd_minor);

	cdev_init(&dev->cdev, &aesd_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &aesd_fops;
	err = cdev_add (&dev->cdev, devno, 1);
	if (err) {
		printk(KERN_ERR "Error %d adding aesd cdev", err);
	}
	return err;
}

int aesd_init_module(void)
{
	dev_t dev = 0;
	int result;
	result = alloc_chrdev_region(&dev, aesd_minor, 1,
			"aesdchar");
	aesd_major = MAJOR(dev);
	if (result < 0) {
		printk(KERN_WARNING "Can't get major %d\n", aesd_major);
		return result;
	}
	memset(&the_dev,0,sizeof(struct aesd_dev));
	sema_init(&the_dev.sem, 1);

	result = aesd_setup_cdev(&the_dev);

	if( result ) {
		unregister_chrdev_region(dev, 1);
	}
	return result;

}

void aesd_cleanup_module(void)
{
	dev_t devno = MKDEV(aesd_major, aesd_minor);

	cdev_del(&the_dev.cdev);

	unregister_chrdev_region(devno, 1);

	aesd_circular_buffer_free(&the_dev.buf);
	kfree(the_dev.ccom);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
