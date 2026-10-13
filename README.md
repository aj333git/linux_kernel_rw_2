# rwlock_demo

## Linux Kernel `rwlock_t` Reader/Writer Demonstration

A small Linux kernel module demonstrating how `rwlock_t` protects a shared kernel linked list when multiple kernel threads perform concurrent reads while another kernel thread modifies the list.

The program is designed for **Linux kernel 5.15** and uses:

* `rwlock_t`
* Kernel threads (`kthread`)
* Kernel linked lists (`struct list_head`)
* Dynamic kernel memory allocation (`kmalloc` / `kfree`)
* Read-side locking with `read_lock()`
* Write-side locking with `write_lock()`
* Module initialization and cleanup

The primary objective is to understand the **reader/writer synchronization model inside the Linux kernel** through an executable experiment rather than only studying the API theoretically.

---

# 1. What This Program Demonstrates

The module maintains a shared linked list:

```text
                    Shared Kernel List
                 +----------------------+
                 | 10 -> 20 -> 30      |
                 +----------+-----------+
                            |
              +-------------+-------------+
              |                           |
              v                           v
        Reader Thread 1             Reader Thread 2
        read_lock()                 read_lock()
              |                           |
              +-------------+-------------+
                            |
                     Concurrent Reads
                            |
                            v
                       list traversal


                       Writer Thread
                            |
                            v
                     write_lock()
                            |
                            v
                    Add new list item
                            |
                            v
                     write_unlock()
```

The fundamental rule is:

```text
                 rwlock_t

        +-----------------------+
        |                       |
        |   Multiple Readers    |
        |        allowed        |
        |                       |
        +-----------------------+

                    OR

        +-----------------------+
        |                       |
        |    Single Writer      |
        |       allowed         |
        |                       |
        +-----------------------+
```

Readers can execute concurrently with other readers.

A writer requires exclusive access.

Therefore:

```text
Reader + Reader     -> allowed
Reader + Writer     -> blocked
Writer + Reader     -> blocked
Writer + Writer     -> blocked
```

---

# 2. Program Architecture

The module creates three kernel threads:

```text
                 rwlock_demo_init()
                         |
          +--------------+--------------+
          |              |              |
          v              v              v
      reader1         reader2         writer
          |              |              |
          v              v              v
    reader_thread() reader_thread() writer_thread()
          |              |              |
          +--------------+--------------+
                         |
                         v
                  Shared my_list
                         |
                         v
                    my_rwlock
```

The shared objects are:

```c
static LIST_HEAD(my_list);

static DEFINE_RWLOCK(my_rwlock);
```

`my_list` is the shared resource.

`my_rwlock` protects access to that resource.

---

# 3. Data Structure

Each list element is represented by:

```c
struct my_item {
    int value;
    struct list_head list;
};
```

The structure contains:

```text
+---------------------------+
| struct my_item            |
+---------------------------+
| value                     |
+---------------------------+
| struct list_head list     |
+---------------------------+
```

The Linux kernel's `list_head` API is used instead of implementing a linked list manually.

The list is initialized using:

```c
static LIST_HEAD(my_list);
```

---

# 4. The Reader/Writer Lock

The synchronization primitive is:

```c
static DEFINE_RWLOCK(my_rwlock);
```

Conceptually:

```text
                 my_rwlock
                     |
          +----------+----------+
          |                     |
          v                     v
     read_lock()          write_lock()
          |                     |
          v                     v
    Shared access         Exclusive access
```

The important distinction is that this is not a mutex.

The program uses the `rwlock_t` family of APIs:

```c
read_lock(&my_rwlock);
read_unlock(&my_rwlock);

write_lock(&my_rwlock);
write_unlock(&my_rwlock);
```

---

# 5. Reader Critical Section

The reader thread obtains the read lock:

```c
read_lock(&my_rwlock);

list_for_each_entry(item, &my_list, list) {
    count++;
    sum += item->value;
}

read_unlock(&my_rwlock);
```

The list traversal is therefore protected.

Multiple reader threads can hold the read lock simultaneously.

For example:

```text
Time ---------------------------------------------------->

Reader 1:
        [====== READ LOCK ======]
        [   traverse list       ]

Reader 2:
        [====== READ LOCK ======]
        [   traverse list       ]

Writer:
                 [ WAITING ]
                 [ WAITING ]
                 [ WRITE LOCK ]
```

