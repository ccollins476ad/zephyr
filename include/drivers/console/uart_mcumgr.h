/*
 * Copyright Runtime.io 2018. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file
 * @brief A driver for sending and receiving mcumgr packets over UART.
 *
 * Mcumgr packets sent over UART are fragmented into frames of 128 bytes or
 * fewer.
 *
 * The initial frame in a packet has the following format:
 *     offset 0:    0x06 0x09
 *     === Begin base64 encoding ===
 *     offset 2:    <16-bit packet-length>
 *     offset ?:    <mcumgr-data>
 *     offset ?:    <crc16 of base64-encoded content>
 *     === End base64 encoding ===
 *     offset ?:    0x0a (newline)
 *
 * All subsequent frames have the following format:
 *     offset 0:    0x04 0x14
 *     === Begin base64 encoding ===
 *     offset 2:    <mcumgr-data>
 *     offset ?:    <crc16 of base64-encoded content>
 *     === End base64 encoding ===
 *     offset ?:    0x0a (newline)
 *
 * The packet is fully received when <packet-length> bytes of <mcumgr-data>
 * have been received.
 */

#include <stdlib.h>
#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @typedef uart_mcumgr_recv_fn
 * @brief Function that gets called when an mcumgr packet is received.
 *
 *  @param buf                  A buffer containing the incoming mcumgr packet.
 *  @param len                  The length of the buffer, in bytes.
 */
typedef void uart_mcumgr_recv_fn(const u8_t *buf, size_t len);

/**
 * @brief Sends an mcumgr packet over UART.
 *
 * @param data                 Buffer containing the mcumgr packet to send.
 * @param len                  The length of the buffer, in bytes.
 *
 * @return                     0 on success or negative error.
 */
int uart_mcumgr_send(const u8_t *data, int len);

/**
 * @brief Registers an mcumgr UART receive handler.
 *
 * Configures the mcumgr UART driver to call the specified function when an
 * mcumgr request packet is received.
 *
 * @param cb The callback to execute when an mcumgr request packet is received.
 */
void uart_mcumgr_register(uart_mcumgr_recv_fn *cb);

#ifdef __cplusplus
}
#endif
