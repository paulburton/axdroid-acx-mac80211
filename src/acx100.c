/* src/acx100.c - main module functions
 *
 * --------------------------------------------------------------------
 *
 * Copyright (C) 2003  ACX100 Open Source Project
 *
 *   The contents of this file are subject to the Mozilla Public
 *   License Version 1.1 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.mozilla.org/MPL/
 *
 *   Software distributed under the License is distributed on an "AS
 *   IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 *   implied. See the License for the specific language governing
 *   rights and limitations under the License.
 *
 *   Alternatively, the contents of this file may be used under the
 *   terms of the GNU Public License version 2 (the "GPL"), in which
 *   case the provisions of the GPL are applicable instead of the
 *   above.  If you wish to allow the use of your version of this file
 *   only under the terms of the GPL and not to allow others to use
 *   your version of this file under the MPL, indicate your decision
 *   by deleting the provisions above and replace them with the notice
 *   and other provisions required by the GPL.  If you do not delete
 *   the provisions above, a recipient may use your version of this
 *   file under either the MPL or the GPL.
 *
 * --------------------------------------------------------------------
 *
 * Inquiries regarding the ACX100 Open Source Project can be
 * made directly to:
 *
 * acx100-users@lists.sf.net
 * http://acx100.sf.net
 *
 * --------------------------------------------------------------------
 * Locking and synchronization (taken from orinoco.c):
 *
 * The basic principle is that everything is serialized through a
 * single spinlock, priv->lock.  The lock is used in user, bh and irq
 * context, so when taken outside hardirq context it should always be
 * taken with interrupts disabled.  The lock protects both the
 * hardware and the struct wlandevice.
 *
 * Another flag, priv->hw_unavailable indicates that the hardware is
 * unavailable for an extended period of time (e.g. suspended, or in
 * the middle of a hard reset).  This flag is protected by the
 * spinlock.  All code which touches the hardware should check the
 * flag after taking the lock, and if it is set, give up on whatever
 * they are doing and drop the lock again.  The acx100_lock()
 * function handles this (it unlocks and returns -EBUSY if
 * hw_unavailable is true). */

/*================================================================*/
/* System Includes */
#ifdef S_SPLINT_S /* some crap that splint needs to not crap out */
#define __signed__ signed
#define __u64 unsigned long long
#define u64 unsigned long long
#define loff_t unsigned long
#define sigval_t unsigned long
#define siginfo_t unsigned long
#define stack_t unsigned long
#define __s64 signed long long
#endif
#include <linux/config.h>
#include <linux/version.h>

#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <linux/wireless.h>
#include <linux/netdevice.h>

#include <wlan_compat.h>

#include <linux/ioport.h>

#include <linux/pci.h>
#include <linux/init.h>

#include <linux/pm.h>

#include <linux/dcache.h>
#include <linux/highmem.h>

/*================================================================*/
/* Project Includes */

#include <version.h>
#include <p80211hdr.h>
#include <p80211mgmt.h>
#include <acx100.h>
#include <acx100_conv.h>
#include <p80211types.h>
#include <acx100_helper.h>
#include <acx100_helper2.h>
#include <idma.h>
#include <ihw.h>
#include <ioregister.h>

/********************************************************************/
/* Module information                                               */
/********************************************************************/

MODULE_AUTHOR("The ACX100 Open Source Driver development team");
MODULE_DESCRIPTION("Driver for TI ACX1xx based wireless cards (CardBus/PCI/USB)");
#ifdef MODULE_LICENSE
MODULE_LICENSE("Dual MPL/GPL");
#endif

/*================================================================*/
/* Local Constants */
#define PCI_TYPE		(PCI_USES_MEM | PCI_ADDR0 | PCI_NO_ACPI_WAKE)
#define PCI_ACX100_REGION1		0x01
#define PCI_ACX100_REGION1_SIZE		0x1000	/* Memory size - 4K bytes */
#define PCI_ACX100_REGION2		0x02
#define PCI_ACX100_REGION2_SIZE   	0x10000 /* Memory size - 64K bytes */

#define PCI_ACX111_REGION1		0x00
#define PCI_ACX111_REGION1_SIZE		0x2000	/* Memory size - 8K bytes */
#define PCI_ACX111_REGION2		0x01
#define PCI_ACX111_REGION2_SIZE   	0x20000 /* Memory size - 128K bytes */

/* Texas Instruments Vendor ID */
#define PCI_VENDOR_ID_TI		0x104c

/* ACX100 22Mb/s WLAN controller */
#define PCI_DEVICE_ID_TI_ACX100		0x8400
#define PCI_DEVICE_ID_TI_ACX100_CB	0x8401

/* ACX111 54Mb/s WLAN controller */
#define PCI_DEVICE_ID_TI_ACX111		0x9066

/* PCI Class & Sub-Class code, Network-'Other controller' */
#define PCI_CLASS_NETWORK_OTHERS 	0x280

/*================================================================*/
/* Local Macros */

/*================================================================*/
/* Local Types */

/*================================================================*/
/* Local Static Definitions */
#define DRIVER_SUFFIX	"_pci"

#define CARD_EEPROM_ID_SIZE 6
#define MAX_IRQLOOPS_PER_JIFFY  (20000/HZ) /* a la orinoco.c */

typedef char *dev_info_t;
static dev_info_t dev_info = "TI acx" DRIVER_SUFFIX;

static char *version = "TI acx" DRIVER_SUFFIX ".o: " WLAN_RELEASE;

#ifdef ACX_DEBUG
int debug = L_BIN|L_ASSOC|L_INIT|L_STD;
int acx100_debug_func_indent = 0;
#endif

int use_eth_name = 0;

char *firmware_dir;

extern const struct iw_handler_def acx100_ioctl_handler_def;

typedef struct device_id {
	unsigned char id[6];
	char *descr;
	char *type;
} device_id_t;

const char *name_acx100 = "ACX100";
const char *name_tnetw1100a = "TNETW1100A";
const char *name_tnetw1100b = "TNETW1100B";

const char *name_acx111 = "ACX111";
const char *name_tnetw1130 = "TNETW1130";

 
/*@-fullinitblock@*/
static const struct pci_device_id acx100_pci_id_tbl[] __devinitdata = {
	{
		.vendor = PCI_VENDOR_ID_TI,
		.device = PCI_DEVICE_ID_TI_ACX100,
		.subvendor = PCI_ANY_ID,
		.subdevice = PCI_ANY_ID,
		.driver_data = CHIPTYPE_ACX100,
	},
	{
		.vendor = PCI_VENDOR_ID_TI,
		.device = PCI_DEVICE_ID_TI_ACX100_CB,
		.subvendor = PCI_ANY_ID,
		.subdevice = PCI_ANY_ID,
		.driver_data = CHIPTYPE_ACX100,
	},
	{
		.vendor = PCI_VENDOR_ID_TI,
		.device = PCI_DEVICE_ID_TI_ACX111,
		.subvendor = PCI_ANY_ID,
		.subdevice = PCI_ANY_ID,
		.driver_data = CHIPTYPE_ACX111,
	},
	{
		.vendor = 0,
		.device = 0,
		.subvendor = 0,
		.subdevice = 0,
		.driver_data = 0,
	}
};
/*@=fullinitblock@*/

MODULE_DEVICE_TABLE(pci, acx100_pci_id_tbl);

static const device_id_t device_ids[] =
{
	/*@-null@*/
	{
		{'G', 'l', 'o', 'b', 'a', 'l'},
		NULL, 
		NULL,
	},
	{
		{(UINT8)0xff, (UINT8)0xff, (UINT8)0xff, (UINT8)0xff, (UINT8)0xff, (UINT8)0xff},
		"uninitialised",
		"SpeedStream SS1021 or Gigafast WF721-AEX"
	},
	{
		{(UINT8)0x80, (UINT8)0x81, (UINT8)0x82, (UINT8)0x83, (UINT8)0x84, (UINT8)0x85},
		"non-standard",
		"DrayTek Vigor 520"
	},
	{
		{'?', '?', '?', '?', '?', '?'},
		"non-standard",
		"Level One WPC-0200"
	},
	{
		{(UINT8)0x00, (UINT8)0x00, (UINT8)0x00, (UINT8)0x00, (UINT8)0x00, (UINT8)0x00},
		"empty",
		"DWL-650+ variant"
	}
	/*@=null@*/
};

static int acx100_probe_pci(struct pci_dev *pdev,
			    const struct pci_device_id *id);
static void acx100_remove_pci(struct pci_dev *pdev);

static int acx100_suspend(struct pci_dev *pdev, u32 state);
static int acx100_resume(struct pci_dev *pdev);
#if THIS_IS_OLD_PM_STUFF_ISNT_IT
static int acx100_pm_callback(struct pm_dev *dev, pm_request_t rqst, void *data);
#endif


