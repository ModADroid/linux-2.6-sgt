/*
 * Driver for keys on GPIO lines capable of generating interrupts.
 *
 * Copyright 2005 Phil Blundell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <linux/workqueue.h>

#include <asm/gpio.h>

#if defined(CONFIG_MACH_S5PC110_P1)
#include <plat/pm.h>
#endif

#ifdef CONFIG_KERNEL_DEBUG_SEC
#include <linux/kernel_sec_common.h>
struct timer_list debug_timer;
struct gpio_keys_platform_data *g_pdata;

void enter_upload_mode(void)
{
    bool uploadmode = true;
    int i;
    
    // not to enter forced upload mode in boot PARAM_LOW
	if( KERNEL_SEC_DEBUG_LEVEL_LOW == kernel_sec_get_debug_level() )
	    return;        

    for (i = 0; i < g_pdata->nbuttons; i++)
    {
        struct gpio_keys_button *button = &g_pdata->buttons[i];
        if(gpio_get_value(button->gpio))
        {
            uploadmode = false;
            break;
        }
    }
    
    if(uploadmode)
    {
        if (kernel_sec_viraddr_wdt_reset_reg)
        {
#ifdef CONFIG_KERNEL_DEBUG_SEC
#ifdef CONFIG_TARGET_LOCALE_KOR 
        	local_irq_disable();
        	local_fiq_disable();
#endif /* CONFIG_TARGET_LOCALE_KOR */
#endif /* CONFIG_KERNEL_DEBUG_SEC */
            kernel_sec_set_cp_upload();
            kernel_sec_save_final_context(); // Save theh final context.
            kernel_sec_set_upload_cause(UPLOAD_CAUSE_FORCED_UPLOAD);
            kernel_sec_hw_reset(false);      // Reboot.
        }
    }
}
#endif

#define __USE_PLARFORM_DATA_IRQ__
#undef __TEST_LCD_BRIGHTNESS_FOR_HW__

struct gpio_button_data {
	struct gpio_keys_button *button;
	struct input_dev *input;
	struct timer_list timer;
	struct work_struct work;
};

struct gpio_keys_drvdata {
	struct input_dev *input;
	struct gpio_button_data data[0];
};

static void gpio_keys_report_event(struct work_struct *work)
{
	struct gpio_button_data *bdata =
		container_of(work, struct gpio_button_data, work);
	struct gpio_keys_button *button = bdata->button;
	struct input_dev *input = bdata->input;
	unsigned int type = button->type ?: EV_KEY;
	int state = (gpio_get_value(button->gpio) ? 1 : 0) ^ button->active_low;

#ifdef __TEST_LCD_BRIGHTNESS_FOR_HW__
extern void nt39411b_set_brightness(int);
	{
	static int intensity=1;

	if(button->code == KEY_VOLUMEUP)
		{
		if(state)
			{
			intensity++;
			if(intensity>16) intensity=16;
			nt39411b_set_brightness(intensity);
			}
		else
			printk("ignore volumeup release\n");
		return;
		}
	else if(button->code == KEY_VOLUMEDOWN)
		{
		if(state)
			{
			intensity--;
			if(intensity<1) intensity=1;
			nt39411b_set_brightness(intensity);
			}
		else
			printk("ignore volumedown release\n");
		return;
		}
	}
#endif

#ifdef CONFIG_KERNEL_DEBUG_SEC
        static bool first=false;
        static bool second=false;
#ifdef CONFIG_TARGET_LOCALE_KOR
        static bool third=false;
#endif /* CONFIG_TARGET_LOCALE_KOR */

        if(state)
        {
            if(button->code == KEY_VOLUMEUP)
            {
                first = true;
            }
                
            if(button->code == KEY_VOLUMEDOWN)
            {
                second = true;
            }

/* forced upload should be very quick and on time, omit the timer operation */
#ifdef CONFIG_TARGET_LOCALE_KOR
            if(button->code == KEY_POWER)
            {
                third = true;
            }
            
            if(first&&second&&third)
                enter_upload_mode();
#endif /* CONFIG_TARGET_LOCALE_KOR */

            /* Entering the forced upload mode should be pressed both volume keys 
            before pressing the power key */
            if(first&&second)
            {
                if(button->code == KEY_POWER)
                {
                    mod_timer(&debug_timer, jiffies + HZ*2);
                    printk(KERN_WARNING "[Key] Waiting for upload mode for 2 seconds.\n");                    
                }
            }                
        }
        else
        {
            if(button->code == KEY_VOLUMEUP)
            {
                first = false;
            }
                
            if(button->code == KEY_VOLUMEDOWN)
            {
                second = false;
            }
#ifdef CONFIG_TARGET_LOCALE_KOR
            if(button->code == KEY_POWER)
            {
                third = false;
            }
#endif /* CONFIG_TARGET_LOCALE_KOR */
        }
#endif // CONFIG_KERNEL_DEBUG_SEC

	if(state)
	{
		printk(KERN_DEBUG "[Key] %d key is pressed.\n", button->code);
	}
	else
	{
		printk(KERN_DEBUG "[Key] %d key is released.\n", button->code);
	}

	input_event(input, type, button->code, !!state);
	input_sync(input);

}

