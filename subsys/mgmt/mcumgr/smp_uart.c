#include <string.h>
#include <zephyr.h>
#include <init.h>
#include "console/uart_mcumgr.h"
#include "mgmt/mgmt.h"
#include "mgmt/znp.h"
#include "zephyr_smp/zephyr_smp.h"

struct device;

static struct zephyr_smp_transport smp_uart_transport;

struct zephyr_nmgr_pkt *alloc_pkt(void);

static void
smp_uart_rx_pkt(const uint8_t *buf, size_t len)
{
    struct zephyr_nmgr_pkt *pkt;

    pkt = alloc_pkt();
    memcpy(pkt->data, buf, len);
    pkt->len = len;

    zephyr_smp_rx_req(&smp_uart_transport, pkt);
}

static uint16_t
smp_uart_get_mtu(const struct zephyr_nmgr_pkt *pkt)
{
    return MGMT_MAX_MTU;
}

static int
smp_uart_tx_pkt(struct zephyr_smp_transport *zst, struct zephyr_nmgr_pkt *pkt)
{
    int rc;

    rc = uart_mcumgr_send(pkt->data, pkt->len);
    k_free(pkt);

    return rc;
}

static int
smp_uart_init(struct device *dev)
{
    ARG_UNUSED(dev);

    zephyr_smp_transport_init(&smp_uart_transport, smp_uart_tx_pkt, smp_uart_get_mtu);
    uart_mcumgr_register(smp_uart_rx_pkt);

    return 0;
}

SYS_INIT(smp_uart_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
