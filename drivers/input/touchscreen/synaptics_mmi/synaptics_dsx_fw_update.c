/*
 * Synaptics DSX touchscreen driver
 *
 * Copyright (C) 2012 Synaptics Incorporated
 *
 * Copyright (C) 2012 Alexandra Chin <alexandra.chin@tw.synaptics.com>
 * Copyright (C) 2012 Scott Lin <scott.lin@tw.synaptics.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include "synaptics_dsx_i2c.h"

#define FORCE_UPDATE false
#define DO_LOCKDOWN false

#define MAX_IMAGE_NAME_LEN 256
#define MAX_FIRMWARE_ID_LEN 10

#define IMAGE_HEADER_VERSION_05 0x05
#define IMAGE_HEADER_VERSION_06 0x06
#define IMAGE_HEADER_VERSION_10 0x10

#define IMAGE_AREA_OFFSET 0x100
#define LOCKDOWN_SIZE 0x50

#define V5V6_BOOTLOADER_ID_OFFSET 0
#define V5V6_CONFIG_ID_SIZE 4

#define V5_PROPERTIES_OFFSET 2
#define V5_BLOCK_SIZE_OFFSET 3
#define V5_BLOCK_COUNT_OFFSET 5
#define V5_BLOCK_NUMBER_OFFSET 0
#define V5_BLOCK_DATA_OFFSET 2

#define V6_PROPERTIES_OFFSET 1
#define V6_BLOCK_SIZE_OFFSET 2
#define V6_BLOCK_COUNT_OFFSET 3
#define V6_PROPERTIES_2_OFFSET 4
#define V6_GUEST_CODE_BLOCK_COUNT_OFFSET 5
#define V6_BLOCK_NUMBER_OFFSET 0
#define V6_BLOCK_DATA_OFFSET 1
#define V6_FLASH_COMMAND_OFFSET 2
#define V6_FLASH_STATUS_OFFSET 3

#define V7_CONFIG_ID_SIZE 32

#define V7_FLASH_STATUS_OFFSET 0
#define V7_PARTITION_ID_OFFSET 1
#define V7_BLOCK_NUMBER_OFFSET 2
#define V7_TRANSFER_LENGTH_OFFSET 3
#define V7_COMMAND_OFFSET 4
#define V7_PAYLOAD_OFFSET 5

#define V7_PARTITION_SUPPORT_BYTES 4

#define F35_ERROR_CODE_OFFSET 0
#define F35_CHUNK_NUM_LSB_OFFSET 0
#define F35_CHUNK_NUM_MSB_OFFSET 1
#define F35_CHUNK_DATA_OFFSET 2
#define F35_CHUNK_COMMAND_OFFSET 18

#define F35_CHUNK_SIZE 16
#define F35_ERASE_ALL_WAIT_MS 3000
#define F35_RESET_WAIT_MS 250

#define SLEEP_MODE_NORMAL (0x00)
#define SLEEP_MODE_SENSOR_SLEEP (0x01)
#define SLEEP_MODE_RESERVED0 (0x02)
#define SLEEP_MODE_RESERVED1 (0x03)

#define ENABLE_WAIT_MS (1 * 1000)
#define WRITE_WAIT_MS (3 * 1000)
#define ERASE_WAIT_MS (5 * 1000)

#define MIN_SLEEP_TIME_US 50
#define MAX_SLEEP_TIME_US 100

#define INT_DISABLE_WAIT_MS 20
#define ENTER_FLASH_PROG_WAIT_MS 20

#define LOGDEV (&fwu->rmi4_data->i2c_client->dev)
#define SYSFS_KOBJ (&rmi4_data->i2c_client->dev.kobj)

static int fwu_do_reflash(void);

static int fwu_recovery_check_status(void);

#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_DSX_FW_UPDATE_EXTRA_SYSFS_MMI
static ssize_t fwu_sysfs_show_image(struct file *data_file,
		struct kobject *kobj, struct bin_attribute *attributes,
		char *buf, loff_t pos, size_t count);

static ssize_t fwu_sysfs_store_image(struct file *data_file,
		struct kobject *kobj, struct bin_attribute *attributes,
		char *buf, loff_t pos, size_t count);

static ssize_t fwu_sysfs_do_recovery_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t fwu_sysfs_write_config_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t fwu_sysfs_read_config_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t fwu_sysfs_config_area_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t fwu_sysfs_image_name_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t fwu_sysfs_image_size_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t fwu_sysfs_block_size_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t fwu_sysfs_firmware_block_count_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t fwu_sysfs_configuration_block_count_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t fwu_sysfs_disp_config_block_count_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t fwu_sysfs_perm_config_block_count_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t fwu_sysfs_bl_config_block_count_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t fwu_sysfs_guest_code_block_count_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t fwu_sysfs_write_guest_code_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
#endif

static ssize_t fwu_sysfs_erase_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t fwu_sysfs_do_reflash_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t fwu_sysfs_force_reflash_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

enum f34_version {
	F34_V0 = 0,
	F34_V1,
	F34_V2,
};

enum bl_version {
	BL_V5 = 5,
	BL_V6 = 6,
	BL_V7 = 7,
	BL_V8 = 8,
};

enum flash_area {
	NONE = 0,
	UI_FIRMWARE,
	UI_CONFIG,
};

enum update_mode {
	NORMAL = 1,
	FORCE = 2,
	LOCKDOWN = 8,
};

enum config_area {
	UI_CONFIG_AREA = 0,
	PM_CONFIG_AREA,
	BL_CONFIG_AREA,
	DP_CONFIG_AREA,
	FLASH_CONFIG_AREA,
};

enum v7_status {
	SUCCESS = 0x00,
	DEVICE_NOT_IN_BOOTLOADER_MODE,
	INVALID_PARTITION,
	INVALID_COMMAND,
	INVALID_BLOCK_OFFSET,
	INVALID_TRANSFER,
	NOT_ERASED,
	FLASH_PROGRAMMING_KEY_INCORRECT,
	BAD_PARTITION_TABLE,
	CHECKSUM_FAILED,
	FLASH_HARDWARE_FAILURE = 0x1f,
};

enum v7_partition_id {
	BOOTLOADER_PARTITION = 0x01,
	DEVICE_CONFIG_PARTITION,
	FLASH_CONFIG_PARTITION,
	MANUFACTURING_BLOCK_PARTITION,
	GUEST_SERIALIZATION_PARTITION,
	GLOBAL_PARAMETERS_PARTITION,
	CORE_CODE_PARTITION,
	CORE_CONFIG_PARTITION,
	GUEST_CODE_PARTITION,
	DISPLAY_CONFIG_PARTITION,
};

enum v7_flash_command {
	CMD_V7_IDLE = 0x00,
	CMD_V7_ENTER_BL,
	CMD_V7_READ,
	CMD_V7_WRITE,
	CMD_V7_ERASE,
	CMD_V7_ERASE_AP,
	CMD_V7_SENSOR_ID,
};

enum v5v6_flash_command {
	CMD_V5V6_IDLE = 0x0,
	CMD_V5V6_WRITE_FW = 0x2,
	CMD_V5V6_ERASE_ALL = 0x3,
	CMD_V5V6_WRITE_LOCKDOWN = 0x4,
	CMD_V5V6_READ_CONFIG = 0x5,
	CMD_V5V6_WRITE_CONFIG = 0x6,
	CMD_V5V6_ERASE_UI_CONFIG = 0x7,
	CMD_V5V6_ERASE_BL_CONFIG = 0x9,
	CMD_V5V6_ERASE_DISP_CONFIG = 0xa,
	CMD_V5V6_ERASE_GUEST_CODE = 0xb,
	CMD_V5V6_WRITE_GUEST_CODE = 0xc,
	CMD_V5V6_ENABLE_FLASH_PROG = 0xf,
};

enum flash_command {
	CMD_IDLE = 0,
	CMD_WRITE_FW,
	CMD_WRITE_CONFIG,
	CMD_WRITE_LOCKDOWN,
	CMD_WRITE_GUEST_CODE,
	CMD_READ_CONFIG,
	CMD_ERASE_ALL,
	CMD_ERASE_UI_FIRMWARE,
	CMD_ERASE_UI_CONFIG,
	CMD_ERASE_BL_CONFIG,
	CMD_ERASE_DISP_CONFIG,
	CMD_ERASE_FLASH_CONFIG,
	CMD_ERASE_GUEST_CODE,
	CMD_ENABLE_FLASH_PROG,
};

enum f35_flash_command {
	CMD_F35_IDLE = 0x0,
	CMD_F35_RESERVED = 0x1,
	CMD_F35_WRITE_CHUNK = 0x2,
	CMD_F35_ERASE_ALL = 0x3,
	CMD_F35_RESET = 0x10,
};

enum container_id {
	TOP_LEVEL_CONTAINER = 0,
	UI_CONTAINER,
	UI_CONFIG_CONTAINER,
	BL_CONTAINER,
	BL_IMAGE_CONTAINER,
	BL_CONFIG_CONTAINER,
	BL_LOCKDOWN_INFO_CONTAINER,
	PERMANENT_CONFIG_CONTAINER,
	GUEST_CODE_CONTAINER,
	BL_PROTOCOL_DESCRIPTOR_CONTAINER,
	UI_PROTOCOL_DESCRIPTOR_CONTAINER,
	RMI_SELF_DISCOVERY_CONTAINER,
	RMI_PAGE_CONTENT_CONTAINER,
	GENERAL_INFORMATION_CONTAINER,
	DEVICE_CONFIG_CONTAINER,
	FLASH_CONFIG_CONTAINER,
	GUEST_SERIALIZATION_CONTAINER,
	GLOBAL_PARAMETERS_CONTAINER,
	CORE_CODE_CONTAINER,
	CORE_CONFIG_CONTAINER,
	DISPLAY_CONFIG_CONTAINER,
};

struct pdt_properties {
	union {
		struct {
			unsigned char reserved_1:6;
			unsigned char has_bsr:1;
			unsigned char reserved_2:1;
		} __packed;
		unsigned char data[1];
	};
};

struct partition_table {
	unsigned char partition_id:5;
	unsigned char byte_0_reserved:3;
	unsigned char byte_1_reserved;
	unsigned char partition_length_7_0;
	unsigned char partition_length_15_8;
	unsigned char start_physical_address_7_0;
	unsigned char start_physical_address_15_8;
	unsigned char partition_properties_7_0;
	unsigned char partition_properties_15_8;
} __packed;

struct f01_device_control {
	union {
		struct {
			unsigned char sleep_mode:2;
			unsigned char nosleep:1;
			unsigned char reserved:2;
			unsigned char charger_connected:1;
			unsigned char report_rate:1;
			unsigned char configured:1;
		} __packed;
		unsigned char data[1];
	};
};

struct f34_v7_query_0 {
	union {
		struct {
			unsigned char subpacket_1_size:3;
			unsigned char has_config_id:1;
			unsigned char f34_query0_b4:1;
			unsigned char has_thqa:1;
			unsigned char f34_query0_b6__7:2;
		} __packed;
		unsigned char data[1];
	};
};

struct f34_v7_query_1_7 {
	union {
		struct {
			/* query 1 */
			unsigned char bl_minor_revision;
			unsigned char bl_major_revision;

			/* query 2 */
			unsigned char bl_fw_id_7_0;
			unsigned char bl_fw_id_15_8;
			unsigned char bl_fw_id_23_16;
			unsigned char bl_fw_id_31_24;

			/* query 3 */
			unsigned char minimum_write_size;
			unsigned char block_size_7_0;
			unsigned char block_size_15_8;
			unsigned char flash_page_size_7_0;
			unsigned char flash_page_size_15_8;

			/* query 4 */
			unsigned char adjustable_partition_area_size_7_0;
			unsigned char adjustable_partition_area_size_15_8;

			/* query 5 */
			unsigned char flash_config_length_7_0;
			unsigned char flash_config_length_15_8;

			/* query 6 */
			unsigned char payload_length_7_0;
			unsigned char payload_length_15_8;

			/* query 7 */
			unsigned char f34_query7_b0:1;
			unsigned char has_bootloader:1;
			unsigned char has_device_config:1;
			unsigned char has_flash_config:1;
			unsigned char has_manufacturing_block:1;
			unsigned char has_guest_serialization:1;
			unsigned char has_global_parameters:1;
			unsigned char has_core_code:1;
			unsigned char has_core_config:1;
			unsigned char has_guest_code:1;
			unsigned char has_display_config:1;
			unsigned char f34_query7_b11__15:5;
			unsigned char f34_query7_b16__23;
			unsigned char f34_query7_b24__31;
		} __packed;
		unsigned char data[21];
	};
};

struct f34_v7_data_1_5 {
	union {
		struct {
			unsigned char partition_id:5;
			unsigned char f34_data1_b5__7:3;
			unsigned char block_offset_7_0;
			unsigned char block_offset_15_8;
			unsigned char transfer_length_7_0;
			unsigned char transfer_length_15_8;
			unsigned char command;
			unsigned char payload_0;
			unsigned char payload_1;
		} __packed;
		unsigned char data[8];
	};
};

struct f34_v5v6_flash_properties {
	union {
		struct {
			unsigned char reg_map:1;
			unsigned char unlocked:1;
			unsigned char has_config_id:1;
			unsigned char has_pm_config:1;
			unsigned char has_bl_config:1;
			unsigned char has_disp_config:1;
			unsigned char has_ctrl1:1;
			unsigned char has_query4:1;
		} __packed;
		unsigned char data[1];
	};
};

struct f34_v5v6_flash_properties_2 {
	union {
		struct {
			unsigned char has_guest_code:1;
			unsigned char reserved:7;
		} __packed;
		unsigned char data[1];
	};
};

struct register_offset {
	unsigned char properties;
	unsigned char properties_2;
	unsigned char block_size;
	unsigned char block_count;
	unsigned char gc_block_count;
	unsigned char flash_status;
	unsigned char partition_id;
	unsigned char block_number;
	unsigned char transfer_length;
	unsigned char flash_cmd;
	unsigned char payload;
};

struct block_count {
	unsigned short ui_firmware;
	unsigned short ui_config;
	unsigned short dp_config;
	unsigned short fl_config;
	unsigned short pm_config;
	unsigned short bl_config;
	unsigned short lockdown;
	unsigned short guest_code;
};

struct physical_address {
	unsigned short ui_firmware;
	unsigned short ui_config;
	unsigned short dp_config;
	unsigned short guest_code;
};

struct container_descriptor {
	unsigned char content_checksum[4];
	unsigned char container_id[2];
	unsigned char minor_version;
	unsigned char major_version;
	unsigned char reserved_08;
	unsigned char reserved_09;
	unsigned char reserved_0a;
	unsigned char reserved_0b;
	unsigned char container_option_flags[4];
	unsigned char content_options_length[4];
	unsigned char content_options_address[4];
	unsigned char content_length[4];
	unsigned char content_address[4];
};

struct image_header_10 {
	unsigned char checksum[4];
	unsigned char reserved_04;
	unsigned char reserved_05;
	unsigned char minor_header_version;
	unsigned char major_header_version;
	unsigned char reserved_08;
	unsigned char reserved_09;
	unsigned char reserved_0a;
	unsigned char reserved_0b;
	unsigned char top_level_container_start_addr[4];
};

