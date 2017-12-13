/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <assert.h>
#include <string.h>

#include "mgmt/mgmt.h"
#include "newtmgr/newtmgr.h"
#include "cbor.h"

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

#ifndef ntohs
#define ntohs(x) (x)
#endif

#ifndef htons
#define htons(x) (x)
#endif

#else
/* Little endian. */

#ifndef ntohs
#define ntohs(x)   ((uint16_t)  \
    ((((x) & 0xff00) >> 8) |    \
     (((x) & 0x00ff) << 8)))
#endif

#ifndef htons
#define htons(x) (ntohs(x))
#endif

#endif

static int
nmgr_align4(int x)
{
    int rem;

    rem = x % 4;
    if (rem == 0) {
        return x;
    } else {
        return x - rem + 4;
    }
}

static uint8_t
nmgr_rsp_op(uint8_t req_op)
{
    if (req_op == NMGR_OP_READ) {
        return NMGR_OP_READ_RSP;
    } else {
        return NMGR_OP_WRITE_RSP;
    }
}

void
nmgr_ntoh_hdr(struct nmgr_hdr *hdr)
{
    hdr->nh_len = ntohs(hdr->nh_len);
    hdr->nh_group = ntohs(hdr->nh_group);
}

static void
nmgr_hton_hdr(struct nmgr_hdr *hdr)
{
    hdr->nh_len = htons(hdr->nh_len);
    hdr->nh_group = htons(hdr->nh_group);
}

static void
nmgr_init_rsp_hdr(const struct nmgr_hdr *req_hdr, struct nmgr_hdr *rsp_hdr)
{
    *rsp_hdr = (struct nmgr_hdr) {
        .nh_len = 0,
        .nh_flags = 0,
        .nh_op = nmgr_rsp_op(req_hdr->nh_op),
        .nh_group = req_hdr->nh_group,
        .nh_seq = req_hdr->nh_seq,
        .nh_id = req_hdr->nh_id,
    };
}

static int
nmgr_read_hdr(struct nmgr_streamer *streamer, struct nmgr_hdr *hdr)
{
    struct mgmt_streamer *base;

    base = &streamer->ns_base;

    if (base->reader->message_size < sizeof *hdr) {
        return MGMT_ERR_EINVAL;
    }

    base->reader->cpy(base->reader, (char *)hdr, 0, sizeof *hdr);
    return 0;
}

static int
nmgr_write_hdr(struct nmgr_streamer *streamer, const struct nmgr_hdr *hdr)
{
    int rc;

    rc = mgmt_streamer_write_at(&streamer->ns_base, 0, hdr, sizeof *hdr);
    return mgmt_err_from_cbor(rc);
}

