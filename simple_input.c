// module_init, module_exit, module_license, module_author, module_description
#include <linux/module.h> 	
// printk
#include <linux/kernel.h> 	
// __init, __exit
#include <linux/init.h>	
// ioremap, iounmap, readl, writel
#include <linux/io.h>	
// file_operations
#include <linux/fs.h>	
// miscdevice, misc_register
#include <linux/miscdevice.h>	
// copy_to_user
#include <linux/uaccess.h>	

#define GPIO2_BASE				0xff260000	// as on rockchip's px30.dtsi 
#define GPIO_MAP_SIZE			0x100		// as on px30.dtsi: reg = <0x0 0xff260000 0x0 0x100>;
#define GPIO_EXT_PORT_OFFSET	0x50 		// live input pin state

#define VOL_MASK				0x3	

static void __iomem *gpio;

static ssize_t simple_input_read(
struct file *file, char __user *buff, size_t count, loff_t *ppos) 
{
	uint32_t ext_port;
	uint8_t val;
	
	// Check if not first read
	if(*ppos > 0) { return 0; } // no more data to read (EOF)
	
	ext_port = readl(gpio + GPIO_EXT_PORT_OFFSET);
	printk(KERN_INFO "r36s_simple_input: Read port %d.\n", ext_port);

	val = ext_port & VOL_MASK; 	// masking the bits we want
	val = (~val) & VOL_MASK;	// invert so 1 is pressed and 0 is not
	
	// Send byte to userspace
	if(copy_to_user(buff, &val, 1)) { return -EFAULT; }
	
	*ppos += 1;
	return 1;
}

static const struct file_operations simple_input_fops = 
{
	.owner = THIS_MODULE,
	.read = simple_input_read,
};

static struct miscdevice simple_input_reg = 
{
	.minor = MISC_DYNAMIC_MINOR, // for `misc_register`
	.name = "r36s_simple_input",
	.fops = &simple_input_fops,
};

static int __init simple_input_init(void)
{	
	int ret;
	
	gpio = ioremap(GPIO2_BASE, GPIO_MAP_SIZE);
	if(!gpio) 
	{
		printk(KERN_ERR "r36s_simple_input: ioremap failed.\n");
		return -ENOMEM; // OOM
	}
	
	ret = misc_register(&simple_input_reg);
	if(ret) 
	{
		printk(KERN_ERR "r36s_simple_input: misc_register failed.\n");
		iounmap(gpio);
		return ret;
	}
	
	printk(KERN_INFO "r36s_simple_input: LOADED\n");
	return 0;
}

static void __exit simple_input_exit(void) 
{
	misc_deregister(&simple_input_reg);
	iounmap(gpio);
	printk(KERN_INFO "r36s_simple_input: UNLOADED\n");
}

module_init(simple_input_init);
module_exit(simple_input_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ph0nsy");
MODULE_DESCRIPTION("Hello World module for R36S build path validation.");