struct image_header_05_06 {
	/* 0x00 - 0x0f */
	unsigned char checksum[4];
	unsigned char reserved_04;
	unsigned char reserved_05;
	unsigned char options_firmware_id:1;
	unsigned char options_bootloader:1;
	unsigned char options_guest_code:1;
	unsigned char options_tddi:1;
	unsigned char options_reserved:4;
	unsigned char header_version;
	unsigned char firmware_size[4];
	unsigned char config_size[4];
	/* 0x10 - 0x1f */
	unsigned char product_id[PRODUCT_ID_SIZE];
	unsigned char package_id[2];
	unsigned char package_id_revision[2];
	unsigned char product_info[PRODUCT_INFO_SIZE];
	/* 0x20 - 0x2f */
	unsigned char bootloader_addr[4];
	unsigned char bootloader_size[4];
	unsigned char ui_addr[4];
	unsigned char ui_size[4];
	/* 0x30 - 0x3f */
	unsigned char ds_id[16];
	/* 0x40 - 0x4f */
	union {
		struct {
			unsigned char cstmr_product_id[PRODUCT_ID_SIZE];
			unsigned char reserved_4a_4f[6];
		};
		struct {
			unsigned char dsp_cfg_addr[4];
			unsigned char dsp_cfg_size[4];
			unsigned char reserved_48_4f[8];
		};
	};
	/* 0x50 - 0x53 */
	unsigned char firmware_id[4];
};

struct block_data {
	unsigned int size;
	const unsigned char *data;
};

struct image_metadata {
	bool contains_firmware_id;
	bool contains_bootloader;
	bool contains_disp_config;
	bool contains_guest_code;
	bool contains_flash_config;
	unsigned int firmware_id;
	unsigned int checksum;
	unsigned int bootloader_size;
	unsigned int disp_config_offset;
	unsigned char bl_version;
	unsigned char product_id[PRODUCT_ID_SIZE + 1];
	unsigned char cstmr_product_id[PRODUCT_ID_SIZE + 1];
	struct block_data bootloader;
	struct block_data ui_firmware;
	struct block_data ui_config;
	struct block_data dp_config;
	struct block_data fl_config;
	struct block_data bl_config;
	struct block_data guest_code;
	struct block_data lockdown;
	struct block_count blkcount;
	struct physical_address phyaddr;
};

struct synaptics_rmi4_fwu_handle {
	enum bl_version bl_version;
	bool initialized;
	bool in_bl_mode;
	bool in_ub_mode;
	bool force_update;
	bool do_lockdown;
	bool has_guest_code;
	bool new_partition_table;
        bool has_erase_all;
	unsigned int data_pos;
	unsigned char *ext_data_source;
	unsigned char *read_config_buf;
	unsigned char intr_mask;
	unsigned char command;
	unsigned char bootloader_id[2];
	unsigned char config_id[32];
	unsigned char flash_status;
	unsigned char partitions;
	unsigned short block_size;
	unsigned short config_size;
	unsigned short config_area;
	unsigned short config_block_count;
	unsigned short flash_config_length;
	unsigned short payload_length;
	unsigned short partition_table_bytes;
	unsigned short read_config_buf_size;
	const unsigned char *config_data;
	const unsigned char *image;
	unsigned char *image_name;
	unsigned int image_size;
	struct image_metadata img;
	struct register_offset off;
	struct block_count blkcount;
	struct physical_address phyaddr;
	struct f34_v5v6_flash_properties flash_properties;
	struct synaptics_rmi4_fn_desc f34_fd;
	struct synaptics_rmi4_fn_desc f35_fd;
	struct synaptics_rmi4_data *rmi4_data;
	struct work_struct fwu_work;
	bool irq_enabled;
	struct semaphore irq_sema;
	struct wakeup_source *flash_wakeup_source;
};

struct image_header {
	unsigned int checksum;
	unsigned int image_size;
	unsigned int config_size;
	unsigned char options;
	unsigned char bootloader_version;
	unsigned char product_id[SYNAPTICS_RMI4_PRODUCT_ID_SIZE + 1];
	unsigned char product_info[SYNAPTICS_RMI4_PRODUCT_INFO_SIZE];
};

#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_DSX_FW_UPDATE_EXTRA_SYSFS_MMI
static struct bin_attribute dev_attr_data = {
	.attr = {
		.name = "data",
		.mode = (S_IRUGO | S_IWUSR | S_IWGRP),
	},
	.size = 0,
	.read = fwu_sysfs_show_image,
	.write = fwu_sysfs_store_image,
};
#endif

static struct device_attribute attrs[] = {
#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_DSX_FW_UPDATE_EXTRA_SYSFS_MMI
	__ATTR(dorecovery, S_IWUSR | S_IWGRP,
			synaptics_rmi4_show_error,
			fwu_sysfs_do_recovery_store),
	__ATTR(writeconfig, S_IWUSR | S_IWGRP,
			synaptics_rmi4_show_error,
			fwu_sysfs_write_config_store),
	__ATTR(readconfig, S_IWUSR | S_IWGRP,
			synaptics_rmi4_show_error,
			fwu_sysfs_read_config_store),
	__ATTR(configarea, S_IWUSR | S_IWGRP,
			synaptics_rmi4_show_error,
			fwu_sysfs_config_area_store),
	__ATTR(imagename, S_IWUSR | S_IWGRP,
			synaptics_rmi4_show_error,
			fwu_sysfs_image_name_store),
	__ATTR(imagesize, S_IWUSR | S_IWGRP,
			synaptics_rmi4_show_error,
			fwu_sysfs_image_size_store),
	__ATTR(blocksize, S_IRUGO,
			fwu_sysfs_block_size_show,
			synaptics_rmi4_store_error),
	__ATTR(fwblockcount, S_IRUGO,
			fwu_sysfs_firmware_block_count_show,
			synaptics_rmi4_store_error),
	__ATTR(configblockcount, S_IRUGO,
			fwu_sysfs_configuration_block_count_show,
			synaptics_rmi4_store_error),
	__ATTR(dispconfigblockcount, S_IRUGO,
			fwu_sysfs_disp_config_block_count_show,
			synaptics_rmi4_store_error),
	__ATTR(permconfigblockcount, S_IRUGO,
			fwu_sysfs_perm_config_block_count_show,
			synaptics_rmi4_store_error),
	__ATTR(blconfigblockcount, S_IRUGO,
			fwu_sysfs_bl_config_block_count_show,
			synaptics_rmi4_store_error),
	__ATTR(guestcodeblockcount, S_IRUGO,
			fwu_sysfs_guest_code_block_count_show,
			synaptics_rmi4_store_error),
	__ATTR(writeguestcode, S_IWUSR | S_IWGRP,
			synaptics_rmi4_show_error,
			fwu_sysfs_write_guest_code_store),
#endif
	__ATTR(doreflash, S_IWUSR | S_IWGRP,
			synaptics_rmi4_show_error,
			fwu_sysfs_do_reflash_store),
	__ATTR(forcereflash, S_IWUSR | S_IWGRP,
			synaptics_rmi4_show_error,
			fwu_sysfs_force_reflash_store),
};


static struct device_attribute erase_attr[] = {
	__ATTR(erase_all, S_IWUSR | S_IWGRP,
			synaptics_rmi4_show_error,
			fwu_sysfs_erase_store),
};

static struct synaptics_rmi4_fwu_handle *fwu;

DECLARE_COMPLETION(fwu_remove_complete);

static inline void sema_clear(struct semaphore *sem)
{
	do {
	} while (!down_trylock(sem));
}

static unsigned int le_to_uint(const unsigned char *ptr)
{
	return (unsigned int)ptr[0] +
			(unsigned int)ptr[1] * 0x100 +
			(unsigned int)ptr[2] * 0x10000 +
			(unsigned int)ptr[3] * 0x1000000;
}

static int fwu_allocate_read_config_buf(unsigned int count)
{
	if (count > fwu->read_config_buf_size) {
		kfree(fwu->read_config_buf);
		fwu->read_config_buf = kzalloc(count, GFP_KERNEL);
		if (!fwu->read_config_buf) {
			dev_err(LOGDEV,
					"%s: Failed to alloc mem for fwu->read_config_buf\n",
					__func__);
			fwu->read_config_buf_size = 0;
			return -ENOMEM;
		}
		fwu->read_config_buf_size = count;
	}
	return 0;
}

static void fwu_compare_partition_tables(void)
{
	if (fwu->phyaddr.ui_firmware != fwu->img.phyaddr.ui_firmware) {
		fwu->new_partition_table = true;
		return;
	}

	if (fwu->phyaddr.ui_config != fwu->img.phyaddr.ui_config) {
		fwu->new_partition_table = true;
		return;
	}

	if (fwu->flash_properties.has_disp_config) {
		if (fwu->phyaddr.dp_config != fwu->img.phyaddr.dp_config) {
			fwu->new_partition_table = true;
			return;
		}
	}

	if (fwu->flash_properties.has_disp_config) {
		if (fwu->phyaddr.dp_config != fwu->img.phyaddr.dp_config) {
			fwu->new_partition_table = true;
			return;
		}
	}

	if (fwu->has_guest_code) {
		if (fwu->phyaddr.guest_code != fwu->img.phyaddr.guest_code) {
			fwu->new_partition_table = true;
			return;
		}
	}

	fwu->new_partition_table = false;
	return;
}

static void fwu_parse_partition_table(const unsigned char *partition_table,
		struct block_count *blkcount, struct physical_address *phyaddr)
{
	unsigned char ii;
	unsigned char index;
	unsigned char offset;
	unsigned short partition_length;
	unsigned short physical_address;
	struct partition_table *ptable;

	for (ii = 0; ii < fwu->partitions; ii++) {
		index = ii * 8 + 2;
		ptable = (struct partition_table *)&partition_table[index];
		partition_length = ptable->partition_length_15_8 << 8 |
				ptable->partition_length_7_0;
		physical_address = ptable->start_physical_address_15_8 << 8 |
				ptable->start_physical_address_7_0;
		dev_dbg(LOGDEV,
				"%s: Partition entry %d:\n",
				__func__, ii);
		for (offset = 0; offset < 8; offset++) {
			dev_dbg(LOGDEV,
					"%s: 0x%02x\n",
					__func__,
					partition_table[index + offset]);
		}
		switch (ptable->partition_id) {
		case CORE_CODE_PARTITION:
			blkcount->ui_firmware = partition_length;
			phyaddr->ui_firmware = physical_address;
			dev_dbg(LOGDEV,
					"%s: Core code block count: %d\n",
					__func__, blkcount->ui_firmware);
			break;
		case CORE_CONFIG_PARTITION:
			blkcount->ui_config = partition_length;
			phyaddr->ui_config = physical_address;
			dev_dbg(LOGDEV,
					"%s: Core config block count: %d\n",
					__func__, blkcount->ui_config);
			break;
		case DISPLAY_CONFIG_PARTITION:
			blkcount->dp_config = partition_length;
			phyaddr->dp_config = physical_address;
			dev_dbg(LOGDEV,
					"%s: Display config block count: %d\n",
					__func__, blkcount->dp_config);
			break;
		case FLASH_CONFIG_PARTITION:
			blkcount->fl_config = partition_length;
			dev_dbg(LOGDEV,
					"%s: Flash config block count: %d\n",
					__func__, blkcount->fl_config);
			break;
		case GUEST_CODE_PARTITION:
			blkcount->guest_code = partition_length;
			phyaddr->guest_code = physical_address;
			dev_dbg(LOGDEV,
					"%s: Guest code block count: %d\n",
					__func__, blkcount->guest_code);
			break;
		case GUEST_SERIALIZATION_PARTITION:
			blkcount->pm_config = partition_length;
			dev_dbg(LOGDEV,
					"%s: Guest serialization block count: %d\n",
					__func__, blkcount->pm_config);
			break;
		case GLOBAL_PARAMETERS_PARTITION:
			blkcount->bl_config = partition_length;
			dev_dbg(LOGDEV,
					"%s: Global parameters block count: %d\n",
					__func__, blkcount->bl_config);
			break;
		case DEVICE_CONFIG_PARTITION:
			blkcount->lockdown = partition_length;
			dev_dbg(LOGDEV,
					"%s: Device config block count: %d\n",
					__func__, blkcount->lockdown);
			break;
		};
	}

	return;
}

static void fwu_parse_image_header_10_bl_container(const unsigned char *image)
{
	unsigned char ii;
	unsigned char num_of_containers;
	unsigned int addr;
	unsigned int container_id;
	unsigned int length;
	const unsigned char *content;
	struct container_descriptor *descriptor;

	num_of_containers = (fwu->img.bootloader.size - 4) / 4;

	for (ii = 1; ii <= num_of_containers; ii++) {
		addr = le_to_uint(fwu->img.bootloader.data + (ii * 4));
		descriptor = (struct container_descriptor *)(image + addr);
		container_id = descriptor->container_id[0] |
				descriptor->container_id[1] << 8;
		content = image + le_to_uint(descriptor->content_address);
		length = le_to_uint(descriptor->content_length);
		switch (container_id) {
		case BL_CONFIG_CONTAINER:
		case GLOBAL_PARAMETERS_CONTAINER:
			fwu->img.bl_config.data = content;
			fwu->img.bl_config.size = length;
			break;
		case BL_LOCKDOWN_INFO_CONTAINER:
		case DEVICE_CONFIG_CONTAINER:
			fwu->img.lockdown.data = content;
			fwu->img.lockdown.size = length;
			break;
		default:
			break;
		};
	}

	return;
}

static void fwu_parse_image_header_10(void)
{
	unsigned char ii;
	unsigned char num_of_containers;
	unsigned int addr;
	unsigned int offset;
	unsigned int container_id;
	unsigned int length;
	const unsigned char *image;
	const unsigned char *content;
	struct container_descriptor *descriptor;
	struct image_header_10 *header;

	image = fwu->image;
	header = (struct image_header_10 *)image;

	fwu->img.checksum = le_to_uint(header->checksum);

	/* address of top level container */
	offset = le_to_uint(header->top_level_container_start_addr);
	descriptor = (struct container_descriptor *)(image + offset);

	/* address of top level container content */
	offset = le_to_uint(descriptor->content_address);
	num_of_containers = le_to_uint(descriptor->content_length) / 4;

	for (ii = 0; ii < num_of_containers; ii++) {
		addr = le_to_uint(image + offset);
		offset += 4;
		descriptor = (struct container_descriptor *)(image + addr);
		container_id = descriptor->container_id[0] |
				descriptor->container_id[1] << 8;
		content = image + le_to_uint(descriptor->content_address);
		length = le_to_uint(descriptor->content_length);
		switch (container_id) {
		case UI_CONTAINER:
		case CORE_CODE_CONTAINER:
			fwu->img.ui_firmware.data = content;
			fwu->img.ui_firmware.size = length;
			break;
		case UI_CONFIG_CONTAINER:
		case CORE_CONFIG_CONTAINER:
			fwu->img.ui_config.data = content;
			fwu->img.ui_config.size = length;
			break;
		case BL_CONTAINER:
			fwu->img.bl_version = *content;
			fwu->img.bootloader.data = content;
			fwu->img.bootloader.size = length;
			fwu_parse_image_header_10_bl_container(image);
			break;
		case GUEST_CODE_CONTAINER:
			fwu->img.contains_guest_code = true;
			fwu->img.guest_code.data = content;
			fwu->img.guest_code.size = length;
			break;
		case DISPLAY_CONFIG_CONTAINER:
			fwu->img.contains_disp_config = true;
			fwu->img.dp_config.data = content;
			fwu->img.dp_config.size = length;
			break;
		case FLASH_CONFIG_CONTAINER:
			fwu->img.contains_flash_config = true;
			fwu->img.fl_config.data = content;
			fwu->img.fl_config.size = length;
			break;
		case GENERAL_INFORMATION_CONTAINER:
			fwu->img.contains_firmware_id = true;
			fwu->img.firmware_id = le_to_uint(content + 4);
			break;
		default:
			break;
		}
	}

	return;
}

