# Simple Read/Write Proc Driver

## Flow

```text
User Space                         Kernel Space (/proc subsystem)
───────────────────────────────────────────────────────────────────

echo "hi" > /proc/myproc
  │
  │  sys_write(fd, "hi\n", 3)
  │──────────────────────────► VFS
  │                               │ looks up proc_entry → myproc_ops
  │                               ▼
  │                          myproc_write()
  │                            copy_from_user("hi\n")
  │                            kbuffer = "hi\n", kbuffer_len = 3
  │◄────────────────────────── returns 3

cat /proc/myproc
  │
  │  sys_read(fd, buf, 4096)   ← 1st call
  │──────────────────────────► VFS → myproc_read()
  │                            *ppos=0, bytes_to_send=3
  │                            copy_to_user("hi\n")
  │◄────────────────────────── returns 3, *ppos=3

  │  sys_read(fd, buf, 4096)   ← 2nd call
  │──────────────────────────► VFS → myproc_read()
  │                            *ppos=3 >= kbuffer_len=3
  │◄────────────────────────── returns 0  ← EOF, cat stops

  prints: hi
```

---

## Build

```sh
make
```

---

## Load Module

```sh
sudo insmod myproc.ko
```

---

## Read & Write

```sh
# Write
echo "Ehab" > /proc/myproc
# Read
cat /proc/myproc
# ouput: Ehab
```

---

## Remove mdoule

```sh
sudo rmmod myproc
```

---

## Clean

```sh
make clean
```

---

## Monitor

```sh
sudo dmesg -w
```
```log
[ 4658.637799] myproc: loading out-of-tree module taints kernel.
[ 4658.640284] myproc: loaded — /proc/myproc is ready
[ 4694.157471] myproc: opened(total opens: 1)
[ 4694.157619] myproc: closed
[ 4703.041698] myproc: opened(total opens: 2)
[ 4703.041839] myproc: stored 5 bytes => "Ehab
               "
[ 4703.041876] myproc: closed
[ 4705.213074] myproc: opened(total opens: 3)
[ 4705.213180] myproc: read 5 bytes(offset now 5)
[ 4705.213741] myproc: closed
[ 5115.956378] myproc: opened(total opens: 4)
[ 5115.956506] myproc: read 5 bytes(offset now 5)
[ 5115.956793] myproc: closed
[ 5150.432574] myproc: unloaded — /proc/myproc removed
```