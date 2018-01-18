/*
 * Copyright Runtime.io 2018. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <crc16.h>
#include <misc/byteorder.h>
#include "mbedtls/base64.h"
#include "mgmt/serial.h"

static u16_t mcumgr_serial_calc_crc(const u8_t *data, int len)
{
	return crc16(data, len, 0x1021, 0, true);
}

/**
 * Base64-decodes an mcumgr request.  The source and destination buffers can
 * overlap.
 */
static int mcumgr_serial_decode_req(const u8_t *src, u8_t *dst,
				    int max_dst_len)
{
	size_t dst_len;
	size_t src_len;
	int rc;

	src_len = strlen(src);
	rc = mbedtls_base64_decode(dst, max_dst_len, &dst_len, src, src_len);
	if (rc != 0) {
		return -1;
	}

	return 0;
}

static int mcumgr_serial_parse_op(const u8_t *buf, int len)
{
	u16_t op;

	if (len < sizeof(op)) {
		return -EINVAL;
	}

	memcpy(&op, buf, sizeof(op));
	op = sys_be16_to_cpu(op);

	if (op != MCUMGR_SERIAL_HDR_PKT && op != MCUMGR_SERIAL_HDR_FRAG) {
		return -EINVAL;
	}

	return op;
}

static int mcumgr_serial_parse_len(struct mcumgr_serial_rx_ctxt *rx_ctxt)
{
	u16_t len;

	if (rx_ctxt->raw_off < sizeof(len)) {
		return -EINVAL;
	}

	memcpy(&len, rx_ctxt->buf, sizeof(len));
	rx_ctxt->pkt_len = sys_be16_to_cpu(len);
	return rx_ctxt->pkt_len;
}

static int mcumgr_serial_decode_frag(struct mcumgr_serial_rx_ctxt *rx_ctxt,
				     const u8_t *frag)
{
	int dec_len;

	dec_len = mcumgr_serial_decode_req(
		frag,
		rx_ctxt->buf + rx_ctxt->raw_off,
		rx_ctxt->buf_size - rx_ctxt->raw_off);
	if (dec_len < 0) {
		return -EINVAL;
	}

	rx_ctxt->raw_off += dec_len;

	return 0;
}

/**
 * Processes a received mcumgr frame.
 *
 * @return                      true if a complete packet was received;
 *                              false if the frame is invalid or if additional
 *                                  fragments are expected.
 */
static bool mcumgr_serial_process_frag(struct mcumgr_serial_rx_ctxt *rx_ctxt,
				       const u8_t *frag, int frag_len)
{
	u16_t crc;
	u16_t op;
	int rc;

	op = mcumgr_serial_parse_op(frag, frag_len);
	switch (op) {
	case MCUMGR_SERIAL_HDR_PKT:
		rx_ctxt->raw_off = 0;
		break;

	case MCUMGR_SERIAL_HDR_FRAG:
		if (rx_ctxt->raw_off == 0) {
			return false;
		}
		break;

	default:
		return false;
	}

	rc = mcumgr_serial_decode_frag(rx_ctxt, frag + sizeof(op));
	if (rc != 0) {
		return false;
	}

	if (op == MCUMGR_SERIAL_HDR_PKT) {
		rc = mcumgr_serial_parse_len(rx_ctxt);
		if (rc == -1) {
			return false;
		}
	}

	if (rx_ctxt->raw_off != rx_ctxt->pkt_len + 2) {
		return false;
	}

	crc = mcumgr_serial_calc_crc(rx_ctxt->buf + 2, rx_ctxt->raw_off - 2);
	if (crc != 0) {
		return false;
	}

	return true;
}

bool mcumgr_serial_rx_byte(struct mcumgr_serial_rx_ctxt *rx_ctxt, u8_t byte,
			   u8_t **out_pkt, int *out_len)
{
	bool complete;
	int byte_off;

	byte_off = rx_ctxt->b64_off + rx_ctxt->b64_len;
	if (byte_off >= rx_ctxt->buf_size) {
		/* Overrun; wrap around. */
		rx_ctxt->raw_off = 0;
		rx_ctxt->b64_off = 0;
		rx_ctxt->b64_len = 0;
	} else {
		rx_ctxt->b64_len++;
	}

	rx_ctxt->buf[byte_off] = byte;
	if (byte != '\n') {
		return false;
	}

	rx_ctxt->buf[byte_off] = '\0';
	complete = mcumgr_serial_process_frag(rx_ctxt,
					      rx_ctxt->buf + rx_ctxt->b64_off,
					      rx_ctxt->b64_len);

	if (!complete) {
		rx_ctxt->b64_off += rx_ctxt->b64_len;
		rx_ctxt->b64_len = 0;
		return false;
	}

	*out_pkt = rx_ctxt->buf + 2;
	*out_len = rx_ctxt->raw_off - 4;
	rx_ctxt->raw_off = 0;
	rx_ctxt->b64_off = 0;
	rx_ctxt->b64_len = 0;
	return true;
}

