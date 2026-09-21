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

#define GPIO1_BASE				0xff250000	// as on rockchip's px30.dtsi 
#define GPIO2_BASE				0xff260000	// same as avobe	 
#define GPIO3_BASE				0xff270000 	// same as avobe, again
#define GPIO_MAP_SIZE			0x100		// as on px30.dtsi: reg = <0x0 0xff260000 0x0 0x100>;
#define GPIO_EXT_PORT_OFFSET	0x50 		// live input pin state

#define GPIO1_INPUT_NO			8
#define GPIO2_INPUT_NO			8
#define GPIO3_INPUT_NO			4

const uint32_t gpio1_pins[GPIO1_INPUT_NO] = {12, 13, 14, 15, 2, 5, 6, 7};
const uint32_t gpio2_pins[GPIO2_INPUT_NO] = {0, 1, 2, 3, 4, 5, 6, 7};
const uint32_t gpio3_pins[GPIO3_INPUT_NO] = {9, 10, 12, 15};

static void __iomem *gpio1;
static void __iomem *gpio2;
static void __iomem *gpio3;

// No designated initializer
typedef struct
{
	/**
	 *  BIT | Button 		| CTRLR	| Notes
	 * -----|---------------|-------|-------
	 *    0 | DPAD_UP  		| GPIO1	|
	 *    1 | DPAD_DOWN 	| GPIO1	|
	 *    2 | DPAD_LEFT		| GPIO1	|
	 *    3 | DPAD_RiGHT	| GPIO1	|
	 *    4 | A 			| GPIO1	| 
	 *    5 | B 			| GPIO1	|
	 *    6 | Y 			| GPIO1	|
	 *    7 | X 			| GPIO1	|
	 *    8 | VOL_UP  		| GPIO2	|
	 *    9 | VOL_DOWN  	| GPIO2	| 
	 *   10 | F3 / TL3 		| GPIO2	| Pressing left joystick
	 *   11 | F4 / TR3 		| GPIO2	| Pressing right joystick 
	 *   12 | F5 / FN		| GPIO2	|
	 *   13 | F6 			| GPIO2	| Unpopulated
	 *   14 | TL  			| GPIO2	|
	 *   15 | TR  			| GPIO2	|
	 *   16 | F1 / SELECT	| GPIO3	|
	 *   17 | TL2  			| GPIO3	|
	 *   18 | F2 / START	| GPIO3	|
	 *   19 | TR2  			| GPIO3	|
	 */
	uint32_t buttons;	// semantic bitmask, one bit per button
	
	/**
	 * Analog stick axes are not exposed since reading them
	 * would require driving the shared analog mux select and SARADC 
	 * control registers, which are already owned and written by 
	 * `odroidgo3-joypad` driver.
	 * This would be a write-write conflict with an aready running 
	 * driver. Which means, not a safe read-only shadow.
	 * 
	 * int16_t lx, ly;		// left stick (normalized)
	 * int16_t rx, ry;		// right stick (normalized) 
	 */
} input_state;

static ssize_t simple_input_read(
struct file *file, char __user *buff, size_t count, loff_t *ppos) 
{
	size_t idx1;
	size_t idx2;
	size_t idx3;
	input_state state = {0};
	uint32_t val = 0;
	uint32_t ext_port[3] = {0};
	
	// Check if not first read
	if(*ppos > 0) { return 0; } // no more data to read (EOF)
	if(count < sizeof(state)) { return -EINVAL; }
	ext_port[0] = readl(gpio1 + GPIO_EXT_PORT_OFFSET);
	printk(KERN_INFO "r36s_simple_input: Read gpio1 controller (port %u).\n", ext_port[0]);

	ext_port[1] = readl(gpio2 + GPIO_EXT_PORT_OFFSET);
	printk(KERN_INFO "r36s_simple_input: Read gpio2 controller (port %u).\n", ext_port[1]);
	
	ext_port[2] = readl(gpio3 + GPIO_EXT_PORT_OFFSET);
	printk(KERN_INFO "r36s_simple_input: Read gpio3 controller (port %u).\n", ext_port[2]);

	for(idx1 = 0; idx1 < GPIO1_INPUT_NO; idx1++)
	{
		val = (ext_port[0] >> gpio1_pins[idx1]) & 0x1; 	// masking the bits we want
		val = !val;	// invert so 1 is pressed and 0 is not	
		state.buttons |= (val << idx1);	
	}
	
	for(idx2 = 0; idx2 < GPIO2_INPUT_NO; idx2++)
	{
		val = (ext_port[1] >> gpio2_pins[idx2]) & 0x1; 	// masking the bits we want
		val = !val;	// invert so 1 is pressed and 0 is not	
		state.buttons |= (val << (idx1 + idx2));	
	}
	
	for(idx3 = 0; idx3 < GPIO3_INPUT_NO; idx3++)
	{
		val = (ext_port[2] >> gpio3_pins[idx3]) & 0x1; 	// masking the bits we want
		val = !val;	// invert so 1 is pressed and 0 is not	
		state.buttons |= (val << (idx1 + idx2 + idx3));	
	}
	
	// Send byte to userspace
	if(copy_to_user(buff, &state, sizeof(state))) { return -EFAULT; }
	
	*ppos += sizeof(state);
	return sizeof(state);
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
	
	gpio1 = ioremap(GPIO1_BASE, GPIO_MAP_SIZE);
	if(!gpio1) 
	{
		printk(KERN_ERR "r36s_simple_input: ioremap gpio1 failed.\n");
		return -ENOMEM; // OOM
	}
	
	gpio2 = ioremap(GPIO2_BASE, GPIO_MAP_SIZE);
	if(!gpio2) 
	{
		iounmap(gpio1);
		printk(KERN_ERR "r36s_simple_input: ioremap gpio2 failed.\n");
		return -ENOMEM; // OOM
	}
	
	gpio3 = ioremap(GPIO3_BASE, GPIO_MAP_SIZE);
	if(!gpio3) 
	{
		iounmap(gpio1);
		iounmap(gpio2);
		printk(KERN_ERR "r36s_simple_input: ioremap gpio3 failed.\n");
		return -ENOMEM; // OOM
	}
	
	ret = misc_register(&simple_input_reg);
	if(ret) 
	{
		printk(KERN_ERR "r36s_simple_input: misc_register failed.\n");
		iounmap(gpio1);
		iounmap(gpio2);
		iounmap(gpio3);
		return ret;
	}
	
	printk(KERN_INFO "r36s_simple_input: LOADED\n");
	return 0;
}

static void __exit simple_input_exit(void) 
{
	misc_deregister(&simple_input_reg);
	iounmap(gpio1);
	iounmap(gpio2);
	iounmap(gpio3);
	printk(KERN_INFO "r36s_simple_input: UNLOADED\n");
}

module_init(simple_input_init);
module_exit(simple_input_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ph0nsy");
MODULE_DESCRIPTION("Module that handles button input for the R36S console via semantic bitmask.");