static void gpio_keys_timer(unsigned long _data)
{
	struct gpio_button_data *data = (struct gpio_button_data *)_data;

	schedule_work(&data->work);
}

static irqreturn_t gpio_keys_isr(int irq, void *dev_id)
{
	struct gpio_button_data *bdata = dev_id;
	struct gpio_keys_button *button = bdata->button;

#ifdef __USE_PLARFORM_DATA_IRQ__
	BUG_ON(irq != button->irq);
#else
	BUG_ON(irq != gpio_to_irq(button->gpio));
#endif

	if (button->debounce_interval)
		mod_timer(&bdata->timer,
			jiffies + msecs_to_jiffies(button->debounce_interval));
	else
		schedule_work(&bdata->work);

	return IRQ_HANDLED;
}

#if defined(CONFIG_MACH_S5PC110_P1)
/* For checking H/W faulty. */
static ssize_t keyshort_test(struct device *dev, struct device_attribute *attr, char *buf)
{
       int i, ret=0;
       int count;
	struct gpio_keys_platform_data *pdata = dev->platform_data;

	for(i = 0; i < pdata->nbuttons; i++ )
	{
	    struct gpio_keys_button *button = &pdata->buttons[i];
	    ret = gpio_get_value( button->gpio);
	    if(!ret)
	        break;
	}
	
       if(!ret)
	{
		count = sprintf(buf,"PRESS\n");
              printk(KERN_DEBUG "[Key] keyshort_test: PRESS\n");
	}
	else
	{
		count = sprintf(buf,"RELEASE\n");
              printk(KERN_DEBUG "[Key] keyshort_test: RELEASE\n");
	}	

	return count;
}
static DEVICE_ATTR(key_pressed, S_IRUGO | S_IWUSR | S_IWOTH | S_IXOTH, keyshort_test, NULL);
#endif

//#define NO_SLEEP
#ifdef NO_SLEEP // for test
#include <linux/wakelock.h>
static struct wake_lock wake_lock_for_dev;
#endif

static int __devinit gpio_keys_probe(struct platform_device *pdev)
{
	struct gpio_keys_platform_data *pdata = pdev->dev.platform_data;
	struct gpio_keys_drvdata *ddata;
	struct input_dev *input;
	int i, error;
	int wakeup = 0;

	ddata = kzalloc(sizeof(struct gpio_keys_drvdata) +
			pdata->nbuttons * sizeof(struct gpio_button_data),
			GFP_KERNEL);
	input = input_allocate_device();
	if (!ddata || !input) {
		error = -ENOMEM;
		goto fail1;
	}

#ifdef NO_SLEEP // for test
	wake_lock(&wake_lock_for_dev);
#endif

	platform_set_drvdata(pdev, ddata);

	input->name = pdev->name;
	input->phys = "gpio-keys/input0";
	input->dev.parent = &pdev->dev;

	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0100;

	/* Enable auto repeat feature of Linux input subsystem */
	if (pdata->rep)
		__set_bit(EV_REP, input->evbit);

	ddata->input = input;

	for (i = 0; i < pdata->nbuttons; i++) {
		struct gpio_keys_button *button = &pdata->buttons[i];
		struct gpio_button_data *bdata = &ddata->data[i];
		int irq;
		unsigned int type = button->type ?: EV_KEY;

		bdata->input = input;
		bdata->button = button;
		setup_timer(&bdata->timer,
			    gpio_keys_timer, (unsigned long)bdata);
		INIT_WORK(&bdata->work, gpio_keys_report_event);

		error = gpio_request(button->gpio, button->desc ?: "gpio_keys");
		if (error < 0) {
			pr_err("gpio-keys: failed to request GPIO %d,"
				" error %d\n", button->gpio, error);
			goto fail2;
		}

		error = gpio_direction_input(button->gpio);
		if (error < 0) {
			pr_err("gpio-keys: failed to configure input"
				" direction for GPIO %d, error %d\n",
				button->gpio, error);
			gpio_free(button->gpio);
			goto fail2;
		}

#ifdef __USE_PLARFORM_DATA_IRQ__
			irq = button->irq;
#else
			irq = gpio_to_irq(button->gpio);
#endif
		
		if (irq < 0) {
			error = irq;
			pr_err("gpio-keys: Unable to get irq number"
				" for GPIO %d, error %d\n",
				button->gpio, error);
			gpio_free(button->gpio);
			goto fail2;
		}

		error = request_irq(irq, gpio_keys_isr,
				    IRQF_SHARED |
				    IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				    button->desc ? button->desc : "gpio_keys",
				    bdata);
		if (error) {
			pr_err("gpio-keys: Unable to claim irq %d; error %d\n",
				irq, error);
			gpio_free(button->gpio);
			goto fail2;
		}

		if (button->wakeup)
			wakeup = 1;

		input_set_capability(input, type, button->code);
	}

	error = input_register_device(input);
	if (error) {
		pr_err("gpio-keys: Unable to register input device, "
			"error: %d\n", error);
		goto fail2;
	}

	device_init_wakeup(&pdev->dev, wakeup);

	if (device_create_file(&pdev->dev, &dev_attr_key_pressed) < 0)
	{
		pr_err("Failed to create device file(%s)!\n", dev_attr_key_pressed.attr.name);
	}

#ifdef CONFIG_KERNEL_DEBUG_SEC
        g_pdata = pdata;
        init_timer(&debug_timer);
        debug_timer.function = enter_upload_mode;
#endif

	return 0;

 fail2:
	while (--i >= 0) {
#ifdef __USE_PLARFORM_DATA_IRQ__
		free_irq(pdata->buttons[i].irq, &ddata->data[i]);
#else
		free_irq(gpio_to_irq(pdata->buttons[i].gpio), &ddata->data[i]);
#endif
		if (pdata->buttons[i].debounce_interval)
			del_timer_sync(&ddata->data[i].timer);
		cancel_work_sync(&ddata->data[i].work);
		gpio_free(pdata->buttons[i].gpio);
	}

	platform_set_drvdata(pdev, NULL);
 fail1:
	input_free_device(input);
	kfree(ddata);

	return error;
}

