/*
 * hello.c - sample loadable module for uClinux/ST20 (arch/st20/kernel/module.c)
 *
 * /dev/hello (char major 61): read returns a greeting with the open count and the
 * last message written; write stores a message (up to 63 bytes).  /proc/hello shows the
 * same state.  Static data with pointer initialisers (file_operations) exercises the
 * absolute relocations, calls into the kernel the PC-relative ones.
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>

#define HELLO_MAJOR 61

static int opens;
static unsigned long loaded_at;
static char message[64] = "(nothing written yet)";

static int hello_state(char *buf)
{
	return sprintf(buf, "Hello from an ST20 module: loaded %lu jiffies ago, opened %d times, "
		       "message \"%s\"\n", jiffies - loaded_at, opens, message);
}

static int hello_open(struct inode *inode, struct file *file)
{
	opens++;
	return 0;
}

static ssize_t hello_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	char tmp[200];
	int len = hello_state(tmp);

	if (*ppos >= len)
		return 0;
	if (count > len - *ppos)
		count = len - *ppos;
	if (copy_to_user(buf, tmp + *ppos, count))
		return -EFAULT;
	*ppos += count;
	return count;
}

static ssize_t hello_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	size_t n = count < sizeof(message) - 1 ? count : sizeof(message) - 1;
	char first;

	/* uC-libc's fflush writes 0 bytes, echo may write the newline on its own: keep the
	 * message then */
	if (count == 0)
		return 0;
	if (count == 1 && !copy_from_user(&first, buf, 1) && first == '\n')
		return 1;
	if (copy_from_user(message, buf, n))
		return -EFAULT;
	message[n] = 0;
	if (n && message[n - 1] == '\n')
		message[n - 1] = 0;
	return count;
}

static struct file_operations hello_fops = {
	owner:	NULL,
	read:	hello_read,
	write:	hello_write,
	open:	hello_open,
};

static int hello_proc(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	*eof = 1;
	return hello_state(page);
}

static int __init hello_init(void)
{
	int err = register_chrdev(HELLO_MAJOR, "hello", &hello_fops);

	if (err < 0) {
		printk(KERN_ERR "hello: major %d busy\n", HELLO_MAJOR);
		return err;
	}
	create_proc_read_entry("hello", 0, NULL, hello_proc, NULL);
	loaded_at = jiffies;
	printk(KERN_INFO "hello: module loaded, /dev/hello (major %d), /proc/hello\n", HELLO_MAJOR);
	return 0;
}

static void __exit hello_exit(void)
{
	remove_proc_entry("hello", NULL);
	unregister_chrdev(HELLO_MAJOR, "hello");
	printk(KERN_INFO "hello: module unloaded after %d opens\n", opens);
}

module_init(hello_init);
module_exit(hello_exit);
