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

#include <string.h>
#include <kernel.h>
#include <misc/byteorder.h>

#include "base64/base64.h"
#include <board.h>
#include <uart.h>
#include <crc16.h>

#include <console/uart_mcumgr.h>
#include <misc/printk.h>

static struct device *uart_mcumgr_dev;

static uart_mcumgr_recv_cb app_cb;

#define UART_MCUMGR_BUF_SZ        1024
#define SHELL_NLIP_PKT          0x0609
#define SHELL_NLIP_DATA         0x0414
#define SHELL_NLIP_MAX_FRAME    128

static struct {
    u8_t buf[UART_MCUMGR_BUF_SZ];
    int off;

    /* Length of payload as read from header. */
    uint16_t hdr_len;
} uart_mcumgr_cur;

static u16_t
uart_mcumgr_calc_crc(const u8_t *data, int len)
{
    return crc16(data, len, 0x1021, 0, true);
}

static int
uart_mcumgr_find_nl(const u8_t *buf, int len)
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
uart_mcumgr_decode_req(const u8_t *src, u8_t *dst, int max_dst_len)
{
    if (base64_decode_len((char *)src) > max_dst_len) {
        return -1;
    }

    return base64_decode(src, dst);
}

static int
uart_mcumgr_parse_op(const u8_t *buf, int len)
{
    uint16_t op;

    if (len < sizeof op) {
        return -1;
    }

    memcpy(&op, buf, sizeof op);
    op = sys_be16_to_cpu(op);

    if (op != SHELL_NLIP_PKT && op != SHELL_NLIP_DATA) {
        return -1;
    }

    return op;
}

static int
uart_mcumgr_parse_len(void)
{
    uint16_t len;

    if (uart_mcumgr_cur.off < sizeof len) {
        return -1;
    }

    memcpy(&len, uart_mcumgr_cur.buf, sizeof len);
    return sys_be16_to_cpu(len);
}

static int
uart_mcumgr_decode_frag(const u8_t *buf, int len)
{
    int dec_len;

    dec_len = uart_mcumgr_decode_req(buf,
                                   uart_mcumgr_cur.buf + uart_mcumgr_cur.off,
                                   sizeof uart_mcumgr_cur.buf - uart_mcumgr_cur.off);
    if (dec_len == -1) {
        return -1;
    }

    uart_mcumgr_cur.off += dec_len;

    return 0;
}

static bool
uart_mcumgr_process_frag(const u8_t *buf, int len)
{
    uint16_t crc;
    uint16_t op;
    int rc;

    op = uart_mcumgr_parse_op(buf, len);
    switch (op) {
    case SHELL_NLIP_PKT:
        uart_mcumgr_cur.off = 0;
        break;

    case SHELL_NLIP_DATA:
        if (uart_mcumgr_cur.off == 0) {
            return -1;
        }
        break;

    default:
        return false;
    }

    rc = uart_mcumgr_decode_frag(buf + sizeof op, len - sizeof op);
    if (rc != 0) {
        return false;
    }

    if (op == SHELL_NLIP_PKT) {
        rc = uart_mcumgr_parse_len();
        if (rc == -1) {
            return false;
        }
        uart_mcumgr_cur.hdr_len = rc;
    }

    if (uart_mcumgr_cur.off > uart_mcumgr_cur.hdr_len + 2) {
        return false;
    }

    if (uart_mcumgr_cur.off < uart_mcumgr_cur.hdr_len + 2) {
        return true;
    }

    crc = uart_mcumgr_calc_crc(uart_mcumgr_cur.buf + 2, uart_mcumgr_cur.off - 2);
    if (crc == 0) {
        app_cb(uart_mcumgr_cur.buf + 2, uart_mcumgr_cur.off - 4);
    }

    return false;
}

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
    static uint8_t buf[UART_MCUMGR_BUF_SZ];
    static uint8_t buf_off;
    bool partial;
    int chunk_len;
    int rem_len;
    int old_off;
    int nl_off;

	ARG_UNUSED(unused);

	while (uart_irq_update(uart_mcumgr_dev)
	       && uart_irq_is_pending(uart_mcumgr_dev)) {

        chunk_len = uart_mcumgr_read_chunk(buf + buf_off, sizeof buf - buf_off);
        if (chunk_len == 0) {
            continue;
        }

        old_off = buf_off;
        buf_off += chunk_len;

        while (1) {
            nl_off = uart_mcumgr_find_nl(buf + old_off, buf_off - old_off);
            if (nl_off == -1) {
                break;
            }
            nl_off += old_off;

            buf[nl_off] = '\0';
            partial = uart_mcumgr_process_frag(buf, nl_off);

            rem_len = buf_off - nl_off - 1;
            if (rem_len > 0) {
                memmove(buf, buf + nl_off + 1, rem_len);
            }
            buf_off = rem_len;
            old_off = 0;

            if (partial) {
                break;
            } else {
                uart_mcumgr_cur.off = 0;
            }
        }

        if (buf_off >= sizeof buf) {
            /* Overrun; discard. */
            buf_off = 0;
        }
	}
}

static void
uart_mcumgr_send_raw(const void *data, int len)
{
    const u8_t *u8p;

    u8p = data;
	while (len--)  {
		uart_poll_out(uart_mcumgr_dev, *u8p++);
	}
}

int uart_mcumgr_send(const u8_t *data, int len)
{
    static u8_t buf[BASE64_ENCODE_SIZE(UART_MCUMGR_BUF_SZ)];
    u8_t tmp[3];
    uint16_t u16;
    uint16_t crc;
    int off;
    int rem;
    int i;

    crc = uart_mcumgr_calc_crc(data, len);

    u16 = sys_cpu_to_be16(SHELL_NLIP_PKT);
    uart_mcumgr_send_raw(&u16, sizeof u16);

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

    uart_mcumgr_send_raw(buf, off);

    tmp[0] = '\n';
    uart_mcumgr_send_raw(tmp, 1);

	return 0;
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