static int __devexit gpio_keys_remove(struct platform_device *pdev)
{
	struct gpio_keys_platform_data *pdata = pdev->dev.platform_data;
	struct gpio_keys_drvdata *ddata = platform_get_drvdata(pdev);
	struct input_dev *input = ddata->input;
	int i;

	device_init_wakeup(&pdev->dev, 0);

	for (i = 0; i < pdata->nbuttons; i++) {
		int irq;
#ifdef __USE_PLARFORM_DATA_IRQ__
		irq = pdata->buttons[i].irq;
#else
		irq = gpio_to_irq(pdata->buttons[i].gpio);
#endif
		free_irq(irq, &ddata->data[i]);
		if (pdata->buttons[i].debounce_interval)
			del_timer_sync(&ddata->data[i].timer);
		gpio_free(pdata->buttons[i].gpio);
	}

	input_unregister_device(input);

	return 0;
}


#ifdef CONFIG_PM
static int gpio_keys_suspend(struct platform_device *pdev)
{
	struct gpio_keys_platform_data *pdata = pdev->dev.platform_data;
	int i;

	if (device_may_wakeup(&pdev->dev)) {
		for (i = 0; i < pdata->nbuttons; i++) {
			struct gpio_keys_button *button = &pdata->buttons[i];
			if (button->wakeup) {
				int irq;
#ifdef __USE_PLARFORM_DATA_IRQ__
				irq = button->irq;
#else
				irq = gpio_to_irq(button->gpio);
#endif
				enable_irq_wake(irq);
			}
		}
	}

	return 0;
}

static int gpio_keys_resume(struct platform_device *pdev)
{
	struct gpio_keys_platform_data *pdata = pdev->dev.platform_data;
//	struct gpio_keys_drvdata *ddata = platform_get_drvdata(pdev);
	int i;

	if (device_may_wakeup(&pdev->dev)) {
		for (i = 0; i < pdata->nbuttons; i++) {
			struct gpio_keys_button *button = &pdata->buttons[i];
			if (button->wakeup) {
				int irq;
#ifdef __USE_PLARFORM_DATA_IRQ__
				irq = button->irq;
#else
				irq = gpio_to_irq(button->gpio);
#endif
				disable_irq_wake(irq);
			}
		}
	}
	
/* irq issue is fixed.*/	
#if 0
#if defined(CONFIG_MACH_S5PC110_P1)
	if(s3c_pm_get_wakeup_stat() == PM_POWER_KEY)
	{
		printk("%s(%d) PM_POWER_KEY is pressed\n", __func__, __LINE__);		
		input_report_key(ddata->input, 116,1);
		input_report_key(ddata->input, 116,0);
		
		s3c_pm_clear_wakeup_stat();
	}
#endif // CONFIG_MACH_S5PC110_P1
#endif

	return 0;
}
#else
#define gpio_keys_suspend	NULL
#define gpio_keys_resume	NULL
#endif

static struct platform_driver gpio_keys_device_driver = {
	.probe		= gpio_keys_probe,
	.remove		= __devexit_p(gpio_keys_remove),
	.suspend	= gpio_keys_suspend,
	.resume		= gpio_keys_resume,
	.driver		= {
		.name	= "gpio-keys",
		.owner	= THIS_MODULE,
	}
};


static int __init gpio_keys_init(void)
{

#ifdef NO_SLEEP // for test
	wake_lock_init(&wake_lock_for_dev, WAKE_LOCK_SUSPEND, "wake_lock_dev");
#endif

	return platform_driver_register(&gpio_keys_device_driver);
}

static void __exit gpio_keys_exit(void)
{
	platform_driver_unregister(&gpio_keys_device_driver);
}

module_init(gpio_keys_init);
module_exit(gpio_keys_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Phil Blundell <pb@handhelds.org>");
MODULE_DESCRIPTION("Keyboard driver for CPU GPIOs");
MODULE_ALIAS("platform:gpio-keys");
