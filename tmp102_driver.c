#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/sysfs.h>
#include <linux/of.h>
#include <linux/cdev.h>
#include <linux/device.h>

#include "tmp102_driver.h"

#define DEVICE_NAME "tmp102"
#define CLASS_NAME "tmp102_class"

static int major_number;
static struct class *tmp102_class = NULL;
static struct device *tmp102_device = NULL;
static struct cdev tmp102_cdev;

struct tmp102_data
{
    struct i2c_client *client;
    struct mutex lock;
    int temperature;
};

static int dev_open(struct inode *inodep, struct file *filep)
{
    pr_info("i2c: tmp102: device opened\n");
    return 0;
}

static int dev_release(struct inode *inodep, struct file *filep)
{
    pr_info("i2c: tmp102: device closed\n");
    return 0;
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset)
{
    // Implement read functionality
    return 0;
}

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset)
{
    // Implement write functionality
    return -EINVAL; // Writing is not supported
}

static struct file_operations fops = {
    .open = dev_open,
    .read = dev_read,
    .write = dev_write,
    .release = dev_release,
};

static int tmp102_read_temperature(struct tmp102_data *data)
{
    u16 raw_temp;
    int temperature;

    mutex_lock(&data->lock);
    raw_temp = i2c_smbus_read_word_data(data->client, 0x00);
    mutex_unlock(&data->lock);

    if (raw_temp < 0)
    {
        pr_err("i2c: tmp102: Failed to read TMP102 temperature data\n");
        return -EIO;
    }

    raw_temp = ((raw_temp << 8) & 0xFF00) | ((raw_temp >> 8) & 0x00FF);
    raw_temp = raw_temp >> 4;
    // Check if the temperature is negative
    if (raw_temp & 0x800)
    {
        raw_temp = raw_temp - 0x1000; // Negative temperature (two's complement)
    }

    // Temperature in Celsius = raw value * 0.0625
    temperature = raw_temp * 625 / 10000;

    return temperature;
}

static ssize_t tmp102_show_all_data(struct device *dev,
                                    struct device_attribute *attr,
                                    char *buf)
{
    struct tmp102_data *data = dev_get_drvdata(dev);

    u16 raw_data, swapped_raw_data, raw_temp;
    int temp_int, temp_frac;

    mutex_lock(&data->lock);
    raw_data = i2c_smbus_read_word_data(data->client, 0x00);
    mutex_unlock(&data->lock);

    if (raw_data < 0)
    {
        pr_err("i2c: tmp102: Failed to read TMP102 temperature data\n");
        return -EIO;
    }

    swapped_raw_data = ((raw_data << 8) & 0xFF00) | ((raw_data >> 8) & 0x00FF);
    raw_temp = swapped_raw_data >> 4;
    // Check if the temperature is negative
    if (raw_temp & 0x800)
    {
        raw_temp = raw_temp - 0x1000; // Negative temperature (two's complement)
    }

    // Temperature in Celsius = raw value * 0.0625
    temp_int = raw_temp * 625 / 10000; // 625 / 10000 = 0.0625
    temp_frac = raw_temp * 625 % 10000;

    return sprintf(buf,
                   "Raw data        : 0x%x\nSwapped raw data: 0x%x\nRaw temp        : 0x%x\nRaw temp        : %d\nTemp int        : %d\nTemp frac       : %d\nTemp float      : %d.%d\n",
                   raw_data, swapped_raw_data, raw_temp, raw_temp,
                   temp_int, temp_frac, temp_int, temp_frac);
}
static DEVICE_ATTR(all_data, 0444, tmp102_show_all_data, NULL);

static ssize_t tmp102_show_temperature(struct device *dev,
                                       struct device_attribute *attr, char *buf)
{
    struct tmp102_data *data = dev_get_drvdata(dev);
    int temperature = tmp102_read_temperature(data);
    return sprintf(buf, "%d\n", temperature);
}
static DEVICE_ATTR(temperature, 0444, tmp102_show_temperature, NULL);

static int tmp102_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct tmp102_data *data;
    int ret;

    pr_info("i2c: tmp102: device probed\n");

    // Allocate memory for data structure
    data = devm_kzalloc(&client->dev, sizeof(struct tmp102_data), GFP_KERNEL);
    if (!data)
    {
        pr_err("i2c: tmp102: Failed to allocate memory for TMP102 data\n");
        return -ENOMEM;
    }

    data->client = client;
    mutex_init(&data->lock);

    // Register the device with sysfs
    dev_set_drvdata(&client->dev, data);
    ret = device_create_file(&client->dev, &dev_attr_temperature);
    if (ret)
    {
        pr_err("i2c: tmp102: Failed to create temperature attribute in sysfs\n");
        return ret;
    }
    ret = device_create_file(&client->dev, &dev_attr_all_data);
    if (ret)
    {
        pr_err("i2c: tmp102: Failed to create all_data attribute in sysfs\n");
        device_remove_file(&client->dev, &dev_attr_temperature);
        return ret;
    }

    pr_info("i2c: tmp102: sysfs attributes created\n");

    // Register the character device
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0)
    {
        pr_err("i2c: tmp102: Failed to register a major number\n");
        device_remove_file(&client->dev, &dev_attr_temperature);
        device_remove_file(&client->dev, &dev_attr_all_data);
        return major_number;
    }

    // Register the device class
    tmp102_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(tmp102_class))
    {
        unregister_chrdev(major_number, DEVICE_NAME);
        pr_err("i2c: tmp102: Failed to register device class\n");
        return PTR_ERR(tmp102_class);
    }

    // Register the device driver
    tmp102_device = device_create(tmp102_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);
    if (IS_ERR(tmp102_device))
    {
        class_destroy(tmp102_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        pr_err("i2c: tmp102: Failed to create the device\n");
        return PTR_ERR(tmp102_device);
    }

    cdev_init(&tmp102_cdev, &fops);
    tmp102_cdev.owner = THIS_MODULE;
    ret = cdev_add(&tmp102_cdev, MKDEV(major_number, 0), 1);
    if (ret)
    {
        device_destroy(tmp102_class, MKDEV(major_number, 0));
        class_destroy(tmp102_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        pr_err("i2c: tmp102: Failed to add cdev\n");
        return ret;
    }

    pr_info("i2c: tmp102: device registered\n");

    return 0;
}

static int tmp102_remove(struct i2c_client *client)
{
    device_remove_file(&client->dev, &dev_attr_temperature);
    device_remove_file(&client->dev, &dev_attr_all_data);
    pr_info("i2c: tmp102: device removed\n");

    cdev_del(&tmp102_cdev);
    device_destroy(tmp102_class, MKDEV(major_number, 0));
    class_unregister(tmp102_class);
    class_destroy(tmp102_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    pr_info("i2c: tmp102: device unregistered\n");

    return 0;
}

static const struct of_device_id tmp102_of_match[] = {
    {.compatible = "rinvel0,tmp102"},
    {}};
MODULE_DEVICE_TABLE(of, tmp102_of_match);

static const struct i2c_device_id tmp102_id[] = {
    {"tmp102", 0},
    {}};
MODULE_DEVICE_TABLE(i2c, tmp102_id);

static struct i2c_driver tmp102_rinvel0 = {
    .driver = {
        .name = "tmp102",
        .of_match_table = tmp102_of_match,
    },
    .probe = tmp102_probe,
    .remove = tmp102_remove,
    .id_table = tmp102_id,
};
module_i2c_driver(tmp102_rinvel0);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rinvel0");
MODULE_DESCRIPTION("TMP102 Temperature Driver");