static int
nmgr_build_err_rsp(struct nmgr_streamer *streamer,
                   const struct nmgr_hdr *req_hdr,
                   int status)
{
    struct CborEncoder map;
    struct mgmt_cbuf cbuf;
    struct nmgr_hdr rsp_hdr;
    int rc;

    rc = mgmt_cbuf_init(&cbuf, &streamer->ns_base);
    if (rc != 0) {
        return rc;
    }

    nmgr_init_rsp_hdr(req_hdr, &rsp_hdr);
    rc = nmgr_write_hdr(streamer, &rsp_hdr);
    if (rc != 0) {
        return rc;
    }

    rc = cbor_encoder_create_map(&cbuf.encoder, &map, CborIndefiniteLength);
    if (rc != 0) {
        return rc;
    }

    rc = mgmt_cbuf_setoerr(&cbuf, status);
    if (rc != 0) {
        return rc;
    }

    rc = cbor_encoder_close_container(&cbuf.encoder, &map);
    if (rc != 0) {
        return rc;
    }

    rsp_hdr.nh_len = htons(cbor_encode_bytes_written(&cbuf.encoder));
    rc = nmgr_write_hdr(streamer, &rsp_hdr);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

int
nmgr_handle_single_payload(struct mgmt_cbuf *cbuf,
                           const struct nmgr_hdr *req_hdr)
{
    const struct mgmt_handler *handler;
    struct CborEncoder payload_encoder;
    int rc;

    handler = mgmt_find_handler(req_hdr->nh_group, req_hdr->nh_id);
    if (!handler) {
        return MGMT_ERR_ENOENT;
    }

    /* Begin response payload.  Response fields are inserted into the root
     * map as key value pairs.
     */
    rc = cbor_encoder_create_map(&cbuf->encoder, &payload_encoder,
                                 CborIndefiniteLength);
    rc = mgmt_err_from_cbor(rc);
    if (rc != 0) {
        return rc;
    }

    if (req_hdr->nh_op == NMGR_OP_READ) {
        if (handler->mh_read) {
            rc = handler->mh_read(cbuf);
        } else {
            rc = MGMT_ERR_ENOENT;
        }
    } else if (req_hdr->nh_op == NMGR_OP_WRITE) {
        if (handler->mh_write) {
            rc = handler->mh_write(cbuf);
        } else {
            rc = MGMT_ERR_ENOENT;
        }
    } else {
        rc = MGMT_ERR_EINVAL;
    }
    if (rc != 0) {
        return rc;
    }

    /* End response payload. */
    rc = cbor_encoder_close_container(&cbuf->encoder, &payload_encoder);
    rc = mgmt_err_from_cbor(rc);
    if (rc != 0) {
        return rc;
    }

    return 0;
}


static int
nmgr_handle_single_req(struct nmgr_streamer *streamer,
                       const struct nmgr_hdr *req_hdr)
{
    struct mgmt_cbuf cbuf;
    struct nmgr_hdr rsp_hdr;
    int rc;

    rc = mgmt_cbuf_init(&cbuf, &streamer->ns_base);
    if (rc != 0) {
        return rc;
    }

    /* Build response header a priori, then pass to the handlers to fill out
     * the response data and adjust length and flags.
     */
    nmgr_init_rsp_hdr(req_hdr, &rsp_hdr);
    rc = nmgr_write_hdr(streamer, &rsp_hdr);
    if (rc != 0) {
        return rc;
    }

    rc = nmgr_handle_single_payload(&cbuf, req_hdr);
    if (rc != 0) {
        return rc;
    }

    rsp_hdr.nh_len = cbor_encode_bytes_written(&cbuf.encoder);
    nmgr_hton_hdr(&rsp_hdr);
    rc = nmgr_write_hdr(streamer, &rsp_hdr);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static void
nmgr_on_err(struct nmgr_streamer *streamer,
            const struct nmgr_hdr *req_hdr,
            void *req,
            void *rsp,
            int status)
{
    int rc;

    if (rsp == NULL) {
        rsp = req;
        req = NULL;
    }

    mgmt_streamer_reset_buf(&streamer->ns_base, rsp);
    mgmt_streamer_init_writer(&streamer->ns_base, rsp);

    rc = nmgr_build_err_rsp(streamer, req_hdr, status);
    if (rc == 0) {
        streamer->ns_tx_rsp(streamer, rsp, streamer->ns_base.cb_arg);
        rsp = NULL;
    }

    mgmt_streamer_free_buf(&streamer->ns_base, req);
    mgmt_streamer_free_buf(&streamer->ns_base, rsp);
}

int
nmgr_process_single_packet(struct nmgr_streamer *streamer, void *req)
{
    struct nmgr_hdr req_hdr;
    void *rsp;
    bool valid_hdr;
    int rc;

    rsp = NULL;
    valid_hdr = true;

    while (1) {
        rc = mgmt_streamer_init_reader(&streamer->ns_base, req);
        if (rc != 0) {
            valid_hdr = false;
            break;
        }

        rc = nmgr_read_hdr(streamer, &req_hdr);
        if (rc != 0) {
            valid_hdr = false;
            break;
        }
        nmgr_ntoh_hdr(&req_hdr);
        rc = mgmt_streamer_trim_front(&streamer->ns_base, req, NMGR_HDR_SIZE);
        assert(rc == 0);

        rsp = mgmt_streamer_alloc_rsp(&streamer->ns_base, req);
        if (rsp == NULL) {
            rc = MGMT_ERR_ENOMEM;
            break;
        }

        rc = mgmt_streamer_init_writer(&streamer->ns_base, rsp);
        if (rc != 0) {
            break;
        }

        rc = nmgr_handle_single_req(streamer, &req_hdr);
        if (rc != 0) {
            break;
        }

        rc = streamer->ns_tx_rsp(streamer, rsp, streamer->ns_base.cb_arg);
        rsp = NULL;
        if (rc != 0) {
            break;
        }

        /* Trim processed request to free up space for subsequent responses. */
        rc = mgmt_streamer_trim_front(&streamer->ns_base, req,
                                      nmgr_align4(req_hdr.nh_len));
        assert(rc == 0);
    }

    if (rc != 0 && valid_hdr) {
        nmgr_on_err(streamer, &req_hdr, req, rsp, rc);
        return rc;
    }

    mgmt_streamer_free_buf(&streamer->ns_base, req);
    mgmt_streamer_free_buf(&streamer->ns_base, rsp);
    return 0;
}
