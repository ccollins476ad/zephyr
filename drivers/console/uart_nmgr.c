/** @file
 * @brief Pipe UART driver
 *
 * A nmgr UART driver allowing application to handle all aspects of received
 * protocol data.
 */

/*
 * Copyright (c) 2015 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <kernel.h>
#include <misc/byteorder.h>

#include "base64/base64.h"
#include <board.h>
#include <uart.h>
#include <crc16.h>

#include <console/uart_nmgr.h>
#include <misc/printk.h>

static struct device *uart_nmgr_dev;

static uart_nmgr_recv_cb app_cb;

#define UART_NMGR_BUF_SZ    1024
#define SHELL_NLIP_PKT          0x0609
#define SHELL_NLIP_DATA         0x0414
#define SHELL_NLIP_MAX_FRAME    128

static u16_t
uart_nmgr_calc_crc(const u8_t *data, int len)
{
    return crc16(data, len, 0x1021, 0, true);
}

static int
uart_nmgr_find_nl(const u8_t *buf, int start, int end)
{
    int i;

    for (i = start; i < end; i++) {
        if (buf[i] == '\n') {
            return i;
        }
    }

    return -1;
}

static int
uart_nmgr_decode_req(const u8_t *src, int src_len,
                     u8_t *dst, int max_dst_len)
{
    /* XXX: Assume src is null-terminated. */

    if (base64_decode_len((char *)src) > max_dst_len) {
        return -1;
    }

    return base64_decode(src, dst);
}

static int
uart_nmgr_parse_pkt(u8_t *pkt, int pkt_len)
{
    int dec_len;
    uint16_t op;

    if (pkt_len < sizeof op) {
        return -1;
    }

    memcpy(&op, pkt, sizeof op);
    op = sys_be16_to_cpu(op);
    if (op != SHELL_NLIP_PKT) {
        return -1;
    }

    /* Decode response in place. */
    dec_len = uart_nmgr_decode_req(pkt + sizeof op, pkt_len - sizeof op,
                                   pkt, pkt_len);
    if (dec_len == -1) {
        return -1;
    }

    return dec_len;
}

static int
uart_nmgr_decoded_pkt_is_valid(const u8_t *pkt, int len)
{
    uint16_t hdr_len;
    u16_t crc;

    if (len < sizeof hdr_len) {
        return 0;
    }

    memcpy(&hdr_len, pkt, sizeof hdr_len);
    hdr_len = sys_be16_to_cpu(hdr_len);
    if (hdr_len != len - 2) {
        return 0;
    }

    crc = uart_nmgr_calc_crc(pkt + 2, len - 2);
    if (crc != 0) {
        return 0;
    }

    return 1;
}

static void uart_nmgr_isr(struct device *unused)
{
    static u8_t buf[UART_NMGR_BUF_SZ];
    static int buf_off;

	ARG_UNUSED(unused);

	while (uart_irq_update(uart_nmgr_dev)
	       && uart_irq_is_pending(uart_nmgr_dev)) {
        int old_off;
        int nl_idx;
		int rx;

		if (!uart_irq_rx_ready(uart_nmgr_dev)) {
			continue;
		}

		rx = uart_fifo_read(uart_nmgr_dev, buf + buf_off,
				            sizeof buf - buf_off);
		if (!rx) {
			continue;
		}

        old_off = buf_off;
        buf_off += rx;

        while (1) {
            int rem_len;
            int rc;

            nl_idx = uart_nmgr_find_nl(buf, old_off, buf_off);
            if (nl_idx == -1) {
                break;
            }
            buf[nl_idx] = '\0';

            rc = uart_nmgr_parse_pkt(buf, nl_idx);
            if (rc != -1 && uart_nmgr_decoded_pkt_is_valid(buf, rc)) {
                app_cb(buf + 2, rc - 4);
            }

            rem_len = buf_off - nl_idx - 1;
            if (rem_len > 0) {
                memmove(buf, buf + nl_idx + 1, rem_len);
            }
            buf_off = rem_len;
            old_off = 0;
        }

        if (buf_off >= sizeof buf) {
            /* Overrun; discard. */
            buf_off = 0;
        }
	}
}

static void
uart_nmgr_send_raw(const void *data, int len)
{
    const u8_t *u8p;

    u8p = data;
	while (len--)  {
		uart_poll_out(uart_nmgr_dev, *u8p++);
	}
}

int uart_nmgr_send(const u8_t *data, int len)
{
    static u8_t buf[BASE64_ENCODE_SIZE(UART_NMGR_BUF_SZ)];
    u8_t tmp[3];
    uint16_t u16;
    uint16_t crc;
    int off;
    int rem;
    int i;

    crc = uart_nmgr_calc_crc(data, len);

    u16 = sys_cpu_to_be16(SHELL_NLIP_PKT);
    uart_nmgr_send_raw(&u16, sizeof u16);

    u16 = sys_cpu_to_be16(len);
    memcpy(tmp, &u16, sizeof u16);
    tmp[2] = data[0];
    off = base64_encode(tmp, 3, buf, 0);

    i = 1;
    while (1) {
        rem = len - i;
        if (rem >= 3) {
            memcpy(tmp, data + i, 3);
            off += base64_encode(tmp, 3, buf + off, 0);
        } else if (rem == 0) {
            tmp[0] = (crc & 0xff00) >> 8;
            tmp[1] = crc & 0x00ff;
            off += base64_encode(tmp, 2, buf + off, 1);
            break;
        } else if (rem == 1) {
            tmp[0] = data[i];
            tmp[1] = (crc & 0xff00) >> 8;
            tmp[2] = crc & 0x00ff;
            memcpy(tmp + 1, &crc, 2);
            off += base64_encode(tmp, 3, buf + off, 1);
            break;
        } else if (rem == 2) {
            tmp[0] = data[i];
            tmp[1] = data[i + 1];
            tmp[2] = (crc & 0xff00) >> 8;
            off += base64_encode(tmp, 3, buf + off, 0);

            tmp[0] = crc & 0x00ff;
            off += base64_encode(tmp, 1, buf + off, 1);
            break;
        }

        i += 3;
    }

    uart_nmgr_send_raw(buf, off);

    tmp[0] = '\n';
    uart_nmgr_send_raw(tmp, 1);

	return 0;
}

static void uart_nmgr_setup(struct device *uart)
{
	u8_t c;

	uart_irq_rx_disable(uart);
	uart_irq_tx_disable(uart);

	/* Drain the fifo */
	while (uart_fifo_read(uart, &c, 1)) {
		continue;
	}

	uart_irq_callback_set(uart, uart_nmgr_isr);

	uart_irq_rx_enable(uart);
}

void uart_nmgr_register(uart_nmgr_recv_cb cb)
{
	app_cb = cb;

	uart_nmgr_dev = device_get_binding(CONFIG_UART_NMGR_ON_DEV_NAME);

	if (uart_nmgr_dev != NULL) {
		uart_nmgr_setup(uart_nmgr_dev);
	}
}