/*@-fullinitblock@*/
static struct pci_driver acx100_pci_drv_id = {
	.name        = "acx_pci",
	.id_table    = acx100_pci_id_tbl,
	.probe       = acx100_probe_pci,
	.remove      = __devexit_p(acx100_remove_pci),
	.suspend     = acx100_suspend,
	.resume      = acx100_resume
};
/*@=fullinitblock@*/

typedef struct acx100_device {
	netdevice_t *newest;

} acx100_device_t;

/* if this driver was only about PCI devices, then we probably wouldn't
 * need this linked list.
 * But if we want to register ALL kinds of devices in one global list,
 * then we need it and need to maintain it properly. */
static struct acx100_device root_acx100_dev = {
	.newest        = NULL,
};


static int acx100_start_xmit(struct sk_buff *skb, netdevice_t *dev);
static void acx100_tx_timeout(netdevice_t *dev);
static struct net_device_stats *acx100_get_stats(netdevice_t *dev);
static struct iw_statistics *acx100_get_wireless_stats(netdevice_t *dev);

irqreturn_t acx100_interrupt(int irq, void *dev_id, struct pt_regs *regs);
void acx100_after_interrupt_task(void *data);
static void acx100_set_rx_mode(netdevice_t *dev);
void acx100_rx(struct rxhostdescriptor *rxdesc, wlandevice_t *priv);

static int acx100_open(netdevice_t *dev);
static int acx100_close(netdevice_t *dev);
static void acx100_up(netdevice_t *dev);
static void acx100_down(netdevice_t *dev);

static void acx100_get_firmware_version(wlandevice_t *priv)
{
	fw_ver_t fw;
	UINT8 fw_major = (UINT8)0, fw_minor = (UINT8)0, fw_sub = (UINT8)0, fw_extra = (UINT8)0;

	FN_ENTER;
	
	(void)acx100_interrogate(priv, &fw, ACX1xx_IE_FWREV);
	memcpy(priv->firmware_version, &fw.fw_id, 20);
	if (strncmp((char *)fw.fw_id, "Rev ", 4) != 0)
	{
		acxlog(L_STD|L_INIT, "Huh, strange firmware version string \"%s\" without leading \"Rev \" string detected, please report!\n", fw.fw_id);
		priv->firmware_numver = 0x01090407; /* assume 1.9.4.7 */
		FN_EXIT(0, 0);
		return;
	}
	fw_major = (UINT8)(fw.fw_id[4] - '0');
	fw_minor = (UINT8)(fw.fw_id[6] - '0');
	fw_sub = (UINT8)(fw.fw_id[8] - '0');
	if (strlen((char *)fw.fw_id) >= 11)
	{
		if ((fw.fw_id[10] >= '0') && (fw.fw_id[10] <= '9'))
			fw_extra = (UINT8)(fw.fw_id[10] - '0');
		else
			fw_extra = (UINT8)(fw.fw_id[10] - 'a' + (char)10);
	}
	priv->firmware_numver =
		(UINT32)((fw_major << 24) + (fw_minor << 16) + (fw_sub << 8) + fw_extra);
	acxlog(L_DEBUG, "firmware_numver %08x\n", priv->firmware_numver);

	priv->firmware_id = fw.hw_id;

	/* we're able to find out more detailed chip names now */
	switch (fw.hw_id & 0xffff0000) {
		case 0x01010000:
		case 0x01020000:
			priv->chip_name = name_tnetw1100a;
			break;
		case 0x01030000:
			priv->chip_name = name_tnetw1100b;
			break;
		case 0x03000000:
		case 0x03010000:
			priv->chip_name = name_tnetw1130;
			break;
		default:
			acxlog(0xffff, "unknown chip ID 0x%08x, please report!!\n", fw.hw_id);
			break;
	}

	FN_EXIT(0, 0);
}

/*----------------------------------------------------------------
* acx100_display_hardware_details
*
*
* Arguments:
*	priv: ptr to wlandevice that contains all the details 
*	  displayed by this function
* Returns:
*	void
*
* Side effects:
*	none
* Call context:
*	acx100_probe_pci
* STATUS:
*	stable
* Comment:
*	This function will display strings to the system log according 
* to device form_factor and radio type. It will needed to be 
*----------------------------------------------------------------*/

void acx100_display_hardware_details(wlandevice_t *priv)
{
	char *radio_str, *form_str;

	FN_ENTER;

	switch(priv->radio_type) {
		case RADIO_MAXIM_0D:
			/* hmm, the DWL-650+ seems to have two variants,
			 * according to a windows driver changelog comment:
			 * RFMD and Maxim. */
			radio_str = "Maxim";
			break;
		case RADIO_RFMD_11:
			radio_str = "RFMD";
			break;
		case RADIO_RALINK_15:
			radio_str = "Ralink";
			break;
		case RADIO_RADIA_16:
			radio_str = "Radia";
			break;
		case RADIO_UNKNOWN_17:
			radio_str = "UNKNOWN, used e.g. in ACX111 cards, please report the radio type name!";
			break;
		default:
			radio_str = "UNKNOWN, please report the radio type name!";
			break;
	}

	switch(priv->form_factor) {
		case 0x00:
			form_str = "standard?";
			break;
		case 0x01:
			form_str = "D-Link DWL-520+/650+/G650+/Planet WL-8305?";
			break;
		default:
			form_str = "UNKNOWN, please report!";
			break;
	}

	acxlog(L_STD, "acx100: form factor 0x%02x (%s), radio type 0x%02x (%s), EEPROM version 0x%04x. Uploaded firmware '%s' (0x%08x).\n", priv->form_factor, form_str, priv->radio_type, radio_str, priv->eeprom_version, priv->firmware_version, priv->firmware_id);

	FN_EXIT(0, 0);
}

static inline void acx100_device_chain_add(struct net_device *dev)
{
	wlandevice_t *priv = (struct wlandevice *) dev->priv;

	priv->prev_nd = root_acx100_dev.newest;
	/*@-temptrans@*/
	root_acx100_dev.newest = dev;
	/*@=temptrans@*/
	priv->netdev = dev;
}

void acx100_device_chain_remove(struct net_device *dev)
{
	struct net_device *querydev;
	struct net_device *olderdev;
	struct net_device *newerdev;

	querydev = (struct net_device *) root_acx100_dev.newest;
	newerdev = NULL;
	while (NULL != querydev) {
		olderdev = ((struct wlandevice *) querydev->priv)->prev_nd;
		if (0 == strcmp(querydev->name, dev->name)) {
			if (NULL == newerdev) {
				/* if we were at the beginning of the
				 * list, then it's the list head that
				 * we need to update to point at the
				 * next older device */
				root_acx100_dev.newest = olderdev;
			} else {
				/* it's the device that is newer than us
				 * that we need to update to point at
				 * the device older than us */
				((struct wlandevice *) newerdev->priv)->
				    prev_nd = olderdev;
			}
			break;
		}
		/* "newerdev" is actually the device of the old iteration,
		 * but since the list starts (root_acx100_dev.newest)
		 * with the newest devices,
		 * it's newer than the ones following.
		 * Oh the joys of iterating from newest to oldest :-\ */
		newerdev = querydev;

		/* keep checking old devices for matches until we hit the end
		 * of the list */
		querydev = olderdev;
	}
}

void acx_show_card_eeprom_id(wlandevice_t *priv)
{
	unsigned char buffer[CARD_EEPROM_ID_SIZE];
	UINT16 i;

	memset(&buffer, 0, CARD_EEPROM_ID_SIZE);
	/* use direct eeprom access */
	for (i = 0; i < CARD_EEPROM_ID_SIZE; i++) {
		if (0 == acx100_read_eeprom_offset(priv,
					 (UINT16)(ACX100_EEPROM_ID_OFFSET + i),
					 &buffer[i]))
		{
			acxlog(L_STD, "huh, reading EEPROM failed!?\n");
			break;
		}
	}
	
	for (i = 0; i < (UINT16)(sizeof(device_ids) / sizeof(struct device_id)); i++)
	{
		if (0 == memcmp(&buffer, device_ids[i].id, CARD_EEPROM_ID_SIZE))
		{
			if (NULL != device_ids[i].descr) {
				acxlog(L_STD, "%s: EEPROM card ID string check found %s card ID: this is a %s, no??\n", __func__, device_ids[i].descr, device_ids[i].type);
			}
			break;
		}
	}
	if (i == (UINT16)(sizeof(device_ids) / sizeof(device_id_t)))
	{
		acxlog(L_STD,
	       "%s: EEPROM card ID string check found unknown card: expected \"Global\", got \"%.*s\"! Please report!\n", __func__, CARD_EEPROM_ID_SIZE, buffer);
	}
}

