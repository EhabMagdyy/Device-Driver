# RPi 3B+ LED Driver via `/proc/myled` — Build & Test Walkthrough

## 1. Project Layout

```
02-LEDControlProc/
├── Makefile
└── myled.c
```

---

## 2. Why `LED_GPIO 529` and not `17`?

Modern RPi kernels (6.1+) give the BCM2835 GPIO controller a **dynamic
base offset** instead of starting at 0.

```
$ cat /sys/kernel/debug/gpio | grep 17
 gpio-517 (GPIO5 )
 gpio-529 (GPIO17)
```

```
 BCM pin name          Kernel "global" GPIO number
 ─────────────         ────────────────────────────
   GPIO17     ──────►    512 + 17 = 529   ◄── used in gpio_request()
```

`#define LED_GPIO 529` maps to **physical pin 11** on this kernel build.

> Not guaranteed forever — the 512 offset depends on driver probe order
> at boot. Re-check with the command above after any kernel update.

---

## 3. Build

```
$ make
make -C /lib/modules/6.12.47+rpt-rpi-v8/build M=.../02-LEDControlProc modules

  CC [M]  myled.o
  MODPOST Module.symvers
  CC [M]  myled.mod.o
  CC [M]  .module-common.o
  LD [M]  myled.ko
```

```
┌─────────────┐    make -C KDIR M=PWD    ┌────────────────────────┐
│  myled.c    │ ───────────────────────► │  Kernel build system   │
│  Makefile   │                          │  (headers, .config,    │
└─────────────┘ ◄─────────────────────── │   compiler flags)      │
       │             myled.ko            └────────────────────────┘
       ▼
   myled.ko   (ABI-matched to the running 6.12.47+rpt-rpi-v8 kernel)
```

---

## 4. Load the Module

```
$ sudo insmod myled.ko
```

```
insmod myled.ko
   │
   ▼
myled_init()
   ├─ gpio_request(529, "rpi_myled")    ──► claims GPIO17
   ├─ gpio_direction_output(529, 0)     ──► LED OFF, pin set as output
   └─ proc_create("myled", 0666, ...)   ──► creates /proc/myled
```

```
$ dmesg | tail -1
myled: loaded — GPIO17 → /proc/myled(LED is OFF)
```

---

## 5. Verify the Driver Is Live

```
$ ls -l /proc/myled
-rw-rw-rw- 1 root root 0 ... /proc/myled

$ ls /dev/myled
ls: cannot access '/dev/myled': No such file or directory
                                  ↑ expected — /proc only, no /dev node

$ cat /sys/kernel/debug/gpio | grep 17
 gpio-529 (GPIO17 ) out lo   ← "out"=output, "lo"=LOW (LED off)
```

---

## 6. Control the LED

```
# Turn ON
$ echo "1"  | sudo tee /proc/myled
$ echo "on" | sudo tee /proc/myled

# Turn OFF
$ echo "0"   | sudo tee /proc/myled
$ echo "off" | sudo tee /proc/myled

# Read current state
$ cat /proc/myled
LED: ON
```

```
echo "1" > /proc/myled
   │  write(fd, "1\n", 2)
   ▼
led_write()
   ├─ copy_from_user()  → kbuf = "1"
   ├─ strip trailing '\n'
   ├─ strcmp(kbuf,"1")==0
   └─ led_set(1)
        ├─ led_state = 1
        └─ gpio_set_value(529, 1) ──► GPIO17 = 3.3V ──► LED ON
```

```
cat /proc/myled
   │  read(fd, buf, N)        ← 1st call
   ▼
led_read()
   ├─ msg = "LED: ON\n"  (msg_len = 8)
   ├─ *offset(0) < 8 → copy_to_user("LED: ON\n")
   └─ *offset = 8 → returns 8

   │  read(fd, buf, N)        ← 2nd call
   ▼
led_read()
   └─ *offset(8) >= 8 → return 0 (EOF) → cat stops

prints: LED: ON
```

---

## 7. Watch the Kernel Log Live

```
$ sudo dmesg -w
myled: opened
myled: LED is now ON
myled: closed
```

---

## 8. Unload

```
$ sudo rmmod myled

$ dmesg | tail -2
myled: LED is now OFF
myled: unloaded, GPIO17 released

$ ls /proc/myled
ls: cannot access '/proc/myled': No such file or directory
```

```
rmmod myled
   │
   ▼
myled_exit()
   ├─ led_set(0)        ──► LED forced OFF (safe shutdown)
   ├─ proc_remove(...)  ──► /proc/myled removed
   └─ gpio_free(529)    ──► GPIO17 released for other drivers
```

---

## 9. Quick Reference

| Step              | Command                                 | Result                       |
|-------------------|-----------------------------------------|------------------------------|
| Find global GPIO# | `cat /sys/kernel/debug/gpio \| grep 17` | `gpio-529 (GPIO17)`          |
| Build             | `make`                                  | produces `myled.ko`          |
| Load              | `sudo insmod myled.ko`                  | `/proc/myled` appears        |
| Turn ON           | `echo "1" \| sudo tee /proc/myled`      | LED lights up                |
| Turn OFF          | `echo "0" \| sudo tee /proc/myled`      | LED turns off                |
| Check status      | `cat /proc/myled`                       | `LED: ON` / `LED: OFF`       |
| Live logs         | `sudo dmesg -w`                         | driver messages in real time |
| Unload            | `sudo rmmod myled`                      | LED off, `/proc/myled` gone  |