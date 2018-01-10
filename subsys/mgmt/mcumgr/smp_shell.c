#include <string.h>
#include <zephyr.h>
#include <init.h>
#include "shell/shell.h"
#include "mgmt/mgmt.h"
#include "mgmt/serial.h"
#include "mgmt/znp.h"
#include "zephyr_smp/zephyr_smp.h"

struct device;

static struct zephyr_smp_transport smp_shell_transport;

static u8_t smp_shell_rx_buf[MCUMGR_SERIAL_BUF_SZ];
static struct mcumgr_serial_rx_ctxt smp_shell_rx_ctxt = {
    .buf = smp_shell_rx_buf,
    .buf_size = MCUMGR_SERIAL_BUF_SZ,
};

struct zephyr_nmgr_pkt *alloc_pkt(void);

static int
smp_shell_rx_line(const char *line, void *arg)
{
    struct zephyr_nmgr_pkt *pkt;
    uint8_t *raw;
    bool complete;
    int raw_len;
    int i;

    for (i = 0; line[i] != '\0'; i++) {
        complete = mcumgr_serial_rx_byte(&smp_shell_rx_ctxt, line[i],
                                         &raw, &raw_len);
        if (complete) {
            pkt = alloc_pkt();
            memcpy(pkt->data, raw, raw_len);
            pkt->len = raw_len;

            zephyr_smp_rx_req(&smp_shell_transport, pkt);
        }
    }

    return 0;
}

static uint16_t
smp_shell_get_mtu(const struct zephyr_nmgr_pkt *pkt)
{
    return MGMT_MAX_MTU;
}

static int
smp_shell_tx_raw(const void *data, int len, void *arg)
{
    k_str_out(data, len);
    return 0;
}

static int
smp_shell_tx_pkt(struct zephyr_smp_transport *zst, struct zephyr_nmgr_pkt *pkt)
{
    int rc;

    rc = mcumgr_serial_tx(pkt->data, pkt->len, smp_shell_tx_raw, NULL);
    k_free(pkt);

    return rc;
}

static int
smp_shell_init(struct device *dev)
{
    ARG_UNUSED(dev);

    zephyr_smp_transport_init(&smp_shell_transport, smp_shell_tx_pkt, smp_shell_get_mtu);
    shell_register_nlip_handler(smp_shell_rx_line, NULL);

    return 0;
}

SYS_INIT(smp_shell_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
