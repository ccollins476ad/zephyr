/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <zephyr.h>
#include <misc/printk.h>
#include "mgmt_os/mgmt_os.h"
#include "znp/znp.h"

void main(void)
{
    struct zephyr_nmgr_pkt *pkt;
    int rc;

	printk("Hello World! %s\n", CONFIG_ARCH);

    rc = mgmt_os_group_register();
    assert(rc == 0);

    pkt = malloc(sizeof *pkt);
    assert(pkt != NULL);

    memcpy(pkt->data, ((uint8_t[]){
               0x02, 0x00, 0x00, 0x09, 0x00, 0x00, 0x42, 0x00,
               0xa1, 0x61, 0x64, 0x65, 0x68, 0x65, 0x6c, 0x6c,
               0x6f
           }), 17);
    pkt->len = 17;

    rc = zephyr_nmgr_process_packet(pkt);
    assert(rc == 0);
}
