/** @file
 *  @brief Pipe UART driver header file.
 *
 *  A nmgr UART driver that allows applications to handle all aspects of
 *  received protocol data.
 */

/*
 * Copyright (c) 2015 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Received data callback.
 *
 *  This function is called when new data is received on UART. The off parameter
 *  can be used to alter offset at which received data is stored. Typically,
 *  when the complete data is received and a new buffer is provided off should
 *  be set to 0.
 *
 *  @param buf Buffer with received data.
 *  @param off Data offset on next received and accumulated data length.
 *
 *  @return Buffer to be used on next receive.
 */
typedef void (*uart_nmgr_recv_cb)(const u8_t *buf, size_t len);

/** @brief Send data over UART.
 *
 *  This function is used to send data over UART.
 *
 *  @param data Buffer with data to be send.
 *  @param len Size of data.
 *
 *  @return 0 on success or negative error
 */
int uart_nmgr_send(const u8_t *data, int len);

/** @brief Register UART application.
 *
 *  This function is used to register new UART application.
 *
 *  @param cb Callback to be called on data reception.
 */
void uart_nmgr_register(uart_nmgr_recv_cb cb);

#ifdef __cplusplus
}
#endif
