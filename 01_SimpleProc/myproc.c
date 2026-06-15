#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>      // proc_create, proc_remove, proc_ops
#include <linux/uaccess.h>      // copy_to_user, copy_from_user
#include <linux/mutex.h>        // mutex
#include <linux/string.h>       // memset, strlen

#define PROC_NAME    "myproc"           // => creates /proc/myproc
#define BUFFER_SIZE  1024

static struct proc_dir_entry *proc_entry;  // handle returned by proc_create

static char   kbuffer[BUFFER_SIZE];        // shared kernel mailbox
static size_t kbuffer_len = 0;             // bytes currently stored
static int    open_count  = 0;             // lifetime open counter

static DEFINE_MUTEX(myproc_mutex);         // protects kbuffer

/*
 * Called when any process opens /proc/myproc.
 *
 * `inode` => holds metadata
 * `filp`  => this specific open instance
 */
static int myproc_open(struct inode *inode, struct file *filp){
    open_count++;
    pr_info("myproc: opened(total opens: %d)\n", open_count);
    return 0;
}

/*
 * Called when the last fd referencing this open instance is closed.
 */
static int myproc_release(struct inode *inode, struct file *filp){
    pr_info("myproc: closed\n");
    return 0;
}

/*
 * Called when user does:  cat /proc/myproc
 *
 * The VFS calls read() repeatedly until we return 0(EOF).
 * `offset` tracks how far the user has already read — we use it
 * so the second call correctly returns 0 instead of looping forever.
 *
 * Flow:
 *   1st call: *offset == 0  => copy data => return bytes_read, advance *offset
 *   2nd call: *offset >= kbuffer_len => return 0(EOF) => cat stops
 */
static ssize_t myproc_read(struct file *filp, char __user *buf, size_t count, loff_t *offset){
    ssize_t bytes_to_send;

    mutex_lock(&myproc_mutex);

    // EOF check — user has already read everything
    if(*offset >= kbuffer_len){
        mutex_unlock(&myproc_mutex);
        return 0;
    }

    // How many bytes remain from current position
    bytes_to_send = min(count, kbuffer_len -(size_t)*offset);

    // Returns 0 on success, or bytes NOT copied on partial failure.
    if(copy_to_user(buf, kbuffer + *offset, bytes_to_send)){
        mutex_unlock(&myproc_mutex);
        return -EFAULT;
    }

    *offset += bytes_to_send;   // move offset

    mutex_unlock(&myproc_mutex);

    pr_info("myproc: read %zd bytes(offset now %lld)\n", bytes_to_send, *offset);
    return bytes_to_send;
}

/*
 * Called when user does:  echo "hello" > /proc/myproc
 *
 * Each write REPLACES the buffer(not appends).
 * `echo` sends "hello\n" — we store it including the newline so
 * `cat` output looks natural on the terminal.
 */
static ssize_t myproc_write(struct file *filp, const char __user *buf, size_t count, loff_t *offset){
    size_t to_write;

    mutex_lock(&myproc_mutex);

    // Clamp — never write past buffer, keep 1 byte for '\0'
    to_write = min(count,(size_t)(BUFFER_SIZE - 1));

    // Returns 0 on success.
    if(copy_from_user(kbuffer, buf, to_write)){
        mutex_unlock(&myproc_mutex);
        return -EFAULT;
    }

    kbuffer[to_write] = '\0';   // null-terminate for safe pr_info
    kbuffer_len = to_write;

    mutex_unlock(&myproc_mutex);

    pr_info("myproc: stored %zu bytes => \"%s\"\n", to_write, kbuffer);

    return to_write;    // must return count — tells VFS all bytes consumed
}

// In kernel ≥ 5.6(which Raspbian Bullseye/Bookworm ships), /proc files use `struct proc_ops` instead of `struct file_operations`.
static const struct proc_ops myproc_ops = {
    .proc_open    = myproc_open,
    .proc_release = myproc_release,
    .proc_read    = myproc_read,
    .proc_write   = myproc_write,
};

static int __init myproc_init(void){
    /*
     * proc_create(name, permissions, parent, ops)
     *
     *   name       => "myproc"  creates /proc/myproc
     *   0666       => rw-rw-rw-(readable AND writable by everyone)
     *   NULL       => parent is /proc itself(not a subdirectory)
     *   &myproc_ops=> our dispatch table
     *
     * Returns NULL on failure — no errno, check IS_ERR-style manually.
     */
    proc_entry = proc_create(PROC_NAME, 0666, NULL, &myproc_ops);
    if(!proc_entry){
        pr_err("myproc: proc_create failed — /proc/%s not created\n", PROC_NAME);
        return -ENOMEM;
    }

    memset(kbuffer, 0, BUFFER_SIZE);
    pr_info("myproc: loaded — /proc/%s is ready\n", PROC_NAME);
    return 0;
}

static void __exit myproc_exit(void){
    //removes the entry AND waits for any in-progress read/write calls to finish before returning — safe teardown.
    proc_remove(proc_entry);
    pr_info("myproc: unloaded — /proc/%s removed\n", PROC_NAME);
}

module_init(myproc_init);
module_exit(myproc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ehab");
MODULE_DESCRIPTION("Clean /proc driver — open/read/write demo on Raspbian");
MODULE_VERSION("1.0");