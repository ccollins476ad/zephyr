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

#include <string.h>

#include "fcb.h"
#include "logging/mdlog.h"

/** Tracks the number of entries in an FCB-backed log. */
struct mdlog_fcb {
	struct fcb fcb;
	u8_t entry_count;
};

static int
mdlog_fcb_append_rotate(struct mdlog *mdlog, int len,
                        struct fcb_entry *out_entry)
{
	struct mdlog_fcb *mdlog_fcb;
	struct fcb *fcb;
	int rc;

	mdlog_fcb = mdlog->l_arg;
	fcb = &mdlog_fcb->fcb;

	rc = fcb_append(fcb, len, out_entry);
	if (rc == 0) {
		return 0;
	}

	if (rc != FCB_ERR_NOSPACE) {
		return -EIO;
	}

	if (mdlog_fcb->entry_count == 0) {
		return -EMSGSIZE;
	}

	rc = fcb_rotate(fcb);
	if (rc != 0) {
		return -EIO;
	}

	return EAGAIN;
}

static int
mdlog_fcb_append(struct mdlog *mdlog, const void *buf, int len)
{
	struct mdlog_fcb *mdlog_fcb;
	struct fcb_entry entry;
	struct fcb *fcb;
	int rc;

	mdlog_fcb = mdlog->l_arg;
	fcb = &mdlog_fcb->fcb;

	do {
		rc = mdlog_fcb_append_rotate(mdlog, len, &entry);
	} while (rc == EAGAIN);
	if (rc != 0) {
		return rc;
	}
		
	rc = fcb_flash_write(fcb, entry.fe_sector, entry.fe_data_off, buf,
	                     len);
	if (rc != 0) {
		return -EIO;
	}

	rc = fcb_append_finish(fcb, &entry);
	if (rc != 0) {
		return -EIO;
	}

	return 0;
}

static int
mdlog_fcb_read(struct mdlog *mdlog, const void *descriptor, void *buf,
               u16_t offset, u16_t len)
{
	const struct fcb_entry *entry;
	struct mdlog_fcb *mdlog_fcb;
	struct fcb *fcb;
	int rc;

	mdlog_fcb = mdlog->l_arg;
	fcb = &mdlog_fcb->fcb;

	entry = descriptor;

	if (offset + len > entry->fe_data_len) {
		len = entry->fe_data_len - offset;
	}
	rc = fcb_flash_read(fcb, entry->fe_sector, entry->fe_data_off + offset,
	                    buf, len);
	if (rc != 0) {
		return -EIO;
	}

	return len;
}

static int
mdlog_fcb_walk(struct mdlog *mdlog, mdlog_walk_fn *walk_func,
               struct mdlog_offset *offset)
{
	struct fcb_entry entry;
	struct mdlog_fcb *fm;
	struct fcb *fcb;
	int rc;

	fm = mdlog->l_arg;
	fcb = &fm->fcb;

	memset(&entry, 0, sizeof entry);

	/* If timestamp for request < 0, only operate on the last log entry. */
	if (offset->lo_ts < 0) {
		entry = fcb->f_active;
		return walk_func(mdlog, offset, &entry, entry.fe_data_len);
	}

	/* Otherwise, apply the callback to all entries. */
	while (fcb_getnext(fcb, &entry) == 0) {
		rc = walk_func(mdlog, offset, &entry, entry.fe_data_len);
		if (rc != 0) {
			return rc;
		}
	}
	return 0;
}

static int
mdlog_fcb_flush(struct mdlog *mdlog)
{
	struct mdlog_fcb *mdlog_fcb;
	struct fcb *fcb;

	mdlog_fcb = mdlog->l_arg;
	fcb = &mdlog_fcb->fcb;

	return fcb_clear(fcb);
}

const struct mdlog_handler mdlog_fcb_handler = {
	.type = MDLOG_TYPE_STORAGE,
	.read = mdlog_fcb_read,
	.append = mdlog_fcb_append,
	.walk = mdlog_fcb_walk,
	.flush = mdlog_fcb_flush,
};
