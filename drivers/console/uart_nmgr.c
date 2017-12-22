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

#define UART_NMGR_BUF_SZ        1024
#define SHELL_NLIP_PKT          0x0609
#define SHELL_NLIP_DATA         0x0414
#define SHELL_NLIP_MAX_FRAME    128

static struct {
    u8_t buf[UART_NMGR_BUF_SZ];
    int off;

    /* Length of payload as read from header. */
    uint16_t hdr_len;
} uart_nmgr_cur;

static u16_t
uart_nmgr_calc_crc(const u8_t *data, int len)
{
    return crc16(data, len, 0x1021, 0, true);
}

static int
uart_nmgr_find_nl(const u8_t *buf, int len)
{
    int i;

    for (i = 0; i < len; i++) {
        if (buf[i] == '\n') {
            return i;
        }
    }

    return -1;
}

static int
uart_nmgr_decode_req(const u8_t *src, u8_t *dst, int max_dst_len)
{
    if (base64_decode_len((char *)src) > max_dst_len) {
        return -1;
    }

    return base64_decode(src, dst);
}

static int
uart_nmgr_parse_pkt(const u8_t *buf, int len, bool *first_frag)
{
    int dec_len;
    uint16_t op;

    if (len < sizeof op) {
        return -1;
    }

    memcpy(&op, buf, sizeof op);
    op = sys_be16_to_cpu(op);
    switch (op) {
    case SHELL_NLIP_PKT:
        *first_frag = true;
        break;

    case SHELL_NLIP_DATA:
        *first_frag = false;
        break;

    default:
        return -1;
    }

    dec_len = uart_nmgr_decode_req(buf + 2,
                                   uart_nmgr_cur.buf + uart_nmgr_cur.off,
                                   sizeof uart_nmgr_cur.buf - uart_nmgr_cur.off);
    if (dec_len == -1) {
        return -1;
    }

    return dec_len;
}

static bool
uart_nmgr_process_frag(const u8_t *buf, int len)
{
    uint16_t hdr_len;
    uint16_t crc;
    bool first_frag;
    int frag_len;

    frag_len = uart_nmgr_parse_pkt(buf, len, &first_frag);
    if (frag_len == -1) {
        return false;
    }

    if (!first_frag && uart_nmgr_cur.off == 0) {
        return false;
    }

    if (first_frag) {
        uart_nmgr_cur.off = 0;
        uart_nmgr_cur.hdr_len = 0;

        if (frag_len < sizeof hdr_len) {
            return false;
        }

        memcpy(&hdr_len, uart_nmgr_cur.buf, sizeof hdr_len);
        uart_nmgr_cur.hdr_len = sys_be16_to_cpu(hdr_len);
    }

    uart_nmgr_cur.off += frag_len;
    if (uart_nmgr_cur.off > uart_nmgr_cur.hdr_len + sizeof hdr_len) {
        return false;
    }

    if (uart_nmgr_cur.off < uart_nmgr_cur.hdr_len + sizeof hdr_len) {
        return true;
    }

    crc = uart_nmgr_calc_crc(uart_nmgr_cur.buf + 2, uart_nmgr_cur.off - 2);
    if (crc == 0) {
        app_cb(uart_nmgr_cur.buf + 2, uart_nmgr_cur.off - 4);
    }

    return false;
}

static int
uart_nmgr_read_chunk(void *buf, int cap)
{
    if (!uart_irq_rx_ready(uart_nmgr_dev)) {
        return 0;
    }

    return uart_fifo_read(uart_nmgr_dev, buf, cap);
}

static void
uart_nmgr_isr(struct device *unused)
{
    static uint8_t buf[UART_NMGR_BUF_SZ];
    static uint8_t buf_off;
    bool partial;
    int chunk_len;
    int rem_len;
    int old_off;
    int nl_off;

	ARG_UNUSED(unused);

	while (uart_irq_update(uart_nmgr_dev)
	       && uart_irq_is_pending(uart_nmgr_dev)) {

        chunk_len = uart_nmgr_read_chunk(buf + buf_off, sizeof buf - buf_off);
        if (chunk_len == 0) {
            continue;
        }

        old_off = buf_off;
        buf_off += chunk_len;

        while (1) {
            nl_off = uart_nmgr_find_nl(buf + old_off, buf_off - old_off);
            if (nl_off == -1) {
                break;
            }
            nl_off += old_off;

            buf[nl_off] = '\0';
            partial = uart_nmgr_process_frag(buf, nl_off);

            rem_len = buf_off - nl_off - 1;
            if (rem_len > 0) {
                memmove(buf, buf + nl_off + 1, rem_len);
            }
            buf_off = rem_len;
            old_off = 0;

            if (partial) {
                break;
            } else {
                uart_nmgr_cur.off = 0;
            }
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
