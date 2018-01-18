/*
 * Copyright Runtime.io 2018. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief A driver for sending and receiving mcumgr packets over UART.
 */

#include <assert.h>
#include <string.h>
#include <kernel.h>
#include <uart.h>
#include <mgmt/serial.h>
#include <console/uart_mcumgr.h>

static struct device *uart_mcumgr_dev;

static uart_mcumgr_recv_fn *uart_mgumgr_recv_cb;

static u8_t uart_mcumgr_rx_buf[CONFIG_UART_MCUMGR_RX_BUF_SIZE];
static struct mcumgr_serial_rx_ctxt uart_mcumgr_rx_ctxt = {
	.buf = uart_mcumgr_rx_buf,
	.buf_size = CONFIG_UART_MCUMGR_RX_BUF_SIZE,
};

/**
 * Reads a chunk of received data from the UART.
 */
static int uart_mcumgr_read_chunk(void *buf, int capacity)
{
	if (!uart_irq_rx_ready(uart_mcumgr_dev)) {
		return 0;
	}

	return uart_fifo_read(uart_mcumgr_dev, buf, capacity);
}

/**
 * ISR that is called when UART bytes are received.
 */
static void uart_mcumgr_isr(struct device *unused)
{
	u8_t buf[32];
	u8_t *pkt;
	bool complete;
	int chunk_len;
	int pkt_len;
	int i;

	ARG_UNUSED(unused);

	while (uart_irq_update(uart_mcumgr_dev) &&
	       uart_irq_is_pending(uart_mcumgr_dev)) {

		chunk_len = uart_mcumgr_read_chunk(buf, sizeof(buf));
		if (chunk_len == 0) {
			continue;
		}

		for (i = 0; i < chunk_len; i++) {
			complete = mcumgr_serial_rx_byte(&uart_mcumgr_rx_ctxt,
							 buf[i], &pkt,
							 &pkt_len);
			if (complete) {
				uart_mgumgr_recv_cb(pkt, pkt_len);
			}
		}
	}
}

/**
 * Sends raw data over the UART.
 */
static int uart_mcumgr_send_raw(const void *data, int len, void *arg)
{
	const u8_t *u8p;

	u8p = data;
	while (len--) {
		uart_poll_out(uart_mcumgr_dev, *u8p++);
	}

	return 0;
}

int uart_mcumgr_send(const u8_t *data, int len)
{
	return mcumgr_serial_tx_pkt(data, len, uart_mcumgr_send_raw, NULL);
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

void uart_mcumgr_register(uart_mcumgr_recv_fn *cb)
{
	uart_mgumgr_recv_cb = cb;

	uart_mcumgr_dev = device_get_binding(CONFIG_UART_MCUMGR_ON_DEV_NAME);

	if (uart_mcumgr_dev != NULL) {
		uart_mcumgr_setup(uart_mcumgr_dev);
	}
}