/*----------------------------------------------------------------
* acx100_probe_pci
*
* Probe routine called when a PCI device w/ matching ID is found.
* Here's the sequence:
*   - Allocate the PCI resources.
*   - Read the PCMCIA attribute memory to make sure we have a WLAN card
*   - Reset the MAC
*   - Initialize the dev and wlan data
*   - Initialize the MAC
*
* Arguments:
*	pdev		ptr to pci device structure containing info about
*			pci configuration.
*	id		ptr to the device id entry that matched this device.
*
* Returns:
*	zero		- success
*	negative	- failed
*
* Side effects:
*
*
* Call context:
*	process thread
*
* STATUS: should be pretty much ok. UNVERIFIED.
*
* Comment: The function was rewritten according to the V3 driver.
*	The debugging information from V1 is left even if
*	absent from V3.
----------------------------------------------------------------*/
static int __devinit
acx100_probe_pci(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int result = -EIO;
	int err;

	UINT16 chip_type;

	const char *chip_name;
	unsigned long mem_region1 = 0;
	unsigned long mem_region1_size;
	unsigned long mem_region2 = 0;
	unsigned long mem_region2_size;

	unsigned long phymem1;
	unsigned long phymem2;
	void *mem1 = NULL;
	void *mem2 = NULL;

	wlandevice_t *priv = NULL;
	struct net_device *dev = NULL;

	char *devname_mask;
	UINT32 hardware_info;
	char procbuf[80];

	FN_ENTER;

	/* FIXME: flag device somehow to make sure ioctls don't have access
	 * to uninitialized structures before init is finished */

	/* Enable the PCI device */

	if (0 != pci_enable_device(pdev)) {
		acxlog(L_BINSTD | L_INIT,
		       "%s: %s: pci_enable_device() failed\n",
		       __func__, dev_info);
		result = -ENODEV;
		goto fail;
	}

	/* enable busmastering (required for CardBus) */
	pci_set_master(pdev);

	/* acx100 and acx111 have different pci memory regions */
	chip_type = (UINT16)id->driver_data;
	if (chip_type == CHIPTYPE_ACX100) {
		chip_name = name_acx100;
		mem_region1 = PCI_ACX100_REGION1;
		mem_region1_size  = PCI_ACX100_REGION1_SIZE;

		mem_region2 = PCI_ACX100_REGION2;
		mem_region2_size  = PCI_ACX100_REGION2_SIZE;
	} else if (chip_type == CHIPTYPE_ACX111) {
		acxlog(L_BINSTD, "%s: WARNING: ACX111 support is highly experimental!\n", __func__);

		chip_name = name_acx111;
		mem_region1 = PCI_ACX111_REGION1;
		mem_region1_size  = PCI_ACX111_REGION1_SIZE;

		mem_region2 = PCI_ACX111_REGION2;
		mem_region2_size  = PCI_ACX111_REGION2_SIZE;
	} else {
		acxlog(L_BINSTD, "%s: unknown or bad chip??\n", __func__);
		result = -EIO;
		goto fail;
	}

	/* Figure out our resources */
	phymem1 = pci_resource_start(pdev, mem_region1);
	phymem2 = pci_resource_start(pdev, mem_region2);

	if (!request_mem_region
	    (phymem1, pci_resource_len(pdev, mem_region1), "Acx100_1")) {
		acxlog(L_BINSTD | L_INIT,
		       "%s: acx100: Cannot reserve PCI memory region 1 (or also: are you sure you have CardBus support in kernel?)\n", __func__);
		result = -EIO;
		goto fail;
	}

	if (!request_mem_region
	    (phymem2, pci_resource_len(pdev, mem_region2), "Acx100_2")) {
		acxlog(L_BINSTD | L_INIT,
		       "%s: acx100: Cannot reserve PCI memory region 2\n", __func__);
		result = -EIO;
		goto fail;
	}

	mem1 = ioremap(phymem1, mem_region1_size);
	if (NULL == mem1) {
		acxlog(L_BINSTD | L_INIT,
		       "%s: %s: ioremap() failed.\n",
		       __func__, dev_info);
		result = -EIO;
		goto fail;
	}

	mem2 = ioremap(phymem2, mem_region2_size);
	if (NULL == mem2) {
		acxlog(L_BINSTD | L_INIT,
		       "%s: %s: ioremap() failed.\n",
		       __func__, dev_info);
		result = -EIO;
		goto fail;
	}

	/* Log the device */
	acxlog(L_STD | L_INIT,
	       "Found %s-based wireless network card at %s, irq:%d, phymem1:0x%lx, phymem2:0x%lx, mem1:0x%p, mem1_size:%ld, mem2:0x%p, mem2_size:%ld.\n",
	       chip_name, (char *)pdev->slot_name /* was: pci_name(pdev) */, pdev->irq, phymem1, phymem2,
	       mem1, mem_region1_size,
	       mem2, mem_region2_size);
	acxlog(0xffff, "initial debug setting is 0x%04x\n", debug);

	if (0 == pdev->irq) {
		acxlog(L_BINSTD | L_IRQ | L_INIT, "%s: %s: Can't get IRQ %d\n",
		       __func__, dev_info, 0);
		result = -EIO;
		goto fail;
	}

	acxlog(L_DEBUG, "Allocating %d, %Xh bytes for wlandevice_t\n",sizeof(wlandevice_t),sizeof(wlandevice_t));
	if (NULL == (priv = kmalloc(sizeof(wlandevice_t), GFP_KERNEL))) {
		acxlog(L_BINSTD | L_INIT,
		       "%s: %s: Memory allocation failure\n",
		       __func__, dev_info);
		result = -EIO;
		goto fail;
	}

	memset(priv, 0, sizeof(wlandevice_t));

	priv->chip_type = chip_type;
	priv->chip_name = chip_name;
	if (PCI_HEADER_TYPE_CARDBUS == (int)pdev->hdr_type) {
		priv->bus_type = (UINT8)ACX_CARDBUS;
	} else
	if (PCI_HEADER_TYPE_NORMAL == (int)pdev->hdr_type) {
		priv->bus_type = (UINT8)ACX_PCI;
	} else {
		acxlog(L_STD, "ERROR: card has unknown bus type!!\n");
	}

	/* set the correct io resource list for the active chip */
	if (CHIPTYPE_ACX100 == chip_type) {
		priv->io = acx100_get_io_register_array();
	} else if (CHIPTYPE_ACX111 == chip_type) {
		priv->io = acx111_get_io_register_array();
	}
	if (NULL == priv->io) {
		acxlog(L_BINSTD, "%s: error getting io mappings.\n", __func__);
		result = -EIO;
		goto fail;
	}
	acxlog(L_BINSTD, "%s: using %s io resource addresses (size: %d)\n", __func__, priv->chip_name, IO_INDICES_SIZE);

	spin_lock_init(&priv->lock);

	acxlog(L_INIT, "hw_unavailable = 1\n");
	priv->hw_unavailable = 1;
	priv->dev_state_mask &= ~ACX_STATE_IFACE_UP;

	priv->membase = phymem1;
	priv->iobase = mem1;

	priv->membase2 = phymem2;
	priv->iobase2 = mem2;

	priv->mgmt_timer.function = (void *)0x0000dead; /* to find crashes due to weird driver access to unconfigured interface (ifup) */

	acx_show_card_eeprom_id(priv);

	if (NULL == (dev = kmalloc(sizeof(netdevice_t), GFP_KERNEL))) {
		acxlog(L_BINSTD | L_INIT,
		       "%s: Failed to alloc netdev\n", __func__);
		result = -EIO;
		goto fail;
	}

	memset(dev, 0, sizeof(netdevice_t));
	ether_setup(dev);

#if QUEUE_OPEN_AFTER_ASSOC
	/* now we have our device, so make sure the kernel doesn't try
	 * to send packets even though we're not associated to a network yet */
	acxlog(L_BUF, "stop queue after setup.\n");
	netif_stop_queue(dev);
#endif

	dev->priv = priv;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 70)
	/* this define and its netdev member exist since 2.5.70 */
	SET_NETDEV_DEV(dev, &pdev->dev);
#endif

	/* register new dev in linked list */
	acx100_device_chain_add(dev);

	acxlog(L_BINSTD | L_IRQ | L_INIT,
		       "%s: %s: Using IRQ %d\n",
		       __func__, dev_info, pdev->irq);

	dev->irq = pdev->irq;
	dev->base_addr = pci_resource_start(pdev, 0); /* TODO this is maybe incompatible to ACX111 */

	/* need to be able to restore PCI state after a suspend */
	pci_save_state(pdev, priv->pci_state);

	if (0 == acx100_reset_dev(dev)) {
		acxlog(L_BINSTD | L_INIT,
		       "%s: %s: MAC initialize failure!\n",
		       __func__, dev_info);
		result = -EIO;
		goto fail;
	}

	devname_mask = (1 == use_eth_name) ? "eth%d" : "wlan%d";
	if (dev_alloc_name(dev, devname_mask) < 0)
	{
		result = -EIO;
		goto fail;
	}
	acxlog(L_STD, "acx100: allocated net device %s, driver compiled against wireless extensions v%d and Linux %s\n", dev->name, WIRELESS_EXT, UTS_RELEASE);

	/* now that device init was successful, fill remaining fields... */
	dev->open = &acx100_open;
	dev->stop = &acx100_close;
	dev->hard_start_xmit = &acx100_start_xmit;
	/* FIXME: no handler for dev->hard_reset! could reset CPU,
	 * reinit packet templates etc. */
	dev->get_stats = &acx100_get_stats;
	dev->get_wireless_stats = &acx100_get_wireless_stats;
