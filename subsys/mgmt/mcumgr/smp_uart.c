#include <string.h>
#include <zephyr.h>
#include <init.h>
#include "net/buf.h"
#include "console/uart_mcumgr.h"
#include "mgmt/mgmt.h"
#include "mgmt/buf.h"
#include "zephyr_smp/zephyr_smp.h"

struct device;

static struct zephyr_smp_transport smp_uart_transport;

static void
smp_uart_rx_pkt(const uint8_t *buf, size_t len)
{
    struct net_buf *nb;

    nb = mcumgr_buf_alloc();
    net_buf_add_mem(nb, buf, len);

    zephyr_smp_rx_req(&smp_uart_transport, nb);
}

static uint16_t
smp_uart_get_mtu(const struct net_buf *nb)
{
    return MGMT_MAX_MTU;
}

static int
smp_uart_tx_pkt(struct zephyr_smp_transport *zst, struct net_buf *nb)
{
    int rc;

    rc = uart_mcumgr_send(nb->data, nb->len);
    mcumgr_buf_free(nb);

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
