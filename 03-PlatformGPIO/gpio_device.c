#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>

#define GPIO_BASE 0x3F200000
#define GPIO_SIZE 0xB4

static struct resource bcm_gpio_resources[] ={
    {
        .start = GPIO_BASE,
        .end = GPIO_BASE + GPIO_SIZE - 1,
        .flags = IORESOURCE_MEM,
    }

};

static struct platform_device bcm_gpio_device ={
    .name = "bcm-gpio",
    .id = 0,
    .resource = bcm_gpio_resources,
    .num_resources = ARRAY_SIZE(bcm_gpio_resources)
};

static int __init gpio_device_init(void){
    printk("Registering BCM GPIO platform device\n");
    return platform_device_register(&bcm_gpio_device);
}

static void __exit gpio_device_exit(void){
    platform_device_unregister( &bcm_gpio_device);

}

module_init(gpio_device_init);
module_exit(gpio_device_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ehab");