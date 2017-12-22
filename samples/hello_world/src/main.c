/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <zephyr.h>
#include <string.h>
#include <stdlib.h>
#include "console/uart_nmgr.h"
#include "mgmt_os/mgmt_os.h"
#include "imgmgr/imgmgr.h"
#include "zephyr_nmgr/zephyr_nmgr.h"
#include "znp/znp.h"
 
K_FIFO_DEFINE(fifo_reqs);

static struct k_work work_process_reqs;

static int num_alloced;

static struct zephyr_nmgr_pkt *
alloc_pkt(void)
{
    struct zephyr_nmgr_pkt *pkt;

    pkt = k_malloc(sizeof *pkt);
    assert(pkt != NULL);
    pkt->len = 0;

    num_alloced++;
    return pkt;
}

static void
work_handler_process_reqs(struct k_work *work)
{
    struct zephyr_nmgr_pkt *pkt;

    while ((pkt = k_fifo_get(&fifo_reqs, K_NO_WAIT)) != NULL) {
        zephyr_nmgr_process_packet(pkt);
    }
}

static void
recv_cb(const uint8_t *buf, size_t len)
{
    struct zephyr_nmgr_pkt *pkt;

    pkt = alloc_pkt();
    memcpy(pkt->data, buf, len);
    pkt->len = len;

    k_fifo_put(&fifo_reqs, pkt);
    k_work_submit(&work_process_reqs);
}

void main(void)
{
    int rc;

    k_work_init(&work_process_reqs, work_handler_process_reqs);

    rc = mgmt_os_group_register();
    assert(rc == 0);

    rc = imgmgr_group_register();
    assert(rc == 0);

    uart_nmgr_register(recv_cb);

    while (1) { }
}