/**
 * Base64-encodes a small chunk of data and transmits it.  The data must be no
 * larger than three bytes.
 */
static int mcumgr_serial_tx_small(const void *data, int len,
				  mcumgr_serial_tx_cb cb, void *arg)
{
	size_t dst_len;
	u8_t b64[4];
	int rc;

	rc = mbedtls_base64_encode(b64, 4, &dst_len, data, len);
	assert(rc == 0);

	return cb(b64, 4, arg);
}

/**
 * @brief Transmits a single mcumgr frame over serial.
 *
 * @param data                  The frame payload to transmit.  This does not
 *                                  include a header or CRC.
 * @param first                 Whether this is the first frame in the packet.
 * @param len                   The number of untransmitted data bytes in the
 *                                  packet.
 * @param crc                   The 16-bit CRC of the entire packet.
 * @param cb                    A callback used for transmitting raw data.
 * @param arg                   An optional argument that gets passed to the
 *                                  callback.
 * @param out_data_bytes_txed   On success, the number of data bytes
 *                                  transmitted gets written here.
 *
 * @return                      0 on success; negative error code on failure.
 */
int mcumgr_serial_tx_frame(const u8_t *data, bool first, int len,
			   u16_t crc, mcumgr_serial_tx_cb cb, void *arg,
			   int *out_data_bytes_txed)
{
	u8_t raw[3];
	u16_t u16;
	int dst_off;
	int src_off;
	int rem;
	int rc;

	src_off = 0;
	dst_off = 0;

	if (first) {
		u16 = sys_cpu_to_be16(MCUMGR_SERIAL_HDR_PKT);
	} else {
		u16 = sys_cpu_to_be16(MCUMGR_SERIAL_HDR_FRAG);
	}

	rc = cb(&u16, sizeof(u16), arg);
	if (rc != 0) {
		return rc;
	}
	dst_off += 2;

	/* Only the first fragment contains the packet length. */
	if (first) {
		u16 = sys_cpu_to_be16(len);
		memcpy(raw, &u16, sizeof(u16));
		raw[2] = data[0];

		rc = mcumgr_serial_tx_small(raw, 3, cb, arg);
		if (rc != 0) {
			return rc;
		}

		src_off++;
		dst_off += 4;
	}

	while (1) {
		if (dst_off >= MCUMGR_SERIAL_MAX_FRAME - 4) {
			/* Can't fit any more data in this frame. */
			break;
		}

		/* If we have reached the end of the packet, we need to encode
		 * and send the CRC.
		 */
		rem = len - src_off;
		if (rem == 0) {
			raw[0] = (crc & 0xff00) >> 8;
			raw[1] = crc & 0x00ff;
			rc = mcumgr_serial_tx_small(raw, 2, cb, arg);
			if (rc != 0) {
				return rc;
			}
			break;
		}

		if (rem == 1) {
			raw[0] = data[src_off];
			src_off++;

			raw[1] = (crc & 0xff00) >> 8;
			raw[2] = crc & 0x00ff;
			rc = mcumgr_serial_tx_small(raw, 3, cb, arg);
			if (rc != 0) {
				return rc;
			}
			break;
		}

		if (rem == 2) {
			raw[0] = data[src_off];
			raw[1] = data[src_off + 1];
			src_off += 2;

			raw[2] = (crc & 0xff00) >> 8;
			rc = mcumgr_serial_tx_small(raw, 3, cb, arg);
			if (rc != 0) {
				return rc;
			}

			raw[0] = crc & 0x00ff;
			rc = mcumgr_serial_tx_small(raw, 1, cb, arg);
			if (rc != 0) {
				return rc;
			}
			break;
		}

		/* Otherwise, just encode payload data. */
		memcpy(raw, data + src_off, 3);
		rc = mcumgr_serial_tx_small(raw, 3, cb, arg);
		if (rc != 0) {
			return rc;
		}
		src_off += 3;
		dst_off += 4;
	}

	rc = cb("\n", 1, arg);
	if (rc != 0) {
		return rc;
	}

	*out_data_bytes_txed = src_off;
	return 0;
}

int mcumgr_serial_tx_pkt(const u8_t *data, int len, mcumgr_serial_tx_cb cb,
			 void *arg)
{
	u16_t crc;
	int data_bytes_txed;
	int src_off;
	int rc;

	/* Calculate CRC of entire packet. */
	crc = mcumgr_serial_calc_crc(data, len);

	/* Transmit packet as a sequence of frames. */
	src_off = 0;
	while (src_off < len) {
		rc = mcumgr_serial_tx_frame(data + src_off,
					    src_off == 0,
					    len - src_off,
					    crc, cb, arg,
					    &data_bytes_txed);
		if (rc != 0) {
			return rc;
		}

		src_off += data_bytes_txed;
	}

	return 0;
}
