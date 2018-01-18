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
#include "net/buf.h"
#include "mgmt/buf.h"
#include "compilersupport_p.h"

NET_BUF_POOL_DEFINE(pkt_pool, CONFIG_MCUMGR_BUF_COUNT, CONFIG_MCUMGR_BUF_SIZE,
		    CONFIG_MCUMGR_BUF_USER_DATA_SIZE, NULL);

struct net_buf *
mcumgr_buf_alloc(void)
{
	return net_buf_alloc(&pkt_pool, K_NO_WAIT);
}

void
mcumgr_buf_free(struct net_buf *nb)
{
	net_buf_unref(nb);
}

static u8_t
cbor_nb_reader_get8(struct cbor_decoder_reader *d, int offset)
{
	struct cbor_nb_reader *cnr;

	cnr = (struct cbor_nb_reader *) d;

	if (offset < 0 || offset >= cnr->nb->len) {
		return UINT8_MAX;
	}

	return cnr->nb->data[offset];
}

static u16_t
cbor_nb_reader_get16(struct cbor_decoder_reader *d, int offset)
{
	struct cbor_nb_reader *cnr;
	u16_t val;

	cnr = (struct cbor_nb_reader *) d;

	if (offset < 0 || offset > cnr->nb->len - (int)sizeof(val)) {
		return UINT16_MAX;
	}

	memcpy(&val, cnr->nb->data + offset, sizeof(val));
	return cbor_ntohs(val);
}

static u32_t
cbor_nb_reader_get32(struct cbor_decoder_reader *d, int offset)
{
	struct cbor_nb_reader *cnr;
	u32_t val;

	cnr = (struct cbor_nb_reader *) d;

	if (offset < 0 || offset > cnr->nb->len - (int)sizeof(val)) {
		return UINT32_MAX;
	}

	memcpy(&val, cnr->nb->data + offset, sizeof(val));
	return cbor_ntohl(val);
}

static u64_t
cbor_nb_reader_get64(struct cbor_decoder_reader *d, int offset)
{
	struct cbor_nb_reader *cnr;
	u64_t val;

	cnr = (struct cbor_nb_reader *) d;

	if (offset < 0 || offset > cnr->nb->len - (int)sizeof(val)) {
		return UINT64_MAX;
	}

	memcpy(&val, cnr->nb->data + offset, sizeof(val));
	return cbor_ntohll(val);
}

static uintptr_t
cbor_nb_reader_cmp(struct cbor_decoder_reader *d, char *buf, int offset,
		   size_t len)
{
	struct cbor_nb_reader *cnr;

	cnr = (struct cbor_nb_reader *) d;

	if (offset < 0 || offset > cnr->nb->len - (int)len) {
		return -1;
	}

	return memcmp(cnr->nb->data + offset, buf, len);
}

static uintptr_t
cbor_nb_reader_cpy(struct cbor_decoder_reader *d, char *dst, int offset,
		   size_t len)
{
	struct cbor_nb_reader *cnr;

	cnr = (struct cbor_nb_reader *) d;

	if (offset < 0 || offset > cnr->nb->len - (int)len) {
		return -1;
	}

	return (uintptr_t)memcpy(dst, cnr->nb->data + offset, len);
}

static uintptr_t
cbor_nb_get_string_chunk(struct cbor_decoder_reader *d, int offset,
			 size_t *len)
{
	struct cbor_nb_reader *cnr;

	cnr = (struct cbor_nb_reader *) d;
	return (uintptr_t)cnr->nb->data + offset;
}

void
cbor_nb_reader_init(struct cbor_nb_reader *cnr,
		    struct net_buf *nb)
{
	cnr->r.get8 = &cbor_nb_reader_get8;
	cnr->r.get16 = &cbor_nb_reader_get16;
	cnr->r.get32 = &cbor_nb_reader_get32;
	cnr->r.get64 = &cbor_nb_reader_get64;
	cnr->r.cmp = &cbor_nb_reader_cmp;
	cnr->r.cpy = &cbor_nb_reader_cpy;
	cnr->r.get_string_chunk = &cbor_nb_get_string_chunk;

	cnr->nb = nb;
	cnr->r.message_size = nb->len;
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
