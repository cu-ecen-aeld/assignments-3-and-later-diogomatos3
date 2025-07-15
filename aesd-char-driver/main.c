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
#include <linux/slab.h> // for kfree and kmalloc
#include "aesdchar.h"
#include "aesd-circular-buffer.h" // Ensure this header is included for buffer functions
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Diogo Matos");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

/**
 * @brief Opens the AESD character device.
 * @param inode Pointer to inode structure.
 * @param filp Pointer to file structure.
 * @return 0 on success, negative error code otherwise.
*/
int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    if (!inode->i_cdev) {
        PDEBUG("inode->i_cdev is NULL");
        return -ENODEV;
    }
    // Use container_of to retrieve the aesd_dev structure from the cdev pointer
    filp->private_data = container_of(inode->i_cdev, struct aesd_dev, cdev);
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /* No special cleanup required on release */
    return 0;
}

/*
 * 
 * @param filp Pointer to the file structure.
 * @param buf User-space buffer to copy data into.
 * @param count Number of bytes to read.
 * @param f_pos File position offset.
 * @return Number of bytes read on success, 0 for EOF, or negative error code.
 */
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entry;
    size_t entry_offset = 0;
    size_t bytes_to_copy;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);

    if (mutex_lock_killable(&dev->lock))
        return -ERESTARTSYS;

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, *f_pos, &entry_offset);
    if (!entry) {
        retval = 0; // No data available
        goto out;
    }

    if (entry_offset >= entry->size) {
        retval = 0; // No more data to read in this entry
        goto out;
    }

    bytes_to_copy = min(count, entry->size - entry_offset);

    if (copy_to_user(buf, entry->buffptr + entry_offset, bytes_to_copy)) {
        retval = -EFAULT;
        goto out;
    }

    *f_pos += bytes_to_copy;
    retval = bytes_to_copy;

out:
    mutex_unlock(&dev->lock);
    return retval;
}

/**
 * @brief Writes data to the AESD character device.
 * @param filp Pointer to the file structure.
 * @param buf User-space buffer containing data to write.
 * @param count Number of bytes to write.
 * @param f_pos File position offset.
 * @return Number of bytes written on success, or negative error code on failure.
 */
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    char *kbuf;
    struct aesd_buffer_entry entry, *old_entry = NULL;
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);

    if (mutex_lock_killable(&dev->lock))
        return -ERESTARTSYS;

    kbuf = kmalloc(count, GFP_KERNEL);
    if (!kbuf) {
        retval = -ENOMEM;
        goto out;
    }

    if (copy_from_user(kbuf, buf, count)) {
        kfree(kbuf);
        retval = -EFAULT;
        goto out;
    }

    entry.buffptr = kbuf;
    entry.size = count;

    // Always add a new entry; if the buffer is full, free the memory of the entry being overwritten
    if (dev->buffer.full) {
        old_entry = &dev->buffer.entry[dev->buffer.out_offs];
        if (old_entry->buffptr) {
            kfree(old_entry->buffptr);
            old_entry->buffptr = NULL;
            old_entry->size = 0;
        }
    }
    aesd_circular_buffer_add_entry(&dev->buffer, &entry);
    retval = count;

out:
    mutex_unlock(&dev->lock);
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



/**
 * @brief Initializes the AESD character device module.
 * Allocates device numbers, initializes the device structure, circular buffer, and mutex,
 * and sets up the character device.
 * @return 0 on success, negative error code otherwise.
 */
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
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    aesd_circular_buffer_init(&aesd_device.buffer);

    /*
     * mutex_init does not fail in current kernel implementations,
     * but if future changes allow for failure, error handling should be added here.
     */
    mutex_init(&aesd_device.lock);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        struct aesd_buffer_entry *entry;
        AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, i) {
            if (entry->buffptr) {
                kfree(entry->buffptr);
                entry->buffptr = NULL;
            }
        }
        mutex_destroy(&aesd_device.lock);
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

/**
 * @brief Cleans up the AESD character device module.
 * Frees allocated memory, deletes the character device, and unregisters device numbers.
 */
void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    struct aesd_buffer_entry *entry;
    /**
     * Iterate over all entries in the AESD circular buffer and free any allocated memory.
     * The AESD_CIRCULAR_BUFFER_FOREACH macro is defined in aesd-circular-buffer.h and
     * allows iteration over each buffer entry.
     */
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, i) {
        if (entry->buffptr) {
            kfree(entry->buffptr);
            entry->buffptr = NULL;
            entry->size = 0;
        }
    }

    cdev_del(&aesd_device.cdev);
    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
