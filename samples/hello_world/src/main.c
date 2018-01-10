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
#include "os_mgmt/os_mgmt.h"
#include "img_mgmt/img_mgmt.h"
#include "mgmt/smp_bt.h"
#include "mgmt/buf.h"
 
#define DEVICE_NAME         CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN     (sizeof(DEVICE_NAME) - 1)

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, 0x0d, 0x18, 0x0f, 0x18, 0x05, 0x18),
};

static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

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
