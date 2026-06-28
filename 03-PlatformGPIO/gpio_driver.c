#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>

static void __iomem *gpio_base;

#define GPFSEL0 0x00
#define GPSET0  0x1C
#define GPCLR0  0x28

static int bcm_gpio_probe( struct platform_device *pdev){
    struct resource *res;

    printk("BCM GPIO probe called\n");

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

    // only map memory, because its already used/owned by rpi gpio driver
    gpio_base = devm_ioremap(&pdev->dev, res->start, resource_size(res));

    if(IS_ERR(gpio_base))
        return PTR_ERR(gpio_base);

    printk("GPIO mapped\n");

    unsigned int value;
    value = readl(gpio_base + 0x04);

    value &= ~(7 << 21);
    value |= (1 << 21);

    writel(value, gpio_base + 0x04);

    writel((1 << 17), gpio_base + GPSET0);
    printk("GPIO17 ON\n");

    return 0;
}

static void bcm_gpio_remove(struct platform_device *pdev){
    printk("GPIO remove\n");
}

static struct platform_driver bcm_gpio_driver ={
    .driver = {
        .name = "bcm-gpio",
    },

    .probe = bcm_gpio_probe,
    .remove = bcm_gpio_remove,
};

module_platform_driver(bcm_gpio_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ehab");