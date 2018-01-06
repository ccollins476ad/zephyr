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
#include "znp/znp.h"
 
K_FIFO_DEFINE(fifo_reqs);

static struct zephyr_smp_transport uart_zst;
static struct zephyr_smp_transport bt_zst;

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

static struct bt_conn *
conn_from_pkt(const struct zephyr_nmgr_pkt *pkt)
{
    bt_addr_le_t addr;

    memcpy(&addr, pkt->extra, sizeof addr);
    return bt_conn_lookup_addr_le(&addr);
}

static int
tx_pkt_uart(struct zephyr_smp_transport *zst, struct zephyr_nmgr_pkt *pkt)
{
    int rc;

    rc = uart_mcumgr_send(pkt->data, pkt->len);
    k_free(pkt);

    return rc;
}

static int
tx_pkt_bt(struct zephyr_smp_transport *zst, struct zephyr_nmgr_pkt *pkt)
{
    struct bt_conn *conn;
    int rc;

    conn = conn_from_pkt(pkt);
    if (conn == NULL) {
        rc = -1;
    } else {
        rc = smp_bt_tx_rsp(conn, pkt->data, pkt->len);
        bt_conn_unref(conn);
    }

    k_free(pkt);

    return rc;
}

static uint16_t
get_mtu_bt(const struct zephyr_nmgr_pkt *pkt)
{
    struct bt_conn *conn;
    uint16_t mtu;

    conn = conn_from_pkt(pkt);
    if (conn == NULL) {
        return 0;
    }

    mtu = bt_gatt_get_mtu(conn);
    bt_conn_unref(conn);

    /* 3 is the number of bytes for ATT notification base */
    return mtu - 3;
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

static void
bt_recv_cb(struct bt_conn *conn, const u8_t *buf, size_t len)
{
    struct zephyr_nmgr_pkt *pkt;
    const bt_addr_le_t *addr;

    pkt = alloc_pkt();
    memcpy(pkt->data, buf, len);
    pkt->len = len;

    addr = bt_conn_get_dst(conn);
    memcpy(pkt->extra, addr, sizeof *addr);

    zephyr_smp_rx_req(&bt_zst, pkt);
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

    zephyr_smp_transport_init(&bt_zst, tx_pkt_bt, get_mtu_bt);
    zephyr_smp_transport_init(&uart_zst, tx_pkt_uart, NULL);

    rc = bt_enable(bt_ready);
    if (rc != 0) {
        printk("Bluetooth init failed (err %d)\n", rc);
        return;
    }

    smp_bt_register(bt_recv_cb);

    bt_conn_cb_register(&conn_callbacks);

    while (1) {
        k_sleep(INT32_MAX);
    }
}
