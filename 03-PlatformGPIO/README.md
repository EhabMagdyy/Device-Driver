# Raspberry Pi GPIO Platform Driver (Legacy Platform Device Model)

A minimal Linux kernel module demonstrating the legacy platform device/driver model for direct BCM2835 GPIO control on Raspberry Pi.

## Overview

This project demonstrates the **legacy Linux platform device model** without Device Tree:

| Component | File | Purpose |
|-----------|------|---------|
| **Platform Device** | `gpio_device.c` | Describes hardware resources |
| **Platform Driver** | `gpio_driver.c` | Controls hardware via `probe()` |
| **Register Access** | `ioremap()` / `writel()` | Direct BCM GPIO memory-mapped I/O |

> **No Device Tree is used.** Device and driver are matched by name string (`"bcm-gpio"`).

---

## Architecture

```
+-----------------------------------------+
|           Linux Kernel                  |
|                                         |
|    +-------------+   +-------------+    |
|    |  platform   |   |  platform   |    |
|    |   device    |   |   driver    |    |
|    | gpio_device |   | gpio_driver |    |
|    |    .c       |   |    .c       |    |
|    +------+------+   +------+------+    |
|           |                 |           |
|           +--------+--------+           |
|                    |                    |
|              Match by name              |
|              "bcm-gpio"                 |
|                    |                    |
|                    v                    |
|                 probe()                 |
|                    |                    |
|         +----------+----------+         |
|         |                     |         |
|         v                     v         |
|    Get resource          ioremap()      |
|    0x3F200000        (virtual addr)     |
|         |                     |         |
|         +----------+----------+         |
|                    |                    |
|                    v                    |
|         Configure GPIO17 (GPFSEL1)      |
|                    |                    |
|                    v                    |
|            Set GPIO17 HIGH (GPSET0)     |
|                    |                    |
|                    v                    |
|              GPIO 17 = 3.3V             |
+-----------------------------------------+
```

---

## Project Structure

```
03-PlatformGPIO/
├── gpio_device.c    # Platform device registration (hardware description)
├── gpio_driver.c    # Platform driver implementation (hardware control)
└── Makefile         # Build rules for out-of-tree kernel modules
```

---

## How It Works

### 1. Platform Device (`gpio_device.c`)

Registers a fake hardware description with the kernel:

```c
struct platform_device {
    .name = "bcm-gpio",                    // Must match driver name
    .resource = {
        {
            .start = 0x3F200000,           // BCM GPIO base (Pi 3)
            .end   = 0x3F2000B3,           // GPIO register space
            .flags = IORESOURCE_MEM,
        }
    }
};
```

**Key call:** `platform_device_register()`

### 2. Platform Driver (`gpio_driver.c`)

Registers itself and waits for a matching device:

```c
struct platform_driver {
    .driver = {
        .name = "bcm-gpio",                // Must match device name
    },
    .probe  = gpio_probe,                  // Called on match
    .remove = gpio_remove,
};
```

**Key call:** `platform_driver_register()`

### 3. Probe Flow

When names match, `probe()` executes:

| Step | Action | Register |
|------|--------|----------|
| 1 | Get memory resource | `0x3F200000` |
| 2 | `ioremap()` physical to virtual | — |
| 3 | Configure GPIO17 as output | `GPFSEL1` |
| 4 | Set GPIO17 HIGH | `GPSET0` |

### 4. GPIO Register Map (BCM2835)

```
0x3F200000  +- GPFSEL0   (GPIO 0-9 function select)
            +- GPFSEL1   (GPIO 10-19 function select) <- GPIO17 lives here
            +- GPFSEL2   (GPIO 20-29 function select)
            +- (reserved)
            +- GPSET0    (GPIO 0-31 set)
            +- GPSET1    (GPIO 32-53 set)
            +- (reserved)
            +- GPCLR0    (GPIO 0-31 clear)
            +- GPCLR1    (GPIO 32-53 clear)
```

**GPIO17 configuration (GPFSEL1, bits 21-23):**
- `001` = Output
- `000` = Input

---

## Build Instructions

### Prerequisites

```bash
sudo apt install raspberrypi-kernel-headers
```

### Compile

```bash
make
```

**Output:**
- `gpio_device.ko` — Platform device module
- `gpio_driver.ko` — Platform driver module

---

## Usage

### Load Driver First

```bash
sudo insmod gpio_driver.ko
```

> At this point: driver exists, no device registered yet, `probe()` **not** called.

### Then Load Device

```bash
sudo insmod gpio_device.ko
```

**Execution flow:**
1. `platform_device_register()` -> kernel searches for matching driver
2. Finds `"bcm-gpio"` driver -> calls `probe()`
3. `probe()` maps GPIO registers and sets GPIO17 HIGH

### Verify

```bash
dmesg | tail
```

**Expected output:**
```
[  ...  ] Registering BCM GPIO platform device
[  ...  ] BCM GPIO probe called
[  ...  ] GPIO mapped at <virtual_address>
[  ...  ] GPIO17 ON
```

### Unload (Reverse Order)

```bash
sudo rmmod gpio_device   # Remove device first
sudo rmmod gpio_driver   # Then remove driver
```

---

## Debugging

| Command | Purpose |
|---------|---------|
| `sudo dmesg -w` | Watch kernel messages in real-time |
| `ls /sys/bus/platform/devices` | List registered platform devices |
| `ls /sys/bus/platform/drivers` | List registered platform drivers |
| `cat /proc/iomem` | Verify memory resource registration |

---

## Important Notes

### This Example Bypasses the Linux GPIO Subsystem

**Typical production stack:**
```
Application
    |
GPIO Subsystem (gpiolib)
    |
bcm2835-gpio driver
    |
Hardware
```

**This example (educational only):**
```
Your driver
    |
ioremap() -> direct register access
    |
BCM GPIO registers
    |
Hardware (Pin 11 = GPIO17)
```

### Warnings

- **Educational use only** — not suitable for production
- No concurrency protection (no locking)
- No error handling for overlapping register access
- Hardcoded physical address (`0x3F200000` for Pi 3; use `0xFE200000` for Pi 4)
- Bypasses kernel GPIO abstractions — other drivers may conflict

---

## License

GPL v2 (required for kernel modules)

---

## See Also

- [Linux Device Drivers, 3rd Ed.](https://lwn.net/Kernel/LDD3/) — Chapter 12: PCI Drivers (platform driver concepts)
- [BCM2835 ARM Peripherals Datasheet](https://www.raspberrypi.org/documentation/hardware/raspberrypi/bcm2835/BCM2835-ARM-Peripherals.pdf) — Section 6: GPIO
- `Documentation/driver-api/driver-model/platform.rst` in kernel source