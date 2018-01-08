#include <assert.h>
#include <string.h>
#include "compilersupport_p.h"
#include "mgmt/znp.h"

static int
cbor_znp_reader_off(const struct cbor_znp_reader *czr, int offset)
{
    return offset + czr->init_off;
}

static uint8_t
cbor_znp_reader_get8(struct cbor_decoder_reader *d, int offset)
{
    struct cbor_znp_reader *czr;
    int off;

    czr = (struct cbor_znp_reader *) d;
    off = cbor_znp_reader_off(czr, offset);

    if (off < 0 || off >= czr->pkt->len) {
        return UINT8_MAX;
    }

    return czr->pkt->data[off];
}

static uint16_t
cbor_znp_reader_get16(struct cbor_decoder_reader *d, int offset)
{
    struct cbor_znp_reader *czr;
    uint16_t val;
    int off;

    czr = (struct cbor_znp_reader *) d;
    off = cbor_znp_reader_off(czr, offset);

    if (off < 0 || off > czr->pkt->len - (int)sizeof val) {
        return UINT16_MAX;
    }

    memcpy(&val, czr->pkt->data + off, sizeof val);
    return cbor_ntohs(val);
}

static uint32_t
cbor_znp_reader_get32(struct cbor_decoder_reader *d, int offset)
{
    struct cbor_znp_reader *czr;
    uint32_t val;
    int off;

    czr = (struct cbor_znp_reader *) d;
    off = cbor_znp_reader_off(czr, offset);

    if (off < 0 || off > czr->pkt->len - (int)sizeof val) {
        return UINT32_MAX;
    }

    memcpy(&val, czr->pkt->data + off, sizeof val);
    return cbor_ntohl(val);
}

static uint64_t
cbor_znp_reader_get64(struct cbor_decoder_reader *d, int offset)
{
    struct cbor_znp_reader *czr;
    uint64_t val;
    int off;

    czr = (struct cbor_znp_reader *) d;
    off = cbor_znp_reader_off(czr, offset);

    if (off < 0 || off > czr->pkt->len - (int)sizeof val) {
        return UINT64_MAX;
    }

    memcpy(&val, czr->pkt->data + off, sizeof val);
    return cbor_ntohll(val);
}

static uintptr_t
cbor_znp_reader_cmp(struct cbor_decoder_reader *d, char *buf, int offset,
                    size_t len)
{
    struct cbor_znp_reader *czr;
    int off;

    czr = (struct cbor_znp_reader *) d;
    off = cbor_znp_reader_off(czr, offset);

    if (off < 0 || off > czr->pkt->len - (int)len) {
        return -1;
    }

    return memcmp(czr->pkt->data + off, buf, len);
}

static uintptr_t
cbor_znp_reader_cpy(struct cbor_decoder_reader *d, char *dst, int offset,
                    size_t len)
{
    struct cbor_znp_reader *czr;
    int off;

    czr = (struct cbor_znp_reader *) d;
    off = cbor_znp_reader_off(czr, offset);

    if (off < 0 || off > czr->pkt->len - (int)len) {
        return -1;
    }

    return (uintptr_t)memcpy(dst, czr->pkt->data + off, len);
}

static uintptr_t
cbor_znp_get_string_chunk(struct cbor_decoder_reader *d, int offset, size_t *len)
{
    struct cbor_znp_reader *czr;
    int off;

    czr = (struct cbor_znp_reader *) d;
    off = cbor_znp_reader_off(czr, offset);

    return (uintptr_t)czr->pkt->data + off;
}

void
cbor_znp_reader_init(struct cbor_znp_reader *czr,
                     struct zephyr_nmgr_pkt *pkt,
                     int initial_offset)
{
    czr->r.get8 = &cbor_znp_reader_get8;
    czr->r.get16 = &cbor_znp_reader_get16;
    czr->r.get32 = &cbor_znp_reader_get32;
    czr->r.get64 = &cbor_znp_reader_get64;
    czr->r.cmp = &cbor_znp_reader_cmp;
    czr->r.cpy = &cbor_znp_reader_cpy;
    czr->r.get_string_chunk = &cbor_znp_get_string_chunk;

    czr->pkt = pkt;
    czr->init_off = initial_offset;
    czr->r.message_size = pkt->len - initial_offset;
}

static int
cbor_znp_write(struct cbor_encoder_writer *writer, const char *data, int len)
{
    struct cbor_znp_writer *czw;

    czw = (struct cbor_znp_writer *) writer;
    if (czw->pkt->len + len > ZEPHYR_NMGR_PKT_SZ) {
        return CborErrorOutOfMemory;
    }

    memcpy(czw->pkt->data + czw->pkt->len, data, len);
    czw->pkt->len += len;
    czw->enc.bytes_written += len;

    return CborNoError;
}

void
cbor_znp_writer_init(struct cbor_znp_writer *czw, struct zephyr_nmgr_pkt *pkt)
{
    czw->pkt = pkt;
    czw->enc.bytes_written = 0;
    czw->enc.write = &cbor_znp_write;
}
