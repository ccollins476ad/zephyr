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
#include <stdio.h>
#include <stdarg.h>

#include <zephyr.h>
#include "logging/mdlog.h"

static struct mdlog *mdlog_list;
static u32_t mdlog_next_index;
static uint8_t mdlog_written;

struct mdlog_int_str_pair {
	u8_t id;
	const char *name;
};

static const struct mdlog_int_str_pair mdlog_modules[] = {
	{ MDLOG_MODULE_DEFAULT,         "DEFAULT" },
	{ MDLOG_MODULE_OS,              "OS" },
	{ MDLOG_MODULE_MCUMGR,          "MCUMGR" },
	{ MDLOG_MODULE_BLUETOOTH_CTLR,  "BLUETOOTH_CTLR" },
	{ MDLOG_MODULE_BLUETOOTH_HOST,  "BLUETOOTH_HOST" },
	{ MDLOG_MODULE_FILESYSTEM,      "FILESYSTEM" },
	{ MDLOG_MODULE_REBOOT,          "REBOOT" },
	{ MDLOG_MODULE_TEST,            "TEST" },
};

static const struct mdlog_int_str_pair mdlog_levels[] = {
	{ MDLOG_LEVEL_DEBUG,            "DEBUG" },
	{ MDLOG_LEVEL_INFO,             "INFO" },
	{ MDLOG_LEVEL_WARN,             "WARN" },
	{ MDLOG_LEVEL_ERROR,            "ERROR" },
	{ MDLOG_LEVEL_CRITICAL,         "CRITICAL" },
};

static const char *
mdlog_str_find(const struct mdlog_int_str_pair *pairs, int num_pairs, u8_t id)
{
	int i;

	for (i = 0; i < num_pairs; i++) {
		if (pairs[i].id == id) {
			return pairs[i].name;
		}
	}

	return NULL;
}

const char *
mdlog_module_name(u8_t module_id)
{
	return mdlog_str_find(mdlog_modules,
	                      sizeof mdlog_modules / sizeof mdlog_modules[0],
	                      module_id);
}

const char *
mdlog_level_name(u8_t level_id)
{
	return mdlog_str_find(mdlog_levels,
	                      sizeof mdlog_levels / sizeof mdlog_levels[0],
	                      level_id);
}

struct mdlog *
mdlog_get_next(struct mdlog *mdlog)
{
	if (mdlog == NULL) {
		return mdlog_list;
	} else {
		return mdlog->l_next;
	}
}

struct mdlog *
mdlog_find(const char *log_name)
{
	struct mdlog *mdlog;

	mdlog = NULL;
	while (1) {
		mdlog = mdlog_get_next(mdlog);
		if (mdlog == NULL) {
			return NULL;
		}

		if (strcmp(mdlog->l_name, log_name) == 0) {
			return mdlog;
		}
	}
}

struct mdlog_read_hdr_arg {
	struct mdlog_entry_hdr *hdr;
	int read_success;
};

static int
mdlog_read_hdr_walk(struct mdlog *mdlog, struct mdlog_offset *mdlog_offset,
                    const void *src, uint16_t len)
{
	struct mdlog_read_hdr_arg *arg;
	int num_bytes;

	arg = mdlog_offset->lo_arg;

	num_bytes = mdlog_read(mdlog, src, arg->hdr, 0, sizeof *arg->hdr);
	if (num_bytes >= sizeof *arg->hdr) {
		arg->read_success = 1;
	}

	/* Abort the walk; only one header needed. */
	return 1;
}

/**
 * Reads the final mdlog entry's header from the specified mdlog.
 *
 * @param mdlog                 The mdlog to read from.
 * @param out_hdr               On success, the last entry header gets written
 *                                  here.
 *
 * @return                      0 on success; nonzero on failure.
 */
static int
mdlog_read_last_hdr(struct mdlog *mdlog, struct mdlog_entry_hdr *out_hdr)
{
	struct mdlog_read_hdr_arg arg;
	struct mdlog_offset mdlog_offset;

	arg.hdr = out_hdr;
	arg.read_success = 0;

	mdlog_offset.lo_arg = &arg;
	mdlog_offset.lo_ts = -1;
	mdlog_offset.lo_index = 0;
	mdlog_offset.lo_data_len = 0;

	mdlog_walk(mdlog, mdlog_read_hdr_walk, &mdlog_offset);
	if (!arg.read_success) {
		return -1;
	}

	return 0;
}