static void fwu_parse_image_header_05_06(void)
{
	int retval;
	const unsigned char *image;
	struct image_header_05_06 *header;

	image = fwu->image;
	header = (struct image_header_05_06 *)image;

	fwu->img.checksum = le_to_uint(header->checksum);

	fwu->img.bl_version = header->header_version;

	fwu->img.contains_bootloader = header->options_bootloader;
	if (fwu->img.contains_bootloader)
		fwu->img.bootloader_size = le_to_uint(header->bootloader_size);

	fwu->img.ui_firmware.size = le_to_uint(header->firmware_size);
	if (fwu->img.ui_firmware.size) {
		fwu->img.ui_firmware.data = image + IMAGE_AREA_OFFSET;
		if (fwu->img.contains_bootloader)
			fwu->img.ui_firmware.data += fwu->img.bootloader_size;
	}

	if ((fwu->img.bl_version == BL_V6) && header->options_tddi)
		fwu->img.ui_firmware.data = image + IMAGE_AREA_OFFSET;

	fwu->img.ui_config.size = le_to_uint(header->config_size);
	if (fwu->img.ui_config.size) {
		fwu->img.ui_config.data = fwu->img.ui_firmware.data +
				fwu->img.ui_firmware.size;
	}

	if (fwu->img.contains_bootloader || header->options_tddi)
		fwu->img.contains_disp_config = true;
	else
		fwu->img.contains_disp_config = false;

	if (fwu->img.contains_disp_config) {
		fwu->img.disp_config_offset = le_to_uint(header->dsp_cfg_addr);
		fwu->img.dp_config.size = le_to_uint(header->dsp_cfg_size);
		fwu->img.dp_config.data = image + fwu->img.disp_config_offset;
	} else {
		retval = secure_memcpy(fwu->img.cstmr_product_id,
				sizeof(fwu->img.cstmr_product_id),
				header->cstmr_product_id,
				sizeof(header->cstmr_product_id),
				PRODUCT_ID_SIZE);
		if (retval < 0) {
			dev_err(LOGDEV,
					"%s: Failed to copy custom product ID string\n",
					__func__);
		}
		fwu->img.cstmr_product_id[PRODUCT_ID_SIZE] = 0;
	}

	fwu->img.contains_firmware_id = header->options_firmware_id;
	if (fwu->img.contains_firmware_id)
		fwu->img.firmware_id = le_to_uint(header->firmware_id);

	retval = secure_memcpy(fwu->img.product_id,
			sizeof(fwu->img.product_id),
			header->product_id,
			sizeof(header->product_id),
			PRODUCT_ID_SIZE);
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to copy product ID string\n",
				__func__);
	}
	fwu->img.product_id[PRODUCT_ID_SIZE] = 0;

	fwu->img.lockdown.size = LOCKDOWN_SIZE;
	fwu->img.lockdown.data = image + IMAGE_AREA_OFFSET - LOCKDOWN_SIZE;

	return;
}

static int fwu_parse_image_info(void)
{
	struct image_header_10 *header;

	header = (struct image_header_10 *)fwu->image;

	memset(&fwu->img, 0x00, sizeof(fwu->img));

	switch (header->major_header_version) {
	case IMAGE_HEADER_VERSION_10:
		fwu_parse_image_header_10();
		break;
	case IMAGE_HEADER_VERSION_05:
	case IMAGE_HEADER_VERSION_06:
		fwu_parse_image_header_05_06();
		break;
	default:
		dev_err(LOGDEV,
				"%s: Unsupported image file format (0x%02x)\n",
				__func__, header->major_header_version);
		return -EINVAL;
	}

	if (fwu->bl_version >= BL_V7) {
		if (!fwu->img.contains_flash_config) {
			dev_err(LOGDEV,
					"%s: No flash config found in firmware image\n",
					__func__);
			return -EINVAL;
		}

		fwu_parse_partition_table(fwu->img.fl_config.data,
				&fwu->img.blkcount, &fwu->img.phyaddr);

		fwu_compare_partition_tables();
	} else {
		fwu->new_partition_table = false;
	}

	return 0;
}

static int fwu_read_flash_status(void)
{
	int retval, partition = -1;
	unsigned char status;
        struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	retval = synaptics_rmi4_reg_read(rmi4_data,
			fwu->f34_fd.data_base_addr + fwu->off.flash_status,
			&status,
			sizeof(status));
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to read flash status\n",
				__func__);
		return retval;
	}

	fwu->in_bl_mode = status >> 7;

	if (fwu->bl_version == BL_V5)
		fwu->flash_status = (status >> 4) & MASK_3BIT;
	else if (fwu->bl_version == BL_V6)
		fwu->flash_status = status & MASK_3BIT;
	else
		fwu->flash_status = status & MASK_5BIT;

	if (fwu->bl_version <= BL_V6) {
		unsigned char command;

		retval = synaptics_rmi4_reg_read(rmi4_data,
				fwu->f34_fd.data_base_addr + fwu->off.flash_cmd,
				&command,
				sizeof(command));
		if (retval < 0) {
			dev_err(LOGDEV,
					"%s: Failed to read flash command\n",
					__func__);
			return retval;
		}

		if (fwu->bl_version == BL_V5)
			fwu->command = command & MASK_4BIT;
		else if (fwu->bl_version == BL_V6)
			fwu->command = command & MASK_6BIT;
		else
			fwu->command = command;

	} else {
		struct f34_v7_data_1_5 data15;

		retval = synaptics_rmi4_reg_read(rmi4_data,
				fwu->f34_fd.data_base_addr + fwu->off.partition_id,
				(unsigned char *)&data15,
				sizeof(data15));
		if (retval < 0) {
			dev_err(LOGDEV,
					"%s: Failed to read data15\n",
					__func__);
			return retval;
		}

		if (fwu->flash_status == BAD_PARTITION_TABLE)
			fwu->flash_status = 0x00;

		partition = data15.partition_id;
		fwu->command = data15.command;
	}

	if (fwu->flash_status != 0x00) {
		dev_err(LOGDEV,
				"%s: Flash status = %d, part_id = %d, command = 0x%02x\n",
				__func__, fwu->flash_status, partition, fwu->command);
	}

	return 0;
}

static int fwu_read_interrupt_status(void)
{
	int retval;
	unsigned char interrupt_status;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	retval = synaptics_rmi4_reg_read(rmi4_data,
			rmi4_data->f01_data_base_addr + 1,
			&interrupt_status,
			sizeof(interrupt_status));
	if (retval < 0) {
		dev_err(LOGDEV,
			"%s: Failed to read intr status\n",
			__func__);
		return retval;
	}
	dev_dbg(LOGDEV,
			"%s: F01 interrupt status = 0x%02x\n",
			__func__, interrupt_status);
	return interrupt_status;
}

static irqreturn_t fwu_irq(int irq, void *data)
{
	struct synaptics_rmi4_fwu_handle *fwu_ptr = data;
	up(&fwu_ptr->irq_sema);
	return IRQ_HANDLED;
}

static void fwu_irq_enable(bool enable)
{
	int retval;

	if (enable) {
		if (fwu->irq_enabled) {
			dev_warn(LOGDEV,
				"%s: irq already enabled\n", __func__);
			return;
		}

		fwu_read_interrupt_status();
		retval = request_irq(fwu->rmi4_data->irq, fwu_irq,
				IRQF_TRIGGER_FALLING, "fwu", fwu);
		if (retval < 0) {
			dev_err(&fwu->rmi4_data->i2c_client->dev,
					"%s: Failed to request irq: %d\n",
					__func__, retval);
		}

		dev_dbg(&fwu->rmi4_data->i2c_client->dev,
					"enabling F34 IRQ handler\n");
		fwu->irq_enabled = true;
	} else {
		if (!fwu->irq_enabled) {
			dev_warn(LOGDEV,
				"%s: irq already disabled\n", __func__);
			return;
		}

		dev_dbg(&fwu->rmi4_data->i2c_client->dev,
					"disabling F34 IRQ handler\n");
		disable_irq(fwu->rmi4_data->irq);
		free_irq(fwu->rmi4_data->irq, fwu);
		fwu->irq_enabled = false;
	}
	sema_clear(&fwu->irq_sema);
}

static int fwu_wait_for_idle(int timeout_ms)
{
	int retval;

	retval = down_timeout(&fwu->irq_sema, msecs_to_jiffies(timeout_ms));
	if (retval) {
		retval = -ETIMEDOUT;
		dev_err(LOGDEV,
			"%s: timed out waiting for cmd to complete\n",
			__func__);
	}

	retval = fwu_read_interrupt_status();
	fwu_read_flash_status();
	if ((fwu->command == CMD_IDLE) && (fwu->flash_status == 0x00))
		return 0;

	return retval;
}

static void fwu_reset_device(void)
{
	fwu->rmi4_data->reset_device(fwu->rmi4_data);
	if (fwu->irq_enabled) {
		/* F34 irq handler will override default reset handler */
		/* and since ISR is triggered on falling edge, need to */
		/* wait for idle twice */
		fwu_wait_for_idle(ENABLE_WAIT_MS);
		fwu_wait_for_idle(ENABLE_WAIT_MS);
	}
}

static int fwu_write_f34_v7_command_single_transaction(unsigned char cmd)
{
	int retval;
	unsigned char base;
	struct f34_v7_data_1_5 data_1_5;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	base = fwu->f34_fd.data_base_addr;

	memset(data_1_5.data, 0x00, sizeof(data_1_5.data));

	switch (cmd) {
	case CMD_ERASE_ALL:
		data_1_5.partition_id = CORE_CODE_PARTITION;
		data_1_5.command = CMD_V7_ERASE_AP;
		break;
	case CMD_ERASE_UI_FIRMWARE:
		data_1_5.partition_id = CORE_CODE_PARTITION;
		data_1_5.command = CMD_V7_ERASE;
		break;
	case CMD_ERASE_BL_CONFIG:
		data_1_5.partition_id = GLOBAL_PARAMETERS_PARTITION;
		data_1_5.command = CMD_V7_ERASE;
		break;
	case CMD_ERASE_UI_CONFIG:
		data_1_5.partition_id = CORE_CONFIG_PARTITION;
		data_1_5.command = CMD_V7_ERASE;
		break;
	case CMD_ERASE_DISP_CONFIG:
		data_1_5.partition_id = DISPLAY_CONFIG_PARTITION;
		data_1_5.command = CMD_V7_ERASE;
		break;
	case CMD_ERASE_FLASH_CONFIG:
		data_1_5.partition_id = FLASH_CONFIG_PARTITION;
		data_1_5.command = CMD_V7_ERASE;
		break;
	case CMD_ERASE_GUEST_CODE:
		data_1_5.partition_id = GUEST_CODE_PARTITION;
		data_1_5.command = CMD_V7_ERASE;
		break;
	case CMD_ENABLE_FLASH_PROG:
		data_1_5.partition_id = BOOTLOADER_PARTITION;
		data_1_5.command = CMD_V7_ENTER_BL;
		break;
	};

	data_1_5.payload_0 = fwu->bootloader_id[0];
	data_1_5.payload_1 = fwu->bootloader_id[1];

	retval = synaptics_rmi4_reg_write(rmi4_data,
			base + fwu->off.partition_id,
			data_1_5.data,
			sizeof(data_1_5.data));
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to write single transaction command\n",
				__func__);
		return retval;
	}

	return 0;
}

static int fwu_write_f34_v7_command(unsigned char cmd)
{
	int retval;
	unsigned char base;
	unsigned char command;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	base = fwu->f34_fd.data_base_addr;

	switch (cmd) {
	case CMD_WRITE_FW:
	case CMD_WRITE_CONFIG:
	case CMD_WRITE_GUEST_CODE:
		command = CMD_V7_WRITE;
		break;
	case CMD_READ_CONFIG:
		command = CMD_V7_READ;
		break;
	case CMD_ERASE_ALL:
		command = CMD_V7_ERASE_AP;
		break;
	case CMD_ERASE_UI_FIRMWARE:
	case CMD_ERASE_BL_CONFIG:
	case CMD_ERASE_UI_CONFIG:
	case CMD_ERASE_DISP_CONFIG:
	case CMD_ERASE_FLASH_CONFIG:
	case CMD_ERASE_GUEST_CODE:
		command = CMD_V7_ERASE;
		break;
	case CMD_ENABLE_FLASH_PROG:
		command = CMD_V7_ENTER_BL;
		break;
	default:
		dev_err(LOGDEV,
				"%s: Invalid command 0x%02x\n",
				__func__, cmd);
		return -EINVAL;
	};

	fwu->command = command;

	switch (cmd) {
	case CMD_ERASE_ALL:
	case CMD_ERASE_UI_FIRMWARE:
	case CMD_ERASE_BL_CONFIG:
	case CMD_ERASE_UI_CONFIG:
	case CMD_ERASE_DISP_CONFIG:
	case CMD_ERASE_FLASH_CONFIG:
	case CMD_ERASE_GUEST_CODE:
	case CMD_ENABLE_FLASH_PROG:
		retval = fwu_write_f34_v7_command_single_transaction(cmd);
		if (retval < 0)
			return retval;
		else
			return 0;
	default:
		break;
	};

	retval = synaptics_rmi4_reg_write(rmi4_data,
			base + fwu->off.flash_cmd,
			&command,
			sizeof(command));
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to write flash command\n",
				__func__);
		return retval;
	}

	return 0;
}

static int fwu_write_f34_v5v6_command(unsigned char cmd)
{
	int retval;
	unsigned char base;
	unsigned char command;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	base = fwu->f34_fd.data_base_addr;

	switch (cmd) {
	case CMD_IDLE:
		command = CMD_V5V6_IDLE;
		break;
	case CMD_WRITE_FW:
		command = CMD_V5V6_WRITE_FW;
		break;
	case CMD_WRITE_CONFIG:
		command = CMD_V5V6_WRITE_CONFIG;
		break;
	case CMD_WRITE_LOCKDOWN:
		command = CMD_V5V6_WRITE_LOCKDOWN;
		break;
	case CMD_WRITE_GUEST_CODE:
		command = CMD_V5V6_WRITE_GUEST_CODE;
		break;
	case CMD_READ_CONFIG:
		command = CMD_V5V6_READ_CONFIG;
		break;
	case CMD_ERASE_ALL:
		command = CMD_V5V6_ERASE_ALL;
		break;
	case CMD_ERASE_UI_CONFIG:
		command = CMD_V5V6_ERASE_UI_CONFIG;
		break;
	case CMD_ERASE_DISP_CONFIG:
		command = CMD_V5V6_ERASE_DISP_CONFIG;
		break;
	case CMD_ERASE_GUEST_CODE:
		command = CMD_V5V6_ERASE_GUEST_CODE;
		break;
	case CMD_ENABLE_FLASH_PROG:
		command = CMD_V5V6_ENABLE_FLASH_PROG;
		break;
	default:
		dev_err(LOGDEV,
				"%s: Invalid command 0x%02x\n",
				__func__, cmd);
		return -EINVAL;
	}

	switch (cmd) {
	case CMD_ERASE_ALL:
	case CMD_ERASE_UI_CONFIG:
	case CMD_ERASE_DISP_CONFIG:
	case CMD_ERASE_GUEST_CODE:
	case CMD_ENABLE_FLASH_PROG:
		retval = synaptics_rmi4_reg_write(rmi4_data,
				base + fwu->off.payload,
				fwu->bootloader_id,
				sizeof(fwu->bootloader_id));
		if (retval < 0) {
			dev_err(LOGDEV,
					"%s: Failed to write bootloader ID\n",
					__func__);
			return retval;
		}
		break;
	default:
		break;
	};

	fwu->command = command;

	retval = synaptics_rmi4_reg_write(rmi4_data,
			base + fwu->off.flash_cmd,
			&command,
			sizeof(command));
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to write command 0x%02x\n",
				__func__, command);
		return retval;
	}

	return 0;
}

static int fwu_write_f34_command(unsigned char cmd)
{
	int retval;

	if (fwu->bl_version >= BL_V7)
		retval = fwu_write_f34_v7_command(cmd);
	else
		retval = fwu_write_f34_v5v6_command(cmd);

	return retval;
}

