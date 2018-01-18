/*
 * Copyright (c) 2017 Nordic Semiconductor ASA
 * Copyright (c) 2016 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __MCUBOOT_H__
#define __MCUBOOT_H__

/** Attempt to boot the contents of slot 0. */
#define BOOT_SWAP_TYPE_NONE     1

/** Swap to slot 1.  Absent a confirm command, revert back on next boot. */
#define BOOT_SWAP_TYPE_TEST     2

/** Swap to slot 1, and permanently switch to booting its contents. */
#define BOOT_SWAP_TYPE_PERM     3

/** Swap back to alternate slot.  A confirm changes this state to NONE. */
#define BOOT_SWAP_TYPE_REVERT   4

/** Swap failed because image to be run is not valid */
#define BOOT_SWAP_TYPE_FAIL     5

/**
 * @brief Marks the image in slot 0 as confirmed. The system will continue
 * booting into the image in slot 0 until told to boot from a different slot.
 *
 * This call is expected to be used by the application running on trial.
 *
 * @return 0 on success, negative errno code on fail.
 */
int boot_write_img_confirmed(void);

/**
 * @brief Determines the action, if any, that mcuboot will take on the next
 * reboot.
 * @return a BOOT_SWAP_TYPE_[...] constant on success, negative errno code on
 * fail.
 */
int boot_swap_type(void);

/**
 * @brief Marks the image in slot 1 as pending. On the next reboot, the system
 * will perform a boot of the slot 1 image.
 *
 * @param permanent Whether the image should be used permanently or
 * only tested once:
 *   0=run image once, then confirm or revert.
 *   1=run image forever.
 * @return 0 on success, negative errno code on fail.
 */
int boot_request_upgrade(int permanent);

/**
 * @brief Erase the image Bank.
 *
 * @param bank_offset address of the image bank
 * @return 0 on success, negative errno code on fail.
 */
int boot_erase_img_bank(u32_t bank_offset);

#endif	/* __MCUBOOT_H__ */
