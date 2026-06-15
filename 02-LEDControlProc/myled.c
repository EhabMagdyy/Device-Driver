#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>      // proc_create, proc_remove, proc_ops
#include <linux/uaccess.h>      // copy_to_user, copy_from_user
#include <linux/gpio.h>         // gpio_request, gpio_set_value, gpio_free
#include <linux/mutex.h>        // mutex
#include <linux/string.h>       // strcmp, strcasecmp

#define PROC_NAME   "myled"      // creates /proc/myled
#define LED_GPIO    529          // GPIO 17 on RPi header (17 + 512)

/* ioctl commands — same as /dev version */
#define IOCTL_LED_ON     _IO('L', 0)
#define IOCTL_LED_OFF    _IO('L', 1)
#define IOCTL_LED_TOGGLE _IO('L', 2)
#define IOCTL_LED_GET    _IOR('L', 3, int)

static struct proc_dir_entry *proc_entry;  // handle for /proc/myled

static int led_state = 0;       // 0 = off, 1 = on
static DEFINE_MUTEX(led_mutex);

static void led_set(int state){
    led_state = !!state;
    gpio_set_value(LED_GPIO, led_state);
    pr_info("myled: LED is now %s\n", led_state ? "ON" : "OFF");
}

static int led_open(struct inode *inode, struct file *filp){
    pr_info("myled: opened\n");
    return 0;
}

static int led_release(struct inode *inode, struct file *filp){
    pr_info("myled: closed\n");
    return 0;
}

// cat /proc/led  =>  "LED: ON\n" or "LED: OFF\n"
static ssize_t led_read(struct file *filp, char __user *buf, size_t count, loff_t *offset){
    char msg[32];
    size_t msg_len;
    size_t to_send;

    mutex_lock(&led_mutex);

    msg_len = snprintf(msg, sizeof(msg), "LED: %s\n", led_state ? "ON" : "OFF");

    if(*offset >= msg_len){
        mutex_unlock(&led_mutex);
        return 0;
    }

    to_send = min(count, msg_len -(size_t)*offset);

    if(copy_to_user(buf, msg + *offset, to_send)){
        mutex_unlock(&led_mutex);
        return -EFAULT;
    }

    *offset += to_send;
    mutex_unlock(&led_mutex);

    return to_send;
}

// echo "1"/"0"/"on"/"off" > /proc/led
static ssize_t led_write(struct file *filp, const char __user *buf, size_t count, loff_t *offset){
    char kbuf[16] = {0};
    size_t to_copy;

    to_copy = min(count, sizeof(kbuf) - 1);

    if(copy_from_user(kbuf, buf, to_copy))
        return -EFAULT;

    kbuf[to_copy] = '\0';

    /* strip trailing newline from echo */
    if(to_copy > 0 && kbuf[to_copy - 1] == '\n')
        kbuf[to_copy - 1] = '\0';

    mutex_lock(&led_mutex);

    if(strcmp(kbuf, "1") == 0 || strcasecmp(kbuf, "on") == 0){
        led_set(1);
    }
    else if(strcmp(kbuf, "0") == 0 || strcasecmp(kbuf, "off") == 0){
        led_set(0);
    }
    else {
        mutex_unlock(&led_mutex);
        pr_warn("myled: unknown command \"%s\"\n", kbuf);
        return -EINVAL;
    }

    mutex_unlock(&led_mutex);
    return count;
}

static long led_ioctl(struct file *filp, unsigned int cmd, unsigned long arg){
    int state_copy;

    mutex_lock(&led_mutex);

    switch(cmd){
    case IOCTL_LED_ON:
        led_set(1);
        break;

    case IOCTL_LED_OFF:
        led_set(0);
        break;

    case IOCTL_LED_TOGGLE:
        led_set(!led_state);
        break;

    case IOCTL_LED_GET:
        state_copy = led_state;
        mutex_unlock(&led_mutex);
        if(copy_to_user((int __user *)arg, &state_copy, sizeof(int)))
            return -EFAULT;
        return 0;

    default:
        mutex_unlock(&led_mutex);
        return -ENOTTY;
    }

    mutex_unlock(&led_mutex);
    return 0;
}

static const struct proc_ops led_ops = {
    .proc_open    = led_open,
    .proc_release = led_release,
    .proc_read    = led_read,
    .proc_write   = led_write,
    .proc_ioctl   = led_ioctl,
};

static int __init myled_init(void){
    int ret;

    // 1. Request and configure GPIO17
    ret = gpio_request(LED_GPIO, "rpi_myled");
    if(ret){
        pr_err("myled: gpio_request failed: %d\n", ret);
        return ret;
    }

    ret = gpio_direction_output(LED_GPIO, 0);   // start OFF
    if(ret){
        pr_err("myled: gpio_direction_output failed: %d\n", ret);
        gpio_free(LED_GPIO);
        return ret;
    }

    // 2. Create /proc/myled
    proc_entry = proc_create(PROC_NAME, 0666, NULL, &led_ops);
    if(!proc_entry){
        pr_err("myled: proc_create failed\n");
        gpio_free(LED_GPIO);
        return -ENOMEM;
    }

    led_state = 0;
    pr_info("myled: loaded — GPIO%d → /proc/%s(LED is OFF)\n", LED_GPIO - 512, PROC_NAME);

    return 0;
}

static void __exit myled_exit(void){
    led_set(0);                  // turn LED off on unload
    proc_remove(proc_entry);     // removes /proc/led
    gpio_free(LED_GPIO);         // release GPIO17
    pr_info("myled: unloaded, GPIO%d released\n", LED_GPIO - 512);
}

module_init(myled_init);
module_exit(myled_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ehab");
MODULE_DESCRIPTION("RPi 3B+ LED driver via /proc/led");
MODULE_VERSION("1.0");