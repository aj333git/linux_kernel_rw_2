// rwlock_demo.c
//
// Linux 5.15 example:
// Demonstrates rwlock_t protecting a shared linked list.
//
// Readers:
//   - Multiple readers can enter simultaneously.
//
// Writer:
//   - Writer has exclusive access.
//
// IMPORTANT:
//   - This is a teaching example.
//   - We use kernel threads so the behavior is easy to observe.

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/rwlock.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("example");
MODULE_DESCRIPTION("rwlock_t reader/writer spinlock demonstration");

struct my_item {
	int value;
	struct list_head list;
};

static LIST_HEAD(my_list);

/*
 * Reader/writer spinlock.
 *
 * Readers:
 *     read_lock(&my_rwlock);
 *
 * Writer:
 *     write_lock(&my_rwlock);
 */
static DEFINE_RWLOCK(my_rwlock);

static struct task_struct *reader1;
static struct task_struct *reader2;
static struct task_struct *writer;


static void add_item(int value)
{
	struct my_item *item;

	item = kmalloc(sizeof(*item), GFP_KERNEL);
	if (!item)
		return;

	item->value = value;

	/*
	 * Exclusive section.
	 */
	write_lock(&my_rwlock);

	list_add_tail(&item->list, &my_list);

	pr_info("rwlock_demo: writer added %d\n", value);

	write_unlock(&my_rwlock);
}


static int reader_thread(void *data)
{
	const char *name = data;

	while (!kthread_should_stop()) {
		struct my_item *item;
		int count = 0;
		int sum = 0;

		/*
		 * Shared/read lock.
		 *
		 * Multiple readers can hold this lock
		 * simultaneously.
		 */
		read_lock(&my_rwlock);

		list_for_each_entry(item, &my_list, list) {
			count++;
			sum += item->value;
		}

		pr_info("rwlock_demo: %s read count=%d sum=%d\n",
			name, count, sum);

		read_unlock(&my_rwlock);

		msleep(1000);
	}

	return 0;
}


static int writer_thread(void *data)
{
	int value = 100;

	while (!kthread_should_stop()) {
		add_item(value++);
		msleep(2000);
	}

	return 0;
}


static void free_list(void)
{
	struct my_item *item;
	struct my_item *tmp;

	/*
	 * Nobody should be using the list when this function
	 * is called because module exit stops the threads first.
	 */
	write_lock(&my_rwlock);

	list_for_each_entry_safe(item, tmp, &my_list, list) {
		list_del(&item->list);
		kfree(item);
	}

	write_unlock(&my_rwlock);
}


static int __init rwlock_demo_init(void)
{
	pr_info("rwlock_demo: loading\n");

	/*
	 * Initial data.
	 */
	add_item(10);
	add_item(20);
	add_item(30);

	reader1 = kthread_run(reader_thread, "reader-1",
			      "rw-reader-1");

	if (IS_ERR(reader1)) {
		pr_err("rwlock_demo: failed to start reader1\n");
		return PTR_ERR(reader1);
	}

	reader2 = kthread_run(reader_thread, "reader-2",
			      "rw-reader-2");

	if (IS_ERR(reader2)) {
		pr_err("rwlock_demo: failed to start reader2\n");
		kthread_stop(reader1);
		return PTR_ERR(reader2);
	}

	writer = kthread_run(writer_thread, NULL,
			     "rw-writer");

	if (IS_ERR(writer)) {
		pr_err("rwlock_demo: failed to start writer\n");
		kthread_stop(reader1);
		kthread_stop(reader2);
		return PTR_ERR(writer);
	}

	return 0;
}


static void __exit rwlock_demo_exit(void)
{
	pr_info("rwlock_demo: unloading\n");

	if (writer)
		kthread_stop(writer);

	if (reader1)
		kthread_stop(reader1);

	if (reader2)
		kthread_stop(reader2);

	free_list();

	pr_info("rwlock_demo: unloaded\n");
}


module_init(rwlock_demo_init);
module_exit(rwlock_demo_exit);