This is the central advantage of a reader/writer synchronization model.

If the shared data is read frequently and modified less frequently, allowing concurrent readers can be useful.

---

# 6. Writer Critical Section

The writer modifies the shared list through:

```c
write_lock(&my_rwlock);

list_add_tail(&item->list, &my_list);

write_unlock(&my_rwlock);
```

The writer requires exclusive access.

Conceptually:

```text
Reader 1  ----\
Reader 2  -----+----> write_lock()
Reader 3  ----/             |
                            v
                       EXCLUSIVE
                            |
                            v
                       Modify list
                            |
                            v
                       write_unlock()
```

While the writer owns the lock, readers cannot enter their protected region.

---

# 7. Why the List Must Be Protected

Consider this situation without synchronization:

```text
Reader                         Writer

traverse list
     |
     +----------------------+
                            |
                       add new node
                            |
                       modify links
                            |
                       return
     |
continue traversal
```

The reader and writer could observe the linked-list state at an unsafe time.

Kernel linked-list manipulation involves pointer relationships.

For example:

```text
Before:

A <-> B <-> C


Writer adds X:

A <-> B <-> X <-> C
```

The reader must not race with modifications to these links.

The lock establishes the required synchronization boundary.

---

# 8. `add_item()` Function

The helper function dynamically allocates a list node:

```c
item = kmalloc(sizeof(*item), GFP_KERNEL);
```

The allocated object is initialized:

```c
item->value = value;
```

Then the shared list is modified under the write lock:

```c
write_lock(&my_rwlock);

list_add_tail(&item->list, &my_list);

write_unlock(&my_rwlock);
```

The important design principle is:

```text
Allocation
    |
    v
Initialize object
    |
    v
Acquire write lock
    |
    v
Modify shared list
    |
    v
Release write lock
```

The `kmalloc()` operation itself does not need to be inside the list lock because the object is not yet visible through `my_list`.

---

# 9. Reader Thread

Each reader repeatedly:

1. Checks whether it should stop.
2. Acquires the read lock.
3. Traverses the shared list.
4. Calculates the number of nodes.
5. Calculates the sum.
6. Releases the read lock.
7. Sleeps for one second.

Conceptually:

```text
while (!kthread_should_stop())
        |
        v
   read_lock()
        |
        v
   traverse list
        |
        v
   calculate count/sum
        |
        v
   read_unlock()
        |
        v
    msleep(1000)
        |
        +------------> repeat
```

The `msleep(1000)` occurs **outside** the locked region.

This is important.

---

# 10. Writer Thread

The writer starts with:

```c
int value = 100;
```

It repeatedly calls:

```c
add_item(value++);
```

and then sleeps:

```c
msleep(2000);
```

Therefore, the list gradually grows:

```text
Initial:

10 -> 20 -> 30


Writer:

100


Result:

10 -> 20 -> 30 -> 100


Writer:

101


Result:

10 -> 20 -> 30 -> 100 -> 101
```

Readers observe the changing list.

---

# 11. Expected Output

After loading the module, kernel messages will look approximately like:

```text
rwlock_demo: loading

rwlock_demo: writer added 10
rwlock_demo: writer added 20
rwlock_demo: writer added 30

rwlock_demo: reader-1 read count=3 sum=60
rwlock_demo: reader-2 read count=3 sum=60

rwlock_demo: writer added 100

rwlock_demo: reader-1 read count=4 sum=160
rwlock_demo: reader-2 read count=4 sum=160

rwlock_demo: writer added 101

rwlock_demo: reader-1 read count=5 sum=261
rwlock_demo: reader-2 read count=5 sum=261
```

The exact ordering of messages can vary because the kernel scheduler determines when the threads execute.

Do not assume that the log ordering represents a fixed scheduling sequence.

---

# 12. Build Requirements

This example is intended for a Linux system with kernel development headers installed.

Check the running kernel:

```bash
uname -r
```

For example:

```text
5.15.0-139-generic
```

Check the kernel build directory:

```bash
ls -ld /lib/modules/$(uname -r)/build
```

If the directory exists, the external module can normally be built against the running kernel.

