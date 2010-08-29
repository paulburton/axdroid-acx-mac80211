/*
 * WLAN (TI TNETW1100B) support in the aximx50.
 *
 * Copyright (c) 2010 Paul Burton <paulburton89@gmail.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */


#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/leds.h>

#include <asm/mach-types.h>
#include <asm/gpio.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <mach/aximx50.h>
#include <mach/pxa27x.h>

/* PCMCIA Socket 0 Memory */
#define WLAN_BASE	0x2c000000

static int aximx50_wlan_start(void)
{
	int ret;

	ret = gpio_request(GPIO_NR_AXIMX50_WIFI_RESET, "WIFI RST");
	if (ret)
		goto err1;
	ret = gpio_direction_output(GPIO_NR_AXIMX50_WIFI_RESET, 1);
	if (ret)
		goto err2;

	ret = gpio_request(GPIO_NR_AXIMX50_WIFI_IRQ, "WIFI IRQ");
	if (ret)
		goto err2;
	ret = gpio_direction_input(GPIO_NR_AXIMX50_WIFI_IRQ);
	if (ret)
		goto err3;

	gpio_set_value(GPIO_NR_AXIMX50_WIFI_RESET, 1);
	mdelay(50);
	aximx50_fpga_set(0x1c, 0x0001);
	mdelay(50);
	aximx50_fpga_set(0x16, 0x0004);
	mdelay(50);
	aximx50_fpga_set(0x10, 0x2000);
	aximx50_fpga_set(0x10, 0x0008);
	mdelay(100);
	gpio_set_value(GPIO_NR_AXIMX50_WIFI_RESET, 0);
	mdelay(50);
	aximx50_fpga_set(0x00, 0x0040);

	printk("aximx50_acx: %s: done\n", __func__);
	return 0;

err3:
	gpio_free(GPIO_NR_AXIMX50_WIFI_IRQ);
err2:
	gpio_free(GPIO_NR_AXIMX50_WIFI_RESET);
err1:
	return ret;
}

static int aximx50_wlan_stop(void)
{
	aximx50_fpga_clear(0x10, 0x0008);
	aximx50_fpga_clear(0x10, 0x2000);
	aximx50_fpga_clear(0x16, 0x0004);
	aximx50_fpga_clear(0x1c, 0x0001);
	gpio_set_value(GPIO_NR_AXIMX50_WIFI_RESET, 1);

	gpio_free(GPIO_NR_AXIMX50_WIFI_IRQ);
	gpio_free(GPIO_NR_AXIMX50_WIFI_RESET);

	printk("aximx50_acx: %s: done\n", __func__);
	return 0;
}

static void aximx50_wlan_e_release(struct device *dev) {
}

static struct resource acx_resources[] = {
	[0] = {
		.start	= WLAN_BASE,
		.end	= WLAN_BASE + 0x20,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gpio_to_irq(GPIO_NR_AXIMX50_WIFI_IRQ),
		.end	= gpio_to_irq(GPIO_NR_AXIMX50_WIFI_IRQ),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device acx_device = {
	.name	= "acx-mem",
	.dev	= {
		.release = &aximx50_wlan_e_release
	},
	.num_resources	= ARRAY_SIZE(acx_resources),
	.resource	= acx_resources,
};

static int __init aximx50_wlan_init(void)
{
	int ret;

	ret = aximx50_wlan_start();
	if (ret) {
		printk("aximx50_acx: %s: failed to start!\n", __func__);
		return ret;
	}

	printk("aximx50_acx: %s: platform_device_register ... \n", __func__);
	ret=platform_device_register(&acx_device);
	printk("aximx50_acx: %s: platform_device_register: done\n", __func__);
	// Check if the (or another) driver was found, aka if probe succeeded
	if (acx_device.dev.driver == NULL) {
		printk("aximx50_acx: %s: acx-mem platform_device_register: failed\n", __func__);
		platform_device_unregister(&acx_device);
		aximx50_wlan_stop();
		return(-EINVAL);
	}

	return ret;
}


static void __exit aximx50_wlan_exit(void)
{
	printk("aximx50_acx: %s: platform_device_unregister ... \n", __func__);
	platform_device_unregister(&acx_device);
	printk("aximx50_acx: %s: platform_device_unregister: done\n", __func__);

	aximx50_wlan_stop();
}

module_init(aximx50_wlan_init);
module_exit(aximx50_wlan_exit);

MODULE_AUTHOR("Paul Burton <paulburton89@gmail.com>");
MODULE_DESCRIPTION("WLAN driver for Dell Axim X50/X51(v)");
MODULE_LICENSE("GPL");

