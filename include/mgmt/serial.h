/*
 * Copyright Runtime.io 2018. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Utility functions used by the UART and shell mcumgr transports.
 *
 * Mcumgr packets sent over serial are fragmented into frames of 128 bytes or
 * fewer.
 *
 * The initial frame in a packet has the following format:
 *     offset 0:    0x06 0x09
 *     === Begin base64 encoding ===
 *     offset 2:    <16-bit packet-length>
 *     offset ?:    <mcumgr-data>
 *     offset ?:    [<crc16 of all mcumgr-data>]
 *     === End base64 encoding ===
 *     offset ?:    0x0a (newline)
 *
 * All subsequent frames have the following format:
 *     offset 0:    0x04 0x14
 *     === Begin base64 encoding ===
 *     offset 2:    <mcumgr-data>
 *     offset ?:    [<crc16 of all mcumgr-data>]
 *     === End base64 encoding ===
 *     offset ?:    0x0a (newline)
 *
 * Note: The CRC is only present in the final frame of a packet.
 *
 * The packet is fully received when <packet-length> bytes of mcumgr data
 * have been received.
 */

#ifndef H_MGMT_SERIAL_
#define H_MGMT_SERIAL_

#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MCUMGR_SERIAL_HDR_PKT       0x0609
#define MCUMGR_SERIAL_HDR_FRAG      0x0414
#define MCUMGR_SERIAL_MAX_FRAME     128

#define MCUMGR_SERIAL_HDR_PKT_1     (MCUMGR_SERIAL_HDR_PKT >> 8)
#define MCUMGR_SERIAL_HDR_PKT_2     (MCUMGR_SERIAL_HDR_PKT & 0xff)
#define MCUMGR_SERIAL_HDR_FRAG_1    (MCUMGR_SERIAL_HDR_FRAG >> 8)
#define MCUMGR_SERIAL_HDR_FRAG_2    (MCUMGR_SERIAL_HDR_FRAG & 0xff)

/**
 * @brief Maintains state for an incoming mcumgr request packet.
 */
struct mcumgr_serial_rx_ctxt {
	/* Contains the request packet as it is received.  The packet is
	 * base64-decoded in place, partially overwriting the encoded data.
	 */
	u8_t *buf;
	int buf_size;

	int raw_off;
	int b64_off;
	int b64_len;

	/* Length of full packet, as read from header. */
	u16_t pkt_len;
};

/** @typedef mcumgr_serial_tx_fn
 * @brief Transmits a chunk of raw response data.
 *
 * @param data                  The data to transmit.
 * @param len                   The number of bytes to transmit.
 * @param arg                   An optional argument.
 *
 * @return                      0 on success; negative error code on failure.
 */
typedef int mcumgr_serial_tx_fn(const void *data, int len, void *arg);

/**
 * @brief Decodes an incoming mcumgr request byte.
 *
 * @param rx_ctxt               The receive context associated with the serial
 *                                  transport being used.
 * @param byte                  The incoming byte to process.
 * @param out_pkt               If a full packet has been received, the start
 *                                  of the decoded packet gets written here.
 * @param out_len               If a full packet has been received, the length
 *                                  of the decoded packet gets written here.
 *
 * @return                      true if a complete packet was received;
 *                              false if the frame is invalid or if additional
 *                                  bytes are expected.
 */
bool mcumgr_serial_rx_byte(struct mcumgr_serial_rx_ctxt *rx_ctxt, u8_t byte,
                           u8_t **out_pkt, int *out_len);

/**
 * @brief Encodes and transmits an mcumgr packet over serial.
 *
 * @param data                  The mcumgr packet data to send.
 * @param len                   The length of the unencoded mcumgr packet.
 * @param cb                    A callback used to transmit raw bytes.
 * @param arg                   An optional argument to pass to the callback.
 *
 * @return                      0 on success; negative error code on failure.
 */
int mcumgr_serial_tx_pkt(const u8_t *data, int len, mcumgr_serial_tx_fn *cb,
                         void *arg);

#ifdef __cplusplus
}
#endif

#endif
