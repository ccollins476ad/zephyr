#include <assert.h>
#include <stdbool.h>
#include <crc16.h>
#include <misc/byteorder.h>
#include "mgmt/serial.h"
#include "base64/base64.h"

static u16_t
mcumgr_serial_calc_crc(const u8_t *data, int len)
{
    return crc16(data, len, 0x1021, 0, true);
}

static int
mcumgr_serial_decode_req(const u8_t *src, u8_t *dst, int max_dst_len)
{
    if (base64_decode_len((char *)src) > max_dst_len) {
        return -1;
    }

    return base64_decode(src, dst);
}

static int
mcumgr_serial_parse_op(const u8_t *buf, int len)
{
    uint16_t op;

    if (len < sizeof op) {
        return -1;
    }

    memcpy(&op, buf, sizeof op);
    op = sys_be16_to_cpu(op);

    if (op != MCUMGR_SERIAL_HDR_PKT && op != MCUMGR_SERIAL_HDR_FRAG) {
        return -1;
    }

    return op;
}

static int
mcumgr_serial_parse_len(struct mcumgr_serial_rx_ctxt *rx_ctxt)
{
    uint16_t len;

    if (rx_ctxt->raw_off < sizeof len) {
        return -1;
    }

    memcpy(&len, rx_ctxt->buf, sizeof len);
    rx_ctxt->hdr_len = sys_be16_to_cpu(len);
    return rx_ctxt->hdr_len;
}

static int
mcumgr_serial_decode_frag(struct mcumgr_serial_rx_ctxt *rx_ctxt,
                          const u8_t *frag)
{
    int dec_len;

    dec_len = mcumgr_serial_decode_req(frag,
                                       rx_ctxt->buf + rx_ctxt->raw_off,
                                       rx_ctxt->buf_size - rx_ctxt->raw_off);
    if (dec_len == -1) {
        return -1;
    }

    rx_ctxt->raw_off += dec_len;

    return 0;
}

/**
 * @return true if a complete packet was received.
 */
static bool
mcumgr_serial_process_frag(struct mcumgr_serial_rx_ctxt *rx_ctxt,
                           const u8_t *frag, int frag_len)
{
    uint16_t crc;
    uint16_t op;
    int rc;

    op = mcumgr_serial_parse_op(frag, frag_len);
    switch (op) {
    case MCUMGR_SERIAL_HDR_PKT:
        rx_ctxt->raw_off = 0;
        break;

    case MCUMGR_SERIAL_HDR_FRAG:
        if (rx_ctxt->raw_off == 0) {
            return -1;
        }
        break;

    default:
        return false;
    }

    rc = mcumgr_serial_decode_frag(rx_ctxt, frag + sizeof op);
    if (rc != 0) {
        return false;
    }

    if (op == MCUMGR_SERIAL_HDR_PKT) {
        rc = mcumgr_serial_parse_len(rx_ctxt);
        if (rc == -1) {
            return false;
        }
    }

    if (rx_ctxt->raw_off > rx_ctxt->hdr_len + 2) {
        return false;
    }

    if (rx_ctxt->raw_off < rx_ctxt->hdr_len + 2) {
        return false;
    }

    crc = mcumgr_serial_calc_crc(rx_ctxt->buf + 2, rx_ctxt->raw_off - 2);
    if (crc != 0) {
        return false;
    }

    return true;
}

/**
 * @return true if a complete packet was received.
 */
bool
mcumgr_serial_rx_byte(struct mcumgr_serial_rx_ctxt *rx_ctxt, u8_t byte,
                      u8_t **out_pkt, int *out_len)
{
    bool complete;
    int byte_off;

    byte_off = rx_ctxt->b64_off + rx_ctxt->b64_len;
    if (byte_off >= rx_ctxt->buf_size) {
        /* Overrun; wraparound. */
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
    complete = mcumgr_serial_process_frag(rx_ctxt, rx_ctxt->buf + rx_ctxt->b64_off, rx_ctxt->b64_len);

    if (!complete) {
        rx_ctxt->b64_off += rx_ctxt->b64_len;
        rx_ctxt->b64_len = 0;
        return false;
    } else {
        *out_pkt = rx_ctxt->buf + 2;
        *out_len = rx_ctxt->raw_off - 4;
        rx_ctxt->raw_off = 0;
        rx_ctxt->b64_off = 0;
        rx_ctxt->b64_len = 0;
        return true;
    }
}

static int
mcumgr_serial_tx_small(const void *data, int len, mcumgr_serial_tx_fn *cb, void *arg)
{
    u8_t b64[4];

    assert(len > 0 && len <= 3);
    base64_encode(data, len, b64, 1);

    return cb(b64, 4, arg);
}

int
mcumgr_serial_tx(const u8_t *data, int len, mcumgr_serial_tx_fn *cb,
                 void *arg)
{
    u8_t raw[3];
    uint16_t u16;
    uint16_t crc;
    int off;
    int rem;
    int rc;
    int i;

    crc = mcumgr_serial_calc_crc(data, len);

    u16 = sys_cpu_to_be16(MCUMGR_SERIAL_HDR_PKT);
    rc = cb(&u16, sizeof u16, arg);
    if (rc != 0) {
        return rc;
    }

    u16 = sys_cpu_to_be16(len);
    memcpy(raw, &u16, sizeof u16);
    raw[2] = data[0];

    rc = mcumgr_serial_tx_small(raw, 3, cb, arg);
    if (rc != 0) {
        return rc;
    }

    i = 1;
    while (1) {
        rem = len - i;
        if (rem == 0) {
            raw[0] = (crc & 0xff00) >> 8;
            raw[1] = crc & 0x00ff;
            rc = mcumgr_serial_tx_small(raw, 2, cb, arg);
            break;
        } else if (rem == 1) {
            raw[0] = data[i];
            raw[1] = (crc & 0xff00) >> 8;
            raw[2] = crc & 0x00ff;
            rc = mcumgr_serial_tx_small(raw, 3, cb, arg);
            break;
        } else if (rem == 2) {
            raw[0] = data[i];
            raw[1] = data[i + 1];
            raw[2] = (crc & 0xff00) >> 8;
            rc = mcumgr_serial_tx_small(raw, 3, cb, arg);
            if (rc != 0) {
                return rc;
            }
            off += 4;

            raw[0] = crc & 0x00ff;
            rc = mcumgr_serial_tx_small(raw, 1, cb, arg);
            break;
        } else {
            /* >= 3 raw bytes to send. */
            memcpy(raw, data + i, 3);
            rc = mcumgr_serial_tx_small(raw, 3, cb, arg);
        }

        if (rc != 0) {
            return rc;
        }

        i += 3;
        off += 4;
    }

    rc = cb("\n", 1, arg);
    if (rc != 0) {
        return rc;
    }

	return 0;
}