---

# 13. Makefile

Create a file named:

```text
Makefile
```

with:

```makefile
obj-m += rwlock_demo.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean
```

Directory layout:

```text
rwlock_demo/
|
+-- rwlock_demo.c
|
+-- Makefile
```

---

# 14. Compile the Module

From the project directory:

```bash
make
```

The kernel build system should produce:

```text
rwlock_demo.ko
```

Check:

```bash
ls -l rwlock_demo.ko
```

You can also inspect the module:

```bash
modinfo rwlock_demo.ko
```

---

# 15. Load the Module

Insert the module:

```bash
sudo insmod rwlock_demo.ko
```

Check whether it is loaded:

```bash
lsmod | grep rwlock_demo
```

---

# 16. Observe Kernel Messages

Use:

```bash
sudo dmesg | tail -30
```

For live monitoring:

```bash
sudo dmesg -w
```

Then observe:

```text
reader-1
reader-2
writer
```

The readers run approximately every second.

The writer adds an item approximately every two seconds.

---

# 17. Unload the Module

Stop the module with:

```bash
sudo rmmod rwlock_demo
```

Then check:

```bash
sudo dmesg | tail -20
```

Expected final messages:

```text
rwlock_demo: unloading
rwlock_demo: unloaded
```

---

# 18. Complete Command Sequence

```bash
make

sudo insmod rwlock_demo.ko

lsmod | grep rwlock_demo

sudo dmesg | tail -30

sudo dmesg -w
```

Press:

```text
Ctrl+C
```

to stop `dmesg -w`.

Then unload:

```bash
sudo rmmod rwlock_demo
```

Verify:

```bash
sudo dmesg | tail -20
```

Clean build files:

```bash
make clean
```

---

# 19. Important Cleanup Design

Module unloading must be carefully ordered.

The module first stops the kernel threads:

```c
if (writer)
    kthread_stop(writer);

if (reader1)
    kthread_stop(reader1);

if (reader2)
    kthread_stop(reader2);
```

Only after the threads have stopped does it free the linked-list objects:

```c
free_list();
```

The ordering is critical.

```text
             Module exit
                  |
                  v
          Stop writer thread
                  |
                  v
          Stop reader 1
                  |
                  v
          Stop reader 2
                  |
                  v
          Free list nodes
                  |
                  v
             Module gone
```

If the list were freed while a reader could still access it, the module could suffer from a use-after-free condition.

---

# 20. `kthread_stop()` and `kthread_should_stop()`

The worker threads use:

```c
while (!kthread_should_stop())
```

The module exit path calls:

```c
kthread_stop(thread);
```

The conceptual relationship is:

```text
module_exit()
     |
     v
kthread_stop()
     |
     v
stop request
     |
     v
kthread_should_stop()
     |
     v
loop terminates
     |
     v
thread returns
```

This gives the module a controlled shutdown mechanism.

---

# 21. Why `msleep()` Is Outside the Lock

This is an important kernel synchronization rule.

The reader does:

```c
read_lock(&my_rwlock);

/* short critical section */

read_unlock(&my_rwlock);

msleep(1000);
```

Not:

```c
read_lock(&my_rwlock);

msleep(1000);    /* wrong design */

read_unlock(&my_rwlock);
```

`rwlock_t` is a spinlock-based reader/writer primitive.

The protected critical section should therefore be short.

The same principle applies to the writer:

```c
write_lock(&my_rwlock);

/* modify shared list */

write_unlock(&my_rwlock);

msleep(2000);
```

Sleeping while holding the lock would unnecessarily block other participants and is incompatible with the semantics of spinlock-based locking.

---

# 22. `rwlock_t` vs Mutex

This example is useful for understanding the distinction.

| Feature             | `rwlock_t`                                     | `mutex`                                  |
| ------------------- | ---------------------------------------------- | ---------------------------------------- |
| Multiple readers    | Yes                                            | No                                       |
| Multiple writers    | No                                             | No                                       |
| Reader + writer     | No                                             | No                                       |
| Sleeping while held | No                                             | Yes, subject to mutex rules              |
| Primitive family    | Spinlock-based                                 | Sleeping lock                            |
| Typical use         | Short critical sections with read-heavy access | General process-context mutual exclusion |

