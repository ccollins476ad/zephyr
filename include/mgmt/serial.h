#ifndef H_MGMT_SERIAL_
#define H_MGMT_SERIAL_

#include <zephyr/types.h>

#define MCUMGR_SERIAL_BUF_SZ    1024

#define SHELL_NLIP_PKT          0x0609
#define SHELL_NLIP_DATA         0x0414
#define SHELL_NLIP_MAX_FRAME    128

struct mcumgr_serial_rx_ctxt {
    u8_t *buf;
    int buf_size;

    int raw_off;
    int b64_off;
    int b64_len;

    /* Length of payload as read from header. */
    uint16_t hdr_len;
};

typedef int mcumgr_serial_tx_fn(const void *data, int len, void *arg);

bool mcumgr_serial_rx_byte(struct mcumgr_serial_rx_ctxt *rx_ctxt, u8_t byte,
                           u8_t **out_pkt, int *out_len);
int mcumgr_serial_tx(const u8_t *data, int len, mcumgr_serial_tx_fn *cb,
                     void *arg);
#endif
