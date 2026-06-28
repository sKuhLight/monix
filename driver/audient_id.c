// SPDX-License-Identifier: GPL-2.0
/*
 * audient_id - companion control driver for Audient iD series interfaces
 *
 * Why this module exists
 * ----------------------
 * The Audient iD control protocol is sent as USB class control requests to the
 * device's USB AudioControl interface (interface 0). On Linux that interface is
 * owned by the kernel's snd-usb-audio driver (together with the AudioStreaming
 * interfaces), and the device firmware only accepts these control requests when
 * interface 0 is claimed exclusively.
 *
 * A userspace tool (via libusb/usbfs) therefore cannot send them while audio is
 * running: the kernel rejects control requests to an interface owned by another
 * driver with -EBUSY, and detaching snd-usb-audio to claim it tears down the
 * whole ALSA sound card. That is the long-standing "no audio while the mixer app
 * is open" problem.
 *
 * In-kernel control requests have no such restriction. This module binds the
 * device's otherwise-unused DFU interface (Application Specific / DFU), which
 * gives it a handle to the usb_device, and exposes a small character device
 * (/dev/audient_idN). Userspace writes a control-request descriptor to it and the
 * module performs the transfer with usb_control_msg_send() on the shared default
 * control pipe. snd-usb-audio keeps interface 0 the entire time, so audio and
 * control run simultaneously with no glitches and no card teardown.
 *
 * The module is a thin, validated relay: it only forwards OUT class requests, so
 * it cannot be used as a general-purpose raw-USB backdoor.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/idr.h>

#define AUDIENT_VID 0x2708

/* USB control transfer recipient/type masks (subset of <linux/usb/ch9.h>) */
#define IDCTL_TYPE_MASK  0x60  /* bmRequestType type field */
#define IDCTL_TYPE_CLASS 0x20
#define IDCTL_DIR_IN     0x80  /* bmRequestType direction bit */

#define IDCTL_MAX_DATA   64
#define IDCTL_TIMEOUT_MS 1000

/*
 * Wire format written by userspace (e.g. MixiD) to /dev/audient_idN.
 * One write == one control transfer. Mirrors the libusb_control_transfer args.
 */
struct idctl_msg {
	__u8  bRequestType;          /* class request; write() requires OUT (e.g. 0x21) */
	__u8  bRequest;              /* e.g. 0x01 SET_CUR, 0x81 GET_CUR */
	__u16 wValue;
	__u16 wIndex;
	__u16 wLength;               /* <= IDCTL_MAX_DATA */
	__u8  data[IDCTL_MAX_DATA];
} __packed;

/*
 * Bidirectional transfer. Unlike write() (OUT only), this also allows IN (read)
 * class requests so userspace can sync the current device state. For an IN
 * request the response bytes are copied back into the same struct's data[].
 */
#define IDCTL_IOC_MAGIC 0xA1
#define IDCTL_IOC_XFER  _IOWR(IDCTL_IOC_MAGIC, 1, struct idctl_msg)

struct idctl_dev {
	struct usb_device   *udev;   /* NULL once disconnected */
	struct miscdevice    misc;
	struct mutex         lock;   /* serialises transfers, guards udev */
	char                 name[32];
	int                  id;
};

static DEFINE_IDA(idctl_ida);

static int idctl_open(struct inode *inode, struct file *file)
{
	/* misc core sets file->private_data to the struct miscdevice */
	struct idctl_dev *dev =
		container_of(file->private_data, struct idctl_dev, misc);
	file->private_data = dev;
	return 0;
}

/*
 * Perform one control transfer. Only class requests are permitted (this is a
 * mixer relay, not a general raw-USB passthrough). Direction follows the IN bit
 * of bRequestType. For IN requests the response is left in msg->data.
 */
static int idctl_do_xfer(struct idctl_dev *dev, struct idctl_msg *msg)
{
	int ret;

	if ((msg->bRequestType & IDCTL_TYPE_MASK) != IDCTL_TYPE_CLASS)
		return -EPERM;
	if (msg->wLength > IDCTL_MAX_DATA)
		return -EINVAL;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	if (!dev->udev) {                 /* device unplugged */
		mutex_unlock(&dev->lock);
		return -ENODEV;
	}

	if (msg->bRequestType & IDCTL_DIR_IN)
		ret = usb_control_msg_recv(dev->udev, 0,
					   msg->bRequest, msg->bRequestType,
					   msg->wValue, msg->wIndex,
					   msg->data, msg->wLength,
					   IDCTL_TIMEOUT_MS, GFP_KERNEL);
	else
		ret = usb_control_msg_send(dev->udev, 0,
					   msg->bRequest, msg->bRequestType,
					   msg->wValue, msg->wIndex,
					   msg->data, msg->wLength,
					   IDCTL_TIMEOUT_MS, GFP_KERNEL);
	mutex_unlock(&dev->lock);

	if (ret)
		dev_dbg(&dev->udev->dev,
			"control transfer failed: %d (type=0x%02x req=0x%02x wValue=0x%04x wIndex=0x%04x)\n",
			ret, msg->bRequestType, msg->bRequest, msg->wValue, msg->wIndex);
	return ret;
}