/**
 * Associate an instantiation of a mdlog with the logging infrastructure
 */
int
mdlog_register(const char *name, struct mdlog *mdlog,
               const struct mdlog_handler *lh, void *arg, uint8_t level)
{
	struct mdlog_entry_hdr hdr;
	unsigned int key;
	int rc;

	/* All log registration must be completed before any log messages get
	 * written.  This is because the next entry index is calculated during
	 * registration, so a premature write could create an entry with a
	 * non-unique index.
	 */
	assert(!mdlog_written);
	if (mdlog_find(name) != NULL) {
		return -EALREADY;
	}

	mdlog->l_name = name;
	mdlog->l_handler = lh;
	mdlog->l_arg = arg;
	mdlog->l_level = level;
	mdlog->l_next = mdlog_list;
	mdlog_list = mdlog;

	/* If this is a persisted mdlog, read the index from its most recent
	 * entry.  We need to ensure the index of all subseqently written
	 * entries is monotonically increasing.
	 */
	if (mdlog->l_handler->type == MDLOG_TYPE_STORAGE) {
		rc = mdlog_read_last_hdr(mdlog, &hdr);
		if (rc == 0) {
			key = irq_lock();
			if (hdr.ue_index >= mdlog_next_index) {
				mdlog_next_index = hdr.ue_index + 1;
			}
			irq_unlock(key);
		}
	}

	return 0;
}

int
mdlog_append(struct mdlog *mdlog, uint16_t module, uint16_t level,
             void *data, uint16_t len)
{
	struct mdlog_entry_hdr *ue;
	unsigned int key;
	uint32_t idx;
	int rc;

	if (mdlog->l_name == NULL || mdlog->l_handler == NULL) {
		return -1;
	}

	if (mdlog->l_handler->type == MDLOG_TYPE_STORAGE) {
		/* Remember that a log entry has been persisted since boot. */
		mdlog_written = 1;
	}

	/*
	 * If the mdlog message is below what this mdlog instance is
	 * configured to accept, then just drop it.
	 */
	if (level < mdlog->l_level) {
		return -1;
	}

	ue = data;

	key = irq_lock();
	idx = mdlog_next_index++;
	irq_unlock(key);

	/* XXX: No real time; just use uptime for now. */
	ue->ue_ts = k_uptime_get() * 1000;

	ue->ue_level = level;
	ue->ue_module = module;
	ue->ue_index = idx;

	rc = mdlog->l_handler->append(mdlog, data, len + MDLOG_ENTRY_HDR_SIZE);
	if (rc != 0) {
		return rc;
	}

	return 0;
}

static void
mdlog_vprintf(struct mdlog *mdlog, uint16_t module, uint16_t level,
              const char * restrict msg, va_list ap)
{
	char buf[MDLOG_ENTRY_HDR_SIZE + CONFIG_MDLOG_PRINTF_MAX_ENTRY_LEN];
	int len;

	len = vsnprintf(&buf[MDLOG_ENTRY_HDR_SIZE],
	                CONFIG_MDLOG_PRINTF_MAX_ENTRY_LEN, msg, ap);
	if (len >= CONFIG_MDLOG_PRINTF_MAX_ENTRY_LEN) {
		len = CONFIG_MDLOG_PRINTF_MAX_ENTRY_LEN - 1;
	}

	mdlog_append(mdlog, module, level, buf, len);
}

void
mdlog_printf(struct mdlog *mdlog, uint16_t module, uint16_t level,
             const char * restrict msg, ...)
{
	va_list ap;

	va_start(ap, msg);
	mdlog_vprintf(mdlog, module, level, msg, ap);
	va_end(ap);
}

int
mdlog_walk(struct mdlog *mdlog, mdlog_walk_fn *walk_func,
           struct mdlog_offset *mdlog_offset)
{
	return mdlog->l_handler->walk(mdlog, walk_func, mdlog_offset);
}

/**
 * Reads from the specified mdlog.
 *
 * @return                      The number of bytes read; 0 on failure.
 */
int
mdlog_read(struct mdlog *mdlog, const void *descriptor, void *buf,
           uint16_t off, uint16_t len)
{
	return mdlog->l_handler->read(mdlog, descriptor, buf, off, len);
}

int
mdlog_flush(struct mdlog *mdlog)
{
	return mdlog->l_handler->flush(mdlog);
}

u32_t
mdlog_get_next_index(void)
{
	return mdlog_next_index;
}
