// module_init, module_exit, module_license, module_author, module_description
#include <linux/module.h> 	
// printk
#include <linux/kernel.h> 	
// __init, __exit
#include <linux/init.h>	
// ioremap, iounmap, readl, writel
#include <linux/io.h>	 	 

#define GPIO2_BASE				0xff260000	// as on rockchip's px30.dtsi 
#define GPIO_MAP_SIZE			0x100		// as on px30.dtsi: reg = <0x0 0xff260000 0x0 0x100>;
#define GPIO_EXT_PORT_OFFSET	0x50 		// live input pin state


static void __iomem *gpio;

static int __init simple_input_init(void)
{
	uint32_t ext_port;
	printk(KERN_INFO "simple_input: LOADED");
	gpio = ioremap(GPIO2_BASE, GPIO_MAP_SIZE);
	if(!gpio) 
	{
		printk(KERN_ERR "r36s_simple_input: ioremap failed.\n");
		return -ENOMEM; // OOM
	}
	ext_port = readl(gpio + GPIO_EXT_PORT_OFFSET);
	printk(KERN_INFO "r36s_simple_input: %d.\n", ext_port);
	return 0;
}

static void __exit simple_input_exit(void) 
{
	printk(KERN_INFO "simple_input: UNLOADED");
	iounmap(gpio);
}

module_init(simple_input_init);
module_exit(simple_input_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ph0nsy");
MODULE_DESCRIPTION("Hello World module for R36S build path validation.");
