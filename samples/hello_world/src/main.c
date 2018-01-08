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
#include <bluetooth/gatt.h>
#include "console/uart_mcumgr.h"
#include "os_mgmt/os_mgmt.h"
#include "img_mgmt/img_mgmt.h"
#include "mgmt/smp_bt.h"
#include "zephyr_smp/zephyr_smp.h"
#include "mgmt/znp.h"
 
static struct zephyr_smp_transport uart_zst;

static int num_alloced;

#define DEVICE_NAME         CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN     (sizeof(DEVICE_NAME) - 1)

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, 0x0d, 0x18, 0x0f, 0x18, 0x05, 0x18),
};

static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

struct zephyr_nmgr_pkt *
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

    zephyr_smp_rx_req(&uart_zst, pkt);
}

static void advertise(void)
{
    int rc;

    bt_le_adv_stop();

    rc = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad),
                  sd, ARRAY_SIZE(sd));
    if (rc) {
        printk("Advertising failed to start (rc %d)\n", rc);
        return;
    }

    printk("Advertising successfully started\n");
}

static void connected(struct bt_conn *conn, u8_t err)
{
    if (err) {
        printk("Connection failed (err %u)\n", err);
    } else {
        printk("Connected\n");
    }
}

static void disconnected(struct bt_conn *conn, u8_t reason)
{
    printk("Disconnected (reason %u)\n", reason);
    advertise();
}

static struct bt_conn_cb conn_callbacks = {
    .connected = connected,
    .disconnected = disconnected,
};

static void bt_ready(int err)
{
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return;
    }

    printk("Bluetooth initialized\n");

    advertise();
}

void main(void)
{
    int rc;

    rc = os_mgmt_group_register();
    assert(rc == 0);

    rc = img_mgmt_group_register();
    assert(rc == 0);

    uart_mcumgr_register(recv_cb);

    zephyr_smp_transport_init(&uart_zst, tx_pkt_uart, NULL);

    rc = bt_enable(bt_ready);
    if (rc != 0) {
        printk("Bluetooth init failed (err %d)\n", rc);
        return;
    }

    smp_bt_register();

    bt_conn_cb_register(&conn_callbacks);

    while (1) {
        k_sleep(INT32_MAX);
    }
}
