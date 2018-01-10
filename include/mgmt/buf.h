#ifndef H_MGMT_BUF
#define H_MGMT_BUF

#include <inttypes.h>
#include "cbor_encoder_writer.h"
#include "cbor_decoder_reader.h"
struct net_buf;

struct cbor_nb_reader {
    struct cbor_decoder_reader r;
    int init_off;                     /* initial offset into the data */
    struct net_buf *nb;
};

struct cbor_nb_writer {
    struct cbor_encoder_writer enc;
    struct net_buf *nb;
};

struct net_buf *mcumgr_buf_alloc(void);
void mcumgr_buf_free(struct net_buf *nb);

void cbor_nb_writer_init(struct cbor_nb_writer *czw,
                         struct net_buf *nb);

void cbor_nb_reader_init(struct cbor_nb_reader *czr,
                         struct net_buf *nb,
                         int intial_offset);

#endif
