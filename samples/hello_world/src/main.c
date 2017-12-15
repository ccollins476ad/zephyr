/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <zephyr.h>
#include <misc/printk.h>
#include <stdlib.h>
#include "console/uart_nmgr.h"
#include "mgmt_os/mgmt_os.h"
#include "znp/znp.h"
 
static struct zephyr_nmgr_pkt *
alloc_pkt(void)
{
    struct zephyr_nmgr_pkt *pkt;

    pkt = malloc(sizeof *pkt);
    assert(pkt != NULL);
    pkt->len = 0;

    return pkt;
}

static void
recv_cb(const uint8_t *buf, size_t len)
{
    struct zephyr_nmgr_pkt *pkt;
    int rc;

    pkt = alloc_pkt();
    memcpy(pkt->data, buf, len);
    pkt->len = len;

    rc = zephyr_nmgr_process_packet(pkt);
    assert(rc == 0);
}

void main(void)
{
    int rc;

	printk("Hello World! %s\n", CONFIG_ARCH);

    rc = mgmt_os_group_register();
    assert(rc == 0);

    uart_nmgr_register(recv_cb);

    while (1) { }
}