#if WIRELESS_EXT >= 13
	dev->wireless_handlers = (struct iw_handler_def *)&acx100_ioctl_handler_def;
#else
	dev->do_ioctl = &acx_ioctl_old;
#endif
	dev->set_multicast_list = &acx100_set_rx_mode;
	dev->tx_timeout = &acx100_tx_timeout;
	dev->watchdog_timeo = 4 * HZ;	/* 400 */

	/* ok, basic setup is finished, now start initialising the card */

	hardware_info = acx100_read_reg16(priv, priv->io[IO_ACX_EEPROM_INFORMATION]);
	priv->form_factor = (UINT8)(hardware_info & 0xff);
	priv->radio_type = (UINT8)(hardware_info >> 8 & 0xff);
/*	priv->eeprom_version = hardware_info >> 16; */
	if (0 == acx100_read_eeprom_offset(priv, 0x05, &priv->eeprom_version)) {
		result = -EIO;
		goto fail;
	}

	if (0 != acx100_init_mac(dev, 1)) {
		acxlog(L_DEBUG | L_INIT,
		       "Danger Will Robinson, MAC did not come back\n");
		result = -EIO;
		goto fail;
	}

	/* card initialized, so let's release hardware */
	acxlog(L_INIT, "hw_unavailable--\n");
	priv->hw_unavailable--;

	/* needs to be after acx100_init_mac() due to necessary init stuff */
	acx100_get_firmware_version(priv);

	acx100_display_hardware_details(priv);

	pci_set_drvdata(pdev, dev);

	/* ...and register the card, AFTER everything else has been set up,
	 * since otherwise an ioctl could step on our feet due to
	 * firmware operations happening in parallel or uninitialized data */
	if (0 != (err = register_netdev(dev))) {
		acxlog(L_BINSTD | L_INIT,
		       "%s: %s: Register net device of %s failed: %d\n\n",
		       __func__, dev_info, dev->name, err);
		result = -EIO;
		goto fail;
	}

#if THIS_IS_OLD_PM_STUFF_ISNT_IT
	priv->pm = pm_register(PM_PCI_DEV,PM_PCI_ID(pdev),
			&acx100_pm_callback);
#endif

	sprintf(procbuf, "driver/acx_%s", dev->name);
	acxlog(L_INIT, "creating /proc entry %s\n", procbuf);
        (void)create_proc_read_entry(procbuf, 0, 0, acx100_read_proc, priv);

	sprintf(procbuf, "driver/acx_%s_diag", dev->name);
	acxlog(L_INIT, "creating /proc entry %s\n", procbuf);
        (void)create_proc_read_entry(procbuf, 0, 0, acx100_read_proc_diag, priv);
	sprintf(procbuf, "driver/acx_%s_eeprom", dev->name);
	acxlog(L_INIT, "creating /proc entry %s\n", procbuf);
        (void)create_proc_read_entry(procbuf, 0, 0, acx100_read_proc_eeprom, priv);
	sprintf(procbuf, "driver/acx_%s_phy", dev->name);
	acxlog(L_INIT, "creating /proc entry %s\n", procbuf);
        (void)create_proc_read_entry(procbuf, 0, 0, acx100_read_proc_phy, priv);

	acxlog(L_STD|L_INIT, "%s: %s Loaded Successfully\n", __func__, version);
	result = 0;
	goto done;

/* fail_registered:
	unregister_netdev(dev); */
fail:
	acxlog(L_STD|L_INIT, "%s: %s Loading FAILED\n", __func__, version);

	/* FIXME: that stuff from here on should be merged with remove_pci */

	/* remove new failed dev from linked list */
	acx100_device_chain_remove(dev);

	if (NULL != dev)
	{
		pci_set_drvdata(pdev, NULL);
		kfree(dev);
	}

	if (NULL != priv)
	{
		/* don't free priv->io here, this *global* resource
		 * will be freed on module unload */
#if THIS_IS_OLD_PM_STUFF_ISNT_IT
		if (NULL != priv->pm)
			pm_unregister(priv->pm);
#endif
		kfree(priv);
	}
	
	if (0 != mem1)
		iounmap((void *) mem1);
	if (0 != mem2)
		iounmap((void *) mem2);

	release_mem_region(pci_resource_start(pdev, mem_region1),
			   pci_resource_len(pdev, mem_region1));

	release_mem_region(pci_resource_start(pdev, mem_region2),
			   pci_resource_len(pdev, mem_region2));

	pci_set_drvdata(pdev, NULL); /* to be sure nothing is left */

	pci_disable_device(pdev);

done:
	FN_EXIT(1, result);
	return result;
} /* acx100_probe_pci() */


/*----------------------------------------------------------------
* acx100_remove_pci
*
* Deallocate PCI resources for the ACX100 chip.
*
* This should NOT execute any other hardware operations on the card,
* since the card might already be ejected. Instead, that should be done
* in cleanup_module, since the card is most likely still available there.
*
* Arguments:
*	pdev		ptr to pci device structure containing info about
*			pci configuration.
*
* Returns:
*	nothing
*
* Side effects:
*
* Call context:
*	process thread
*
* STATUS: should be pretty much ok. UNVERIFIED.
*
* Comment: The function was rewritten according to the V3 driver.
----------------------------------------------------------------*/
void __devexit acx100_remove_pci(struct pci_dev *pdev)
{
	struct net_device *dev;
	wlandevice_t *priv;
	UINT16 chip_type;

	FN_ENTER;

	dev = (struct net_device *) pci_get_drvdata(pdev);
	priv = (struct wlandevice *) dev->priv;
	
	if(dev == NULL || priv == NULL) {
		acxlog(L_STD, "%s: card not used. Skipping any release code\n", __func__);
		goto end;
	}	

	/* unregister the device to not let the kernel
	 * (e.g. ioctls) access a half-deconfigured device */
	acxlog(L_INIT, "Removing device %s!\n", dev->name);
	netif_device_detach(dev);
	unregister_netdev(dev);

#if THIS_IS_OLD_PM_STUFF_ISNT_IT
	pm_unregister(priv->pm);
#endif

	/* find our PCI device in the global acx100 list and remove it */
	acx100_device_chain_remove(dev);

	if (0 != (priv->dev_state_mask & ACX_STATE_IFACE_UP))
		acx100_down(dev);

	priv->dev_state_mask &= ~ACX_STATE_IFACE_UP;

	acxlog(L_STD, "hw_unavailable++\n");
	priv->hw_unavailable++;

#if NEWER_KERNELS_ONLY
	free_netdev(dev);
#else
	if (dev)
		kfree(dev);
#endif
	pci_set_drvdata(pdev, NULL);

	acx100_delete_dma_regions(priv);

	iounmap((void *) priv->iobase);
	iounmap((void *) priv->iobase2);

	chip_type = priv->chip_type;
	
	/* don't free priv->io here, this *global* resource
	 * will be freed on module unload */
	kfree(priv);

	/* finally, clean up PCI bus state */

	if(chip_type == CHIPTYPE_ACX100) {

		release_mem_region(pci_resource_start(pdev, PCI_ACX100_REGION1),
				   pci_resource_len(pdev, PCI_ACX100_REGION1));

		release_mem_region(pci_resource_start(pdev, PCI_ACX100_REGION2),
				   pci_resource_len(pdev, PCI_ACX100_REGION2));
	}
	if(chip_type == CHIPTYPE_ACX111) {

		release_mem_region(pci_resource_start(pdev, PCI_ACX111_REGION1),
				   pci_resource_len(pdev, PCI_ACX111_REGION1));

		release_mem_region(pci_resource_start(pdev, PCI_ACX111_REGION2),
				   pci_resource_len(pdev, PCI_ACX111_REGION2));
	}

	/* put device into ACPI D3 mode (shutdown) */
	pci_set_power_state(pdev, 3);

	end:
	FN_EXIT(0, 0);
}


static int if_was_up = 0;
static int acx100_suspend(struct pci_dev *pdev, /*@unused@*/ u32 state)
{
	/*@unused@*/ struct net_device *dev = pci_get_drvdata(pdev);
	wlandevice_t *priv = dev->priv;

	FN_ENTER;
	acxlog(L_STD, "acx100: experimental suspend handler called for %p!\n", (void *)priv);
	if (0 != netif_device_present(dev)) {
		if_was_up = 1;
		acx100_down(dev);
	}
	else
		if_was_up = 0;
	netif_device_detach(dev);
	acx100_delete_dma_regions(priv);

	FN_EXIT(0, 0);
	return 0;
}