static int fwu_write_f34_v7_partition_id(unsigned char cmd)
{
	int retval;
	unsigned char base;
	unsigned char partition;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	base = fwu->f34_fd.data_base_addr;

	switch (cmd) {
	case CMD_WRITE_FW:
		partition = CORE_CODE_PARTITION;
		break;
	case CMD_WRITE_CONFIG:
	case CMD_READ_CONFIG:
		if (fwu->config_area == UI_CONFIG_AREA)
			partition = CORE_CONFIG_PARTITION;
		else if (fwu->config_area == DP_CONFIG_AREA)
			partition = DISPLAY_CONFIG_PARTITION;
		else if (fwu->config_area == PM_CONFIG_AREA)
			partition = GUEST_SERIALIZATION_PARTITION;
		else if (fwu->config_area == BL_CONFIG_AREA)
			partition = GLOBAL_PARAMETERS_PARTITION;
		else if (fwu->config_area == FLASH_CONFIG_AREA)
			partition = FLASH_CONFIG_PARTITION;
		break;
	case CMD_WRITE_GUEST_CODE:
		partition = GUEST_CODE_PARTITION;
		break;
	case CMD_ERASE_ALL:
		partition = CORE_CODE_PARTITION;
		break;
	case CMD_ERASE_BL_CONFIG:
		partition = GLOBAL_PARAMETERS_PARTITION;
		break;
	case CMD_ERASE_UI_CONFIG:
		partition = CORE_CONFIG_PARTITION;
		break;
	case CMD_ERASE_DISP_CONFIG:
		partition = DISPLAY_CONFIG_PARTITION;
		break;
	case CMD_ERASE_FLASH_CONFIG:
		partition = FLASH_CONFIG_PARTITION;
		break;
	case CMD_ERASE_GUEST_CODE:
		partition = GUEST_CODE_PARTITION;
		break;
	case CMD_ENABLE_FLASH_PROG:
		partition = BOOTLOADER_PARTITION;
		break;
	default:
		dev_err(LOGDEV,
				"%s: Invalid command 0x%02x\n",
				__func__, cmd);
		return -EINVAL;
	};

	retval = synaptics_rmi4_reg_write(rmi4_data,
			base + fwu->off.partition_id,
			&partition,
			sizeof(partition));
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to write partition ID\n",
				__func__);
		return retval;
	}

	return 0;
}

static int fwu_write_f34_partition_id(unsigned char cmd)
{
	int retval;

	if (fwu->bl_version >= BL_V7)
		retval = fwu_write_f34_v7_partition_id(cmd);
	else
		retval = 0;

	return retval;
}

static int fwu_read_f34_v7_partition_table(unsigned char *partition_table)
{
	int retval;
	unsigned char base;
	unsigned char length[2];
	unsigned short block_number = 0;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	base = fwu->f34_fd.data_base_addr;

	fwu->config_area = FLASH_CONFIG_AREA;

	retval = fwu_write_f34_partition_id(CMD_READ_CONFIG);
	if (retval < 0)
		return retval;

	retval = synaptics_rmi4_reg_write(rmi4_data,
			base + fwu->off.block_number,
			(unsigned char *)&block_number,
			sizeof(block_number));
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to write block number\n",
				__func__);
		return retval;
	}

	length[0] = (unsigned char)(fwu->flash_config_length & MASK_8BIT);
	length[1] = (unsigned char)(fwu->flash_config_length >> 8);

	retval = synaptics_rmi4_reg_write(rmi4_data,
			base + fwu->off.transfer_length,
			length,
			sizeof(length));
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to write transfer length\n",
				__func__);
		return retval;
	}

	retval = fwu_write_f34_command(CMD_READ_CONFIG);
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to write command\n",
				__func__);
		return retval;
	}

	retval = fwu_wait_for_idle(WRITE_WAIT_MS);
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to wait for idle status\n",
				__func__);
		return retval;
	}

	retval = synaptics_rmi4_reg_read(rmi4_data,
			base + fwu->off.payload,
			partition_table,
			fwu->partition_table_bytes);
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to read block data\n",
				__func__);
		return retval;
	}

	return 0;
}

static int fwu_read_f34_v7_queries(void)
{
	int retval;
	unsigned char ii;
	unsigned char base;
	unsigned char index;
	unsigned char offset;
	unsigned char *ptable;
	struct f34_v7_query_0 query_0;
	struct f34_v7_query_1_7 query_1_7;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	base = fwu->f34_fd.query_base_addr;

	retval = synaptics_rmi4_reg_read(rmi4_data,
			base,
			query_0.data,
			sizeof(query_0.data));
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to read query 0\n",
				__func__);
		return retval;
	}

	offset = query_0.subpacket_1_size + 1;

	retval = synaptics_rmi4_reg_read(rmi4_data,
			base + offset,
			query_1_7.data,
			sizeof(query_1_7.data));
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to read queries 1 to 7\n",
				__func__);
		return retval;
	}

	fwu->bootloader_id[0] = query_1_7.bl_minor_revision;
	fwu->bootloader_id[1] = query_1_7.bl_major_revision;

	if (fwu->bootloader_id[1] == BL_V8)
		fwu->bl_version = BL_V8;

	fwu->block_size = query_1_7.block_size_15_8 << 8 |
			query_1_7.block_size_7_0;

	fwu->flash_config_length = query_1_7.flash_config_length_15_8 << 8 |
			query_1_7.flash_config_length_7_0;

	fwu->payload_length = query_1_7.payload_length_15_8 << 8 |
			query_1_7.payload_length_7_0;

	fwu->off.flash_status = V7_FLASH_STATUS_OFFSET;
	fwu->off.partition_id = V7_PARTITION_ID_OFFSET;
	fwu->off.block_number = V7_BLOCK_NUMBER_OFFSET;
	fwu->off.transfer_length = V7_TRANSFER_LENGTH_OFFSET;
	fwu->off.flash_cmd = V7_COMMAND_OFFSET;
	fwu->off.payload = V7_PAYLOAD_OFFSET;

	fwu->flash_properties.has_disp_config = query_1_7.has_display_config;
	fwu->flash_properties.has_pm_config = query_1_7.has_guest_serialization;
	fwu->flash_properties.has_bl_config = query_1_7.has_global_parameters;

	fwu->has_guest_code = query_1_7.has_guest_code;

	index = sizeof(query_1_7.data) - V7_PARTITION_SUPPORT_BYTES;

	fwu->partitions = 0;
	for (offset = 0; offset < V7_PARTITION_SUPPORT_BYTES; offset++) {
		for (ii = 0; ii < 8; ii++) {
			if (query_1_7.data[index + offset] & (1 << ii))
				fwu->partitions++;
		}

		dev_dbg(LOGDEV,
				"%s: Supported partitions: 0x%02x\n",
				__func__, query_1_7.data[index + offset]);
	}

	fwu->partition_table_bytes = fwu->partitions * 8 + 2;

	ptable = kzalloc(fwu->partition_table_bytes, GFP_KERNEL);
	if (!ptable) {
		dev_err(LOGDEV,
				"%s: Failed to alloc mem for partition table\n",
				__func__);
		return -ENOMEM;
	}

	retval = fwu_read_f34_v7_partition_table(ptable);
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to read partition table\n",
				__func__);
		kfree(ptable);
		return retval;
	}

	fwu_parse_partition_table(ptable, &fwu->blkcount, &fwu->phyaddr);

	kfree(ptable);

	return 0;
}

static int fwu_read_f34_v5v6_queries(void)
{
	int retval;
	unsigned char count;
	unsigned char base;
	unsigned char buf[10];
	struct f34_v5v6_flash_properties_2 properties_2;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	base = fwu->f34_fd.query_base_addr;

	retval = synaptics_rmi4_reg_read(rmi4_data,
			base + V5V6_BOOTLOADER_ID_OFFSET,
			fwu->bootloader_id,
			sizeof(fwu->bootloader_id));
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to read bootloader ID\n",
				__func__);
		return retval;
	}

	if (fwu->bl_version == BL_V5) {
		fwu->off.properties = V5_PROPERTIES_OFFSET;
		fwu->off.block_size = V5_BLOCK_SIZE_OFFSET;
		fwu->off.block_count = V5_BLOCK_COUNT_OFFSET;
		fwu->off.block_number = V5_BLOCK_NUMBER_OFFSET;
		fwu->off.payload = V5_BLOCK_DATA_OFFSET;
	} else if (fwu->bl_version == BL_V6) {
		fwu->off.properties = V6_PROPERTIES_OFFSET;
		fwu->off.properties_2 = V6_PROPERTIES_2_OFFSET;
		fwu->off.block_size = V6_BLOCK_SIZE_OFFSET;
		fwu->off.block_count = V6_BLOCK_COUNT_OFFSET;
		fwu->off.gc_block_count = V6_GUEST_CODE_BLOCK_COUNT_OFFSET;
		fwu->off.block_number = V6_BLOCK_NUMBER_OFFSET;
		fwu->off.payload = V6_BLOCK_DATA_OFFSET;
	}

	retval = synaptics_rmi4_reg_read(rmi4_data,
			base + fwu->off.block_size,
			buf,
			2);
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to read block size info\n",
				__func__);
		return retval;
	}

	batohs(&fwu->block_size, &(buf[0]));

	if (fwu->bl_version == BL_V5) {
		fwu->off.flash_cmd = fwu->off.payload + fwu->block_size;
		fwu->off.flash_status = fwu->off.flash_cmd;
	} else if (fwu->bl_version == BL_V6) {
		fwu->off.flash_cmd = V6_FLASH_COMMAND_OFFSET;
		fwu->off.flash_status = V6_FLASH_STATUS_OFFSET;
	}

	retval = synaptics_rmi4_reg_read(rmi4_data,
			base + fwu->off.properties,
			fwu->flash_properties.data,
			sizeof(fwu->flash_properties.data));
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to read flash properties\n",
				__func__);
		return retval;
	}

	count = 4;

	if (fwu->flash_properties.has_pm_config)
		count += 2;

	if (fwu->flash_properties.has_bl_config)
		count += 2;

	if (fwu->flash_properties.has_disp_config)
		count += 2;

	retval = synaptics_rmi4_reg_read(rmi4_data,
			base + fwu->off.block_count,
			buf,
			count);
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to read block count info\n",
				__func__);
		return retval;
	}

	batohs(&fwu->blkcount.ui_firmware, &(buf[0]));
	batohs(&fwu->blkcount.ui_config, &(buf[2]));

	count = 4;

	if (fwu->flash_properties.has_pm_config) {
		batohs(&fwu->blkcount.pm_config, &(buf[count]));
		count += 2;
	}

	if (fwu->flash_properties.has_bl_config) {
		batohs(&fwu->blkcount.bl_config, &(buf[count]));
		count += 2;
	}

	if (fwu->flash_properties.has_disp_config)
		batohs(&fwu->blkcount.dp_config, &(buf[count]));

	fwu->has_guest_code = false;

	if (fwu->flash_properties.has_query4) {
		retval = synaptics_rmi4_reg_read(rmi4_data,
				base + fwu->off.properties_2,
				properties_2.data,
				sizeof(properties_2.data));
		if (retval < 0) {
			dev_err(LOGDEV,
					"%s: Failed to read flash properties 2\n",
					__func__);
			return retval;
		}

		if (properties_2.has_guest_code) {
			retval = synaptics_rmi4_reg_read(rmi4_data,
					base + fwu->off.gc_block_count,
					buf,
					2);
			if (retval < 0) {
				dev_err(LOGDEV,
						"%s: Failed to read guest code block count\n",
						__func__);
				return retval;
			}

			batohs(&fwu->blkcount.guest_code, &(buf[0]));
			fwu->has_guest_code = true;
		}
	}

	return 0;
}

static int fwu_read_f34_queries(void)
{
	int retval;

	memset(&fwu->blkcount, 0x00, sizeof(fwu->blkcount));
	memset(&fwu->phyaddr, 0x00, sizeof(fwu->phyaddr));

	if (fwu->bl_version == BL_V7)
		retval = fwu_read_f34_v7_queries();
	else
		retval = fwu_read_f34_v5v6_queries();

	dev_info(LOGDEV,
			"%s: BL version = %d\n",
			__func__, fwu->bl_version);

	return retval;
}

static int fwu_write_f34_v7_blocks(unsigned char *block_ptr,
		unsigned short block_cnt, unsigned char command)
{
	int retval;
	unsigned char base;
	unsigned char length[2];
	unsigned short transfer;
	unsigned short max_transfer;
	unsigned short remaining = block_cnt;
	unsigned short block_number = 0;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	base = fwu->f34_fd.data_base_addr;

	retval = fwu_write_f34_partition_id(command);
	if (retval < 0)
		return retval;

	retval = synaptics_rmi4_reg_write(rmi4_data,
			base + fwu->off.block_number,
			(unsigned char *)&block_number,
			sizeof(block_number));
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to write block number\n",
				__func__);
		return retval;
	}

	if (fwu->payload_length > (PAGE_SIZE / fwu->block_size))
		max_transfer = PAGE_SIZE / fwu->block_size;
	else
		max_transfer = fwu->payload_length;

	dev_dbg(LOGDEV,	"%s: update %s (%3d / %3d)\n",
		__func__,
		command == CMD_WRITE_CONFIG ?
		"config" : "firmware",
		block_cnt - remaining, block_cnt);

	do {
		if (remaining / max_transfer)
			transfer = max_transfer;
		else
			transfer = remaining;

		length[0] = (unsigned char)(transfer & MASK_8BIT);
		length[1] = (unsigned char)(transfer >> 8);

		retval = synaptics_rmi4_reg_write(rmi4_data,
				base + fwu->off.transfer_length,
				length,
				sizeof(length));
		if (retval < 0) {
			dev_err(LOGDEV,
					"%s: Failed to write transfer length (%d blocks remaining)\n",
					__func__, remaining);
			return retval;
		}

		retval = fwu_write_f34_command(command);
		if (retval < 0) {
			dev_err(LOGDEV,
					"%s: Failed to write command (%d blocks remaining)\n",
					__func__, remaining);
			return retval;
		}

		retval = synaptics_rmi4_reg_write(rmi4_data,
				base + fwu->off.payload,
				block_ptr,
				transfer * fwu->block_size);
		if (retval < 0) {
			dev_err(LOGDEV,
					"%s: Failed to write block data (%d blocks remaining)\n",
					__func__, remaining);
			return retval;
		}

		retval = fwu_wait_for_idle(WRITE_WAIT_MS);
		if (retval < 0) {
			dev_err(LOGDEV,
					"%s: Failed to wait for idle status (%d blocks remaining)\n",
					__func__, remaining);
			return retval;
		}

		block_ptr += (transfer * fwu->block_size);
		remaining -= transfer;
		dev_dbg(LOGDEV,	"%s: update %s (%3d / %3d)\n",
					__func__,
					command == CMD_WRITE_CONFIG ?
					"config" : "firmware",
					block_cnt - remaining, block_cnt);
	} while (remaining);

	return 0;
}

static int fwu_write_f34_v5v6_blocks(unsigned char *block_ptr,
		unsigned short block_cnt, unsigned char command)
{
	int retval;
	unsigned char base;
	unsigned char block_number[] = {0, 0};
	unsigned short blk;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;
	unsigned int progress = (command == CMD_WRITE_CONFIG) ?
				10 : 100;

	base = fwu->f34_fd.data_base_addr;

	block_number[1] |= (fwu->config_area << 5);

	retval = synaptics_rmi4_reg_write(rmi4_data,
			base + fwu->off.block_number,
			block_number,
			sizeof(block_number));
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to write block number\n",
				__func__);
		return retval;
	}

	for (blk = 0; blk < block_cnt; blk++) {
		if (blk % progress == 0)
			dev_dbg(LOGDEV,
				"%s: update %s %3d / %3d\n",
				__func__,
				command == CMD_WRITE_CONFIG ?
				"config" : "firmware",
				blk,
				block_cnt);

		retval = synaptics_rmi4_reg_write(rmi4_data,
				base + fwu->off.payload,
				block_ptr,
				fwu->block_size);
		if (retval < 0) {
			dev_err(LOGDEV,
					"%s: Failed to write block data (block %d)\n",
					__func__, blk);
			return retval;
		}

		retval = fwu_write_f34_command(command);
		if (retval < 0) {
			dev_err(LOGDEV,
					"%s: Failed to write command for block %d\n",
					__func__, blk);
			return retval;
		}

		retval = fwu_wait_for_idle(WRITE_WAIT_MS);
		if (retval < 0) {
			dev_err(LOGDEV,
					"%s: Failed to wait for idle status (block %d)\n",
					__func__, blk);
			return retval;
		}

		block_ptr += fwu->block_size;
	}

	return 0;
}

static int fwu_write_f34_blocks(unsigned char *block_ptr,
		unsigned short block_cnt, unsigned char cmd)
{
	int retval;

	if (fwu->bl_version >= BL_V7)
		retval = fwu_write_f34_v7_blocks(block_ptr, block_cnt, cmd);
	else
		retval = fwu_write_f34_v5v6_blocks(block_ptr, block_cnt, cmd);

	return retval;
}

static int fwu_read_f34_v7_blocks(unsigned short block_cnt,
		unsigned char command)
{
	int retval;
	unsigned char base;
	unsigned char length[2];
	unsigned short transfer;
	unsigned short max_transfer;
	unsigned short remaining = block_cnt;
	unsigned short block_number = 0;
	unsigned short index = 0;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	base = fwu->f34_fd.data_base_addr;

	retval = fwu_write_f34_partition_id(command);
	if (retval < 0)
		return retval;

	retval = synaptics_rmi4_reg_write(rmi4_data,
			base + fwu->off.block_number,
			(unsigned char *)&block_number,
			sizeof(block_number));
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to write block number\n",
				__func__);
		return retval;
	}

	if (fwu->payload_length > (PAGE_SIZE / fwu->block_size))
		max_transfer = PAGE_SIZE / fwu->block_size;
	else
		max_transfer = fwu->payload_length;

	do {
		if (remaining / max_transfer)
			transfer = max_transfer;
		else
			transfer = remaining;

		length[0] = (unsigned char)(transfer & MASK_8BIT);
		length[1] = (unsigned char)(transfer >> 8);

		retval = synaptics_rmi4_reg_write(rmi4_data,
				base + fwu->off.transfer_length,
				length,
				sizeof(length));
		if (retval < 0) {
			dev_err(LOGDEV,
					"%s: Failed to write transfer length (%d blocks remaining)\n",
					__func__, remaining);
			return retval;
		}

		retval = fwu_write_f34_command(command);
		if (retval < 0) {
			dev_err(LOGDEV,
					"%s: Failed to write command (%d blocks remaining)\n",
					__func__, remaining);
			return retval;
		}

		retval = fwu_wait_for_idle(WRITE_WAIT_MS);
		if (retval < 0) {
			dev_err(LOGDEV,
					"%s: Failed to wait for idle status (%d blocks remaining)\n",
					__func__, remaining);
			return retval;
		}

		retval = synaptics_rmi4_reg_read(rmi4_data,
				base + fwu->off.payload,
				&fwu->read_config_buf[index],
				transfer * fwu->block_size);
		if (retval < 0) {
			dev_err(LOGDEV,
					"%s: Failed to read block data (%d blocks remaining)\n",
					__func__, remaining);
			return retval;
		}

		index += (transfer * fwu->block_size);
		remaining -= transfer;
	} while (remaining);

	return 0;
}

static int fwu_read_f34_v5v6_blocks(unsigned short block_cnt,
		unsigned char command)
{
	int retval;
	unsigned char base;
	unsigned char block_number[] = {0, 0};
	unsigned short blk;
	unsigned short index = 0;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	base = fwu->f34_fd.data_base_addr;

	block_number[1] |= (fwu->config_area << 5);

	retval = synaptics_rmi4_reg_write(rmi4_data,
			base + fwu->off.block_number,
			block_number,
			sizeof(block_number));
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to write block number\n",
				__func__);
		return retval;
	}

	for (blk = 0; blk < block_cnt; blk++) {
		retval = fwu_write_f34_command(command);
		if (retval < 0) {
			dev_err(LOGDEV,
					"%s: Failed to write read config command\n",
					__func__);
			return retval;
		}

		retval = fwu_wait_for_idle(WRITE_WAIT_MS);
		if (retval < 0) {
			dev_err(LOGDEV,
					"%s: Failed to wait for idle status\n",
					__func__);
			return retval;
		}

		retval = synaptics_rmi4_reg_read(rmi4_data,
				base + fwu->off.payload,
				&fwu->read_config_buf[index],
				fwu->block_size);
		if (retval < 0) {
			dev_err(LOGDEV,
					"%s: Failed to read block data (block %d)\n",
					__func__, blk);
			return retval;
		}

		index += fwu->block_size;
	}

	return 0;
}

static int fwu_read_f34_blocks(unsigned short block_cnt, unsigned char cmd)
{
	int retval;

	if (fwu->bl_version >= BL_V7)
		retval = fwu_read_f34_v7_blocks(block_cnt, cmd);
	else
		retval = fwu_read_f34_v5v6_blocks(block_cnt, cmd);

	return retval;
}

static void fwu_get_device_firmware_id(unsigned int *dev_id)
{
	struct synaptics_rmi4_device_info *rmi =
		&fwu->rmi4_data->rmi4_mod_info;
	unsigned char *buf = (unsigned char *)dev_id;
	buf[0] = 0;
	buf[1] = rmi->build_id[0];
	buf[2] = rmi->build_id[1];
	buf[3] = rmi->build_id[2];
}

static int fwu_get_image_firmware_id(unsigned int *fw_id)
{
	if (fwu->img.contains_firmware_id) {
		*fw_id = fwu->img.firmware_id;
		return 0;
	}

	dev_err(LOGDEV, "%s: no build ID in image\n", __func__);
	return -EINVAL;
}

static int fwu_get_device_config_id(void)
{
	int retval;
	unsigned char config_id_size;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	if (fwu->bl_version >= BL_V7)
		config_id_size = V7_CONFIG_ID_SIZE;
	else
		config_id_size = V5V6_CONFIG_ID_SIZE;

	retval = synaptics_rmi4_reg_read(rmi4_data,
				fwu->f34_fd.ctrl_base_addr,
				fwu->config_id,
				config_id_size);
	if (retval < 0)
		return retval;

	return 0;
}

static enum flash_area fwu_go_nogo(void)
{
	int retval;
	enum flash_area flash_area = NONE;
	unsigned char ii;
	unsigned char config_id_size;
	unsigned int device_fw_id;
	unsigned int image_fw_id;

	if (fwu->force_update) {
		flash_area = UI_FIRMWARE;
		goto exit;
	}

	/* Update both UI and config if device is in bootloader mode */
	if (fwu->in_bl_mode) {
		flash_area = UI_FIRMWARE;
		goto exit;
	}

	/* Get device firmware ID */
	fwu_get_device_firmware_id(&device_fw_id);
	dev_info(LOGDEV,
			"%s: Device firmware ID = %d\n",
			__func__, device_fw_id);

	/* Get image firmware ID */
	retval = fwu_get_image_firmware_id(&image_fw_id);
	if (retval < 0) {
		flash_area = NONE;
		goto exit;
	}
	dev_info(LOGDEV,
			"%s: Image firmware ID = %d\n",
			__func__, image_fw_id);

	if (image_fw_id != device_fw_id) {
		dev_info(LOGDEV,
			"%s: Image firmware ID differs from device firmware ID 0x%x 0x%x\n",
			__func__, image_fw_id, device_fw_id);
		flash_area = UI_FIRMWARE;
		goto exit;
	}

	/* Get device config ID */
	retval = fwu_get_device_config_id();
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to read device config ID\n",
				__func__);
		flash_area = NONE;
		goto exit;
	}

	if (fwu->bl_version >= BL_V7)
		config_id_size = V7_CONFIG_ID_SIZE;
	else
		config_id_size = V5V6_CONFIG_ID_SIZE;

	for (ii = 0; ii < config_id_size; ii++) {
		if (fwu->img.ui_config.data[ii] > fwu->config_id[ii]) {
			flash_area = UI_CONFIG;
			goto exit;
		} else if (fwu->img.ui_config.data[ii] < fwu->config_id[ii]) {
			flash_area = NONE;
			goto exit;
		}
	}

	flash_area = NONE;

exit:
	if (flash_area == NONE) {
		dev_info(LOGDEV,
				"%s: No need to do reflash\n",
				__func__);
	} else {
		dev_info(LOGDEV,
				"%s: Updating %s\n",
				__func__,
				flash_area == UI_FIRMWARE ?
				"UI firmware and config" :
				"UI config only");
	}

	return flash_area;
}

static int fwu_scan_pdt(void)
{
	int retval;
	unsigned char ii;
	unsigned char intr_count = 0;
	unsigned char intr_off;
	unsigned char intr_src;
	unsigned short addr;
	bool f01found = false;
	bool f34found = false;
	bool f35found = false;
	struct synaptics_rmi4_fn_desc rmi_fd;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	fwu->in_ub_mode = false;

	for (addr = PDT_START; addr > PDT_END; addr -= PDT_ENTRY_SIZE) {
		retval = synaptics_rmi4_reg_read(rmi4_data,
				addr,
				(unsigned char *)&rmi_fd,
				sizeof(rmi_fd));
		if (retval < 0)
			return retval;

		if (rmi_fd.fn_number) {
			dev_dbg(LOGDEV,
					"%s: Found F%02x\n",
					__func__, rmi_fd.fn_number);
			switch (rmi_fd.fn_number) {
			case SYNAPTICS_RMI4_F01:
				f01found = true;

				rmi4_data->f01_query_base_addr =
						rmi_fd.query_base_addr;
				rmi4_data->f01_ctrl_base_addr =
						rmi_fd.ctrl_base_addr;
				rmi4_data->f01_data_base_addr =
						rmi_fd.data_base_addr;
				rmi4_data->f01_cmd_base_addr =
						rmi_fd.cmd_base_addr;
				break;
			case SYNAPTICS_RMI4_F34:
				f34found = true;
				fwu->f34_fd.query_base_addr =
						rmi_fd.query_base_addr;
				fwu->f34_fd.ctrl_base_addr =
						rmi_fd.ctrl_base_addr;
				fwu->f34_fd.data_base_addr =
						rmi_fd.data_base_addr;

				switch (rmi_fd.fn_version) {
				case F34_V0:
					fwu->bl_version = BL_V5;
					break;
				case F34_V1:
					fwu->bl_version = BL_V6;
					break;
				case F34_V2:
					fwu->bl_version = BL_V7;
					break;
				default:
					dev_err(LOGDEV,
							"%s: Unrecognized F34 version\n",
							__func__);
					return -EINVAL;
				}

				fwu->intr_mask = 0;
				intr_src = rmi_fd.intr_src_count;
				intr_off = intr_count % 8;
				for (ii = intr_off;
						ii < (intr_src + intr_off);
						ii++) {
					fwu->intr_mask |= 1 << ii;
				}
				break;
			case SYNAPTICS_RMI4_F35:
				f35found = true;
				fwu->f35_fd.query_base_addr =
						rmi_fd.query_base_addr;
				fwu->f35_fd.ctrl_base_addr =
						rmi_fd.ctrl_base_addr;
				fwu->f35_fd.data_base_addr =
						rmi_fd.data_base_addr;
				break;
			}
		} else {
			break;
		}

		intr_count += rmi_fd.intr_src_count;
	}

	if (!f01found || !f34found) {
		dev_err(LOGDEV,
				"%s: Failed to find both F01 and F34\n",
				__func__);
		if (!f35found) {
			dev_err(LOGDEV,
					"%s: Failed to find F35\n",
					__func__);
			return -EINVAL;
		} else {
			fwu->in_ub_mode = true;
			dev_dbg(LOGDEV,
					"%s: In microbootloader mode\n",
					__func__);
			fwu_recovery_check_status();
			return 0;
		}
	}

	return 0;
}

int fwu_check_intr_en(struct synaptics_rmi4_data *rmi4_data,
	unsigned char *drv_intr_en, size_t size)
{
	int i;
	unsigned char intr_en[MAX_INTR_REGISTERS];
	int retval = 0;
	unsigned char addr = rmi4_data->f01_ctrl_base_addr + 1;

	retval = synaptics_rmi4_reg_read(rmi4_data,
			addr,
			intr_en,
			size);
	if (retval < 0) {
		dev_err(LOGDEV,
			"%s: Failed to read interrupt enable register\n",
			__func__);
		return retval;
	}
	/* ensure interrupt enable mask expected by driver is subset of */
	/* touch IC interrupt enable register */
	for (i = 0; i < size; ++i)
		if (drv_intr_en[i] != (drv_intr_en[i] & intr_en[i])) {
			retval = 1;
			dev_err(LOGDEV,
				"%s: Interrupt enable mismatch:"
				" drv[%d] = 0x%02x, fw[%d] = 0x%02x\n",
				__func__, i, drv_intr_en[i], i, intr_en[i]);
		}

	return retval;
}

static int fwu_enter_flash_prog(void)
{
	int retval;
	struct f01_device_control f01_device_control;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	retval = fwu_read_flash_status();
	if (retval < 0)
		return retval;

	if (fwu->in_bl_mode)
		return 0;

	retval = fwu_write_f34_command(CMD_ENABLE_FLASH_PROG);
	if (retval < 0)
		goto out;

	retval = fwu_wait_for_idle(ENABLE_WAIT_MS);
	if (retval < 0)
		goto out;

	if (!fwu->in_bl_mode) {
		dev_err(LOGDEV,
				"%s: BL mode not entered\n",
				__func__);
		retval = -EINVAL;
		goto out;
	}

	retval = fwu_scan_pdt();
	if (retval < 0)
		goto out;

	fwu_check_intr_en(rmi4_data, &fwu->intr_mask, sizeof(fwu->intr_mask));

	retval = fwu_read_f34_queries();
	if (retval < 0)
		goto out;

	retval = synaptics_rmi4_reg_read(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			f01_device_control.data,
			sizeof(f01_device_control.data));
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to read F01 device control\n",
				__func__);
		goto out;
	}

	f01_device_control.nosleep = true;
	f01_device_control.sleep_mode = SLEEP_MODE_NORMAL;

	retval = synaptics_rmi4_reg_write(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			f01_device_control.data,
			sizeof(f01_device_control.data));
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to write F01 device control\n",
				__func__);
		goto out;
	}

	msleep(ENTER_FLASH_PROG_WAIT_MS);
out:
	return retval;
}

static int fwu_check_ui_firmware_size(void)
{
	unsigned short block_count;

	block_count = fwu->img.ui_firmware.size / fwu->block_size;

	if (block_count != fwu->blkcount.ui_firmware) {
		dev_err(LOGDEV,
			"%s: UI firmware size mismatch: %d (expected %d)\n",
			__func__, block_count, fwu->blkcount.ui_firmware);
		return -EINVAL;
	}

	return 0;
}

static int fwu_check_ui_configuration_size(void)
{
	unsigned short block_count;

	block_count = fwu->img.ui_config.size / fwu->block_size;

	if (block_count != fwu->blkcount.ui_config) {
		dev_err(LOGDEV,
			"%s: UI config size mismatch: %d (expected %d)\n",
			__func__, block_count, fwu->blkcount.ui_config);
		return -EINVAL;
	}

	return 0;
}

static int fwu_check_dp_configuration_size(void)
{
	unsigned short block_count;

	block_count = fwu->img.dp_config.size / fwu->block_size;

	if (block_count != fwu->blkcount.dp_config) {
		dev_err(LOGDEV,
				"%s: Display configuration size mismatch\n",
				__func__);
		return -EINVAL;
	}

	return 0;
}

static int fwu_check_bl_configuration_size(void)
{
	unsigned short block_count;

	block_count = fwu->img.bl_config.size / fwu->block_size;

	if (block_count != fwu->blkcount.bl_config) {
		dev_err(LOGDEV,
				"%s: Bootloader configuration size mismatch\n",
				__func__);
		return -EINVAL;
	}

	return 0;
}

static int fwu_check_guest_code_size(void)
{
	unsigned short block_count;

	block_count = fwu->img.guest_code.size / fwu->block_size;
	if (block_count != fwu->blkcount.guest_code) {
		dev_err(LOGDEV,
				"%s: Guest code size mismatch\n",
				__func__);
		return -EINVAL;
	}

	return 0;
}

static int fwu_write_firmware(void)
{
	unsigned short firmware_block_count;

	firmware_block_count = fwu->img.ui_firmware.size / fwu->block_size;

	return fwu_write_f34_blocks((unsigned char *)fwu->img.ui_firmware.data,
			firmware_block_count, CMD_WRITE_FW);
}

static int fwu_erase_configuration(void)
{
	int retval;

	switch (fwu->config_area) {
	case UI_CONFIG_AREA:
		retval = fwu_write_f34_command(CMD_ERASE_UI_CONFIG);
		if (retval < 0)
			return retval;
		break;
	case DP_CONFIG_AREA:
		retval = fwu_write_f34_command(CMD_ERASE_DISP_CONFIG);
		if (retval < 0)
			return retval;
		break;
	case BL_CONFIG_AREA:
		retval = fwu_write_f34_command(CMD_ERASE_BL_CONFIG);
		if (retval < 0)
			return retval;
		break;
	}

	dev_dbg(LOGDEV,
			"%s: Erase command written\n",
			__func__);

	retval = fwu_wait_for_idle(ERASE_WAIT_MS);
	if (retval < 0)
		return retval;

	dev_dbg(LOGDEV,
			"%s: Idle status detected\n",
			__func__);

	return retval;
}

static int fwu_erase_guest_code(void)
{
	int retval;

	retval = fwu_write_f34_command(CMD_ERASE_GUEST_CODE);
	if (retval < 0)
		return retval;

	dev_dbg(LOGDEV,
			"%s: Erase command written\n",
			__func__);

	retval = fwu_wait_for_idle(ERASE_WAIT_MS);
	if (retval < 0)
		return retval;

	dev_dbg(LOGDEV,
			"%s: Idle status detected\n",
			__func__);

	return 0;
}

static int fwu_erase_all(void)
{
	int retval;

	if (fwu->bl_version == BL_V7) {
		retval = fwu_write_f34_command(CMD_ERASE_UI_FIRMWARE);
		if (retval < 0)
			return retval;

		dev_dbg(LOGDEV,
				"%s: Erase command written\n",
				__func__);

		retval = fwu_wait_for_idle(ERASE_WAIT_MS);
		if (retval < 0)
			return retval;

		dev_dbg(LOGDEV,
				"%s: Idle status detected\n",
				__func__);

		fwu->config_area = UI_CONFIG_AREA;
		retval = fwu_erase_configuration();
		if (retval < 0)
			return retval;
	} else {
		retval = fwu_write_f34_command(CMD_ERASE_ALL);
		if (retval < 0)
			return retval;

		dev_dbg(LOGDEV,
				"%s: Erase all command written\n",
				__func__);

		retval = fwu_wait_for_idle(ERASE_WAIT_MS);
		if (!(fwu->bl_version == BL_V8 &&
				fwu->flash_status == BAD_PARTITION_TABLE)) {
			if (retval < 0)
				return retval;
		}

		dev_dbg(LOGDEV,
				"%s: Idle status detected\n",
				__func__);

		if (fwu->bl_version == BL_V8)
			return 0;
	}

	if (fwu->flash_properties.has_disp_config) {
		fwu->config_area = DP_CONFIG_AREA;
		retval = fwu_erase_configuration();
		if (retval < 0)
			return retval;
	}

	if (fwu->new_partition_table && fwu->has_guest_code) {
		retval = fwu_erase_guest_code();
		if (retval < 0)
			return retval;
	}

	return 0;
}

static int fwu_write_configuration(void)
{
	return fwu_write_f34_blocks((unsigned char *)fwu->config_data,
			fwu->config_block_count, CMD_WRITE_CONFIG);
}

static int fwu_write_ui_configuration(void)
{
	fwu->config_area = UI_CONFIG_AREA;
	fwu->config_data = fwu->img.ui_config.data;
	fwu->config_size = fwu->img.ui_config.size;
	fwu->config_block_count = fwu->config_size / fwu->block_size;

	return fwu_write_configuration();
}

static int fwu_write_dp_configuration(void)
{
	fwu->config_area = DP_CONFIG_AREA;
	fwu->config_data = fwu->img.dp_config.data;
	fwu->config_size = fwu->img.dp_config.size;
	fwu->config_block_count = fwu->config_size / fwu->block_size;

	return fwu_write_configuration();
}

static int fwu_write_flash_configuration(void)
{
	int retval;

	fwu->config_area = FLASH_CONFIG_AREA;
	fwu->config_data = fwu->img.fl_config.data;
	fwu->config_size = fwu->img.fl_config.size;
	fwu->config_block_count = fwu->config_size / fwu->block_size;

	if (fwu->config_block_count != fwu->blkcount.fl_config) {
		dev_err(LOGDEV,
				"%s: Flash configuration size mismatch\n",
				__func__);
		return -EINVAL;
	}

	retval = fwu_write_f34_command(CMD_ERASE_FLASH_CONFIG);
	if (retval < 0)
		return retval;

	dev_dbg(LOGDEV,
			"%s: Erase flash configuration command written\n",
			__func__);

	retval = fwu_wait_for_idle(ERASE_WAIT_MS);
	if (retval < 0)
		return retval;

	dev_dbg(LOGDEV,
			"%s: Idle status detected\n",
			__func__);

	retval = fwu_write_configuration();
	if (retval < 0)
		return retval;

	fwu_reset_device();

	return 0;
}

static int fwu_write_guest_code(void)
{
	int retval;
	unsigned short guest_code_block_count;

	guest_code_block_count = fwu->img.guest_code.size / fwu->block_size;

	retval = fwu_write_f34_blocks((unsigned char *)fwu->img.guest_code.data,
			guest_code_block_count, CMD_WRITE_GUEST_CODE);
	if (retval < 0)
		return retval;

	return 0;
}

static int fwu_write_lockdown(void)
{
	unsigned short lockdown_block_count;

	lockdown_block_count = fwu->img.lockdown.size / fwu->block_size;

	return fwu_write_f34_blocks((unsigned char *)fwu->img.lockdown.data,
			lockdown_block_count, CMD_WRITE_LOCKDOWN);
}

static int fwu_write_partition_table_v8(void)
{
	int retval;

	fwu->config_area = FLASH_CONFIG_AREA;
	fwu->config_data = fwu->img.fl_config.data;
	fwu->config_size = fwu->img.fl_config.size;
	fwu->config_block_count = fwu->config_size / fwu->block_size;

	if (fwu->config_block_count != fwu->blkcount.fl_config) {
		dev_err(LOGDEV,
				"%s: Flash configuration size mismatch\n",
				__func__);
		return -EINVAL;
	}

	retval = fwu_write_configuration();
	if (retval < 0)
		return retval;

	fwu_reset_device();

	return 0;
}

static int fwu_write_partition_table_v7(void)
{
	int retval;
	unsigned short block_count;

	block_count = fwu->blkcount.bl_config;
	fwu->config_area = BL_CONFIG_AREA;
	fwu->config_size = fwu->block_size * block_count;

	retval = fwu_allocate_read_config_buf(fwu->config_size);
	if (retval < 0)
		return retval;

	retval = fwu_read_f34_blocks(block_count, CMD_READ_CONFIG);
	if (retval < 0)
		return retval;

	retval = fwu_erase_configuration();
	if (retval < 0)
		return retval;

	retval = fwu_write_flash_configuration();
	if (retval < 0)
		return retval;

	fwu->config_area = BL_CONFIG_AREA;
	fwu->config_data = fwu->read_config_buf;
	fwu->config_size = fwu->img.bl_config.size;
	fwu->config_block_count = fwu->config_size / fwu->block_size;

	retval = fwu_write_configuration();
	if (retval < 0)
		return retval;

	return 0;
}

static int fwu_do_reflash(void)
{
	int retval;

	if (!fwu->new_partition_table) {
		retval = fwu_check_ui_firmware_size();
		if (retval < 0)
			return retval;

		retval = fwu_check_ui_configuration_size();
		if (retval < 0)
			return retval;

		if (fwu->flash_properties.has_disp_config &&
				fwu->img.contains_disp_config) {
			retval = fwu_check_dp_configuration_size();
			if (retval < 0)
				return retval;
		}

		if (fwu->has_guest_code && fwu->img.contains_guest_code) {
			retval = fwu_check_guest_code_size();
			if (retval < 0)
				return retval;
		}
	} else if (fwu->bl_version == BL_V7) {
		retval = fwu_check_bl_configuration_size();
		if (retval < 0)
			return retval;
	}

	retval = fwu_erase_all();
	if (retval < 0)
		return retval;

	if (fwu->bl_version == BL_V7 && fwu->new_partition_table) {
		retval = fwu_write_partition_table_v7();
		if (retval < 0)
			return retval;
		pr_notice("%s: Partition table programmed\n", __func__);
	} else if (fwu->bl_version == BL_V8) {
		retval = fwu_write_partition_table_v8();
		if (retval < 0)
			return retval;
		pr_notice("%s: Partition table programmed\n", __func__);
	}

	retval = fwu_write_firmware();
	if (retval < 0)
		return retval;
	pr_notice("%s: Firmware programmed\n", __func__);

	fwu->config_area = UI_CONFIG_AREA;
	retval = fwu_write_ui_configuration();
	if (retval < 0)
		return retval;
	pr_notice("%s: Configuration programmed\n", __func__);

	if (fwu->flash_properties.has_disp_config &&
			fwu->img.contains_disp_config) {
		retval = fwu_write_dp_configuration();
		if (retval < 0)
			return retval;
		pr_notice("%s: Display configuration programmed\n", __func__);
	}

	if (fwu->new_partition_table) {
		if (fwu->has_guest_code && fwu->img.contains_guest_code) {
			retval = fwu_write_guest_code();
			if (retval < 0)
				return retval;
			pr_notice("%s: Guest code programmed\n", __func__);
		}
	}

	return retval;
}

#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_DSX_FW_UPDATE_EXTRA_SYSFS_MMI
static int fwu_do_read_config(void)
{
	int retval;
	unsigned short block_count;
	unsigned short config_area;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	switch (fwu->config_area) {
	case UI_CONFIG_AREA:
		block_count = fwu->blkcount.ui_config;
		break;
	case DP_CONFIG_AREA:
		if (!fwu->flash_properties.has_disp_config) {
			dev_err(LOGDEV,
					"%s: Display configuration not supported\n",
					__func__);
			return -EINVAL;
		}
		block_count = fwu->blkcount.dp_config;
		break;
	case PM_CONFIG_AREA:
		if (!fwu->flash_properties.has_pm_config) {
			dev_err(LOGDEV,
					"%s: Permanent configuration not supported\n",
					__func__);
			return -EINVAL;
		}
		block_count = fwu->blkcount.pm_config;
		break;
	case BL_CONFIG_AREA:
		if (!fwu->flash_properties.has_bl_config) {
			dev_err(LOGDEV,
					"%s: Bootloader configuration not supported\n",
					__func__);
			return -EINVAL;
		}
		block_count = fwu->blkcount.bl_config;
		break;
	default:
		dev_err(LOGDEV,
				"%s: Invalid config area\n",
				__func__);
		return -EINVAL;
	}

	if (block_count == 0) {
		dev_err(LOGDEV,
				"%s: Invalid block count\n",
				__func__);
		return -EINVAL;
	}

	mutex_lock(&rmi4_data->rmi4_exp_init_mutex);

	config_area = fwu->config_area;

	retval = fwu_enter_flash_prog();
	if (retval < 0)
		goto exit;

	fwu->config_area = config_area;

	fwu->config_size = fwu->block_size * block_count;

	retval = fwu_allocate_read_config_buf(fwu->config_size);
	if (retval < 0)
		goto exit;

	retval = fwu_read_f34_blocks(block_count, CMD_READ_CONFIG);

exit:
	fwu_reset_device();

	mutex_unlock(&rmi4_data->rmi4_exp_init_mutex);

	return retval;
}
#endif

static int fwu_do_lockdown(void)
{
	int retval;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	retval = fwu_enter_flash_prog();
	if (retval < 0)
		return retval;

	retval = synaptics_rmi4_reg_read(rmi4_data,
			fwu->f34_fd.query_base_addr + fwu->off.properties,
			fwu->flash_properties.data,
			sizeof(fwu->flash_properties.data));
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to read flash properties\n",
				__func__);
		return retval;
	}

	if (fwu->flash_properties.unlocked == 0) {
		dev_info(LOGDEV,
				"%s: Device already locked down\n",
				__func__);
		return 0;
	}

	retval = fwu_write_lockdown();
	if (retval < 0)
		return retval;

	pr_notice("%s: Lockdown programmed\n", __func__);

	return retval;
}

#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_DSX_FW_UPDATE_EXTRA_SYSFS_MMI
static int fwu_start_write_guest_code(void)
{
	int retval;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	retval = fwu_parse_image_info();
	if (retval < 0)
		return -EINVAL;

	if (!fwu->has_guest_code) {
		dev_err(LOGDEV,
				"%s: Guest code not supported\n",
				__func__);
		return -EINVAL;
	}

	if (!fwu->img.contains_guest_code) {
		dev_err(LOGDEV,
				"%s: No guest code in firmware image\n",
				__func__);
		return -EINVAL;
	}

	fwu->rmi4_data->set_state(fwu->rmi4_data, STATE_INIT);

	mutex_lock(&rmi4_data->rmi4_exp_init_mutex);

	pr_notice("%s: Start of write guest code process\n", __func__);

	retval = fwu_enter_flash_prog();
	if (retval < 0)
		goto exit;

	retval = fwu_check_guest_code_size();
	if (retval < 0)
		goto exit;

	retval = fwu_erase_guest_code();
	if (retval < 0)
		goto exit;

	retval = fwu_write_guest_code();
	if (retval < 0)
		goto exit;

	pr_notice("%s: Guest code programmed\n", __func__);

exit:
	fwu_reset_device();

	pr_notice("%s: End of write guest code process\n", __func__);

	mutex_unlock(&rmi4_data->rmi4_exp_init_mutex);

	return retval;
}

static int fwu_start_write_config(void)
{
	int retval;
	unsigned short config_area;
	unsigned int device_fw_id;
	unsigned int image_fw_id;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	retval = fwu_parse_image_info();
	if (retval < 0)
		return -EINVAL;

	switch (fwu->config_area) {
	case UI_CONFIG_AREA:
		fwu_get_device_firmware_id(&device_fw_id);
		retval = fwu_get_image_firmware_id(&image_fw_id);
		if (retval < 0)
			return retval;
		if (device_fw_id != image_fw_id) {
			dev_err(LOGDEV,
					"%s: Device and image firmware IDs don't match\n",
					__func__);
			return -EINVAL;
		}
		retval = fwu_check_ui_configuration_size();
		if (retval < 0)
			return retval;
		break;
	case DP_CONFIG_AREA:
		if (!fwu->flash_properties.has_disp_config) {
			dev_err(LOGDEV,
					"%s: Display configuration not supported\n",
					__func__);
			return -EINVAL;
		}
		if (!fwu->img.contains_disp_config) {
			dev_err(LOGDEV,
					"%s: No display configuration in firmware image\n",
					__func__);
			return -EINVAL;
		}
		retval = fwu_check_dp_configuration_size();
		if (retval < 0)
			return retval;
		break;
	default:
		dev_err(LOGDEV,
				"%s: Configuration not supported\n",
				__func__);
		return -EINVAL;
	}

	mutex_lock(&rmi4_data->rmi4_exp_init_mutex);

	pr_notice("%s: Start of write config process\n", __func__);

	config_area = fwu->config_area;

	retval = fwu_enter_flash_prog();
	if (retval < 0)
		goto exit;

	fwu->config_area = config_area;

	retval = fwu_erase_configuration();
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to erase config\n",
				__func__);
		goto exit;
	}

	switch (fwu->config_area) {
	case UI_CONFIG_AREA:
		retval = fwu_write_ui_configuration();
		if (retval < 0)
			goto exit;
		break;
	case DP_CONFIG_AREA:
		retval = fwu_write_dp_configuration();
		if (retval < 0)
			goto exit;
		break;
	}

	pr_notice("%s: Config written\n", __func__);

exit:
	switch (fwu->config_area) {
	case UI_CONFIG_AREA:
		fwu_reset_device();
		break;
	case DP_CONFIG_AREA:
		fwu_reset_device();
		break;
	}

	pr_notice("%s: End of write config process\n", __func__);

	mutex_unlock(&rmi4_data->rmi4_exp_init_mutex);

	return retval;
}
#endif

static bool fwu_tdat_image_format(const unsigned char *fw_image)
{
	return fw_image[0] == 0x31;
}

static void fwu_tdat_config_set(const unsigned char *data, unsigned int size,
		const unsigned char **image, unsigned int *image_size)
{
	unsigned short id;
	unsigned int length, offset;

	for (offset = 0; offset < size; offset += length+5) {
		id = (data[offset+1] << 8) | data[offset];
		length = (data[offset+4] << 8) | data[offset+3];
		if (id == 0x0001) {
			*image = &data[offset+5];
			*image_size = length;
		}
	}
}

static void fwu_tdat_section_offset(
		const unsigned char **image, unsigned int *image_size)
{
	unsigned int offset;
	offset = (*image)[0] + 1;
	*image_size -= offset;
	*image = &(*image)[offset];
}

static int fwu_parse_tdat_image(void)
{
	int ii;
	unsigned int id;
	unsigned int length, offset;
	struct image_metadata *img = &fwu->img;
	const unsigned char *section, *data = fwu->image;
	unsigned int fw_size = fwu->image_size;
	img->contains_firmware_id = false;
	img->contains_bootloader = false;
	img->contains_disp_config = false;
	img->contains_guest_code = false;
	img->contains_flash_config = false;
	img->lockdown.data = NULL;


	pr_notice("%s: Start TDAT image processing\n", __func__);

	for (ii = 0, offset = 1; offset < fw_size; offset += length+4) {
		length = (data[offset+3] << 16) |
			 (data[offset+2] << 8) | data[offset+1];

		dev_dbg(&fwu->rmi4_data->i2c_client->dev,
				"Record[%d]: length %u, offset %u\n",
				ii++, length, offset);

		if ((offset+length+4) > fw_size) {
			dev_err(&fwu->rmi4_data->i2c_client->dev,
					"Data overflow at offset %u (%u)\n",
					offset, data[offset]);
			return -EINVAL;
		}
	}

	if (offset != fw_size) {
		dev_err(&fwu->rmi4_data->i2c_client->dev,
				"Data is misaligned\n");
		return -EINVAL;
	}

	for (offset = 1; offset < fw_size; offset += length+4) {
		id = data[offset];
		length = (data[offset+3] << 16) |
			 (data[offset+2] << 8) | data[offset+1];
		section = &data[offset+4];

		switch (id) {
		case 1: /* config */
			dev_dbg(&fwu->rmi4_data->i2c_client->dev,
				"%s: Config record %d, size %u\n",
				__func__, id, length);

			fwu_tdat_config_set(section, length,
					&img->ui_config.data,
					&img->ui_config.size);
			fwu_tdat_section_offset(&img->ui_config.data,
						&img->ui_config.size);
			break;

		case 2: /* firmware */
			dev_dbg(&fwu->rmi4_data->i2c_client->dev,
				"%s: Firmware record %d, size %u\n",
				__func__,
				id,
				length);

			img->contains_firmware_id = true;
			batohui(&fwu->img.firmware_id,
					(unsigned char *)&section[1],
					SYNAPTICS_RMI4_BUILD_ID_SIZE);

			dev_dbg(&fwu->rmi4_data->i2c_client->dev,
				"%s: Firmware build ID %x\n",
				__func__,
				fwu->img.firmware_id);

			img->ui_firmware.data = section;
			img->ui_firmware.size = length;
			fwu_tdat_section_offset(&img->ui_firmware.data,
						&img->ui_firmware.size);
			break;
		default:
			dev_dbg(&fwu->rmi4_data->i2c_client->dev,
				"%s: Don't care section id %d\n",
				__func__,
				id);
			break;
		}
	}

	dev_dbg(&fwu->rmi4_data->i2c_client->dev,
		"%s: Firwmare size %u, config size %u\n",
		__func__,
		img->ui_firmware.size,
		img->ui_config.size);
	return 0;
}

static int fwu_start_reflash(void)
{
	int retval = 0;
	enum flash_area flash_area;
	const struct firmware *fw_entry = NULL;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	__pm_stay_awake(fwu->flash_wakeup_source);
	mutex_lock(&rmi4_data->rmi4_exp_init_mutex);
	pr_notice("%s: Start of reflash process\n", __func__);

	fwu->rmi4_data->set_state(fwu->rmi4_data, STATE_INIT);
	fwu_irq_enable(true);

	if (fwu->image == NULL) {
		dev_dbg(LOGDEV,
				"%s: Requesting firmware image %s\n",
				__func__, fwu->image_name);

		retval = request_firmware(&fw_entry, fwu->image_name,
				LOGDEV);
		if (retval != 0) {
			dev_err(LOGDEV,
					"%s: Firmware image %s not available\n",
					__func__, fwu->image_name);
			retval = -EINVAL;
			goto exit;
		}

		dev_dbg(LOGDEV,
				"%s: Firmware image size = %zd\n",
				__func__, fw_entry->size);

		fwu->image = fw_entry->data;
		fwu->image_size = fw_entry->size;
	}

	if (fwu_tdat_image_format(fwu->image))
		fwu_parse_tdat_image();
	else {
		retval = fwu_parse_image_info();
		if (retval < 0)
			goto exit;

		if (fwu->bl_version != fwu->img.bl_version) {
			dev_err(LOGDEV,
				"%s: Bootloader version mismatch\n",
					__func__);
			retval = -EINVAL;
			goto exit;
		}

		if (!fwu->force_update && fwu->new_partition_table) {
			dev_err(LOGDEV,
				"%s: Partition table mismatch\n",
					__func__);
			retval = -EINVAL;
			goto exit;
		}
	}

	retval = fwu_read_flash_status();
	if (retval < 0)
		goto exit;

	if (fwu->in_bl_mode) {
		sema_clear(&fwu->irq_sema);
		dev_info(LOGDEV,
				"%s: Device in bootloader mode\n",
				__func__);
	}

	if (fwu->do_lockdown && (fwu->img.lockdown.data != NULL)) {
		switch (fwu->bl_version) {
		case BL_V5:
		case BL_V6:
			retval = fwu_do_lockdown();
			if (retval < 0) {
				dev_err(LOGDEV,
						"%s: Failed to do lockdown\n",
						__func__);
			}
			fwu_reset_device();
			break;
		default:
			break;
		}
	}

	flash_area = fwu_go_nogo();

	if (flash_area != NONE) {
		retval = fwu_enter_flash_prog();
		if (retval < 0) {
			fwu_reset_device();
			goto exit;
		}

	}

	fwu->rmi4_data->set_state(fwu->rmi4_data, STATE_FLASH);

	switch (flash_area) {
	case UI_FIRMWARE:
		retval = fwu_do_reflash();
		break;
	case UI_CONFIG:
		retval = fwu_check_ui_configuration_size();
		if (retval < 0)
			break;
		fwu->config_area = UI_CONFIG_AREA;
		retval = fwu_erase_configuration();
		if (retval < 0)
			break;
		retval = fwu_write_ui_configuration();
		break;
	case NONE:
	default:
		break;
	}

	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to do reflash\n",
				__func__);
	}

exit:
	if (fw_entry)
		release_firmware(fw_entry);

	fwu_irq_enable(false);
	fwu->rmi4_data->set_state(fwu->rmi4_data, STATE_UNKNOWN);
	fwu_reset_device();
	pr_notice("%s: End of reflash process\n", __func__);

	/* Rescan PDT after flashing and before register access */
	retval = fwu_scan_pdt();
	if (retval < 0)
		dev_err(LOGDEV, "%s: Failed to scan PDT\n", __func__);

	if (!fwu->in_ub_mode) {
		retval = fwu_read_f34_queries();
		if (retval < 0)
			dev_err(LOGDEV, "%s: Failed to query F34\n", __func__);
	}

	fwu_check_intr_en(rmi4_data, &rmi4_data->intr_mask[0],
		rmi4_data->num_of_intr_regs);

	retval = fwu_get_device_config_id();
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to read device config ID\n",
				__func__);
	}

	memcpy(rmi4_data->rmi4_mod_info.config_id, fwu->config_id,
		V5V6_CONFIG_ID_SIZE);

	fwu->rmi4_data->ready_state(fwu->rmi4_data, false);
	mutex_unlock(&rmi4_data->rmi4_exp_init_mutex);
	__pm_relax(fwu->flash_wakeup_source);

	return retval;
}

static int fwu_recovery_check_status(void)
{
	int retval;
	unsigned char base;
	unsigned char status;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	base = fwu->f35_fd.data_base_addr;

	retval = synaptics_rmi4_reg_read(rmi4_data,
			base + F35_ERROR_CODE_OFFSET,
			&status,
			1);
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to read status\n",
				__func__);
		return retval;
	}

	status = status & MASK_7BIT;

	if (status != 0x00) {
		dev_err(LOGDEV,
				"%s: Recovery mode status = %d\n",
				__func__, status);
		return -EINVAL;
	}

	return 0;
}

#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_DSX_FW_UPDATE_EXTRA_SYSFS_MMI
static int fwu_recovery_erase_all(void)
{
	int retval;
	unsigned char base;
	unsigned char command = CMD_F35_ERASE_ALL;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	base = fwu->f35_fd.ctrl_base_addr;

	retval = synaptics_rmi4_reg_write(rmi4_data,
			base + F35_CHUNK_COMMAND_OFFSET,
			&command,
			sizeof(command));
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to issue erase all command\n",
				__func__);
		return retval;
	}

	msleep(F35_ERASE_ALL_WAIT_MS);

	retval = fwu_recovery_check_status();
	if (retval < 0)
		return retval;

	return 0;
}

static int fwu_recovery_write_chunk(void)
{
	int retval;
	unsigned char base;
	unsigned char chunk_number[] = {0, 0};
	unsigned char chunk_spare;
	unsigned char chunk_size;
	unsigned char buf[F35_CHUNK_SIZE + 1];
	unsigned short chunk;
	unsigned short chunk_total;
	unsigned short bytes_written = 0;
	unsigned char *chunk_ptr = (unsigned char *)fwu->image;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	base = fwu->f35_fd.ctrl_base_addr;

	retval = synaptics_rmi4_reg_write(rmi4_data,
			base + F35_CHUNK_NUM_LSB_OFFSET,
			chunk_number,
			sizeof(chunk_number));
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to write chunk number\n",
				__func__);
		return retval;
	}

	buf[sizeof(buf) - 1] = CMD_F35_WRITE_CHUNK;

	chunk_total = fwu->image_size / F35_CHUNK_SIZE;
	chunk_spare = fwu->image_size % F35_CHUNK_SIZE;
	if (chunk_spare)
		chunk_total++;

	for (chunk = 0; chunk < chunk_total; chunk++) {
		if (chunk_spare && chunk == chunk_total - 1)
			chunk_size = chunk_spare;
		else
			chunk_size = F35_CHUNK_SIZE;

		memset(buf, 0x00, F35_CHUNK_SIZE);
		secure_memcpy(buf, sizeof(buf), chunk_ptr,
					fwu->image_size - bytes_written,
					chunk_size);

		retval = synaptics_rmi4_reg_write(rmi4_data,
				base + F35_CHUNK_DATA_OFFSET,
				buf,
				sizeof(buf));
		if (retval < 0) {
			dev_err(LOGDEV,
					"%s: Failed to write chunk data (chunk %d)\n",
					__func__, chunk);
			return retval;
		}
		chunk_ptr += chunk_size;
		bytes_written += chunk_size;
	}

	retval = fwu_recovery_check_status();
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to write chunk data\n",
				__func__);
		return retval;
	}

	return 0;
}

static int fwu_recovery_reset(void)
{
	int retval;
	unsigned char base;
	unsigned char command = CMD_F35_RESET;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	base = fwu->f35_fd.ctrl_base_addr;

	retval = synaptics_rmi4_reg_write(rmi4_data,
			base + F35_CHUNK_COMMAND_OFFSET,
			&command,
			sizeof(command));
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to issue reset command\n",
				__func__);
		return retval;
	}

	msleep(F35_RESET_WAIT_MS);

	return 0;
}

static int fwu_start_recovery(void)
{
	int retval;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	mutex_lock(&rmi4_data->rmi4_exp_init_mutex);

	pr_notice("%s: Start of recovery process\n", __func__);

	__pm_stay_awake(fwu->flash_wakeup_source);

	retval = rmi4_data->irq_enable(rmi4_data, false);
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to disable interrupt\n",
				__func__);
		goto exit;
	}

	msleep(INT_DISABLE_WAIT_MS);

	fwu_irq_enable(true);

	retval = fwu_recovery_erase_all();
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to do erase all in recovery mode\n",
				__func__);
		goto exit;
	}

	pr_notice("%s: External flash erased\n", __func__);

	retval = fwu_recovery_write_chunk();
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to write chunk data in recovery mode\n",
				__func__);
		goto exit;
	}

	pr_notice("%s: Chunk data programmed\n", __func__);

	retval = fwu_recovery_reset();
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to reset device in recovery mode\n",
				__func__);
		goto exit;
	}

	pr_notice("%s: Recovery mode reset issued\n", __func__);


	retval = 0;

exit:
	fwu->rmi4_data->set_state(fwu->rmi4_data, STATE_UNKNOWN);
	fwu_reset_device();
	fwu_irq_enable(false);
	__pm_relax(fwu->flash_wakeup_source);

	pr_notice("%s: End of recovery process\n", __func__);

	mutex_unlock(&rmi4_data->rmi4_exp_init_mutex);

	fwu->rmi4_data->ready_state(fwu->rmi4_data, false);

	return retval;
}
#endif

int synaptics_fw_updater(const unsigned char *fw_data)
{
	int retval;

	if (!fwu)
		return -ENODEV;

	if (!fwu->initialized)
		return -ENODEV;

	if (fwu->in_ub_mode)
		return -ENODEV;

	fwu->image = fw_data;

	retval = fwu_start_reflash();

	fwu->image = NULL;

	return retval;
}
EXPORT_SYMBOL(synaptics_fw_updater);

#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_DSX_FW_UPDATE_EXTRA_SYSFS_MMI
static ssize_t fwu_sysfs_show_image(struct file *data_file,
		struct kobject *kobj, struct bin_attribute *attributes,
		char *buf, loff_t pos, size_t count)
{
	int retval;

	if (count < fwu->config_size) {
		dev_err(LOGDEV,
				"%s: Not enough space (%zd bytes) in buffer\n",
				__func__, count);
		return -EINVAL;
	}

	retval = secure_memcpy(buf, count, fwu->read_config_buf,
			fwu->read_config_buf_size, fwu->config_size);
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to copy config data\n",
				__func__);
		return retval;
	}

	return fwu->config_size;
}

static ssize_t fwu_sysfs_store_image(struct file *data_file,
		struct kobject *kobj, struct bin_attribute *attributes,
		char *buf, loff_t pos, size_t count)
{
	int retval;

	retval = secure_memcpy(&fwu->ext_data_source[fwu->data_pos],
			fwu->image_size - fwu->data_pos, buf, count, count);
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to copy image data\n",
				__func__);
		return retval;
	}

	fwu->data_pos += count;

	return count;
}

static ssize_t fwu_sysfs_do_recovery_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	unsigned int input;

	if (sscanf(buf, "%u", &input) != 1) {
		retval = -EINVAL;
		goto exit;
	}

	if (!fwu->in_ub_mode) {
		dev_err(LOGDEV,
				"%s: Not in microbootloader mode\n",
				__func__);
		retval = -EINVAL;
		goto exit;
	}

	if (!fwu->ext_data_source)
		return -EINVAL;
	else
		fwu->image = fwu->ext_data_source;

	retval = fwu_start_recovery();
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to do recovery\n",
				__func__);
		goto exit;
	}

	retval = count;

exit:
	kfree(fwu->ext_data_source);
	fwu->ext_data_source = NULL;
	fwu->image = NULL;
	return retval;
}
#endif

static ssize_t fwu_sysfs_do_reflash_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	char prefix[SYNAPTICS_RMI4_FILENAME_SIZE] = "synaptics";
	char template[SYNAPTICS_RMI4_FILENAME_SIZE];
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;
	struct synaptics_rmi4_device_info *rmi;

	rmi = &(rmi4_data->rmi4_mod_info);

	if (count > MAX_IMAGE_NAME_LEN) {
		dev_err(&rmi4_data->i2c_client->dev,
			"%s: FW filename is too long\n",
			__func__);
		retval = -EINVAL;
		goto exit;
	}

	if (!fwu->force_update) {
		if (strncmp(buf, prefix,
			strnlen(prefix, sizeof(prefix)))) {
			dev_err(&rmi4_data->i2c_client->dev,
				"%s: FW does not belong to Synaptics\n",
				__func__);
			retval = -EINVAL;
			goto exit;
		}

		snprintf(template, sizeof(template), "-%s-",
						rmi->product_id_string);
		if (!strnstr(buf + strnlen(prefix, sizeof(prefix)), template,
			count)) {
			dev_err(&rmi4_data->i2c_client->dev,
				"%s: FW does not belong to %s\n",
				__func__,
				rmi->product_id_string);
			retval = -EINVAL;
			goto exit;
		}
	}

	strlcpy(fwu->image_name, buf, count);
	dev_dbg(&rmi4_data->i2c_client->dev,
			"%s: FW filename: %s\n",
			__func__,
			fwu->image_name);

	retval = synaptics_fw_updater(fwu->ext_data_source);
	if (retval < 0) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to do reflash\n",
				__func__);
		goto exit;
	}

	retval = count;

exit:
	kfree(fwu->ext_data_source);
	fwu->ext_data_source = NULL;
	fwu->image = NULL;
	fwu->image_name[0] = 0;
	fwu->force_update = FORCE_UPDATE;
	fwu->do_lockdown = DO_LOCKDOWN;
	return retval;
}

static ssize_t fwu_sysfs_force_reflash_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int input;

	if (sscanf(buf, "%u", &input) != 1)
		return -EINVAL;

	if (input != 1)
		return -EINVAL;

	fwu->force_update = true;

	return count;
}

#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_DSX_FW_UPDATE_EXTRA_SYSFS_MMI
static ssize_t fwu_sysfs_write_config_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	unsigned int input;

	if (sscanf(buf, "%u", &input) != 1) {
		retval = -EINVAL;
		goto exit;
	}

	if (input != 1) {
		retval = -EINVAL;
		goto exit;
	}

	if (fwu->in_ub_mode) {
		dev_err(LOGDEV,
				"%s: In microbootloader mode\n",
				__func__);
		retval = -EINVAL;
		goto exit;
	}

	if (!fwu->ext_data_source)
		return -EINVAL;
	else
		fwu->image = fwu->ext_data_source;

	retval = fwu_start_write_config();
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to write config\n",
				__func__);
		goto exit;
	}

	retval = count;

exit:
	kfree(fwu->ext_data_source);
	fwu->ext_data_source = NULL;
	fwu->image = NULL;
	return retval;
}

static ssize_t fwu_sysfs_read_config_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	unsigned int input;

	if (sscanf(buf, "%u", &input) != 1)
		return -EINVAL;

	if (input != 1)
		return -EINVAL;

	if (fwu->in_ub_mode) {
		dev_err(LOGDEV,
				"%s: In microbootloader mode\n",
				__func__);
		return -EINVAL;
	}

	retval = fwu_do_read_config();
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to read config\n",
				__func__);
		return retval;
	}

	return count;
}

static ssize_t fwu_sysfs_config_area_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	unsigned long config_area;

	retval = sstrtoul(buf, 10, &config_area);
	if (retval)
		return retval;

	fwu->config_area = config_area;

	return count;
}

static ssize_t fwu_sysfs_image_name_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;

	retval = secure_memcpy(fwu->image_name, MAX_IMAGE_NAME_LEN,
			buf, count, count);
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to copy image file name\n",
				__func__);
		return retval;
	}

	return count;
}

static ssize_t fwu_sysfs_image_size_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	unsigned long size;

	retval = sstrtoul(buf, 10, &size);
	if (retval)
		return retval;

	fwu->image_size = size;
	fwu->data_pos = 0;

	kfree(fwu->ext_data_source);
	fwu->ext_data_source = kzalloc(fwu->image_size, GFP_KERNEL);
	if (!fwu->ext_data_source) {
		dev_err(LOGDEV,
				"%s: Failed to alloc mem for image data\n",
				__func__);
		return -ENOMEM;
	}

	return count;
}

static ssize_t fwu_sysfs_block_size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", fwu->block_size);
}

static ssize_t fwu_sysfs_firmware_block_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", fwu->blkcount.ui_firmware);
}

static ssize_t fwu_sysfs_configuration_block_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", fwu->blkcount.ui_config);
}

static ssize_t fwu_sysfs_disp_config_block_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", fwu->blkcount.dp_config);
}

static ssize_t fwu_sysfs_perm_config_block_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", fwu->blkcount.pm_config);
}

static ssize_t fwu_sysfs_bl_config_block_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", fwu->blkcount.bl_config);
}

static ssize_t fwu_sysfs_guest_code_block_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", fwu->blkcount.guest_code);
}

static ssize_t fwu_sysfs_write_guest_code_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	unsigned int input;

	if (sscanf(buf, "%u", &input) != 1) {
		retval = -EINVAL;
		goto exit;
	}

	if (input != 1) {
		retval = -EINVAL;
		goto exit;
	}

	if (fwu->in_ub_mode) {
		dev_err(LOGDEV,
				"%s: In microbootloader mode\n",
				__func__);
		retval = -EINVAL;
		goto exit;
	}

	if (!fwu->ext_data_source)
		return -EINVAL;
	else
		fwu->image = fwu->ext_data_source;

	retval = fwu_start_write_guest_code();
	if (retval < 0) {
		dev_err(LOGDEV,
				"%s: Failed to write guest code\n",
				__func__);
		goto exit;
	}

	retval = count;

exit:
	kfree(fwu->ext_data_source);
	fwu->ext_data_source = NULL;
	fwu->image = NULL;
	return retval;
}
#endif

static ssize_t fwu_sysfs_erase_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int input;
	int retval;
	struct synaptics_rmi4_data *rmi4_data = fwu->rmi4_data;

	if (sscanf(buf, "%u", &input) != 1)
		return -EINVAL;

	if (input != 1)
		return -EINVAL;

	__pm_stay_awake(fwu->flash_wakeup_source);
	mutex_lock(&rmi4_data->rmi4_exp_init_mutex);

	if (!fwu->in_bl_mode) {
		fwu_irq_enable(true);

		retval = fwu_write_f34_command(CMD_ENABLE_FLASH_PROG);
		if (retval < 0)
			goto reset_and_exit;

		retval = fwu_wait_for_idle(ENABLE_WAIT_MS);
		fwu_irq_enable(false);
		if (retval < 0 || !fwu->in_bl_mode)
			goto reset_and_exit;
	}

	fwu->rmi4_data->set_state(fwu->rmi4_data, STATE_INIT);
	fwu_irq_enable(true);

	retval = fwu_erase_all();
	if (retval < 0)
		pr_err("%s: ERASE_ALL failed\n", __func__);

	fwu_irq_enable(false);
	fwu->rmi4_data->set_state(fwu->rmi4_data, STATE_UNKNOWN);

reset_and_exit:
	fwu_reset_device();
	pr_notice("%s: End of reflash process\n", __func__);

	/* Rescan PDT after flashing and before register access */
	retval = fwu_scan_pdt();
	if (retval < 0)
		dev_err(LOGDEV, "%s: Failed to scan PDT\n", __func__);

	fwu->rmi4_data->ready_state(fwu->rmi4_data, false);
	mutex_unlock(&rmi4_data->rmi4_exp_init_mutex);
	__pm_relax(fwu->flash_wakeup_source);

	return count;
}

static void synaptics_rmi4_fwu_attn(struct synaptics_rmi4_data *rmi4_data,
		unsigned char intr_mask)
{
	if (!fwu)
		return;

	if (fwu->intr_mask & intr_mask)
		fwu_read_flash_status();

	return;
}

static int synaptics_rmi4_fwu_init(struct synaptics_rmi4_data *rmi4_data)
{
	int retval, attr_count;
	struct pdt_properties pdt_props;

	if (fwu) {
		dev_dbg(LOGDEV,
				"%s: Handle already exists\n",
				__func__);
		return 0;
	}

	fwu = kzalloc(sizeof(*fwu), GFP_KERNEL);
	if (!fwu) {
		dev_err(LOGDEV,
				"%s: Failed to alloc mem for fwu\n",
				__func__);
		retval = -ENOMEM;
		goto exit;
	}

	fwu->image_name = kzalloc(MAX_IMAGE_NAME_LEN, GFP_KERNEL);
	if (!fwu->image_name) {
		dev_err(LOGDEV,
				"%s: Failed to alloc mem for image name\n",
				__func__);
		retval = -ENOMEM;
		goto exit_free_fwu;
	}

	fwu->rmi4_data = rmi4_data;
	sema_init(&fwu->irq_sema, 0);
	mutex_init(&rmi4_data->rmi4_exp_init_mutex);

	retval = synaptics_rmi4_reg_read(rmi4_data,
			PDT_PROPS,
			pdt_props.data,
			sizeof(pdt_props.data));
	if (retval < 0) {
		dev_dbg(LOGDEV,
			"%s: Failed to read PDT properties, assuming 0x00\n",
			__func__);
	} else if (pdt_props.has_bsr) {
		dev_err(LOGDEV,
			"%s: Reflash for LTS not currently supported\n",
			__func__);
		retval = -ENODEV;
		goto exit_free_mem;
	}

	retval = fwu_scan_pdt();
	if (retval < 0)
		goto exit_free_mem;

	if (!fwu->in_ub_mode) {
		fwu_irq_enable(true);
		retval = fwu_read_f34_queries();
		fwu_irq_enable(false);
		if (retval < 0)
			goto exit_free_mem;

		retval = fwu_get_device_config_id();
		if (retval < 0) {
			dev_err(LOGDEV,
				"%s: Failed to read device config ID\n",
				__func__);
			goto exit_free_mem;
		}
	}

	fwu->force_update = FORCE_UPDATE;
	fwu->do_lockdown = DO_LOCKDOWN;

	fwu->flash_wakeup_source = wakeup_source_register(NULL, "synaptics_fw_flash");

	fwu->initialized = true;

#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_DSX_FW_UPDATE_EXTRA_SYSFS_MMI
	retval = sysfs_create_bin_file(SYSFS_KOBJ, &dev_attr_data);
	if (retval < 0) {
		dev_err(LOGDEV,
			"%s: Failed to create sysfs bin file\n",
			__func__);
		goto exit_free_mem;
	}
#endif
	for (attr_count = 0; attr_count < ARRAY_SIZE(attrs); attr_count++) {
		retval = sysfs_create_file(SYSFS_KOBJ, &attrs[attr_count].attr);
		if (retval < 0) {
			dev_err(LOGDEV,
					"%s: Failed to create sysfs attributes\n",
					__func__);
			retval = -ENODEV;
			goto exit_remove_attrs;
		}
	}

	return 0;

exit_remove_attrs:
	for (attr_count--; attr_count >= 0; attr_count--)
		sysfs_remove_file(SYSFS_KOBJ, &attrs[attr_count].attr);
#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_DSX_FW_UPDATE_EXTRA_SYSFS_MMI
	sysfs_remove_bin_file(SYSFS_KOBJ, &dev_attr_data);
#endif
exit_free_mem:
	kfree(fwu->image_name);

exit_free_fwu:
	kfree(fwu);
	fwu = NULL;

exit:
	return retval;
}

static void synaptics_rmi4_fwu_remove(struct synaptics_rmi4_data *rmi4_data)
{
	int attr_count;

	if (!fwu)
		goto exit;

	for (attr_count = 0; attr_count < ARRAY_SIZE(attrs); attr_count++) {
		sysfs_remove_file(SYSFS_KOBJ, &attrs[attr_count].attr);
	}

        if (fwu->has_erase_all)
            sysfs_remove_file(SYSFS_KOBJ, &erase_attr[0].attr);

#ifdef CONFIG_TOUCHSCREEN_SYNAPTICS_DSX_FW_UPDATE_EXTRA_SYSFS_MMI
	sysfs_remove_bin_file(SYSFS_KOBJ, &dev_attr_data);
#endif
	kfree(fwu->read_config_buf);
	kfree(fwu->image_name);
	kfree(fwu);
	fwu = NULL;

exit:
	complete(&fwu_remove_complete);

	return;
}

static int synaptics_rmi4_fwu_flash_status(
	struct synaptics_rmi4_data *rmi4_data)
{
	int istatus = 0, retval;
	unsigned char status;

	if (fwu == NULL)
		return -EINVAL;

	rmi4_data = fwu->rmi4_data;
	retval = synaptics_rmi4_reg_read(rmi4_data,
			fwu->f34_fd.data_base_addr + fwu->off.flash_status,
			&status,
			sizeof(status));
	if (retval < 0) {
		dev_err(LOGDEV,
			"%s: Failed to read flash status\n", __func__);
		return retval;
	}
	istatus = status >> 7;

	return istatus;
}

static int __init rmi4_fw_update_module_init(void)
{
	synaptics_rmi4_new_function(RMI_FW_UPDATER, true,
		synaptics_rmi4_fwu_init,
		synaptics_rmi4_fwu_remove,
		synaptics_rmi4_fwu_attn,
		synaptics_rmi4_fwu_flash_status,
		IC_MODE_ANY);

	return 0;
}

static void __exit rmi4_fw_update_module_exit(void)
{
	init_completion(&fwu_remove_complete);
	synaptics_rmi4_new_function(RMI_FW_UPDATER, false,
		synaptics_rmi4_fwu_init,
		synaptics_rmi4_fwu_remove,
		synaptics_rmi4_fwu_attn,
		synaptics_rmi4_fwu_flash_status,
		IC_MODE_ANY);

	wait_for_completion(&fwu_remove_complete);

	return;
}

module_init(rmi4_fw_update_module_init);
module_exit(rmi4_fw_update_module_exit);

MODULE_AUTHOR("Synaptics, Inc.");
MODULE_DESCRIPTION("Synaptics DSX FW Update Module");
MODULE_LICENSE("GPL v2");
