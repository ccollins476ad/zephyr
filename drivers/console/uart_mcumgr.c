/** @file
 * @brief Pipe UART driver
 *
 * A mcumgr UART driver allowing application to handle all aspects of received
 * protocol data.
 */

/*
 * Copyright (c) 2015 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <string.h>
#include <kernel.h>

#include <board.h>
#include <uart.h>

#include "mgmt/serial.h"
#include <console/uart_mcumgr.h>

static struct device *uart_mcumgr_dev;

static uart_mcumgr_recv_cb app_cb;

static u8_t uart_mcumgr_rx_buf[MCUMGR_SERIAL_BUF_SZ];
static struct mcumgr_serial_rx_ctxt uart_mcumgr_rx_ctxt = {
    .buf = uart_mcumgr_rx_buf,
    .buf_size = MCUMGR_SERIAL_BUF_SZ,
};

static int
uart_mcumgr_read_chunk(void *buf, int cap)
{
    if (!uart_irq_rx_ready(uart_mcumgr_dev)) {
        return 0;
    }

    return uart_fifo_read(uart_mcumgr_dev, buf, cap);
}

static void
uart_mcumgr_isr(struct device *unused)
{
    uint8_t buf[32];
    uint8_t *pkt;
    bool complete;
    int chunk_len;
    int pkt_len;
    int i;

	ARG_UNUSED(unused);

	while (uart_irq_update(uart_mcumgr_dev)
	       && uart_irq_is_pending(uart_mcumgr_dev)) {

        chunk_len = uart_mcumgr_read_chunk(buf, sizeof buf);
        if (chunk_len == 0) {
            continue;
        }

        for (i = 0; i < chunk_len; i++) {
            complete = mcumgr_serial_rx_byte(&uart_mcumgr_rx_ctxt, buf[i],
                                             &pkt, &pkt_len);
            if (complete) {
                app_cb(pkt, pkt_len);
                uart_mcumgr_rx_ctxt.raw_off = 0;
            }
        }
	}
}

static int
uart_mcumgr_send_raw(const void *data, int len, void *arg)
{
    const u8_t *u8p;

    u8p = data;
	while (len--)  {
		uart_poll_out(uart_mcumgr_dev, *u8p++);
	}

    return 0;
}

int uart_mcumgr_send(const u8_t *data, int len)
{
    return mcumgr_serial_tx(data, len, uart_mcumgr_send_raw, NULL);
}

static void uart_mcumgr_setup(struct device *uart)
{
	u8_t c;

	uart_irq_rx_disable(uart);
	uart_irq_tx_disable(uart);

	/* Drain the fifo */
	while (uart_fifo_read(uart, &c, 1)) {
		continue;
	}

	uart_irq_callback_set(uart, uart_mcumgr_isr);

	uart_irq_rx_enable(uart);
}

void uart_mcumgr_register(uart_mcumgr_recv_cb cb)
{
	app_cb = cb;

	uart_mcumgr_dev = device_get_binding(CONFIG_UART_MCUMGR_ON_DEV_NAME);

	if (uart_mcumgr_dev != NULL) {
		uart_mcumgr_setup(uart_mcumgr_dev);
	}
}
