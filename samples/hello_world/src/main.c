/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <zephyr.h>
#include <string.h>
#include <stdlib.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/conn.h>
#include "console/uart_mcumgr.h"
#include "mgmt_os/mgmt_os.h"
#include "img/img.h"
#include "zephyr_smp/zephyr_smp.h"
#include "znp/znp.h"
 
K_FIFO_DEFINE(fifo_reqs);

static struct zephyr_smp_transport uart_mcumgr_zst;

static int num_alloced;

/* XXX: TEMP */
//static struct bt_conn *prev_conn;

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

static int
tx_pkt_uart(struct zephyr_smp_transport *zst, struct zephyr_nmgr_pkt *pkt)
{
    int rc;

    rc = uart_mcumgr_send(pkt->data, pkt->len);
    k_free(pkt);

    return rc;
}

static void
recv_cb(const uint8_t *buf, size_t len)
{
    struct zephyr_nmgr_pkt *pkt;

    pkt = alloc_pkt();
    memcpy(pkt->data, buf, len);
    pkt->len = len;

    zephyr_smp_rx_req(&uart_mcumgr_zst, pkt);
}

#if 0
static void
bt_recv_cb(struct bt_conn *conn, const u8_t *buf, size_t len)
{
    struct zephyr_nmgr_pkt *pkt;

    bt_conn_ref(conn);
    prev_conn = conn;

    pkt = alloc_pkt();
    memcpy(pkt->data, buf, len);
    pkt->len = len;

    k_fifo_put(&fifo_reqs, pkt);
    k_work_submit(&work_process_reqs);
}
#endif

void main(void)
{
    int rc;

    rc = mgmt_os_group_register();
    assert(rc == 0);

    rc = img_group_register();
    assert(rc == 0);

    uart_mcumgr_register(recv_cb);

    zephyr_smp_transport_init(&uart_mcumgr_zst, tx_pkt_uart, NULL);

    while (1) { }
}
