#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/io.h>
#include <linux/ide.h>
#include <linux/device.h>

#define NEWCHRDEV_NAME          "newchrdev"
#define NEWCHRDEV_COUNT         1

/* 寄存器物理地址 */
#define CCM_CCGR1_BASE          (0X020C406C)
#define SW_MUX_GPIO1_IO03_BASE  (0X020E0068)
#define SW_PAD_GPIO1_IO03_BASE  (0X020E02F4)
#define GPIO1_DR_BASE           (0X0209C000)
#define GPIO1_DIR_BASE          (0X0209C004)

#define LED_ON          1
#define LED_OFF         0

/* 映射后的虚拟地址 */
static void __iomem *CCM_CCGR1;
static void __iomem *SW_MUX_GPIO1_IO03;
static void __iomem *SW_PAD_GPIO1_IO03;
static void __iomem *GPIO1_DR;
static void __iomem *GPIO1_DIR;

static int newchrled_open(struct inode *inode, struct file *file);
static ssize_t newchrled_write(struct file *file, const char __user *user, size_t size, loff_t *loff);
static int newchrled_release(struct inode *inode, struct file *file);

struct newchrled_dev {
        struct cdev led_cdev;   /* 字符设备结构体 */
        struct class *class;    /* 类 */
        struct device *device;  /* 设备 */
        dev_t devid;            /* 设备号 */
        int major;              /* 主设备号 */
        int minor;              /* 次设备号 */
};
static struct newchrled_dev newchrdev;

static const struct file_operations led_ops = {
        .owner = THIS_MODULE,
        .open = newchrled_open,
        .write = newchrled_write,
        .release = newchrled_release,
};

static void led_switch(unsigned char led_status)
{
        unsigned int val = 0;
        if (led_status == LED_ON) {
                val = readl(GPIO1_DR);
                val &= ~(1<<3);
                writel(val, GPIO1_DR);
        } else {
                val = readl(GPIO1_DR);
                val |= (1<<3);
                writel(val, GPIO1_DR);
        }
}

static void led_gpio_init(void)
{
        unsigned int val = 0;

        /* 完成内存地址映射 */
        CCM_CCGR1 = ioremap(CCM_CCGR1_BASE, 4);
        SW_MUX_GPIO1_IO03 = ioremap(SW_MUX_GPIO1_IO03_BASE, 4);
        SW_PAD_GPIO1_IO03 = ioremap(SW_PAD_GPIO1_IO03_BASE, 4);
        GPIO1_DR = ioremap(GPIO1_DR_BASE, 4);
        GPIO1_DIR = ioremap(GPIO1_DIR_BASE, 4);

        /* 完成初始化 */
        /* 配置时钟*/
        val = readl(CCM_CCGR1);
        val |= (3<<26);
        writel(val, CCM_CCGR1);

        /* 初始化IO复用 */
        writel(0x05, SW_MUX_GPIO1_IO03);

        /* 配置IO属性 */
        writel(0x10B0, SW_PAD_GPIO1_IO03);
        
        /* 配置为输出模式 */
        val = readl(GPIO1_DIR);
        val |= (1<<3);
        writel(val, GPIO1_DIR);

        led_switch(LED_OFF);
}

static int newchrled_open(struct inode *inode, struct file *file)
{
        printk("led open!\n");
        file->private_data = &newchrdev;

        return 0;
}

static ssize_t newchrled_write(struct file *file, const char __user *user, size_t size, loff_t *loff)
{
        int ret = 0;
        unsigned char buf[1];
        struct newchrled_dev *led_dev = file->private_data;

        ret = copy_from_user(buf, user, 1);
        if (ret < 0) {
                printk("kernel write failed!\n");
                ret = -EFAULT;
                goto error;
        }

        if((buf[0] != LED_OFF) && (buf[0] != LED_ON)) {
                ret = -1;
                goto error;
        }
        led_switch(buf[0]);

error:
        return ret;
}

static int newchrled_release(struct inode *inode, struct file *file)
{
        printk("led release!\n");
        file->private_data = NULL;
        return 0;
}


static int __init led_init(void)
{
        int ret = 0;
        printk("newchrled init!\n");
        /* LED初始化 */
        led_gpio_init();

        /* 注册设备号 */
        if (newchrdev.major) {
                newchrdev.devid = MKDEV(newchrdev.major, 0);
                ret = register_chrdev_region(newchrdev.devid, NEWCHRDEV_COUNT, NEWCHRDEV_NAME);
        } else {
                ret = alloc_chrdev_region(&newchrdev.devid, 0, NEWCHRDEV_COUNT, NEWCHRDEV_NAME);
                newchrdev.major = MAJOR(newchrdev.devid);
                newchrdev.minor = MINOR(newchrdev.devid);
        }
        if (ret < 0) {
                printk("newchrdev led region err:%d!\n", ret);
                goto region_error;
        }
        printk("major:%d minor:%d\n", newchrdev.major, newchrdev.minor);

        /* 注册字符设备 */
        newchrdev.led_cdev.owner = THIS_MODULE,
        newchrdev.led_cdev.ops = &led_ops,
        cdev_init(&newchrdev.led_cdev, &led_ops);
        cdev_add(&newchrdev.led_cdev, newchrdev.devid, NEWCHRDEV_COUNT);

        /* 自动创建设备节点 */
        /* 1.申请类 */
        newchrdev.class = class_create(THIS_MODULE, NEWCHRDEV_NAME);
        if (IS_ERR(newchrdev.class)) {
		ret = PTR_ERR(newchrdev.class);
		goto class_create_error;
	}
        /* 2.创建设备 */
        newchrdev.device = device_create(newchrdev.class, NULL, 
                                         newchrdev.devid, NULL, NEWCHRDEV_NAME);
        if (IS_ERR(newchrdev.device)) {
		ret = PTR_ERR(newchrdev.device);
		goto device_create_error;
	};
        goto success;
        
device_create_error:
        class_destroy(newchrdev.class);
class_create_error:
        unregister_chrdev_region(newchrdev.devid, NEWCHRDEV_COUNT);
        cdev_del(&newchrdev.led_cdev);
region_error:
success:
        return ret;
}

static void __exit led_exit(void)
{
        printk("newchrled exit!\n");
        led_switch(LED_OFF);

        device_destroy(newchrdev.class, newchrdev.devid);
        class_destroy(newchrdev.class);
        unregister_chrdev_region(newchrdev.devid, NEWCHRDEV_COUNT);
        cdev_del(&newchrdev.led_cdev);
}

module_init(led_init);
module_exit(led_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("wanglei");