static int acx100_resume(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	/*@unused@*/ wlandevice_t *priv = dev->priv;

	FN_ENTER;
	acxlog(L_STD, "acx100: experimental resume handler called for %p!\n", (void *)priv);
	pci_set_power_state(pdev, 0);
	pci_restore_state(pdev, priv->pci_state);

	acx100_reset_dev(dev);

	acx100_init_mac(dev, 0);
	
	if (1 == if_was_up)
		acx100_up(dev);
	
	/* now even reload all card parameters as they were before suspend,
	 * and possibly be back in the network again already :-) */
	acx100_update_card_settings(priv, 0, 0, 1);
	netif_device_attach(dev);
	
	FN_EXIT(0, 0);
	return 0;
}

#if THIS_IS_OLD_PM_STUFF_ISNT_IT
static int acx100_pm_callback(struct pm_dev *pmdev, pm_request_t rqst, void *data)
{
	int result = 0;
	netdevice_t *dev = root_acx100_dev.newest;
	wlandevice_t *priv = (wlandevice_t*)dev->priv;
	client_t client;

	FN_ENTER;
	
	switch(rqst)
	{
		case PM_SUSPEND: /* OK, we got a suspend request */
			if (netif_running(dev) && netif_device_present(dev))
				netif_device_detach(dev);
			
			/* Cancel our association */
			if (acx100_transmit_disassoc(&client, priv) == 1) {
				result = -EINVAL;
			}
			/* better wait for some time to make sure Tx is
			 * finished */
			current->state = TASK_UNINTERRUPTIBLE;
			schedule_timeout(HZ / 4);

			/* Then we set our flag */
			acx100_set_status(priv, ISTATUS_0_STARTED);

			/* Close off IRQs */
			acx100_disable_irq(priv);

			/* Disable the Rx/Tx queues */
			acx100_issue_cmd(priv, ACX100_CMD_DISABLE_TX, NULL, 0, 5000);
			acx100_issue_cmd(priv, ACX100_CMD_DISABLE_RX, NULL, 0, 5000);
/*			acx100_issue_cmd(priv, ACX100_CMD_FLUSH_QUEUE, NULL, 0, 5000); */

			/* disable power LED to save power */
			acxlog(L_INIT, "switching off power LED to save power. :-)\n");
			acx100_power_led(priv, 0);
	
			printk("Asked to suspend: %X\n", rqst);
			/* Now shut off everything else */
			if (0 == acx100_issue_cmd(priv, ACX100_CMD_SLEEP, NULL, 0, 5000))
			{
				result = -EBUSY;
			} else
			{
				result = 0;
				break;
			}
		case PM_RESUME:
			pm_access(priv->pm);
#ifndef RESUME_STANDBY_ONLY
			/* not sure whether we actually had our power
			 * removed or not, so let's do a full reset! */
			acx100_reset_dev(dev);
#else
			acx100_issue_cmd(priv, ACX100_CMD_WAKE, NULL, 0, 5000);

			acx100_issue_cmd(priv, ACX100_CMD_ENABLE_TX, NULL, 0, 5000);
			acx100_issue_cmd(priv, ACX100_CMD_ENABLE_RX, NULL, 0, 5000);
#endif

			acx100_enable_irq(priv);
/*			acx100_join_bssid(priv); */
			acx100_set_status(priv, ISTATUS_0_STARTED);
			printk("Asked to resume: %X\n", rqst);
			if (netif_running(dev) && !netif_device_present(dev))
				netif_device_attach(dev);
			break;
		default:
			printk("Asked for PM: %X\n", rqst);
			result = -EINVAL;
			break;
	}
	FN_EXIT(1, result);
	return result;
}
#endif

/*----------------------------------------------------------------
* acx100_up
*
*
* Arguments:
*	dev: netdevice structure that contains the private priv
* Returns:
*	void
* Side effects:
*	- Enables on-card interrupt requests
*	- calls acx100_start
* Call context:
*	- process thread
* STATUS:
*	stable
* Comment:
*	This function is called by acx100_open (when ifconfig sets the
*	device as up).
*----------------------------------------------------------------*/
static void acx100_up(netdevice_t *dev)
{
	wlandevice_t *priv = (wlandevice_t *) dev->priv;

	FN_ENTER;

	acx100_enable_irq(priv);
	if ((priv->firmware_numver >= 0x0109030e) || (priv->chip_type == CHIPTYPE_ACX111) ) /* FIXME: first version? */ {
		/* newer firmware versions don't use a hardware timer any more */
		acxlog(L_INIT, "firmware version >= 1.9.3.e --> using software timer\n");
		init_timer(&priv->mgmt_timer);
		priv->mgmt_timer.function = acx100_timer;
		priv->mgmt_timer.data = (unsigned long)priv;
	}
	else {
		acxlog(L_INIT, "firmware version < 1.9.3.e --> using hardware timer\n");
	}
	acx100_start(priv);
	
	acxlog(L_BUF, "start queue on startup.\n");
	netif_start_queue(dev);

	FN_EXIT(0, 0);
}

/*----------------------------------------------------------------
* acx100_down
*
*
* Arguments:
*	dev: netdevice structure that contains the private priv
* Returns:
*	void
* Side effects:
*	- disables on-card interrupt request
* Call context:
*	process thread
* STATUS:
*	stable
* Comment:
*	this disables the netdevice
*----------------------------------------------------------------*/
static void acx100_down(netdevice_t *dev)
{
	wlandevice_t *priv = (wlandevice_t *) dev->priv;

	FN_ENTER;

	acxlog(L_BUF, "stop queue during close.\n");
	netif_stop_queue(dev);
	acx100_set_status(priv, ISTATUS_0_STARTED);

	if ((priv->firmware_numver >= 0x0109030e) || (priv->chip_type == CHIPTYPE_ACX111)) /* FIXME: first version? */
	{ /* newer firmware versions don't use a hardware timer any more */
		del_timer_sync(&priv->mgmt_timer);
	}
	acx100_disable_irq(priv);
	FN_EXIT(0, 0);
}


/*----------------------------------------------------------------
* acx100_open
*
* WLAN device open method.  Called from p80211netdev when kernel
* device open (start) method is called in response to the
* SIOCSIFFLAGS ioctl changing the flags bit IFF_UP
* from clear to set.
*
* Arguments:
*	dev		network device structure
*
* Returns:
*	0	success
*	>0	f/w reported error
*	<0	driver reported error
*
* Side effects:
*
* Call context:
*	process thread
* STATUS: should be ok.
----------------------------------------------------------------*/

static int acx100_open(netdevice_t *dev)
{
	wlandevice_t *priv = (wlandevice_t *) dev->priv;
	int result;
	unsigned long flags;

	FN_ENTER;

	acxlog(L_STD, "OPENING DEVICE\n");
	if (0 != acx100_lock(priv, &flags)) {
		acxlog(L_INIT, "card unavailable, bailing\n");
		result = -ENODEV;
		goto done;
	}

	/* configure task scheduler */
#ifdef USE_QUEUE_TASKS
	priv->after_interrupt_task.routine = acx100_after_interrupt_task;
	priv->after_interrupt_task.data = priv;
#else
	INIT_WORK(&priv->after_interrupt_task, acx100_after_interrupt_task, priv);
#endif

	/* request shared IRQ handler */
	if (0 != request_irq(dev->irq, acx100_interrupt, SA_SHIRQ, dev->name, dev)) {
		acxlog(L_BINSTD | L_INIT | L_IRQ, "request_irq failed\n");
		result = -EAGAIN;
		goto done;
	}
	acxlog(L_DEBUG | L_IRQ, "%s: request_irq %d successful\n", __func__, dev->irq);
	acx100_up(dev);

	priv->dev_state_mask |= ACX_STATE_IFACE_UP;

	acxlog(L_INIT, "module count ++\n");
	WLAN_MOD_INC_USE_COUNT;
	result = 0;

	/* We don't currently have to do anything else.
	 * The setup of the MAC should be subsequently completed via
	 * the mlme commands.
	 * Higher layers know we're ready from dev->start==1 and
	 * dev->tbusy==0.  Our rx path knows to pass up received/
	 * frames because of dev->flags&IFF_UP is true.
	 */
done:
	acx100_unlock(priv, &flags);
	FN_EXIT(1, result);
	return result;
}

/*----------------------------------------------------------------
* acx100_close
*
* WLAN device close method.  Called from p80211netdev when kernel
* device close method is called in response to the
* SIOCSIIFFLAGS ioctl changing the flags bit IFF_UP
* from set to clear.
* (i.e. called for "ifconfig DEV down")
*
* Arguments:
*	dev		network device structure
*
* Returns:
*	0	success
*	>0	f/w reported error
*	<0	driver reported error
*
* Side effects:
*
* Call context:
*	process thread
*
* STATUS: Should be pretty much perfect now.
----------------------------------------------------------------*/

