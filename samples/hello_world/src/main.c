/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <zephyr.h>
#include <misc/printk.h>
#include "mgmt_os/mgmt_os.h"

void main(void)
{
    int rc;

	printk("Hello World! %s\n", CONFIG_ARCH);

    rc = mgmt_os_group_register();
    assert(rc == 0);
}
