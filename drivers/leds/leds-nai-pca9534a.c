/*
 * Copyright 2015 NAI
 *
 * Author: ANI <naii@naii.com>
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 *
 * LED driver for the PCA9534A I2C LED driver (7-bit slave address 0x38)
 *
 * Note that hardware blinking violates the leds infrastructure driver
 * interface since the hardware only supports blinking all LEDs with the
 * same delay_on/delay_off rates.  That is, only the LEDs that are set to
 * blink will actually blink but all LEDs that are set to blink will blink
 * in identical fashion.  The delay_on/delay_off values of the last LED
 * that is set to blink will be used for all of the blinking LEDs.
 * Hardware blinking is disabled by default but can be enabled by setting
 * the 'blink_type' member in the platform_data struct to 'pca9534a_HW_BLINK'
 * or by adding the 'nxp,hw-blink' property to the DTS.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/leds.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/platform_data/leds-nai-pca9534a.h>


/* LED select registers determine the source that drives LED outputs */
#define pca9534a_LED_OFF		0x0	/* LED driver off */
#define pca9534a_LED_ON		0x1	/* LED driver on */
#define DEFAULT_ON_LED_STR	"green"


enum pca963x_type {
	pca9534a3,
};

static const struct i2c_device_id pca9534a_id[] = {
	{ "pca9534a", pca9534a3 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pca9534a_id);

enum pca9534a_cmd {
	BRIGHTNESS_SET,
	BLINK_SET,
};

struct pca9534a_chipdef {
	u8			input_base;
	u8			output_base;
	u8			polarity_base;
	u8			config_base;
	int			n_port;
};

static struct pca9534a_chipdef pca9534a_chipdefs[] = {
	[pca9534a3]{
		.input_base			= 0x0,
		.output_base		= 0x1,
		.polarity_base		= 0x2,
		.config_base		= 0x3,
		.n_port				= 8,
	},
};

struct pca9534a_led;

struct pca9534a {
	struct pca9534a_chipdef *chipdef;
	struct mutex mutex;
	struct i2c_client *client;
	struct pca9534a_led *leds;
};

struct pca9534a_led {
	struct pca9534a *chip;
	struct work_struct work;
	enum led_brightness brightness;
	struct led_classdev led_cdev;
	int led_num; /* 0 .. 7 potentially */
	enum pca9534a_cmd cmd;
	char name[32];
};

static int totalLeds = 0;

static void pca9534a_brightness_work(struct pca9534a_led *pca9534a)
{
	u8 ledout_addr = pca9534a->chip->chipdef->output_base;
	u8 ledout = 0;
	int shift = pca9534a->led_num;
	u8 mask = 0x1 << shift;

	mutex_lock(&pca9534a->chip->mutex);
	ledout = i2c_smbus_read_byte_data(pca9534a->chip->client, ledout_addr);
	
	switch (pca9534a->brightness) {
	case LED_FULL:
		ledout &= ~mask;
		i2c_smbus_write_byte_data(pca9534a->chip->client, ledout_addr,
			ledout);
		break;
	case LED_OFF:
		ledout |= mask;
		i2c_smbus_write_byte_data(pca9534a->chip->client, ledout_addr,
			ledout);
		break;
	default:
		break;
	}
	mutex_unlock(&pca9534a->chip->mutex);
}

static void pca9534a_work(struct work_struct *work)
{
	struct pca9534a_led *pca9534a = container_of(work,
		struct pca9534a_led, work);
		
	switch (pca9534a->cmd) {
	case BRIGHTNESS_SET:
		pca9534a_brightness_work(pca9534a);
		break;
	default:
		break;
	}
}

static void pca9534a_led_set(struct led_classdev *led_cdev,
	enum led_brightness value)
{
	struct pca9534a_led *pca9534a;
	
	pca9534a = container_of(led_cdev, struct pca9534a_led, led_cdev);

	pca9534a->cmd = BRIGHTNESS_SET;
	pca9534a->brightness = value;
	/*
	 * Must use workqueue for the actual I/O since I2C operations
	 * can sleep.
	 */
	schedule_work(&pca9534a->work);
}

static struct pca9534a_platform_data * pca9534a_dt_init(struct i2c_client *client, struct pca9534a_chipdef *chip)
{
	struct device_node *np = client->dev.of_node, *child;
	struct pca9534a_platform_data *pdata;
	struct led_info *pca9534a_leds;
	int count;

	count = of_get_child_count(np);
	if (!count || count > chip->n_port)
		return ERR_PTR(-ENODEV);

	pca9534a_leds = devm_kzalloc(&client->dev,
			sizeof(struct led_info) * count, GFP_KERNEL);
	if (!pca9534a_leds)
		return ERR_PTR(-ENOMEM);

	for_each_child_of_node(np, child) {
		struct led_info led;
		u32 reg;
		int res;

		res = of_property_read_u32(child, "reg", &reg);
		if (res != 0)
			continue;
		led.name =
			of_get_property(child, "label", NULL) ? : child->name;
		led.default_trigger =
			of_get_property(child, "linux,default-trigger", NULL);
		pca9534a_leds[reg] = led;
	}
	pdata = devm_kzalloc(&client->dev,
			     sizeof(struct pca9534a_platform_data), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	pdata->leds.leds = pca9534a_leds;
	pdata->leds.num_leds = count;

	return pdata;
}

static const struct of_device_id of_pca9534a_match[] = {
	{ .compatible = "nai,pca9534a", },
};

static int pca9534a_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct pca9534a *pca9534a_chip;
	struct pca9534a_led *pca9534a;
	struct pca9534a_platform_data *pdata;
	struct pca9534a_chipdef *chip;
	int i, err;
	u8 configMask = 0xFF;
	s8 retData = 0;
	
	chip = &pca9534a_chipdefs[id->driver_data];
	pdata = dev_get_platdata(&client->dev);
	
	if (!pdata) {
		pdata = pca9534a_dt_init(client, chip);
		if (IS_ERR(pdata)) {
			dev_warn(&client->dev, "could not parse configuration\n");
			pdata = NULL;
		}
	}

	if (pdata && (pdata->leds.num_leds < 1 ||
				 pdata->leds.num_leds > chip->n_port)) {
		dev_err(&client->dev, "board info must claim 1-%d LEDs",
								chip->n_port);
		return -EINVAL;
	}

	pca9534a_chip = devm_kzalloc(&client->dev, sizeof(*pca9534a_chip),
								GFP_KERNEL);
	if (!pca9534a_chip)
		return -ENOMEM;
	pca9534a = devm_kzalloc(&client->dev, chip->n_port * sizeof(*pca9534a),
								GFP_KERNEL);
	if (!pca9534a)
		return -ENOMEM;

	i2c_set_clientdata(client, pca9534a_chip);

	mutex_init(&pca9534a_chip->mutex);
	pca9534a_chip->chipdef = chip;
	pca9534a_chip->client = client;
	pca9534a_chip->leds = pca9534a;
	
	/*check if pca9534a chip present*/
	retData = i2c_smbus_read_byte_data(pca9534a_chip->client, pca9534a_chip->chipdef->polarity_base);
	if(retData < 0){
		dev_warn(&client->dev, "pca9534a chip not present 0x%02x \n",retData);
		return -EINVAL;
	}
	
	for (i = 0; i <  pdata->leds.num_leds; i++) {
		pca9534a[i].led_num = i;
		pca9534a[i].chip = pca9534a_chip;

		/* Platform data can specify LED names and default triggers */
		if (pdata && i < pdata->leds.num_leds) {
			if (pdata->leds.leds[i].name)
				snprintf(pca9534a[i].name,
					 sizeof(pca9534a[i].name), "nai_ext:%s",
					 pdata->leds.leds[i].name);
			if (pdata->leds.leds[i].default_trigger)
				pca9534a[i].led_cdev.default_trigger =
					pdata->leds.leds[i].default_trigger;
		}

		pca9534a[i].led_cdev.name = pca9534a[i].name;
		pca9534a[i].led_cdev.brightness_set = pca9534a_led_set;
		
		if(strcmp(pdata->leds.leds[i].name,  DEFAULT_ON_LED_STR) == 0){
			pca9534a[i].led_cdev.brightness = LED_FULL;
		}else{
			pca9534a[i].led_cdev.brightness = LED_OFF;
		}
		
		INIT_WORK(&pca9534a[i].work, pca9534a_work);
		
		err = led_classdev_register(&client->dev, &pca9534a[i].led_cdev);
		if (err < 0)
			goto exit;
		
		configMask &= ~(1 << i);
		totalLeds = i+1;
	}

	/*configure gpio to output for availabe leds*/
	i2c_smbus_write_byte_data(client, chip->config_base, configMask);
	
	/*enable/disable leds*/
	for (i = 0; i <  pdata->leds.num_leds; i++){
		pca9534a_led_set(&(pca9534a[i].led_cdev), pca9534a[i].led_cdev.brightness);
	}
	
	return 0;

exit:
	while (i--) {
		led_classdev_unregister(&pca9534a[i].led_cdev);
		cancel_work_sync(&pca9534a[i].work);
	}

	return err;
}

static int pca9534a_remove(struct i2c_client *client)
{
	struct pca9534a *pca9534a = i2c_get_clientdata(client);
	int i;

	for (i = 0; i < totalLeds; i++) {
		led_classdev_unregister(&pca9534a->leds[i].led_cdev);
		cancel_work_sync(&pca9534a->leds[i].work);
	}

	return 0;
}

static struct i2c_driver pca9534a_driver = {
	.driver = {
		.name	= "leds-pca9534a",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(of_pca9534a_match),
	},
	.probe	= pca9534a_probe,
	.remove	= pca9534a_remove,
	.id_table = pca9534a_id,
};

module_i2c_driver(pca9534a_driver);

MODULE_AUTHOR("NAI <nai@naii.com>");
MODULE_DESCRIPTION("pca9534a LED driver");
MODULE_LICENSE("GPL v2");