static int acx100_close(netdevice_t *dev)
{
	wlandevice_t *priv = (wlandevice_t *) dev->priv;

	FN_ENTER;

	/* don't use acx100_lock() here: need to close card even in case
	 * of hw_unavailable set (card ejected) */
#ifdef BROKEN_LOCKING
	spin_lock_irq(&priv->lock);
#endif

	priv->dev_state_mask &= ~ACX_STATE_IFACE_UP;

	if (0 != netif_device_present(dev)) {
		acx100_down(dev);
	}

	free_irq(dev->irq, dev);
	/* priv->val0x240c = 0; */

	/* We currently don't have to do anything else.
	 * Higher layers know we're not ready from dev->start==0 and
	 * dev->tbusy==1.  Our rx path knows to not pass up received
	 * frames because of dev->flags&IFF_UP is false.
	 */

	acxlog(L_INIT, "module count --\n");
	WLAN_MOD_DEC_USE_COUNT;
#ifdef BROKEN_LOCKING
	spin_unlock_irq(&priv->lock);
#endif

	FN_EXIT(0, 0);
	return 0;
}
/*----------------------------------------------------------------
* acx100_start_xmit
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

static int acx100_start_xmit(struct sk_buff *skb, netdevice_t *dev)
{
	int txresult = 0;
	unsigned long flags;
	wlandevice_t *priv = (wlandevice_t *) dev->priv;
	struct txdescriptor *tx_desc;
	unsigned int templen;

	FN_ENTER;

	if (unlikely(!skb)) {
		return 0;
	}
	if (unlikely(!priv)) {
		return 1;
	}
	if (unlikely(0 == (priv->dev_state_mask & ACX_STATE_IFACE_UP))) {
		return 1;
	}

	if (unlikely(0 != acx100_lock(priv, &flags)))
		return 1;

	if (0 != netif_queue_stopped(dev)) {
		acxlog(L_BINSTD, "%s: called when queue stopped\n", __func__);
		txresult = 1;
		goto fail;
	}

	if (ISTATUS_4_ASSOCIATED != priv->status) {
		acxlog(L_XFER, "Trying to xmit, but not associated yet: aborting...\n");
		/* silently drop the packet, since we're not connected yet */
		dev_kfree_skb(skb);
		priv->stats.tx_errors++;
		txresult = 0;
		goto fail;
	}

#if 0
	/* we're going to transmit now, so stop another packet from entering.
	 * FIXME: most likely we shouldn't do it like that, but instead:
	 * stop the queue during card init, then wake the queue once
	 * we're associated to the network, then stop the queue whenever
	 * we don't have any free Tx buffers left, and wake it again once a
	 * Tx buffer becomes free again. And of course also stop the
	 * queue once we lose association to the network (since it
	 * doesn't make sense to allow more user packets if we can't
	 * forward them to a network).
	 * FIXME: Hmm, seems this is all wrong. We SHOULD leave the
	 * queue open from the beginning (as long as we're not full,
	 * and also even before we're even associated),
	 * otherwise we'll get NETDEV WATCHDOG transmit timeouts... */
	acxlog(L_BUF, "stop queue during Tx.\n");
	netif_stop_queue(dev);
#endif
#if UNUSED
	if (acx100_lock(priv,&flags)) ...

	memset(pb, 0, sizeof(wlan_pb_t) /*0x14*4 */ );

	pb->ethhostbuf = skb;
	pb->ethbuf = skb->data;
#endif

	templen = skb->len;
/*	if (templen > ETH_FRAME_LEN) {
		templen = ETH_FRAME_LEN;
	}
	pb->ethbuflen = templen;
	pb->ethfrmlen = templen;

	pb->eth_hdr = (wlan_ethhdr_t *) pb->ethbuf;
 */
	if (unlikely((tx_desc = acx100_get_tx_desc(priv)) == NULL)) {
		acxlog(L_BINSTD,"BUG: txdesc ring full\n");
		txresult = 1;
		goto fail;
	}

	acx100_ether_to_txdesc(priv, tx_desc, skb);
	acx100_dma_tx_data(priv, tx_desc);
	dev->trans_start = jiffies;

	dev_kfree_skb(skb);

/*	if (txresult == 1){
		return 1;
	} */

	/* tx_desc = &priv->dc.pTxDescQPool[priv->dc.pool_idx]; */
#if 0
	/* if((tx_desc->Ctl & 0x80) != 0){ */
		acxlog(L_BUF, "wake queue after Tx start.\n");
		netif_wake_queue(dev);
	/* } */
#endif
	priv->stats.tx_packets++;
	priv->stats.tx_bytes += templen;

fail:
	acx100_unlock(priv, &flags);

	FN_EXIT(1, txresult);
	return txresult;
}
/*----------------------------------------------------------------
* acx100_tx_timeout
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

/* acx100_tx_timeout()
 * STATUS: ok.
 */
static void acx100_tx_timeout(netdevice_t *dev)
{
	wlandevice_t *priv = (wlandevice_t *)dev->priv;
/*	char tmp[4]; */
/*	dev->trans_start = jiffies;
	netif_wake_queue(dev);
*/

	FN_ENTER;
	/* clean tx descs, they may have been completely full */
	acx100_clean_tx_desc(priv);
	priv->stats.tx_errors++;
	printk("acx100: Tx timeout!\n");
	FN_EXIT(0, 0);
}

/*----------------------------------------------------------------
* acx100_get_stats
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

/* acx100_get_stats()
 * STATUS: should be ok..
 */
static struct net_device_stats *acx100_get_stats(netdevice_t *dev)
{
	wlandevice_t *priv = (wlandevice_t *)dev->priv;
#if ANNOYING_GETS_CALLED_TOO_OFTEN
	FN_ENTER;
	FN_EXIT(1, (int)&priv->stats);
#endif
	return &priv->stats;
}

/*----------------------------------------------------------------
* acx100_get_wireless_stats
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

static struct iw_statistics *acx100_get_wireless_stats(netdevice_t *dev)
{
	wlandevice_t *priv = (wlandevice_t *)dev->priv;
	FN_ENTER;
	FN_EXIT(1, (int)&priv->wstats);
	return &priv->wstats;
}

/*----------------------------------------------------------------
* acx100_set_rx_mode
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

static void acx100_set_rx_mode(/*@unused@*/ netdevice_t *netdev)
{
	FN_ENTER;
	FN_EXIT(0, 0);
}

/*----------------------------------------------------------------
* acx100_enable_irq
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

void acx100_enable_irq(wlandevice_t *priv)
{
	FN_ENTER;
#if (WLAN_HOSTIF!=WLAN_USB)
	acx100_write_reg16(priv, priv->io[IO_ACX_IRQ_MASK], priv->irq_mask);
	acx100_write_reg16(priv, priv->io[IO_ACX_FEMR], 0x8000);
	priv->irqs_active = 1;
#endif
	FN_EXIT(0, 0);
}

/*----------------------------------------------------------------
* acx100_disable_irq
*
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

void acx100_disable_irq(wlandevice_t *priv)
{
	FN_ENTER;
#if (WLAN_HOSTIF!=WLAN_USB)
	acx100_write_reg16(priv, priv->io[IO_ACX_IRQ_MASK], priv->irq_mask_off);
	acx100_write_reg16(priv, priv->io[IO_ACX_FEMR], 0x0);
	priv->irqs_active = 0;
#endif
	FN_EXIT(0, 0);
}

void acx100_handle_info_irq(wlandevice_t *priv)
{
	acx100_get_info_state(priv);
	acxlog(L_STD | L_IRQ, "Got Info IRQ: type 0x%04x, status 0x%04x\n", priv->info_type, priv->info_status);

	priv->info_status = 0x0001; /* we received it */
	acx100_write_reg16(priv, priv->io[IO_ACX_INT_TRIG], 0x2); /* notify ACX1xx */
}

