# Linux Device Drivers — Complete Guide

> **Platform:** Raspberry Pi 3B+ · Raspbian (kernel 6.12.47+rpt-rpi-v8) · C · kbuild

---

## Navigation

- [1. What Is a Device Driver?](#1-what-is-a-device-driver)
- [2. Kernel Space vs User Space](#2-kernel-space-vs-user-space)
- [3. The Three Virtual Filesystems](#3-the-three-virtual-filesystems)
  - [3.1 /dev — Device Files](#31-dev--device-files)
  - [3.2 /proc — Kernel & Process Info](#32-proc--kernel--process-info)
  - [3.3 /sys — Hardware Topology](#33-sys--hardware-topology)
  - [3.4 Side-by-Side Comparison](#34-side-by-side-comparison)
- [4. How the Kernel Routes a System Call](#4-how-the-kernel-routes-a-system-call)
- [5. Character Device Driver Architecture](#5-character-device-driver-architecture)
  - [5.1 Key Data Structures](#51-key-data-structures)
  - [5.2 Registration Flow](#52-registration-flow)
  - [5.3 The file_operations Table](#53-the-file_operations-table)
- [6. /proc Driver Architecture](#6-proc-driver-architecture)
  - [6.1 Key Data Structures](#61-key-data-structures)
  - [6.2 Registration Flow](#62-registration-flow)
  - [6.3 The proc_ops Table](#63-the-proc_ops-table)
- [7. Complete Example — Simple Buffer Driver via /dev](#7-complete-example--simple-buffer-driver-via-dev)
- [8. Complete Example — Same Driver via /proc](#8-complete-example--same-driver-via-proc)
- [9. Complete Example — LED Driver via /dev](#9-complete-example--led-driver-via-dev)
- [10. Complete Example — LED Driver via /proc](#10-complete-example--led-driver-via-proc)
- [11. Full Under-the-Hood Flow — ASCII Diagrams](#11-full-under-the-hood-flow--ascii-diagrams)
  - [11.1 write() system call flow](#111-write-system-call-flow)
  - [11.2 read() system call flow](#112-read-system-call-flow)
  - [11.3 Module load/unload lifecycle](#113-module-loadunload-lifecycle)
- [12. The Makefile Explained](#12-the-makefile-explained)
- [13. GPIO Numbering on Modern RPi Kernels](#13-gpio-numbering-on-modern-rpi-kernels)
- [14. Quick Reference Cheatsheet](#14-quick-reference-cheatsheet)

---

## 1. What Is a Device Driver?

A device driver is a **software layer inside the Linux kernel** that translates
generic operating system requests (open, read, write, ioctl) into
hardware-specific operations (toggle a GPIO pin, send a byte over UART,
read a sensor register).

Without a driver, user space has no safe, standardized way to talk to hardware.

```
┌─────────────────────────────────────────────────────────┐
│                     User Space                          │
│  Applications: cat, echo, Python scripts, C programs    │
└──────────────────────┬──────────────────────────────────┘
                       │  system calls: open() read() write() ioctl()
┌──────────────────────▼──────────────────────────────────┐
│                     Kernel Space                        │
│                                                         │
│  ┌────────────┐   ┌─────────────┐   ┌───────────────┐  │
│  │    VFS     │   │  /proc fs   │   │    sysfs      │  │
│  │ (Virtual   │   │  (procfs)   │   │               │  │
│  │ File Sys)  │   └──────┬──────┘   └───────────────┘  │
│  └─────┬──────┘          │                              │
│        │          ┌──────▼──────────────────────┐       │
│        └─────────►│      Device Driver          │       │
│                   │  (your .ko module)          │       │
│                   └──────────────┬──────────────┘       │
└──────────────────────────────────┼──────────────────────┘
                                   │
                          ┌────────▼────────┐
                          │    Hardware     │
                          │  GPIO, I2C,     │
                          │  UART, SPI ...  │
                          └─────────────────┘
```

### Why Write a Driver?

| Reason | Example |
|---|---|
| Hardware doesn't have a Linux driver | Custom sensor on RPi GPIO |
| Expose kernel state cleanly | Debug buffer via `/proc` |
| Abstract hardware differences | Same `read()` API for 10 different sensors |
| Safety boundary | Kernel validates all user requests |

---

## 2. Kernel Space vs User Space

Linux enforces a **hard CPU privilege boundary** between the two:

```
CPU Privilege Levels (ARM64 / AArch64 on RPi 3B+)
──────────────────────────────────────────────────
  EL0  →  User Space    (applications, restricted)
  EL1  →  Kernel Space  (drivers, full hardware access)
  EL2  →  Hypervisor    (optional)
  EL3  →  Secure Monitor
```

| | User Space | Kernel Space |
|---|---|---|
| **Memory access** | Own virtual memory only | All physical memory |
| **Hardware access** | Forbidden (causes fault) | Direct via MMIO |
| **Crash impact** | Process dies, OS survives | Kernel panic, system crashes |
| **Your code** | Applications, scripts | Device drivers (.ko) |
| **Entry point** | `main()` | `module_init()` |
| **Headers** | `<stdio.h>`, `<stdlib.h>` | `<linux/module.h>`, `<linux/fs.h>` |

The **system call interface** is the only legal crossing point. When user
space calls `write(fd, buf, n)`, the CPU raises its privilege level,
the kernel handles the request, then drops back to user space.

```
User Space          Boundary            Kernel Space
──────────          ────────            ────────────

write("1\n")   ──► syscall trap ──►   sys_write()
                   (privilege ↑)           │
                                      VFS lookup
                                           │
                                      led_write()   ← your driver
                                           │
                   return to user ◄── gpio_set_value()
                   (privilege ↓)
```

Data cannot be directly shared across this boundary. Two dedicated functions
handle the crossing safely:

```c
copy_from_user(kernel_buf, user_buf, n)   // user → kernel
copy_to_user(user_buf, kernel_buf, n)     // kernel → user
```

Using plain `memcpy` across the boundary is a kernel bug and will cause
memory corruption or a crash.

---

## 3. The Three Virtual Filesystems

All three look like normal directories with files, but **nothing is stored on
disk**. Everything is generated live by the kernel when you read/write.

### 3.1 `/dev` — Device Files

**Purpose:** I/O doorway to hardware. Every entry is a device node with a
major:minor number pair. Opening one routes directly to a device driver.

```
/dev/
├── led         ← your GPIO LED driver   (char device)
├── ttyS0       ← UART serial port       (char device)
├── mmcblk0     ← SD card                (block device)
├── mmcblk0p1   ← SD card partition 1   (block device)
├── i2c-1       ← I2C bus 1             (char device)
├── spidev0.0   ← SPI device            (char device)
├── null        ← discard all writes    (special)
├── zero        ← infinite zero bytes   (special)
└── random      ← hardware entropy      (special)
```

```bash
ls -l /dev/ttyS0
# crwxrwxrwx 1 root dialout 4, 64 ...
# ↑                          ↑  ↑
# 'c' = character device   major minor
```

```bash
# Major number 4 → which driver subsystem owns this device
# Minor number 64 → which specific device within that subsystem
cat /proc/devices   # lists all registered major numbers
```

**Key properties:**
- Needs: major/minor number, `cdev`, `class`, `device_create`
- udev auto-creates the node when your driver calls `device_create()`
- Supports: read, write, ioctl, mmap, poll, DMA
- Correct place for: any hardware device you control

---

### 3.2 `/proc` — Kernel & Process Info

**Purpose:** A window into the running kernel. Originally process info only,
now exposes all kinds of kernel state. Simpler API than `/dev`.

```
/proc/
├── cpuinfo       ← CPU model, cores, features
├── meminfo       ← RAM usage breakdown
├── uptime        ← seconds since boot
├── modules       ← loaded kernel modules (same as lsmod)
├── interrupts    ← IRQ counts per CPU per device
├── mounts        ← currently mounted filesystems
├── 1/            ← PID 1 (systemd/init) directory
│   ├── cmdline   ← full command line
│   ├── status    ← memory, state, uid
│   ├── maps      ← memory map
│   └── fd/       ← open file descriptors
├── sys/          ← writable kernel tunables
│   ├── net/ipv4/ip_forward
│   └── vm/swappiness
└── myled         ← YOUR driver entry (custom)
```

**Key properties:**
- Needs: only `proc_create()` — one function call
- No major/minor, no class, no udev, no `/dev` node
- Uses `struct proc_ops` instead of `struct file_operations`
- Supports: read, write, ioctl (unusual), lseek
- Correct place for: debug info, stats, kernel tunables

---

### 3.3 `/sys` — Hardware Topology (sysfs)

**Purpose:** A structured, hierarchical view of every device and driver the
kernel knows about. Introduced in kernel 2.6 to organize what `/proc` had
become — a dumping ground.

```
/sys/
├── class/
│   ├── gpio/           ← GPIO devices
│   ├── leds/           ← LED class devices
│   ├── net/            ← network interfaces
│   └── led_class/led/  ← YOUR /dev/led entry lives here
├── bus/
│   ├── i2c/devices/    ← I2C devices (1-0048, 1-0068...)
│   ├── spi/            ← SPI devices
│   └── platform/       ← platform devices
├── devices/
│   └── platform/
│       ├── 3f200000.gpio   ← BCM2835 GPIO controller
│       └── 3f804000.i2c    ← I2C controller
└── module/
    └── myled/
        └── parameters/ ← module_param() values
```

**Control GPIO without ANY driver:**
```bash
echo 17  > /sys/class/gpio/export           # claim GPIO17
echo out > /sys/class/gpio/gpio17/direction # set as output
echo 1   > /sys/class/gpio/gpio17/value     # LED ON
echo 0   > /sys/class/gpio/gpio17/value     # LED OFF
echo 17  > /sys/class/gpio/unexport         # release
```

---

### 3.4 Side-by-Side Comparison

| | `/dev` | `/proc` | `/sys` |
|---|---|---|---|
| **Filesystem type** | devtmpfs | procfs | sysfs |
| **Primary purpose** | Hardware I/O | Kernel/process info | Device topology |
| **Setup complexity** | High (5+ calls) | Low (1 call) | Automatic (kernel) |
| **Major:minor needed** | Yes | No | No |
| **udev node created** | Yes `/dev/X` | No | No |
| **Struct used** | `file_operations` | `proc_ops` | `kobj_attribute` |
| **ioctl support** | Natural | Possible, unusual | Not typical |
| **mmap / DMA** | Supported | Not supported | Not supported |
| **LED control** | `echo 1 > /dev/led` | `echo 1 > /proc/led` | `echo 1 > /sys/class/gpio/gpio17/value` |
| **See CPU info** | No | `cat /proc/cpuinfo` | `cat /sys/devices/system/cpu/...` |
| **Driver required** | Yes | Optional | No (kernel manages) |
| **Semantically correct for hardware** | ✅ Yes | ❌ No (misuse) | ✅ For attributes |

---

## 4. How the Kernel Routes a System Call

When any user-space program calls `open()`, `read()`, `write()`, or `ioctl()`
on a file under `/dev` or `/proc`, this is the full routing path:

```
User Space
─────────────────────────────────────────────────────────────
  open("/dev/mydev", O_RDWR)
  write(fd, "hello", 5)
  read(fd, buf, 64)
─────────────────────────────────────────────────────────────
  ▼  syscall trap — CPU privilege EL0 → EL1
─────────────────────────────────────────────────────────────
Kernel Space
  ┌──────────────────────────────────────┐
  │  VFS (Virtual File System layer)     │
  │                                      │
  │  1. Look up the inode for the path   │
  │  2. Check permissions (0666 etc.)    │
  │  3. For /dev: look up major number   │
  │     → find registered cdev           │
  │     → get file_operations table      │
  │  4. For /proc: look up proc_entry    │
  │     → get proc_ops table             │
  │  5. Call the appropriate function    │
  │     pointer from the table           │
  └──────────────┬───────────────────────┘
                 │
         ┌───────▼────────┐
         │  Your Driver   │
         │                │
         │  .open()       │
         │  .read()       │
         │  .write()      │
         │  .unlocked_ioctl()
         └───────┬────────┘
                 │
         ┌───────▼────────┐
         │  Hardware      │
         │  gpio_set_value│
         │  ioread32()    │
         │  i2c_transfer()│
         └────────────────┘
─────────────────────────────────────────────────────────────
  ▼  return — CPU privilege EL1 → EL0
─────────────────────────────────────────────────────────────
User Space receives return value
```

---

## 5. Character Device Driver Architecture

### 5.1 Key Data Structures

```c
dev_t dev_num;           // packed major + minor (single integer)
                         // MAJOR(dev_num) → extracts major
                         // MINOR(dev_num) → extracts minor

struct cdev my_cdev;     // kernel's internal char device object
                         // holds a pointer to file_operations
                         // registered via cdev_add()

struct class *my_class;  // logical device class
                         // appears as /sys/class/<name>/
                         // triggers udev to create /dev/<name>

struct device *my_dev;   // the actual device object
                         // calls udev which creates the /dev node
```

### 5.2 Registration Flow

```
module_init()
│
├─ alloc_chrdev_region(&dev_num, 0, 1, "mydev")
│      Asks kernel for a free major number
│      dev_num now holds major:minor
│
├─ cdev_init(&my_cdev, &my_fops)
│      Binds your file_operations table to the cdev object
│
├─ cdev_add(&my_cdev, dev_num, 1)
│      Registers cdev with the kernel
│      Now the kernel can route calls to your functions
│
├─ class_create(THIS_MODULE, "mydev_class")
│      Creates /sys/class/mydev_class/
│      udev watches this directory
│
└─ device_create(my_class, NULL, dev_num, NULL, "mydev")
       Creates the /dev/mydev node automatically
       udev fires KOBJ_ADD event → creates /dev/mydev

module_exit()
│
├─ device_destroy(my_class, dev_num)   → removes /dev/mydev
├─ class_destroy(my_class)            → removes /sys/class entry
├─ cdev_del(&my_cdev)                 → unregisters cdev
└─ unregister_chrdev_region(dev_num, 1) → frees major number
```

### 5.3 The file_operations Table

```c
static const struct file_operations my_fops = {
    .owner          = THIS_MODULE,   // prevents unload while in use
    .open           = my_open,       // called on open()
    .release        = my_release,    // called on close() (last fd)
    .read           = my_read,       // called on read()
    .write          = my_write,      // called on write()
    .unlocked_ioctl = my_ioctl,      // called on ioctl()
    .llseek         = default_llseek,// enables ppos / lseek()
};
```

Each field is a **function pointer**. Setting a field to NULL means that
operation returns `-EINVAL` to user space. You only implement what your
device needs.

---

## 6. /proc Driver Architecture

### 6.1 Key Data Structures

```c
struct proc_dir_entry *proc_entry;
// A single opaque handle returned by proc_create()
// That's all you need — no major, no cdev, no class
```

### 6.2 Registration Flow

```
module_init()
│
└─ proc_create("mydev", 0666, NULL, &my_pops)
       │
       ├─ name:   "mydev"  → creates /proc/mydev
       ├─ mode:   0666     → rw-rw-rw- permissions
       ├─ parent: NULL     → parent is /proc itself
       └─ pops:   &my_pops → your proc_ops table
       
       Returns proc_entry handle (NULL on failure)
       /proc/mydev appears immediately

module_exit()
│
└─ proc_remove(proc_entry)
       Removes /proc/mydev
       Waits for in-progress reads/writes to finish first (safe)
```

### 6.3 The proc_ops Table

```c
// Kernel >= 5.6 uses proc_ops, NOT file_operations
static const struct proc_ops my_pops = {
    .proc_open    = my_open,      // called on open()
    .proc_release = my_release,   // called on close()
    .proc_read    = my_read,      // called on read()
    .proc_write   = my_write,     // called on write()
    .proc_ioctl   = my_ioctl,     // called on ioctl()
    .proc_lseek   = default_llseek,
};
```

Field names use `proc_` prefix instead of bare names. Otherwise the
function signatures (`open`, `read`, `write`) are **identical** between
`file_operations` and `proc_ops`.

---

## 7. Complete Example — Simple Buffer Driver via `/dev`

A kernel mailbox: write any string in, read it back.

```c
// mydev.c
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>

#define DEVICE_NAME  "mydev"
#define CLASS_NAME   "mydev_class"
#define BUFFER_SIZE  1024

static dev_t            dev_num;
static struct cdev      my_cdev;
static struct class    *my_class;
static struct device   *my_device;
static char             kbuffer[BUFFER_SIZE];
static size_t           kbuffer_len = 0;
static DEFINE_MUTEX(mydev_mutex);

static int mydev_open(struct inode *inode, struct file *filp)
{
    pr_info("mydev: opened\n");
    return 0;
}

static int mydev_release(struct inode *inode, struct file *filp)
{
    pr_info("mydev: closed\n");
    return 0;
}

static ssize_t mydev_read(struct file *filp, char __user *buf,
                          size_t count, loff_t *ppos)
{
    ssize_t to_send;
    mutex_lock(&mydev_mutex);
    if (*ppos >= kbuffer_len) { mutex_unlock(&mydev_mutex); return 0; }
    to_send = min(count, kbuffer_len - (size_t)*ppos);
    if (copy_to_user(buf, kbuffer + *ppos, to_send)) {
        mutex_unlock(&mydev_mutex); return -EFAULT;
    }
    *ppos += to_send;
    mutex_unlock(&mydev_mutex);
    return to_send;
}

static ssize_t mydev_write(struct file *filp, const char __user *buf,
                           size_t count, loff_t *ppos)
{
    size_t to_write = min(count, (size_t)(BUFFER_SIZE - 1));
    mutex_lock(&mydev_mutex);
    if (copy_from_user(kbuffer, buf, to_write)) {
        mutex_unlock(&mydev_mutex); return -EFAULT;
    }
    kbuffer[to_write] = '\0';
    kbuffer_len = to_write;
    mutex_unlock(&mydev_mutex);
    pr_info("mydev: stored %zu bytes\n", to_write);
    return to_write;
}

static const struct file_operations mydev_fops = {
    .owner   = THIS_MODULE,
    .open    = mydev_open,
    .release = mydev_release,
    .read    = mydev_read,
    .write   = mydev_write,
};

static int __init mydev_init(void)
{
    int ret;
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) return ret;
    cdev_init(&my_cdev, &mydev_fops);
    my_cdev.owner = THIS_MODULE;
    cdev_add(&my_cdev, dev_num, 1);
    my_class  = class_create(THIS_MODULE, CLASS_NAME);
    my_device = device_create(my_class, NULL, dev_num, NULL, DEVICE_NAME);
    pr_info("mydev: /dev/%s ready (major=%d)\n", DEVICE_NAME, MAJOR(dev_num));
    return 0;
}

static void __exit mydev_exit(void)
{
    device_destroy(my_class, dev_num);
    class_destroy(my_class);
    cdev_del(&my_cdev);
    unregister_chrdev_region(dev_num, 1);
    pr_info("mydev: unloaded\n");
}

module_init(mydev_init);
module_exit(mydev_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ehab");
```

**Usage:**
```bash
sudo insmod mydev.ko
echo "Hello kernel" > /dev/mydev
cat /dev/mydev
# → Hello kernel
sudo rmmod mydev
```

---

## 8. Complete Example — Same Driver via `/proc`

Identical logic, only the registration and struct name change:

```c
// myproc.c
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>

#define PROC_NAME    "myproc"
#define BUFFER_SIZE  1024

static struct proc_dir_entry *proc_entry;
static char   kbuffer[BUFFER_SIZE];
static size_t kbuffer_len = 0;
static DEFINE_MUTEX(myproc_mutex);

// open / release / read / write — IDENTICAL function bodies to /dev version

static const struct proc_ops myproc_ops = {    // ← only name changed
    .proc_open    = myproc_open,
    .proc_release = myproc_release,
    .proc_read    = myproc_read,
    .proc_write   = myproc_write,
    .proc_lseek   = default_llseek,
};

static int __init myproc_init(void)
{
    // ONE line instead of five
    proc_entry = proc_create(PROC_NAME, 0666, NULL, &myproc_ops);
    if (!proc_entry) return -ENOMEM;
    pr_info("myproc: /proc/%s ready\n", PROC_NAME);
    return 0;
}

static void __exit myproc_exit(void)
{
    proc_remove(proc_entry);   // ONE line instead of four
    pr_info("myproc: unloaded\n");
}

module_init(myproc_init);
module_exit(myproc_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ehab");
```

**Usage:**
```bash
sudo insmod myproc.ko
echo "Hello kernel" > /proc/myproc
cat /proc/myproc
# → Hello kernel
sudo rmmod myproc
```

**What's different:**

| | `/dev/mydev` | `/proc/myproc` |
|---|---|---|
| Registration | 5 calls | 1 call |
| Node location | `/dev/mydev` | `/proc/myproc` |
| Struct | `file_operations` | `proc_ops` |
| Field prefix | none | `proc_` |
| Logic functions | identical | identical |

---

## 9. Complete Example — LED Driver via `/dev`

Adds GPIO control on top of the buffer driver pattern from Section 7.
Key additions:

```c
#define LED_GPIO     529     // BCM GPIO17, global number on kernel 6.12

// In init — before cdev registration:
gpio_request(LED_GPIO, "rpi_led");
gpio_direction_output(LED_GPIO, 0);    // output, start OFF

// Central helper — keeps led_state and hardware in sync:
static void led_set(int state) {
    led_state = !!state;
    gpio_set_value(LED_GPIO, led_state);
}

// write() parses "1"/"0"/"on"/"off" then calls led_set()
// read()  returns "LED: ON\n" or "LED: OFF\n"
// ioctl() handles ON / OFF / TOGGLE / GET

// In exit — after device_destroy:
gpio_free(LED_GPIO);
```

```bash
sudo insmod led_dev.ko
echo "1"   | sudo tee /dev/led    # LED ON
echo "on"  | sudo tee /dev/led    # LED ON
echo "0"   | sudo tee /dev/led    # LED OFF
echo "off" | sudo tee /dev/led    # LED OFF
cat /dev/led                       # → LED: OFF
sudo rmmod led_dev
```

## 10. Complete Example — LED Driver via `/proc`

Identical GPIO and led_set() logic as Section 9.
Only the registration and struct change:

```c
// Replace the 5-call /dev registration block with:
proc_entry = proc_create("myled", 0666, NULL, &led_pops);

// Replace file_operations with proc_ops:
static const struct proc_ops led_pops = {
    .proc_open    = led_open,
    .proc_release = led_release,
    .proc_read    = led_read,
    .proc_write   = led_write,
    .proc_ioctl   = led_ioctl,
    .proc_lseek   = default_llseek,
};

// Replace exit cleanup block with:
led_set(0);
proc_remove(proc_entry);
gpio_free(LED_GPIO);
```

```bash
sudo insmod led_proc.ko
echo "1"   | sudo tee /proc/myled   # LED ON
echo "off" | sudo tee /proc/myled   # LED OFF
cat /proc/myled                      # → LED: OFF
sudo rmmod led_proc
```

**Everything else — gpio_request, gpio_direction_output, led_set,
led_read, led_write, led_ioctl bodies — is byte-for-byte identical
between the /dev and /proc versions.**

## 11. Full Under-the-Hood Flow — ASCII Diagrams

### 11.1 `write()` System Call Flow

```
Shell: echo "1" > /dev/led
       (or /proc/myled — same path from here)

─────────────────────────────────────────────────────────────────────
USER SPACE
─────────────────────────────────────────────────────────────────────

  bash                 glibc
  ──────               ─────
  fork() + exec()      
  open("/dev/led")  →  open()   → SVC #0  (ARM64 syscall trap)
  write(fd,"1\n",2) →  write()  → SVC #0

─────────────────────────────────────────────────────────────────────
CPU: privilege EL0 → EL1  (hardware enforced)
─────────────────────────────────────────────────────────────────────

KERNEL SPACE
─────────────────────────────────────────────────────────────────────

  Entry point: el0_svc  (arch/arm64/kernel/entry.S)
       │
       ▼
  sys_write(fd=3, buf=0x7ffeabc0, count=2)
       │
       ▼
  VFS: ksys_write()
       │
       ├─ Look up file struct from fd table
       ├─ Get inode from file struct
       │
       ├─ /dev/led path:
       │     major = 240, minor = 0
       │     → chrdev_open() → find cdev in chrdev_map[]
       │     → get file_operations → call .write()
       │
       └─ /proc/myled path:
             → proc inode lookup → find proc_dir_entry
             → get proc_ops → call .proc_write()
       │
       ▼
  led_write(filp, buf=0x7ffeabc0, count=2, ppos)
       │
       ├─ to_copy = min(2, 15) = 2
       │
       ├─ copy_from_user(kbuf, 0x7ffeabc0, 2)
       │       Safely copies "1\n" from user virtual address
       │       into kernel buffer kbuf
       │       kbuf[2] = '\0'
       │
       ├─ Strip '\n':  kbuf = "1"
       │
       ├─ mutex_lock(&led_mutex)
       │
       ├─ strcmp(kbuf, "1") == 0  →  led_set(1)
       │       │
       │       ├─ led_state = 1
       │       └─ gpio_set_value(529, 1)
       │               │
       │               ▼
       │       BCM2835 GPIO controller MMIO
       │       GPSET0 register at 0x3F20001C
       │       Bit 17 = 1  →  GPIO17 pin = 3.3V
       │                                     │
       │                               330Ω resistor
       │                                     │
       │                              LED anode(+)
       │                              LED cathode(-)
       │                                     │
       │                                    GND
       │                               *** LED ON ***
       │
       ├─ mutex_unlock(&led_mutex)
       │
       └─ return 2   (bytes written)
       │
       ▼
  sys_write returns 2 to user space

─────────────────────────────────────────────────────────────────────
CPU: privilege EL1 → EL0
─────────────────────────────────────────────────────────────────────

USER SPACE
  echo exits, shell shows next prompt
```

---

### 11.2 `read()` System Call Flow

```
Shell: cat /dev/led
       (or cat /proc/myled)

─────────────────────────────────────────────────────────────────────
USER SPACE
─────────────────────────────────────────────────────────────────────

  cat opens /dev/led  →  fd = 3
  calls read(3, buf, 4096)   ← 1st call
  calls read(3, buf, 4096)   ← 2nd call (needed for EOF detection)

─────────────────────────────────────────────────────────────────────
KERNEL SPACE — 1st read() call
─────────────────────────────────────────────────────────────────────

  sys_read() → VFS → led_read(filp, buf, 4096, ppos=0)
       │
       ├─ mutex_lock(&led_mutex)
       ├─ msg = "LED: ON\n"     (led_state = 1)
       ├─ msg_len = 8
       │
       ├─ *ppos(0) < 8  →  proceed
       ├─ to_send = min(4096, 8-0) = 8
       │
       ├─ copy_to_user(buf, msg+0, 8)
       │       Copies "LED: ON\n" into user buffer at buf
       │
       ├─ *ppos += 8   →  *ppos = 8
       ├─ mutex_unlock(&led_mutex)
       └─ return 8

─────────────────────────────────────────────────────────────────────
KERNEL SPACE — 2nd read() call
─────────────────────────────────────────────────────────────────────

  sys_read() → VFS → led_read(filp, buf, 4096, ppos=8)
       │
       ├─ *ppos(8) >= msg_len(8)  →  EOF
       └─ return 0

─────────────────────────────────────────────────────────────────────
USER SPACE
─────────────────────────────────────────────────────────────────────

  cat receives 8 bytes first call, 0 second call
  cat knows EOF → prints "LED: ON" → exits

  Terminal shows:
  LED: ON
```

---

### 11.3 Module Load/Unload Lifecycle

```
sudo insmod led_dev.ko
─────────────────────────────────────────────────────────────────────

  kernel/module.c: load_module()
       │
       ├─ Read ELF sections from led_dev.ko
       ├─ Verify MODULE_LICENSE("GPL") — required for gpio_* symbols
       ├─ Check symbol versions against Module.symvers
       ├─ Allocate kernel memory for the module
       ├─ Relocate code/data sections
       └─ Call module_init() → led_init()
               │
               ├─ gpio_request(529, "rpi_led")
               │       gpiolib marks GPIO529 as owned
               │       /sys/kernel/debug/gpio shows "out lo"
               │
               ├─ gpio_direction_output(529, 0)
               │       Sets BCM GPIO17 as output, initial LOW
               │
               ├─ alloc_chrdev_region(&dev_num, 0, 1, "led")
               │       Kernel assigns major=240 (example)
               │       dev_num = MKDEV(240, 0)
               │
               ├─ cdev_init(&led_cdev, &led_fops)
               │       led_cdev.ops = &led_fops
               │
               ├─ cdev_add(&led_cdev, dev_num, 1)
               │       Registers in kernel's chrdev_map[]
               │       Kernel can now route fd 240:0 → led_fops
               │
               ├─ class_create(THIS_MODULE, "led_class")
               │       Creates /sys/class/led_class/
               │
               └─ device_create(led_class, NULL, dev_num, NULL, "led")
                       Creates /sys/class/led_class/led/dev  (= "240:0")
                       udev daemon sees KOBJ_ADD event
                       udev runs: mknod /dev/led c 240 0
                       /dev/led now exists ← user can open it
─────────────────────────────────────────────────────────────────────

sudo rmmod led_dev
─────────────────────────────────────────────────────────────────────

  kernel checks refcount (nobody has /dev/led open)
  calls module_exit() → led_exit()
       │
       ├─ led_set(0)                     → GPIO17 LOW, LED OFF
       ├─ device_destroy(led_class, dev_num)
       │       udev sees KOBJ_REMOVE → deletes /dev/led
       ├─ class_destroy(led_class)       → removes /sys/class/led_class/
       ├─ cdev_del(&led_cdev)            → unregisters from chrdev_map[]
       ├─ unregister_chrdev_region()     → major 240 is free again
       └─ gpio_free(529)                 → GPIO17 released for others
```

---

## 12. The Makefile Explained

```makefile
obj-m += mydev.o
KDIR  := /lib/modules/$(shell uname -r)/build
PWD   := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
```

```
Line by line:

obj-m += mydev.o
  │
  └─ kbuild variable: "build mydev.c as a loadable module (.ko)"
     obj-y = built into kernel (static)
     obj-m = loadable module
     obj-n = not built

KDIR := /lib/modules/6.12.47+rpt-rpi-v8/build
  │
  ├─ $(shell uname -r) runs uname -r → "6.12.47+rpt-rpi-v8"
  ├─ := immediate assignment (evaluated once)
  └─ Points to kernel headers matching the RUNNING kernel
     (symlink → /usr/src/linux-headers-6.12.47+rpt-rpi-v8)

$(MAKE) -C $(KDIR) M=$(PWD) modules
  │
  ├─ $(MAKE)     re-invoke make (not literal "make" — handles flags)
  ├─ -C $(KDIR)  cd into kernel headers dir FIRST, read THEIR Makefile
  ├─ M=$(PWD)    then come back to YOUR dir to build obj-m targets
  └─ modules     kbuild target: compile all obj-m → .ko files

Flow:
 YOUR Makefile            KERNEL Makefile (KDIR)
 ─────────────     ────►  ──────────────────────
 obj-m = mydev.o          Reads YOUR obj-m list
 calls make -C            Sets -D__KERNEL__ -DMODULE
                          Uses .config from running kernel
                          Cross-checks symbol versions
                          Compiles mydev.c → mydev.o
                          Links → mydev.ko
                   ◄────  Output lands back in YOUR dir
```

---

## 13. GPIO Numbering on Modern RPi Kernels

Kernel 6.1+ changed how the BCM2835 GPIO controller is registered.
It no longer gets base offset 0 — it gets a **dynamic** base (typically 512).

```
Older kernels (< 6.1):          Modern kernels (>= 6.1):
─────────────────────           ────────────────────────
gpio_request(17, ...)  ✅        gpio_request(17, ...) ❌ EPROBE_DEFER
BCM GPIO17 = global 17           BCM GPIO17 = global 529 (512 + 17)

$ cat /sys/kernel/debug/gpio | grep 17
 gpio-517 (GPIO5  )
 gpio-529 (GPIO17 )   ← global number to use in gpio_request()
```

**Find the right number for any BCM pin:**
```bash
BCM=17
cat /sys/kernel/debug/gpio | grep "GPIO${BCM} "
# gpio-529 (GPIO17)   →  use 529 in your driver
```

**Make it a module parameter for safety:**
```c
static int led_gpio = 529;
module_param(led_gpio, int, 0444);
MODULE_PARM_DESC(led_gpio, "Global GPIO number (check /sys/kernel/debug/gpio)");

// Then use led_gpio instead of the #define everywhere
// Override at load time:
// sudo insmod led_dev.ko led_gpio=530
```

---

## 14. Quick Reference Cheatsheet

### Build & Load

```bash
sudo apt install raspberrypi-kernel-headers   # install headers once
make                                           # build .ko
sudo insmod myled.ko                          # load module
lsmod | grep myled                            # confirm loaded
sudo dmesg -w                                 # watch kernel log
sudo rmmod myled                              # unload module
```

### Control & Debug

```bash
# /dev driver
echo "1"   | sudo tee /dev/led
echo "off" | sudo tee /dev/led
cat /dev/led

# /proc driver
echo "1"   | sudo tee /proc/myled
echo "off" | sudo tee /proc/myled
cat /proc/myled

# GPIO debug
cat /sys/kernel/debug/gpio | grep GPIO17      # check ownership & state
cat /proc/devices                             # list major numbers
cat /sys/module/myled/parameters/led_gpio     # read module param

# Find global GPIO number for BCM pin N
cat /sys/kernel/debug/gpio | grep "GPION "
```

### /dev vs /proc vs /sys At a Glance

```
Task                              Use
─────────────────────────         ──────────────────────────────
Control hardware (LED, sensor) →  /dev    (character device driver)
Expose debug stats/info        →  /proc   (proc driver)
Inspect device tree/attrs      →  /sys    (automatic, no driver needed)
Tune GPIO without a driver     →  /sys/class/gpio/export
Check kernel module params     →  /sys/module/<name>/parameters/
Check who owns a GPIO pin      →  /sys/kernel/debug/gpio
List all registered drivers    →  /sys/bus/<bus>/drivers/
```

### Key Kernel Functions

| Function | Purpose |
|---|---|
| `gpio_request(gpio, label)` | Claim a GPIO pin |
| `gpio_direction_output(gpio, val)` | Set pin as output |
| `gpio_set_value(gpio, val)` | Drive pin HIGH or LOW |
| `gpio_free(gpio)` | Release a GPIO pin |
| `copy_from_user(dst, src, n)` | Safe user→kernel copy |
| `copy_to_user(dst, src, n)` | Safe kernel→user copy |
| `proc_create(name, mode, parent, ops)` | Create `/proc/<name>` |
| `proc_remove(entry)` | Remove `/proc/<name>` |
| `alloc_chrdev_region(&dev, 0, 1, name)` | Get major:minor |
| `cdev_init(&cdev, &fops)` | Bind ops to cdev |
| `cdev_add(&cdev, dev, 1)` | Register with kernel |
| `class_create(owner, name)` | Create `/sys/class/<name>` |
| `device_create(cls, NULL, dev, NULL, name)` | Create `/dev/<name>` |
| `mutex_lock / mutex_unlock` | Protect shared data |
| `pr_info / pr_err / pr_warn` | Kernel log output |

---

*Document covers: kernel 6.12.47+rpt-rpi-v8 · RPi 3B+ (BCM2835/AArch64) · Raspbian Bookworm*