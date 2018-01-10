#include <assert.h>
#include <string.h>
#include "net/buf.h"
#include "mgmt/buf.h"
#include "compilersupport_p.h"

NET_BUF_POOL_DEFINE(pkt_pool, 8, 1024, 7, NULL);

struct net_buf *
mcumgr_buf_alloc(void)
{
    struct net_buf *buf;

    buf = net_buf_alloc(&pkt_pool, K_NO_WAIT);
    assert(buf != NULL);

    return buf;
}

void
mcumgr_buf_free(struct net_buf *nb)
{
    net_buf_unref(nb);
}

static int
cbor_nb_reader_off(const struct cbor_nb_reader *cnr, int offset)
{
    return offset + cnr->init_off;
}

static uint8_t
cbor_nb_reader_get8(struct cbor_decoder_reader *d, int offset)
{
    struct cbor_nb_reader *cnr;
    int off;

    cnr = (struct cbor_nb_reader *) d;
    off = cbor_nb_reader_off(cnr, offset);

    if (off < 0 || off >= cnr->nb->len) {
        return UINT8_MAX;
    }

    return cnr->nb->data[off];
}

static uint16_t
cbor_nb_reader_get16(struct cbor_decoder_reader *d, int offset)
{
    struct cbor_nb_reader *cnr;
    uint16_t val;
    int off;

    cnr = (struct cbor_nb_reader *) d;
    off = cbor_nb_reader_off(cnr, offset);

    if (off < 0 || off > cnr->nb->len - (int)sizeof val) {
        return UINT16_MAX;
    }

    memcpy(&val, cnr->nb->data + off, sizeof val);
    return cbor_ntohs(val);
}

static uint32_t
cbor_nb_reader_get32(struct cbor_decoder_reader *d, int offset)
{
    struct cbor_nb_reader *cnr;
    uint32_t val;
    int off;

    cnr = (struct cbor_nb_reader *) d;
    off = cbor_nb_reader_off(cnr, offset);

    if (off < 0 || off > cnr->nb->len - (int)sizeof val) {
        return UINT32_MAX;
    }

    memcpy(&val, cnr->nb->data + off, sizeof val);
    return cbor_ntohl(val);
}

static uint64_t
cbor_nb_reader_get64(struct cbor_decoder_reader *d, int offset)
{
    struct cbor_nb_reader *cnr;
    uint64_t val;
    int off;

    cnr = (struct cbor_nb_reader *) d;
    off = cbor_nb_reader_off(cnr, offset);

    if (off < 0 || off > cnr->nb->len - (int)sizeof val) {
        return UINT64_MAX;
    }

    memcpy(&val, cnr->nb->data + off, sizeof val);
    return cbor_ntohll(val);
}

static uintptr_t
cbor_nb_reader_cmp(struct cbor_decoder_reader *d, char *buf, int offset,
                    size_t len)
{
    struct cbor_nb_reader *cnr;
    int off;

    cnr = (struct cbor_nb_reader *) d;
    off = cbor_nb_reader_off(cnr, offset);

    if (off < 0 || off > cnr->nb->len - (int)len) {
        return -1;
    }

    return memcmp(cnr->nb->data + off, buf, len);
}

static uintptr_t
cbor_nb_reader_cpy(struct cbor_decoder_reader *d, char *dst, int offset,
                    size_t len)
{
    struct cbor_nb_reader *cnr;
    int off;

    cnr = (struct cbor_nb_reader *) d;
    off = cbor_nb_reader_off(cnr, offset);

    if (off < 0 || off > cnr->nb->len - (int)len) {
        return -1;
    }

    return (uintptr_t)memcpy(dst, cnr->nb->data + off, len);
}

static uintptr_t
cbor_nb_get_string_chunk(struct cbor_decoder_reader *d, int offset, size_t *len)
{
    struct cbor_nb_reader *cnr;
    int off;

    cnr = (struct cbor_nb_reader *) d;
    off = cbor_nb_reader_off(cnr, offset);

    return (uintptr_t)cnr->nb->data + off;
}

void
cbor_nb_reader_init(struct cbor_nb_reader *cnr,
                     struct net_buf *nb,
                     int initial_offset)
{
    cnr->r.get8 = &cbor_nb_reader_get8;
    cnr->r.get16 = &cbor_nb_reader_get16;
    cnr->r.get32 = &cbor_nb_reader_get32;
    cnr->r.get64 = &cbor_nb_reader_get64;
    cnr->r.cmp = &cbor_nb_reader_cmp;
    cnr->r.cpy = &cbor_nb_reader_cpy;
    cnr->r.get_string_chunk = &cbor_nb_get_string_chunk;

    cnr->nb = nb;
    cnr->init_off = initial_offset;
    cnr->r.message_size = nb->len - initial_offset;
}

static int
cbor_nb_write(struct cbor_encoder_writer *writer, const char *data, int len)
{
    struct cbor_nb_writer *cnw;

    cnw = (struct cbor_nb_writer *) writer;
    if (len > net_buf_tailroom(cnw->nb)) {
        return CborErrorOutOfMemory;
    }

    net_buf_add_mem(cnw->nb, data, len);
    cnw->enc.bytes_written += len;

    return CborNoError;
}

void
cbor_nb_writer_init(struct cbor_nb_writer *cnw, struct net_buf *nb)
{
    cnw->nb = nb;
    cnw->enc.bytes_written = 0;
    cnw->enc.write = &cbor_nb_write;
}
