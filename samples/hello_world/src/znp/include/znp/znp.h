#ifndef H_ZNP_
#define H_ZNP_

#include <inttypes.h>
#include "cbor_encoder_writer.h"
#include "cbor_decoder_reader.h"

#define ZEPHYR_NMGR_PKT_SZ          512
#define ZEPHYR_NMGR_PKT_EXTRA_SZ    8

struct zephyr_nmgr_pkt {
    void *fifo_reserved;
    uint8_t data[ZEPHYR_NMGR_PKT_SZ];
    int len;
    uint8_t extra[ZEPHYR_NMGR_PKT_EXTRA_SZ];
};

struct cbor_znp_reader {
    struct cbor_decoder_reader r;
    int init_off;                     /* initial offset into the data */
    struct zephyr_nmgr_pkt *pkt;
};

struct cbor_znp_writer {
    struct cbor_encoder_writer enc;
    struct zephyr_nmgr_pkt *pkt;
};

void cbor_znp_writer_init(struct cbor_znp_writer *czw,
                          struct zephyr_nmgr_pkt *pkt);

void cbor_znp_reader_init(struct cbor_znp_reader *czr,
                          struct zephyr_nmgr_pkt *pkt,
                          int intial_offset);

#endif
