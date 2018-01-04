/* IEEE 802.15.4 settings code */

/*
 * Copyright (c) 2017 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <errno.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>

#include <mgmt/smp_bt.h>

static smp_bt_cb smp_bt_recv_cb;

/* {8D53DC1D-1DB7-4CD3-868B-8A527460AA84} */
static struct bt_uuid_128 smp_bt_svc_uuid = BT_UUID_INIT_128(
    0x84, 0xaa, 0x60, 0x74, 0x52, 0x8a, 0x8b, 0x86,
    0xd3, 0x4c, 0xb7, 0x1d, 0x1d, 0xdc, 0x53, 0x8d);

/* {DA2E7828-FBCE-4E01-AE9E-261174997C48} */
static struct bt_uuid_128 smp_bt_chr_uuid = BT_UUID_INIT_128(
    0x48, 0x7c, 0x99, 0x74, 0x11, 0x26, 0x9e, 0xae,
    0x01, 0x4e, 0xce, 0xfb, 0x28, 0x78, 0x2e, 0xda);

static ssize_t smp_bt_chr_write(struct bt_conn *conn,
              const struct bt_gatt_attr *attr,
			  const void *buf, u16_t len, u16_t offset,
			  u8_t flags)
{
    smp_bt_recv_cb(conn, buf, len);

    return len;
}

static void smp_bt_ccc_changed(const struct bt_gatt_attr *attr, u16_t value)
{
}

static struct bt_gatt_ccc_cfg smp_bt_ccc[BT_GATT_CCC_MAX] = {};
static struct bt_gatt_attr smp_bt_attrs[] = {
    /* SMP Primary Service Declaration */
    BT_GATT_PRIMARY_SERVICE(&smp_bt_svc_uuid),

    BT_GATT_CHARACTERISTIC(&smp_bt_chr_uuid.uuid,
                   BT_GATT_CHRC_WRITE_WITHOUT_RESP |
                   BT_GATT_CHRC_NOTIFY),
	BT_GATT_DESCRIPTOR(&smp_bt_chr_uuid.uuid,
			   BT_GATT_PERM_WRITE, NULL, smp_bt_chr_write, NULL),
	BT_GATT_CCC(smp_bt_ccc, smp_bt_ccc_changed),
};

static struct bt_gatt_service smp_bt_svc = BT_GATT_SERVICE(smp_bt_attrs);

int smp_bt_register(smp_bt_cb cb)
{
	smp_bt_recv_cb = cb;

    return bt_gatt_service_register(&smp_bt_svc);
}

int smp_bt_tx_rsp(struct bt_conn *conn, const void *data, u16_t len)
{
    return bt_gatt_notify(conn, smp_bt_attrs + 2, data, len);
}