/*----------------------------------------------------------------
* acx100_interrupt
* 
* Never call a schedule or sleep in this method. Result will be a Kernel Panic.
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/

irqreturn_t acx100_interrupt(/*@unused@*/ int irq, void *dev_id, /*@unused@*/ struct pt_regs *regs)
{
	wlandevice_t *priv;
	UINT16 irqtype;
	int irqcount = MAX_IRQLOOPS_PER_JIFFY;
	static int loops_this_jiffy = 0;
	static unsigned long last_irq_jiffies = 0;

	FN_ENTER;

	priv = (wlandevice_t *) (((netdevice_t *) dev_id)->priv);
	irqtype = acx100_read_reg16(priv, priv->io[IO_ACX_IRQ_STATUS_CLEAR]);
	if (unlikely(0xffff == irqtype))
	{
		/* 0xffff value hints at missing hardware,
		 * so don't do anything.
		 * FIXME: that's not very clean - maybe we are able to
		 * establish a flag which definitely tells us that some
		 * hardware is absent and which we could check here? */
		FN_EXIT(0, 0);
		return IRQ_NONE;
	}
		
	irqtype &= ~(priv->irq_mask); /* check only "interesting" IRQ types */
	/* pm_access(priv->pm); OUTDATED, thus disabled at the moment */
	acxlog(L_IRQ, "IRQTYPE: 0x%X, irq_mask: 0x%X\n", irqtype, priv->irq_mask);
	/* immediately return if we don't get signalled that an interrupt
	 * has occurred that we are interested in (interrupt sharing
	 * with other cards!) */
	if (0 == irqtype) {
		FN_EXIT(0, 0);
		return IRQ_NONE;
	}
/*	if (acx100_lock(priv,&flags)) ... */
#define IRQ_ITERATE 1
#if IRQ_ITERATE
  if (jiffies != last_irq_jiffies)
      loops_this_jiffy = 0;
  last_irq_jiffies = jiffies;

  while ((irqtype != 0) && (irqcount-- > 0)) {
    if (unlikely(++loops_this_jiffy > MAX_IRQLOOPS_PER_JIFFY)) {
      acxlog(-1, "HARD ERROR: Too many interrupts this jiffy\n");
      priv->irq_mask = 0;
        break;
        }
#endif

        /* do most important IRQ types first */
	if (0 != (irqtype & HOST_INT_RX_COMPLETE)) {
		acx100_process_rx_desc(priv);
		acxlog(L_IRQ, "Got Rx Complete IRQ\n");
		acx100_write_reg16(priv, priv->io[IO_ACX_IRQ_ACK], HOST_INT_RX_COMPLETE);
	}
	if (0 != (irqtype & HOST_INT_TX_COMPLETE)) {
		static int txcnt = 0;

		if (txcnt++ % 4 == 0)
			acx100_clean_tx_desc(priv);
		acxlog(L_IRQ, "Got Tx Complete IRQ\n");
		acx100_write_reg16(priv, priv->io[IO_ACX_IRQ_ACK], HOST_INT_TX_COMPLETE);
#if 0
/* this shouldn't happen here generally, since we'd also enable user packet
 * xmit for management packets, which we really DON'T want */
/* BS: disabling this caused my card to stop working after a few 
 * seconds when floodpinging. This should be reinvestigated ! */
		  if (netif_queue_stopped(dev_id)) {
			  acxlog(L_BUF, "wake queue after Tx complete.\n");
			  netif_wake_queue(dev_id);
		  }
#endif
	}
	/* group all further IRQ types to improve performance */
	if (0 != (irqtype & (  /* 00f5 */
		HOST_INT_RX_DATA |
		HOST_INT_TX_XFER |
		HOST_INT_DTIM |
		HOST_INT_BEACON |
		HOST_INT_TIMER |
		HOST_INT_KEY_NOT_FOUND
		)
	)) {
		if (0 != (irqtype & HOST_INT_RX_DATA)) {
			acx100_write_reg16(priv, priv->io[IO_ACX_IRQ_ACK], HOST_INT_RX_DATA);
			acxlog(L_STD|L_IRQ, "Got Rx Data IRQ\n");
		}
		if (0 != (irqtype & HOST_INT_TX_XFER)) {
			acx100_write_reg16(priv, priv->io[IO_ACX_IRQ_ACK], HOST_INT_TX_XFER);
			acxlog(L_STD|L_IRQ, "Got Tx Xfer IRQ\n");
		}
		if (0 != (irqtype & HOST_INT_DTIM)) {
			acx100_write_reg16(priv, priv->io[IO_ACX_IRQ_ACK], HOST_INT_DTIM);
			acxlog(L_STD|L_IRQ, "Got DTIM IRQ\n");
		}
		if (0 != (irqtype & HOST_INT_BEACON)) {
			acx100_write_reg16(priv, priv->io[IO_ACX_IRQ_ACK], HOST_INT_BEACON);
			acxlog(L_STD|L_IRQ, "Got Beacon IRQ\n");
		}
		if (0 != (irqtype & HOST_INT_TIMER)) {
			acx100_timer((unsigned long)priv);
			acx100_write_reg16(priv, priv->io[IO_ACX_IRQ_ACK], HOST_INT_TIMER);
			acxlog(L_IRQ, "Got Timer IRQ\n");
		}
		if (unlikely(0 != (irqtype & HOST_INT_KEY_NOT_FOUND))) {
			acx100_write_reg16(priv, priv->io[IO_ACX_IRQ_ACK], HOST_INT_KEY_NOT_FOUND);
			acxlog(L_STD|L_IRQ, "Got Key Not Found IRQ\n");
		}
	}
	if (0 != (irqtype & (  /* 0f00 */
		HOST_INT_IV_ICV_FAILURE |
		HOST_INT_CMD_COMPLETE |
		HOST_INT_INFO |
		HOST_INT_OVERFLOW
		)
	)) {
		if (unlikely(0 != (irqtype & HOST_INT_IV_ICV_FAILURE))) {
			acx100_write_reg16(priv, priv->io[IO_ACX_IRQ_ACK], HOST_INT_IV_ICV_FAILURE);
			acxlog(L_STD|L_IRQ, "Got IV ICV Failure IRQ\n");
		}
		if (0 != (irqtype & HOST_INT_CMD_COMPLETE)) {
			acx100_write_reg16(priv, priv->io[IO_ACX_IRQ_ACK], HOST_INT_CMD_COMPLETE);
			/* save the state for the running issue cmd */
			priv->irq_status |= HOST_INT_CMD_COMPLETE;
			acxlog(L_IRQ, "Got Command Complete IRQ\n");
		}
		if (0 != (irqtype & HOST_INT_INFO)) {
			acx100_handle_info_irq(priv);
			acx100_write_reg16(priv, priv->io[IO_ACX_IRQ_ACK], HOST_INT_INFO);
		}
		if (unlikely(0 != (irqtype & HOST_INT_OVERFLOW))) {
			acx100_write_reg16(priv, priv->io[IO_ACX_IRQ_ACK], HOST_INT_OVERFLOW);
			acxlog(L_STD|L_IRQ, "Got Overflow IRQ\n");
		}
	}
	if (0 != (irqtype & ( /* f000 */
		HOST_INT_PROCESS_ERROR |
		HOST_INT_SCAN_COMPLETE |
		HOST_INT_FCS_THRESHOLD |
		HOST_INT_UNKNOWN
		)
	)) {
		if (unlikely(0 != (irqtype & HOST_INT_PROCESS_ERROR))) {
			acx100_write_reg16(priv, priv->io[IO_ACX_IRQ_ACK], HOST_INT_PROCESS_ERROR);
			acxlog(L_STD|L_IRQ, "Got Process Error IRQ\n");
		}
		if (0 != (irqtype & HOST_INT_SCAN_COMPLETE)) {

			/* place after_interrupt_task into schedule to get
			   out of interrupt context */
			acx_schedule_after_interrupt_task(priv);

			acx100_write_reg16(priv, priv->io[IO_ACX_IRQ_ACK], HOST_INT_SCAN_COMPLETE);
			priv->irq_status |= HOST_INT_SCAN_COMPLETE;

			acxlog(L_IRQ, "<%s> HOST_INT_SCAN_COMPLETE\n", __func__);
		}
		if (0 != (irqtype & HOST_INT_FCS_THRESHOLD)) {
			acx100_write_reg16(priv, priv->io[IO_ACX_IRQ_ACK], HOST_INT_FCS_THRESHOLD);
			acxlog(L_STD|L_IRQ, "Got FCS Threshold IRQ\n");
		}
		if (unlikely(0 != (irqtype & HOST_INT_UNKNOWN))) {
			acx100_write_reg16(priv, priv->io[IO_ACX_IRQ_ACK], HOST_INT_UNKNOWN);
			acxlog(L_STD|L_IRQ, "Got Unknown IRQ\n");
		}
	}
/*	acx100_write_reg16(priv, priv->io[IO_ACX_IRQ_ACK], irqtype); */
#if IRQ_ITERATE
	irqtype = acx100_read_reg16(priv, priv->io[IO_ACX_IRQ_STATUS_CLEAR]) & ~(priv->irq_mask);
	if (0 != irqtype) {
		acxlog(L_IRQ, "IRQTYPE: %X\n", irqtype);
	}
   }
#endif

/*	acx100_unlock(priv,&flags); */
	FN_EXIT(0, 0);
	return IRQ_HANDLED;
}

