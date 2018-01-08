/* IEEE 802.15.4 settings code */

/*
 * Copyright (c) 2017 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr.h>
#include <init.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>

#include <mgmt/smp_bt.h>
#include <mgmt/znp.h>

#include <zephyr_smp/zephyr_smp.h>

struct device;

static struct zephyr_smp_transport smp_bt_transport;

/* {8D53DC1D-1DB7-4CD3-868B-8A527460AA84} */
static struct bt_uuid_128 smp_bt_svc_uuid = BT_UUID_INIT_128(
    0x84, 0xaa, 0x60, 0x74, 0x52, 0x8a, 0x8b, 0x86,
    0xd3, 0x4c, 0xb7, 0x1d, 0x1d, 0xdc, 0x53, 0x8d);

/* {DA2E7828-FBCE-4E01-AE9E-261174997C48} */
static struct bt_uuid_128 smp_bt_chr_uuid = BT_UUID_INIT_128(
    0x48, 0x7c, 0x99, 0x74, 0x11, 0x26, 0x9e, 0xae,
    0x01, 0x4e, 0xce, 0xfb, 0x28, 0x78, 0x2e, 0xda);

struct zephyr_nmgr_pkt *alloc_pkt(void);
static void smp_bt_recv_cb(struct bt_conn *conn, const u8_t *buf, size_t len);

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

int smp_bt_register(void)
{
    return bt_gatt_service_register(&smp_bt_svc);
}

int smp_bt_tx_rsp(struct bt_conn *conn, const void *data, u16_t len)
{
    return bt_gatt_notify(conn, smp_bt_attrs + 2, data, len);
}

static struct bt_conn *
smp_bt_conn_from_pkt(const struct zephyr_nmgr_pkt *pkt)
{
    bt_addr_le_t addr;

    memcpy(&addr, pkt->extra, sizeof addr);
    return bt_conn_lookup_addr_le(&addr);
}

static uint16_t
smp_bt_get_mtu(const struct zephyr_nmgr_pkt *pkt)
{
    struct bt_conn *conn;
    uint16_t mtu;

    conn = smp_bt_conn_from_pkt(pkt);
    if (conn == NULL) {
        return 0;
    }

    mtu = bt_gatt_get_mtu(conn);
    bt_conn_unref(conn);

    /* 3 is the number of bytes for ATT notification base */
    return mtu - 3;
}

static int
smp_bt_tx_pkt(struct zephyr_smp_transport *zst, struct zephyr_nmgr_pkt *pkt)
{
    struct bt_conn *conn;
    int rc;

    conn = smp_bt_conn_from_pkt(pkt);
    if (conn == NULL) {
        rc = -1;
    } else {
        rc = smp_bt_tx_rsp(conn, pkt->data, pkt->len);
        bt_conn_unref(conn);
    }

    k_free(pkt);

    return rc;
}

static void
smp_bt_recv_cb(struct bt_conn *conn, const u8_t *buf, size_t len)
{
    struct zephyr_nmgr_pkt *pkt;
    const bt_addr_le_t *addr;

    pkt = alloc_pkt();
    memcpy(pkt->data, buf, len);
    pkt->len = len;

    addr = bt_conn_get_dst(conn);
    memcpy(pkt->extra, addr, sizeof *addr);

    zephyr_smp_rx_req(&smp_bt_transport, pkt);
}

static int
smp_bt_init(struct device *dev)
{
    ARG_UNUSED(dev);

    zephyr_smp_transport_init(&smp_bt_transport, smp_bt_tx_pkt, smp_bt_get_mtu);
    return 0;
}

SYS_INIT(smp_bt_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