Conceptually:

```text
rwlock_t

Readers:
R1 + R2 + R3
     |
     v
  concurrent


Writer:
W
|
v
exclusive
```

Whereas a mutex provides:

```text
Thread 1 ---> [LOCK] ---> critical section ---> [UNLOCK]
                                      ^
                                      |
Thread 2 -----------------------------+
```

---

# 23. Why Use `rwlock_t` Here?

The linked list is intentionally designed to demonstrate a workload where:

```text
Reads  >> Writes
```

Two reader threads frequently inspect the list.

The writer modifies the list less frequently.

Therefore, allowing readers to share the lock illustrates the motivation behind reader/writer synchronization.

The example is educational rather than a claim that `rwlock_t` is always the best solution.

Real kernel code should choose synchronization primitives based on:

* execution context
* critical-section length
* read/write ratio
* contention
* preemption requirements
* lock ordering
* possible sleeping
* performance characteristics
* kernel subsystem conventions

---

# 24. Important Race-Condition Concept

The lock protects the relationship between the reader and the shared list.

Without synchronization:

```text
Reader
  |
  +---- traverse list
  |
  +--------------------+
                       |
                       v
                     Writer
                       |
                       +---- modify list
```

With the lock:

```text
Reader
  |
  v
read_lock()
  |
  v
safe traversal
  |
  v
read_unlock()


Writer
  |
  v
write_lock()
  |
  v
exclusive modification
  |
  v
write_unlock()
```

The lock establishes mutual exclusion between readers and writers while still allowing reader-reader concurrency.

---

# 25. Error Handling

The module checks whether each kernel thread was successfully created.

For example:

```c
reader1 = kthread_run(reader_thread,
                      "reader-1",
                      "rw-reader-1");

if (IS_ERR(reader1)) {
    pr_err("rwlock_demo: failed to start reader1\n");
    return PTR_ERR(reader1);
}
```

This is important because `kthread_run()` can return an error pointer.

The standard kernel pattern is:

```c
IS_ERR(ptr)
PTR_ERR(ptr)
```

This should not be confused with:

```c
ptr == NULL
```

Kernel APIs frequently use encoded error pointers.

---

# 26. Failure During Initialization

The initialization sequence is:

```text
Start reader1
      |
      +-- failure --> abort

      |
      v
Start reader2
      |
      +-- failure --> stop reader1
      |
      v
Start writer
      |
      +-- failure --> stop reader1
      |               stop reader2
      |
      v
Module ready
```

This is important for understanding resource cleanup during partial initialization.

A kernel module should not assume that all initialization operations succeed.

---

# 27. List Cleanup

The cleanup function uses:

```c
list_for_each_entry_safe(item, tmp, &my_list, list)
```

The `_safe` variant is important because the current node is removed during traversal.

The sequence is:

```c
list_del(&item->list);
kfree(item);
```

Conceptually:

```text
List:

A -> B -> C -> D

       |
       v
 remove B
       |
       v
 free B

Remaining:

A -> C -> D
```

Using the appropriate safe traversal primitive is essential when modifying the list during iteration.

---

# 28. Debugging

If `insmod` reports:

```text
File exists
```

first check whether the module is already loaded:

```bash
lsmod | grep rwlock_demo
```

If it is loaded:

```bash
sudo rmmod rwlock_demo
```

Then load again:

```bash
sudo insmod rwlock_demo.ko
```

Check kernel messages:

```bash
sudo dmesg | tail -50
```

---

# 29. Check Module Information

Use:

```bash
modinfo ./rwlock_demo.ko
```

This displays information such as:

```text
filename
license
author
description
vermagic
```

The `vermagic` information is useful when diagnosing kernel/module compatibility issues.

---

# 30. Kernel Version Compatibility

Check:

```bash
uname -r
```

Build against the running kernel:

```bash
make
```

The Makefile uses:

```makefile
KDIR := /lib/modules/$(shell uname -r)/build
```

Therefore:

```text
Running Kernel
      |
      v
uname -r
      |
      v
/lib/modules/<kernel-version>/build
      |
      v
External module build
```

For this particular teaching example, the target environment is Linux 5.15.

Kernel APIs can change between kernel versions, so code that compiles on 5.15 should