/*----------------------------------------------------------------
* acx_schedule_after_interrupt_task
*
* Schedule the call of the after_interrupt method after leaving
* the interrupt context.
*
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/
void acx_schedule_after_interrupt_task(wlandevice_t *priv) 
{

#ifdef USE_QUEUE_TASKS
	schedule_task(&priv->after_interrupt_task);
#else
	schedule_work(&priv->after_interrupt_task);
#endif

}

/*----------------------------------------------------------------
* acx100_after_interrupt_task
* 
* Arguments:
*
* Returns:
*
* Side effects:
*
* Call context:
*
* STATUS:
*
* Comment:
*
*----------------------------------------------------------------*/
void acx100_after_interrupt_task(void *data) {
	wlandevice_t *priv = (wlandevice_t *) data;

	FN_ENTER;

	if(in_interrupt()) {
		acxlog(L_IRQ, "You cannot call this method within interrupt context.\n");
		return;
	}
			
	if (priv->irq_status & HOST_INT_SCAN_COMPLETE) {

		if (priv->status == ISTATUS_5_UNKNOWN) {
			acx100_set_status(priv, priv->unknown0x2350);
			priv->unknown0x2350 = 0;
		} else if (priv->status == ISTATUS_1_SCANNING) {
			acx100_complete_dot11_scan(priv);
		}
		priv->irq_status ^= HOST_INT_SCAN_COMPLETE; /* clear the bit */
	}

	if(priv->after_interrupt_jobs != 0) { /* ok, some jobs to do */

		if (priv->after_interrupt_jobs & ACX100_AFTER_INTERRUPT_CMD_STOP_SCAN) {
			acxlog(L_IRQ, "Send a stop scan cmd....\n");
			acx100_issue_cmd(priv, ACX1xx_CMD_STOP_SCAN, NULL, 0, 5000);

			priv->after_interrupt_jobs ^= ACX100_AFTER_INTERRUPT_CMD_STOP_SCAN; /* clear the bit */
		}
		
		if (priv->after_interrupt_jobs & ACX100_AFTER_INTERRUPT_CMD_ASSOCIATE) {

			memmap_t pdr;
			acx100_configure(priv, &pdr, ACX1xx_IE_ASSOC_ID);
			acx100_set_status(priv, ISTATUS_4_ASSOCIATED);
			acxlog(L_BINSTD | L_ASSOC, "ASSOCIATED!\n");

			priv->after_interrupt_jobs ^= ACX100_AFTER_INTERRUPT_CMD_ASSOCIATE; /* clear the bit */
		}
	}

	FN_EXIT(0, 0);
}

/*------------------------------------------------------------------------------
 * acx100_rx
 *
 * The end of the Rx path. Pulls data from a rxhostdescriptor into a socket
 * buffer and feeds it to the network stack via netif_rx().
 *
 * Arguments:
 * 	rxdesc:	the rxhostdescriptor to pull the data from
 *	priv:	the acx100 private struct of the interface
 *
 * Returns:
 *
 * Side effects:
 *
 * Call context:
 *
 * STATUS:
 * 	*much* better now, maybe finally bug-free, VERIFIED.
 *
 * Comment:
 *
 *----------------------------------------------------------------------------*/
void acx100_rx(struct rxhostdescriptor *rxdesc, wlandevice_t *priv)
{
	struct sk_buff *skb;

	FN_ENTER;
	if (0 != (priv->dev_state_mask & ACX_STATE_IFACE_UP)) {
		if ((skb = acx100_rxdesc_to_ether(priv, rxdesc))) {
			(void)netif_rx(skb);
			priv->netdev->last_rx = jiffies;
			priv->stats.rx_packets++;
			priv->stats.rx_bytes += skb->len;
		}
	}
	FN_EXIT(0, 0);
}

/*----------------------------------------------------------------
* init_module
*
* Module initialization routine, called once at module load time.
* This one simulates some of the pcmcia calls.
*
* Arguments:
*	none
*
* Returns:
*	0	- success
*	~0	- failure, module is unloaded.
*
* Side effects:
* 	alot
*
* Call context:
*	process thread (insmod or modprobe)
* STATUS: should be ok.. NONV3.
----------------------------------------------------------------*/

#ifdef MODULE

#ifdef ACX_DEBUG
MODULE_PARM(debug, "i"); /* doh, 2.6.x screwed up big time: here the define has its own ";" ("double ; detected"), yet in 2.4.x it DOESN'T (the sane thing to do), grrrrr! */
MODULE_PARM_DESC(debug, "Debug level mask: 0x0000 - 0x3fff (see L_xxx in include/acx100.h)");
#endif

/*@-fullinitblock@*/
MODULE_PARM(use_eth_name, "i");
MODULE_PARM_DESC(use_eth_name, "Allocate device ethX instead of wlanX");
MODULE_PARM(firmware_dir, "s");
MODULE_PARM_DESC(firmware_dir, "Directory to load acx100 firmware files from");
/*@=fullinitblock@*/

static int __init acx100_init_module(void)
{
	int res;

	FN_ENTER;


	acxlog(L_STD,
	       "acx100: It looks like you've been coaxed into buying a wireless network card\n");
	acxlog(L_STD,
	       "acx100: that uses the mysterious ACX100/ACX111 chip from Texas Instruments.\n");
	acxlog(L_STD,
	       "acx100: You should better have bought e.g. a PRISM(R) chipset based card,\n");
	acxlog(L_STD,
	       "acx100: since that would mean REAL vendor Linux support.\n");
	acxlog(L_STD,
	       "acx100: Given this info, it's evident that this driver is quite EXPERIMENTAL,\n");
	acxlog(L_STD,
	       "acx100: thus your mileage may vary. Visit http://acx100.sf.net for support.\n");

#if (WLAN_HOSTIF==WLAN_USB)
	acxlog(L_STD, "acx100: ENABLED USB SUPPORT!\n");
#endif

#if (ACX_IO_WIDTH==32)
	acxlog(L_STD, "acx100: Compiled to use 32 bit I/O access.\n");
#else
	acxlog(L_STD, "acx100: Warning: compiled to use 16 bit I/O access only!\n");
#endif

	acxlog(L_BINDEBUG, "%s: dev_info is: %s\n", __func__, dev_info);
	acxlog(L_STD|L_INIT, "%s: %s Driver initialized, waiting for cards to probe...\n", __func__, version);

	res = pci_module_init(&acx100_pci_drv_id);
	FN_EXIT(1, res);
	return res;
}

/*----------------------------------------------------------------
* cleanup_module
*
* Called at module unload time.  This is our last chance to
* clean up after ourselves.
*
* Arguments:
*	none
*
* Returns:
*	nothing
*
* Side effects:
* 	alot
*
* Call context:
*	process thread
*
* STATUS: should be ok.
----------------------------------------------------------------*/

static void __exit acx100_cleanup_module(void)
{
	struct net_device *dev;
	char procbuf[80];

	FN_ENTER;
	
	/* Since the whole module is about to be unloaded,
	 * we recursively shutdown all cards we handled instead
	 * of doing it in remove_pci() (which will be activated by us
	 * via pci_unregister_driver at the end).
	 * remove_pci() might just get called after a card eject,
	 * that's why hardware operations have to be done here instead
	 * when the hardware is available. */

	dev = (struct net_device *) root_acx100_dev.newest;
	while (dev != NULL) {
		wlandevice_t *priv = (struct wlandevice *) dev->priv;

		sprintf(procbuf, "driver/acx_%s_phy", dev->name);
		acxlog(L_INIT, "removing /proc entry %s\n", procbuf);
        	remove_proc_entry(procbuf, NULL);
		sprintf(procbuf, "driver/acx_%s_eeprom", dev->name);
		acxlog(L_INIT, "removing /proc entry %s\n", procbuf);
        	remove_proc_entry(procbuf, NULL);
		sprintf(procbuf, "driver/acx_%s_diag", dev->name);
		acxlog(L_INIT, "removing /proc entry %s\n", procbuf);
        	remove_proc_entry(procbuf, NULL);
		sprintf(procbuf, "driver/acx_%s", dev->name);
		acxlog(L_INIT, "removing /proc entry %s\n", procbuf);
        	remove_proc_entry(procbuf, NULL);

		/* disable both Tx and Rx to shut radio down properly */
		acx100_issue_cmd(priv, ACX1xx_CMD_DISABLE_TX, NULL, 0, 5000);
		acx100_issue_cmd(priv, ACX1xx_CMD_DISABLE_RX, NULL, 0, 5000);
	
		/* disable power LED to save power :-) */
		acxlog(L_INIT, "switching off power LED to save power. :-)\n");
		acx100_power_led(priv, (UINT8)0);

#if REDUNDANT
		/* put the eCPU to sleep to save power
		 * Halting is not possible currently,
		 * since not supported by all firmware versions */
		acx100_issue_cmd(priv, ACX100_CMD_SLEEP, NULL, 0, 5000);
#endif

		/* stop our ecpu */
		if(priv->chip_type == CHIPTYPE_ACX111) {
			/* FIXME: does this actually keep halting the eCPU?
			 * I don't think so...
			 */
			acx100_reset_mac(priv);
		}
		else if (CHIPTYPE_ACX100 == priv->chip_type) {
			UINT16 temp;

			/* halt eCPU */
			temp = acx100_read_reg16(priv, priv->io[IO_ACX_ECPU_CTRL]) | 0x1;
			acx100_write_reg16(priv, priv->io[IO_ACX_ECPU_CTRL], temp);

		}

		dev = priv->prev_nd;
	}

	/* now let the pci layer recursively remove all PCI related things
	 * (acx100_remove_pci()) */
	pci_unregister_driver(&acx100_pci_drv_id);
	
	FN_EXIT(0, 0);
}

module_init(acx100_init_module)
module_exit(acx100_cleanup_module)

#else
static int __init acx_get_firmware_dir(char *str)
{
	/* I've seen other drivers just pass the string pointer,
	 * so hopefully that's safe */
	firmware_dir = str;
	return 1;
}

__setup("acx_firmware_dir=", acx_get_firmware_dir);
#endif /* MODULE */
