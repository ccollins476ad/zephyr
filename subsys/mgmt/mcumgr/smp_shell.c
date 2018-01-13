/*
 * Copyright Runtime.io 2018. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file
 * @brief Shell transport for the mcumgr SMP protocol.
 */

#include <string.h>
#include <zephyr.h>
#include <init.h>
#include "net/buf.h"
#include "shell/shell.h"
#include "mgmt/mgmt.h"
#include "mgmt/serial.h"
#include "zephyr_mgmt/buf.h"
#include "zephyr_smp/zephyr_smp.h"

/* XXX: Make configurable. */
#define SMP_SHELL_MTU   1024

struct device;

static struct zephyr_smp_transport smp_shell_transport;

static u8_t smp_shell_rx_buf[SMP_SHELL_MTU];
static struct mcumgr_serial_rx_ctxt smp_shell_rx_ctxt = {
    .buf = smp_shell_rx_buf,
    .buf_size = SMP_SHELL_MTU,
};

/**
 * Processes a single line (i.e., a single SMP frame)
 */
static int
smp_shell_rx_line(const char *line, void *arg)
{
    struct net_buf *nb;
    u8_t *raw;
    bool complete;
    int raw_len;
    int i;

    for (i = 0; line[i] != '\0'; i++) {
        complete = mcumgr_serial_rx_byte(&smp_shell_rx_ctxt, line[i],
                                         &raw, &raw_len);
        if (complete) {
            nb = mcumgr_buf_alloc();
            net_buf_add_mem(nb, raw, raw_len);

            zephyr_smp_rx_req(&smp_shell_transport, nb);
        }
    }

    return 0;
}

static u16_t
smp_shell_get_mtu(const struct net_buf *nb)
{
    return SMP_SHELL_MTU;
}

static int
smp_shell_tx_raw(const void *data, int len, void *arg)
{
    /* Cast away const. */
    k_str_out((void *)data, len);
    return 0;
}

static int
smp_shell_tx_pkt(struct zephyr_smp_transport *zst, struct net_buf *nb)
{
    int rc;

    rc = mcumgr_serial_tx_pkt(nb->data, nb->len, smp_shell_tx_raw, NULL);
    mcumgr_buf_free(nb);

    return rc;
}

static int
smp_shell_init(struct device *dev)
{
    ARG_UNUSED(dev);

    zephyr_smp_transport_init(&smp_shell_transport, smp_shell_tx_pkt,
                              smp_shell_get_mtu);
    shell_register_nlip_handler(smp_shell_rx_line, NULL);

    return 0;
}

SYS_INIT(smp_shell_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
