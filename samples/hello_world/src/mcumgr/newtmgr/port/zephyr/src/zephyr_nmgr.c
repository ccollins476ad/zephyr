#include <zephyr.h>
#include "drivers/console/uart_nmgr.h"
#include "mgmt/mgmt.h"
#include "newtmgr/newtmgr.h"
#include "znp/znp.h"

/* Shared queue that newtmgr uses for work items. */
struct os_eventq *nmgr_evq;

static mgmt_alloc_rsp_fn zephyr_nmgr_alloc_rsp;
static mgmt_trim_front_fn zephyr_nmgr_trim_front;
static mgmt_reset_buf_fn zephyr_nmgr_reset_buf;
static mgmt_write_at_fn zephyr_nmgr_write_at;
static mgmt_init_reader_fn zephyr_nmgr_init_reader;
static mgmt_init_writer_fn zephyr_nmgr_init_writer;
static mgmt_free_buf_fn zephyr_nmgr_free_buf;
static nmgr_tx_rsp_fn zephyr_nmgr_tx_rsp;

static const struct mgmt_streamer_cfg zephyr_nmgr_cbor_cfg = {
    .alloc_rsp = zephyr_nmgr_alloc_rsp,
    .trim_front = zephyr_nmgr_trim_front,
    .reset_buf = zephyr_nmgr_reset_buf,
    .write_at = zephyr_nmgr_write_at,
    .init_reader = zephyr_nmgr_init_reader,
    .init_writer = zephyr_nmgr_init_writer,
    .free_buf = zephyr_nmgr_free_buf,
};

#if 0
/**
 * Allocates an mbuf to contain an outgoing response fragment.
 */
static struct os_mbuf *
zephyr_nmgr_rsp_frag_alloc(uint16_t frag_size, void *arg)
{
    struct os_mbuf *src_rsp;
    struct os_mbuf *frag;

    /* We need to duplicate the user header from the source response, as that
     * is where transport-specific information is stored.
     */
    src_rsp = arg;

    frag = os_msys_get_pkthdr(frag_size, OS_MBUF_USRHDR_LEN(src_rsp));
    if (frag != NULL) {
        /* Copy the user header from the response into the fragment mbuf. */
        memcpy(OS_MBUF_USRHDR(frag), OS_MBUF_USRHDR(src_rsp),
               OS_MBUF_USRHDR_LEN(src_rsp));
    }

    return frag;
}
#endif

static void *
zephyr_nmgr_alloc_rsp(const void *req, void *arg)
{
    struct zephyr_nmgr_pkt *rsp;

    rsp = k_malloc(sizeof *rsp);
    if (rsp == NULL) {
        assert(0);
        return NULL;
    }
    rsp->len = 0;

    return rsp;
}

static int
zephyr_nmgr_trim_front(void *buf, int len, void *arg)
{
    struct zephyr_nmgr_pkt *pkt;

    if (len > 0) {
        pkt = buf;
        if (len >= pkt->len) {
            pkt->len = 0;
        } else {
            memmove(pkt->data, pkt->data + len, pkt->len - len);
            pkt->len -= len;
        }
    }

    return 0;
}

static void
zephyr_nmgr_reset_buf(void *buf, void *arg)
{
    struct zephyr_nmgr_pkt *pkt;

    pkt = buf;
    pkt->len = 0;
}

static int
zephyr_nmgr_write_at(struct cbor_encoder_writer *writer, int offset,
                     const void *data, int len, void *arg)
{
    struct cbor_znp_writer *czw;
    struct zephyr_nmgr_pkt *pkt;

    czw = (struct cbor_znp_writer *)writer;
    pkt = czw->pkt;

    if (offset < 0 || offset > pkt->len) {
        return MGMT_ERR_EINVAL;
    }

    if (offset + len > sizeof pkt->data) {
        return MGMT_ERR_EINVAL;
    }

    memcpy(pkt->data + offset, data, len);
    if (pkt->len < offset + len) {
        pkt->len = offset + len;
        writer->bytes_written = pkt->len;
    }

    return 0;
}

static int
zephyr_nmgr_tx_rsp(struct nmgr_streamer *ns, void *rsp, void *arg)
{
    struct zephyr_nmgr_pkt *pkt;
    int rc;

    pkt = rsp;

    rc = uart_nmgr_send(pkt->data, pkt->len);
    assert(rc == 0);

    mgmt_streamer_free_buf(&ns->ns_base, pkt);
    return MGMT_ERR_EOK;
}

static void
zephyr_nmgr_free_buf(void *buf, void *arg)
{
    k_free(buf);
}

static int
zephyr_nmgr_init_reader(struct cbor_decoder_reader *reader, void *buf,
                        void *arg)
{
    struct cbor_znp_reader *czr;

    czr = (struct cbor_znp_reader *)reader;
    cbor_znp_reader_init(czr, buf, 0);

    return 0;
}

static int
zephyr_nmgr_init_writer(struct cbor_encoder_writer *writer, void *buf,
                        void *arg)
{
    struct cbor_znp_writer *czw;

    czw = (struct cbor_znp_writer *)writer;
    cbor_znp_writer_init(czw, buf);

    return 0;
}

int
zephyr_nmgr_process_packet(struct zephyr_nmgr_pkt *pkt)
{
    struct cbor_znp_reader reader;
    struct cbor_znp_writer writer;
    struct nmgr_streamer streamer;
    int rc;

    streamer = (struct nmgr_streamer) {
        .ns_base = {
            .cfg = &zephyr_nmgr_cbor_cfg,
            .reader = &reader.r,
            .writer = &writer.enc,
            .cb_arg = NULL,
        },
        .ns_tx_rsp = zephyr_nmgr_tx_rsp,
    };

    rc = nmgr_process_single_packet(&streamer, pkt);
    return rc;
}
