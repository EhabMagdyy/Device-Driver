
# This is a kbuild variable, not a normal Makefile variable. The kernel's build system reads this to decide what to build and how.
# obj-y: Built into the kernel (built-in, static)
# obj-m: Built as a loadable module (.ko)
# it looks for myproc.c in the same dir and compiles it to myproc.o, then links/wraps it into myproc.ko

# If your driver had multiple .c files, e.g. myproc.c and helper.c:
# makefileobj-m += myproc.o
# myproc-objs := myproc.o helper.o

obj-m += myproc.o

# Your module must be compiled against the exact same kernel source/config that's currently running.
# If KDIR pointed to a different kernel version, insmod would refuse to load your .ko

# := → immediate/simple assignment — evaluated once, right now
# = → recursive/lazy assignment — re-evaluated every time it's used
KDIR := /lib/modules/$(shell uname -r)/build

# This tells kbuild where your module's source code is, since kbuild itself runs from the kernel headers directory
PWD  := $(shell pwd)

# This is the default target — runs when you type plain make.
all:
# $(MAKE) -C /lib/modules/6.1.21-v8+/build M=/home/ehab/drivers/myproc modules
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean