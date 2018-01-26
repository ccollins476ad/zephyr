/*
 * Copyright (c) 2017 Nordic Semiconductor ASA
 * Copyright (c) 2016-2017 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <stddef.h>
#include <errno.h>
#include <string.h>
#include <flash.h>
#include <zephyr.h>
#include <init.h>

#include <misc/__assert.h>
#include <board.h>
#include <dfu/mcuboot.h>

/*
 * Helpers for image trailer, as defined by mcuboot.
 * Image trailer consists of sequence of fields:
 *   u8_t copy_done
 *   u8_t padding_1[BOOT_MAX_ALIGN - 1]
 *   u8_t image_ok
 *   u8_t padding_2[BOOT_MAX_ALIGN - 1]
 *   u8_t magic[16]
 */

/*
 * Strict defines: Defines in block below must be equal to coresponding
 * mcuboot defines
 */
#define BOOT_MAX_ALIGN 8
#define BOOT_MAGIC_SZ  16
#define BOOT_FLAG_SET 0x01
#define BOOT_FLAG_UNSET 0xff
/* end_of Strict defines */

#define BOOT_MAGIC_GOOD  1
#define BOOT_MAGIC_BAD   2
#define BOOT_MAGIC_UNSET 3

#define BOOT_FLAG_IMAGE_OK 0
#define BOOT_FLAG_COPY_DONE 1

#define FLASH_MIN_WRITE_SIZE FLASH_WRITE_BLOCK_SIZE
#define FLASH_BANK0_OFFSET FLASH_AREA_IMAGE_0_OFFSET

/* FLASH_AREA_IMAGE_XX_YY values used below are auto-generated thanks to DT */
#define FLASH_BANK_SIZE FLASH_AREA_IMAGE_0_SIZE
#define FLASH_BANK1_OFFSET FLASH_AREA_IMAGE_1_OFFSET
#define FLASH_STATE_OFFSET (FLASH_AREA_IMAGE_SCRATCH_OFFSET +\
			    FLASH_AREA_IMAGE_SCRATCH_SIZE)

#define VERSION_OFFSET(bank_offs) (bank_offs + 20)
#define COPY_DONE_OFFS(bank_offs) (bank_offs + FLASH_BANK_SIZE -\
				   BOOT_MAGIC_SZ - BOOT_MAX_ALIGN * 2)

#define IMAGE_OK_OFFS(bank_offs) (bank_offs + FLASH_BANK_SIZE - BOOT_MAGIC_SZ -\
				  BOOT_MAX_ALIGN)
#define MAGIC_OFFS(bank_offs) (bank_offs + FLASH_BANK_SIZE - BOOT_MAGIC_SZ)

static const u32_t boot_img_magic[4] = {
	0xf395c277,
	0x7fefd260,
	0x0f505235,
	0x8079b62c,
};

struct boot_swap_table {
	/** * For each field, a value of 0 means "any". */
	uint8_t magic_slot0;
	uint8_t magic_slot1;
	uint8_t image_ok_slot0;
	uint8_t image_ok_slot1;
	uint8_t copy_done_slot0;
	
	uint8_t swap_type;
};

/** Represents the management state of a single image slot. */
struct boot_swap_state {
	uint8_t magic;  /* One of the BOOT_MAGIC_[...] values. */
	uint8_t copy_done;
	uint8_t image_ok;
};

/**
 * This set of tables maps image trailer contents to swap operation type.
 * When searching for a match, these tables must be iterated sequentially.
 */
static const struct boot_swap_table boot_swap_tables[] = {
	{
		/*          | slot-0     | slot-1     |
		 *----------+------------+------------|
		 *    magic | Any        | Good       |
		 * image-ok | Any        | Unset      |
		 * ---------+------------+------------+
		 * swap: test                         |
		 * -----------------------------------'
		 */
		.magic_slot0 =      0,
		.magic_slot1 =      BOOT_MAGIC_GOOD,
		.image_ok_slot0 =   0,
		.image_ok_slot1 =   0xff,
		.copy_done_slot0 =  0,
		.swap_type =        BOOT_SWAP_TYPE_TEST,
	},
	{
		/*          | slot-0     | slot-1     |
		 *----------+------------+------------|
		 *    magic | Any        | Good       |
		 * image-ok | Any        | 0x01       |
		 * ---------+------------+------------+
		 * swap: permanent                    |
		 * -----------------------------------'
		 */
		.magic_slot0 =      0,
		.magic_slot1 =      BOOT_MAGIC_GOOD,
		.image_ok_slot0 =   0,
		.image_ok_slot1 =   0x01,
		.copy_done_slot0 =  0,
		.swap_type =        BOOT_SWAP_TYPE_PERM,
	},
	{
		/*          | slot-0     | slot-1     |
		 *----------+------------+------------|
		 *    magic | Good       | Unset      |
		 * image-ok | Unset      | Any        |
		 * ---------+------------+------------+
		 * swap: revert (test image running)  |
		 * -----------------------------------'
		 */
		.magic_slot0 =      BOOT_MAGIC_GOOD,
		.magic_slot1 =      BOOT_MAGIC_UNSET,
		.image_ok_slot0 =   0xff,
		.image_ok_slot1 =   0,
		.copy_done_slot0 =  0x01,
		.swap_type =        BOOT_SWAP_TYPE_REVERT,
	},
};
#define BOOT_SWAP_TABLES_COUNT \
	(sizeof boot_swap_tables / sizeof boot_swap_tables[0])

static struct device *flash_dev;

static int boot_flag_offs(int flag, u32_t bank_offs, u32_t *offs)
{
	switch (flag) {
	case BOOT_FLAG_COPY_DONE:
		*offs = COPY_DONE_OFFS(bank_offs);
		return 0;
	case BOOT_FLAG_IMAGE_OK:
		*offs = IMAGE_OK_OFFS(bank_offs);
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int boot_flash_write(off_t offs, const void *data, size_t len)
{
	int rc;

	rc = flash_write_protection_set(flash_dev, false);
	if (rc) {
		return rc;
	}

	rc = flash_write(flash_dev, offs, data, len);
	if (rc) {
		return rc;
	}

	rc = flash_write_protection_set(flash_dev, true);

	return rc;
}

static int boot_flag_write(int flag, u32_t bank_offs)
{
	u8_t buf[FLASH_MIN_WRITE_SIZE];
	u32_t offs;
	int rc;

	rc = boot_flag_offs(flag, bank_offs, &offs);
	if (rc != 0) {
		return rc;
	}

	memset(buf, BOOT_FLAG_UNSET, sizeof(buf));
	buf[0] = BOOT_FLAG_SET;

	rc = boot_flash_write(offs, buf, sizeof(buf));

	return rc;
}

static int boot_flag_read(int flag, u32_t bank_offs)
{
	u32_t offs;
	int rc;
	u8_t flag_val;

	rc = boot_flag_offs(flag, bank_offs, &offs);
	if (rc != 0) {
		return rc;
	}

	rc = flash_read(flash_dev, offs, &flag_val, sizeof(flag_val));
	if (rc != 0) {
		return rc;
	}

	return flag_val;
}

static int boot_image_ok_read(u32_t bank_offs)
{
	return boot_flag_read(BOOT_FLAG_IMAGE_OK, bank_offs);
}

static int boot_image_ok_write(u32_t bank_offs)
{
	return boot_flag_write(BOOT_FLAG_IMAGE_OK, bank_offs);
}

static int boot_copy_done_read(u32_t bank_offs)
{
	return boot_flag_read(BOOT_FLAG_COPY_DONE, bank_offs);
}

static int boot_version_read(u32_t bank_offs, struct image_version *out_ver)
{
	u32_t offs;

	offs = VERSION_OFFSET(bank_offs);
	return flash_read(flash_dev, offs, out_ver, sizeof(*out_ver));
}

static int boot_magic_write(u32_t bank_offs)
{
	u32_t offs;
	int rc;

	offs = MAGIC_OFFS(bank_offs);

	rc = boot_flash_write(offs, boot_img_magic, BOOT_MAGIC_SZ);

	return rc;
}

static int boot_magic_code_check(const u32_t *magic)
{
	int i;

	if (memcmp(magic, boot_img_magic, sizeof(boot_img_magic)) == 0) {
		return BOOT_MAGIC_GOOD;
	}

	for (i = 0; i < ARRAY_SIZE(boot_img_magic); i++) {
		if (magic[i] != 0xffffffff) {
			return BOOT_MAGIC_BAD;
		}
	}

	return BOOT_MAGIC_UNSET;
}

static int boot_magic_state_read(u32_t bank_offs)
{
	u32_t magic[4];
	u32_t offs;
	int rc;

	offs = MAGIC_OFFS(bank_offs);
	rc = flash_read(flash_dev, offs, magic, sizeof(magic));
	if (rc != 0) {
		return rc;
	}

	return boot_magic_code_check(magic);
}

static int boot_read_swap_state(u32_t bank_offs, struct boot_swap_state *state)
{
	int rc;

	rc = boot_magic_state_read(bank_offs);
	if (rc < 0) {
		return rc;
	}
	state->magic = rc;

	if (bank_offs != FLASH_AREA_IMAGE_SCRATCH_OFFSET) {
		rc = boot_copy_done_read(bank_offs);
		if (rc < 0) {
			return rc;
		}
		state->copy_done = rc;
	}

	rc = boot_image_ok_read(bank_offs);
	if (rc < 0) {
		return rc;
	}
	state->image_ok = rc;

	return 0;
}

int boot_current_image_version(struct image_version *out_ver)
{
	return boot_version_read(FLASH_BANK0_OFFSET, out_ver);
}

int boot_swap_type(void)
{
	const struct boot_swap_table *table;
	struct boot_swap_state state_slot0;
	struct boot_swap_state state_slot1;
	int rc;
	int i;
	
	rc = boot_read_swap_state(FLASH_BANK0_OFFSET, &state_slot0);
	if (rc != 0) {
		return rc;
	}
	
	rc = boot_read_swap_state(FLASH_BANK1_OFFSET, &state_slot1);
	if (rc != 0) {
		return rc;
	}
	
	for (i = 0; i < BOOT_SWAP_TABLES_COUNT; i++) {
		table = boot_swap_tables + i;
	
		if ((table->magic_slot0     == 0    ||
		     table->magic_slot0     == state_slot0.magic)           &&
		    (table->magic_slot1     == 0    ||
		     table->magic_slot1     == state_slot1.magic)           &&
		    (table->image_ok_slot0  == 0    ||
		     table->image_ok_slot0  == state_slot0.image_ok)        &&
		    (table->image_ok_slot1  == 0    ||
		     table->image_ok_slot1  == state_slot1.image_ok)        &&
		    (table->copy_done_slot0 == 0    ||
		     table->copy_done_slot0 == state_slot0.copy_done)) {
	
			assert(table->swap_type == BOOT_SWAP_TYPE_TEST ||
			       table->swap_type == BOOT_SWAP_TYPE_PERM ||
			       table->swap_type == BOOT_SWAP_TYPE_REVERT);
			return table->swap_type;
		}
	}
	
	return BOOT_SWAP_TYPE_NONE;
}

int boot_request_upgrade(int permanent)
{
	int rc;

	rc = boot_magic_write(FLASH_BANK1_OFFSET);
	if (rc == 0 && permanent) {
		rc = boot_image_ok_write(FLASH_BANK1_OFFSET);
	}

	return rc;
}

int boot_write_img_confirmed(void)
{
	int rc;

	switch (boot_magic_state_read(FLASH_BANK0_OFFSET)) {
	case BOOT_MAGIC_GOOD:
		/* Confirm needed; proceed. */
		break;

	case BOOT_MAGIC_UNSET:
		/* Already confirmed. */
		return 0;

	case BOOT_MAGIC_BAD:
		/* Unexpected state. */
		return -EFAULT;
	}

	if (boot_image_ok_read(FLASH_BANK0_OFFSET) != BOOT_FLAG_UNSET) {
		/* Already confirmed. */
		return 0;
	}

	rc = boot_image_ok_write(FLASH_BANK0_OFFSET);

	return rc;
}

int boot_erase_img_bank(u32_t bank_offset)
{
	int rc;

	rc = flash_write_protection_set(flash_dev, false);
	if (rc) {
		return rc;
	}

	rc = flash_erase(flash_dev, bank_offset, FLASH_BANK_SIZE);
	if (rc) {
		return rc;
	}

	rc = flash_write_protection_set(flash_dev, true);

	return rc;
}

static int boot_init(struct device *dev)
{
	ARG_UNUSED(dev);
	flash_dev = device_get_binding(FLASH_DRIVER_NAME);
	if (!flash_dev) {
		return -ENODEV;
	}
	return 0;
}

SYS_INIT(boot_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