static ssize_t idctl_write(struct file *file, const char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct idctl_dev *dev = file->private_data;
	struct idctl_msg msg;
	size_t hdr = offsetof(struct idctl_msg, data);
	int ret;

	/* Need at least the header; data beyond wLength is ignored. */
	if (count < hdr || count > sizeof(msg))
		return -EINVAL;
	if (copy_from_user(&msg, buf, count))
		return -EFAULT;

	if (hdr + msg.wLength > count)
		return -EINVAL;
	/* write() is OUT-only; use the IDCTL_IOC_XFER ioctl for reads. */
	if (msg.bRequestType & IDCTL_DIR_IN)
		return -EPERM;

	ret = idctl_do_xfer(dev, &msg);
	return ret ? ret : (ssize_t)count;
}

static long idctl_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct idctl_dev *dev = file->private_data;
	struct idctl_msg msg;
	int ret;

	if (cmd != IDCTL_IOC_XFER)
		return -ENOTTY;
	if (copy_from_user(&msg, (void __user *)arg, sizeof(msg)))
		return -EFAULT;

	ret = idctl_do_xfer(dev, &msg);
	if (ret)
		return ret;

	/* Return the response bytes for IN requests. */
	if ((msg.bRequestType & IDCTL_DIR_IN) &&
	    copy_to_user((void __user *)arg, &msg, sizeof(msg)))
		return -EFAULT;
	return 0;
}

static const struct file_operations idctl_fops = {
	.owner          = THIS_MODULE,
	.open           = idctl_open,
	.write          = idctl_write,
	.unlocked_ioctl = idctl_ioctl,
	.compat_ioctl   = compat_ptr_ioctl,
};

static int idctl_probe(struct usb_interface *intf,
		       const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct idctl_dev *dev;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	mutex_init(&dev->lock);
	dev->udev = usb_get_dev(udev);

	dev->id = ida_alloc(&idctl_ida, GFP_KERNEL);
	if (dev->id < 0) {
		ret = dev->id;
		goto err_put;
	}
	snprintf(dev->name, sizeof(dev->name), "audient_id%d", dev->id);

	dev->misc.minor = MISC_DYNAMIC_MINOR;
	dev->misc.name  = dev->name;
	dev->misc.fops  = &idctl_fops;
	dev->misc.mode  = 0660;        /* udev rule sets the owning group */

	ret = misc_register(&dev->misc);
	if (ret)
		goto err_ida;

	usb_set_intfdata(intf, dev);
	dev_info(&intf->dev,
		 "Audient iD control relay ready at /dev/%s (product 0x%04x)\n",
		 dev->name, le16_to_cpu(udev->descriptor.idProduct));
	return 0;

err_ida:
	ida_free(&idctl_ida, dev->id);
err_put:
	usb_put_dev(dev->udev);
	kfree(dev);
	return ret;
}

static void idctl_disconnect(struct usb_interface *intf)
{
	struct idctl_dev *dev = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);
	misc_deregister(&dev->misc);

	mutex_lock(&dev->lock);
	usb_put_dev(dev->udev);
	dev->udev = NULL;
	mutex_unlock(&dev->lock);

	ida_free(&idctl_ida, dev->id);
	dev_info(&intf->dev, "Audient iD control relay removed (/dev/%s)\n",
		 dev->name);
	kfree(dev);
}

/*
 * Match the DFU (Application Specific / DFU) interface on ANY Audient device.
 * That interface is otherwise unused at runtime (firmware update only), so it is
 * a safe anchor to bind on every iD model regardless of its interface layout.
 */
static const struct usb_device_id idctl_ids[] = {
	{ .match_flags = USB_DEVICE_ID_MATCH_VENDOR |
			 USB_DEVICE_ID_MATCH_INT_CLASS |
			 USB_DEVICE_ID_MATCH_INT_SUBCLASS,
	  .idVendor = AUDIENT_VID,
	  .bInterfaceClass = USB_CLASS_APP_SPEC,   /* 0xFE */
	  .bInterfaceSubClass = 0x01,              /* DFU */
	},
	{ }
};
MODULE_DEVICE_TABLE(usb, idctl_ids);

static struct usb_driver idctl_driver = {
	.name        = "audient_id",
	.probe       = idctl_probe,
	.disconnect  = idctl_disconnect,
	.id_table    = idctl_ids,
};
module_usb_driver(idctl_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MixiD project");
MODULE_DESCRIPTION("Audient iD control relay - lets userspace control the mixer while snd-usb-audio keeps audio running");
MODULE_VERSION("0.1");
