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

#ifndef H_MGMT_MGMT_
#define H_MGMT_MGMT_

#include <inttypes.h>

#include "cbor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* MTU for newtmgr responses */
#define MGMT_MAX_MTU 1024

#define NMGR_OP_READ            (0)
#define NMGR_OP_READ_RSP        (1)
#define NMGR_OP_WRITE           (2)
#define NMGR_OP_WRITE_RSP       (3)

/* First 64 groups are reserved for system level newtmgr commands.
 * Per-user commands are then defined after group 64.
 */
#define MGMT_GROUP_ID_OS        (0)
#define MGMT_GROUP_ID_IMAGE     (1)
#define MGMT_GROUP_ID_STATS     (2)
#define MGMT_GROUP_ID_CONFIG    (3)
#define MGMT_GROUP_ID_LOGS      (4)
#define MGMT_GROUP_ID_CRASH     (5)
#define MGMT_GROUP_ID_SPLIT     (6)
#define MGMT_GROUP_ID_RUN       (7)
#define MGMT_GROUP_ID_FS        (8)
#define MGMT_GROUP_ID_PERUSER   (64)

/**
 * Newtmgr error codes
 */
#define MGMT_ERR_EOK        (0)
#define MGMT_ERR_EUNKNOWN   (1)
#define MGMT_ERR_ENOMEM     (2)
#define MGMT_ERR_EINVAL     (3)
#define MGMT_ERR_ETIMEOUT   (4)
#define MGMT_ERR_ENOENT     (5)
#define MGMT_ERR_EBADSTATE  (6)     /* Current state disallows command. */
#define MGMT_ERR_EMSGSIZE   (7)     /* Response too large. */
#define MGMT_ERR_EPERUSER   (256)

#define NMGR_HDR_SIZE           (8)

struct nmgr_hdr {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t  nh_op:3;           /* NMGR_OP_XXX */
    uint8_t  _res1:5;
#endif
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    uint8_t  _res1:5;
    uint8_t  nh_op:3;           /* NMGR_OP_XXX */
#endif
    uint8_t  nh_flags;          /* reserved for future flags */
    uint16_t nh_len;            /* length of the payload */
    uint16_t nh_group;          /* NMGR_GROUP_XXX */
    uint8_t  nh_seq;            /* sequence number */
    uint8_t  nh_id;             /* message ID within group */
};

typedef void *mgmt_alloc_rsp_fn(const void *req, void *arg);
typedef int mgmt_trim_front_fn(void *buf, int len, void *arg);
typedef void mgmt_reset_buf_fn(void *buf, void *arg);
typedef int mgmt_write_at_fn(struct cbor_encoder_writer *writer, int offset,
                             const void *data, int len, void *arg);
typedef int mgmt_init_reader_fn(struct cbor_decoder_reader *reader, void *buf,
                                void *arg);
typedef int mgmt_init_writer_fn(struct cbor_encoder_writer *writer, void *buf,
                                void *arg);
typedef void mgmt_free_buf_fn(void *buf, void *arg);

struct mgmt_streamer_cfg {
    mgmt_alloc_rsp_fn *alloc_rsp;
    mgmt_trim_front_fn *trim_front;
    mgmt_reset_buf_fn *reset_buf;
    mgmt_write_at_fn *write_at;
    mgmt_init_reader_fn *init_reader;
    mgmt_init_writer_fn *init_writer;
    mgmt_free_buf_fn *free_buf;
};

/** Decodes requests and encodes responses. */
struct mgmt_streamer {
    const struct mgmt_streamer_cfg *cfg;
    void *cb_arg;
    struct cbor_decoder_reader *reader;
    struct cbor_encoder_writer *writer;
};

/**
 * Context required by command handlers for parsing requests and writing
 * responses.
 */
struct mgmt_cbuf {
    struct CborEncoder encoder;
    struct CborParser parser;
    struct CborValue it;
};

typedef int mgmt_handler_fn(struct mgmt_cbuf *cbuf);

/** Read and write handlers for a single command ID. */
struct mgmt_handler {
    mgmt_handler_fn *mh_read;
    mgmt_handler_fn *mh_write;
};

/** A collection of handlers for every command in a single group. */
struct mgmt_group {
    const struct mgmt_handler *mg_handlers;
    uint16_t mg_handlers_count;
    uint16_t mg_group_id;
    struct mgmt_group *mg_next;
};

#define MGMT_GROUP_SET_HANDLERS(group__, handlers__) do {   \
    (group__)->mg_handlers = (handlers__);                  \
    (group__)->mg_handlers_count =                          \
        sizeof (handlers__) / sizeof (handlers__)[0];       \
} while (0)

void *mgmt_streamer_alloc_rsp(struct mgmt_streamer *streamer, const void *req);
int mgmt_streamer_trim_front(struct mgmt_streamer *streamer, void *buf,
                             int len);
void mgmt_streamer_reset_buf(struct mgmt_streamer *streamer, void *buf);
int mgmt_streamer_write_at(struct mgmt_streamer *streamer, int offset,
                           const void *data, int len);
int mgmt_streamer_init_reader(struct mgmt_streamer *streamer, void *buf);
int mgmt_streamer_init_writer(struct mgmt_streamer *streamer, void *buf);
void mgmt_streamer_free_buf(struct mgmt_streamer *streamer, void *buf);

int mgmt_group_register(struct mgmt_group *group);
int mgmt_cbuf_setoerr(struct mgmt_cbuf *cbuf, int errcode);
const struct mgmt_handler *mgmt_find_handler(uint16_t group_id,
                                             uint16_t command_id);
int mgmt_err_from_cbor(int cbor_status);
int mgmt_cbuf_init(struct mgmt_cbuf *cbuf, struct mgmt_streamer *streamer);

#ifdef __cplusplus
}
#endif

#endif /* MGMT_MGMT_H_ */
