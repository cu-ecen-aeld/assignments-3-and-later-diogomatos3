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
#include <linux/uaccess.h> // for copy_to_user and copy_from_user
#include "aesdchar.h"
#include "aesd-circular-buffer.h" // Ensure this header is included for buffer functions
#include "aesd_ioctl.h" // Include the ioctl header
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

/**
 * @brief Handle seek operations on the character device
 * @param filp File pointer
 * @param offset Offset to seek to
 * @param whence SEEK_SET, SEEK_CUR, or SEEK_END
 * @return New position after seek, or negative error code
 */
loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
{
    loff_t newpos;
    struct aesd_dev *dev = filp->private_data;
    size_t total_size = 0;
    struct aesd_buffer_entry *entry;
    uint8_t index;
    PDEBUG("llseek offset=%lld, whence=%d", offset, whence);

    // Lock the device while we calculate positions
    if (mutex_lock_killable(&dev->lock))
        return -ERESTARTSYS;

    // Calculate total size of all entries for SEEK_END
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->buffer, index) {
        if (entry->buffptr && entry->size > 0) {
            total_size += entry->size;
        }
    }

    switch(whence) {
        case SEEK_SET:
            newpos = offset;
            break;
        case SEEK_CUR:
            newpos = filp->f_pos + offset;
            break;
        case SEEK_END:
            newpos = total_size + offset;
            break;
        default:
            mutex_unlock(&dev->lock);
            return -EINVAL;
    }

    if (newpos < 0) {
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }

    // Verify the new position is within bounds
    if (newpos > total_size) {
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }

    filp->f_pos = newpos;
    mutex_unlock(&dev->lock);
    return newpos;
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
    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);

    if (mutex_lock_killable(&dev->lock))
        return -ERESTARTSYS;

    // Find the buffer entry and offset for the current file position
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, *f_pos, &entry_offset);
    if (!entry) {
        retval = 0; // No data available (EOF)
        goto out;
    }

    // If the offset is beyond the entry size, nothing to read
    if (entry_offset >= entry->size) {
        retval = 0;
        goto out;
    }

    // Only return up to 'count' bytes per read
    bytes_to_copy = min(count, entry->size - entry_offset);

    if (copy_to_user(buf, entry->buffptr + entry_offset, bytes_to_copy)) {
        retval = -EFAULT;
        goto out;
    }

    *f_pos += bytes_to_copy; // Advance file position
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
    char *kbuf = NULL, *new_buf = NULL;
    struct aesd_buffer_entry entry, *old_entry = NULL;
    ssize_t retval = -ENOMEM;
    size_t new_size, copy_offset = 0;
    char *newline_ptr = NULL;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);

    if (mutex_lock_killable(&dev->lock))
        return -ERESTARTSYS;

    // Allocate kernel buffer for incoming data
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

    // If there is a partial write, append new data to it
    if (dev->partial_write_buf) {
        new_size = dev->partial_write_size + count;
        new_buf = kmalloc(new_size, GFP_KERNEL);
        if (!new_buf) {
            kfree(kbuf);
            retval = -ENOMEM;
            goto out;
        }
        memcpy(new_buf, dev->partial_write_buf, dev->partial_write_size);
        memcpy(new_buf + dev->partial_write_size, kbuf, count);
        kfree(dev->partial_write_buf);
        kfree(kbuf);
        dev->partial_write_buf = new_buf;
        dev->partial_write_size = new_size;
    } else {
        dev->partial_write_buf = kbuf;
        dev->partial_write_size = count;
    }

    // Now, process the buffer for any complete commands (ending with '\n')
    while ((newline_ptr = memchr(dev->partial_write_buf + copy_offset, '\n', dev->partial_write_size - copy_offset))) {
        size_t cmd_len = (newline_ptr - dev->partial_write_buf) + 1;
        char *cmd_buf = kmalloc(cmd_len, GFP_KERNEL);
        if (!cmd_buf) {
            retval = -ENOMEM;
            goto out;
        }
        memcpy(cmd_buf, dev->partial_write_buf, cmd_len);
        entry.buffptr = cmd_buf;
        entry.size = cmd_len;
        // If buffer is full, free the memory of the entry being overwritten
        if (dev->buffer.full) {
            old_entry = &dev->buffer.entry[dev->buffer.out_offs];
            if (old_entry->buffptr) {
                kfree(old_entry->buffptr);
                old_entry->buffptr = NULL;
                old_entry->size = 0;
            }
        }
        aesd_circular_buffer_add_entry(&dev->buffer, &entry);
        copy_offset = cmd_len;
    }

    // If there is leftover data after the last '\n', keep it as the new partial write
    if (copy_offset < dev->partial_write_size) {
        size_t leftover = dev->partial_write_size - copy_offset;
        char *leftover_buf = kmalloc(leftover, GFP_KERNEL);
        if (!leftover_buf) {
            retval = -ENOMEM;
            goto out;
        }
        memcpy(leftover_buf, dev->partial_write_buf + copy_offset, leftover);
        kfree(dev->partial_write_buf);
        dev->partial_write_buf = leftover_buf;
        dev->partial_write_size = leftover;
    } else {
        kfree(dev->partial_write_buf);
        dev->partial_write_buf = NULL;
        dev->partial_write_size = 0;
    }

    retval = count;

out:
    mutex_unlock(&dev->lock);
    return retval;
}

/**
 * @brief Handles IOCTL commands for the AESD char driver
 * @param filp File pointer
 * @param cmd The IOCTL command
 * @param arg The argument for the IOCTL command
 * @return 0 on success, negative error code on failure
 */
long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_seekto seekto;
    int cmd_index;
    size_t total_offset = 0;
    struct aesd_buffer_entry *entry;
    uint8_t index;
    loff_t new_pos = 0;

    PDEBUG("ioctl cmd=%u, arg=%lu", cmd, arg);

    if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC) return -ENOTTY;
    if (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR) return -ENOTTY;

    if (mutex_lock_killable(&dev->lock))
        return -ERESTARTSYS;

    switch (cmd) {
        case AESDCHAR_IOCSEEKTO:
            if (copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto))) {
                mutex_unlock(&dev->lock);
                return -EFAULT;
            }

            // Find the correct command and offset
            cmd_index = 0;
            AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->buffer, index) {
                if (!entry->buffptr || !entry->size)
                    continue;

                if (cmd_index == seekto.write_cmd) {
                    // Check if offset is within bounds of this command
                    if (seekto.write_cmd_offset >= entry->size) {
                        mutex_unlock(&dev->lock);
                        return -EINVAL;
                    }
                    new_pos = total_offset + seekto.write_cmd_offset;
                    filp->f_pos = new_pos;
                    mutex_unlock(&dev->lock);
                    return 0;
                }
                total_offset += entry->size;
                cmd_index++;
            }
            // If we get here, the command number was out of range
            mutex_unlock(&dev->lock);
            return -EINVAL;

        default:
            mutex_unlock(&dev->lock);
            return -ENOTTY;
    }
}

struct file_operations aesd_fops = {
    .owner =          THIS_MODULE,
    .read =           aesd_read,
    .write =          aesd_write,
    .open =           aesd_open,
    .release =        aesd_release,
    .llseek =         aesd_llseek,
    .unlocked_ioctl = aesd_ioctl,
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
    int i; // Needed for AESD_CIRCULAR_BUFFER_FOREACH
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
    int i; // Needed for AESD_CIRCULAR_BUFFER_FOREACH
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
