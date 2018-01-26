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

#include <zephyr.h>
#include "logging/mdlog.h"

static int
mdlog_console_append(struct mdlog *mdlog, const void *buf, int len)
{
	const struct mdlog_entry_hdr *hdr;
	char *text;

	hdr = buf;
	text = (char *)(hdr + 1);

	printk("[ts=%luusb, mod=%u level=%u] %s\n",
	       (unsigned long) hdr->ue_ts, hdr->ue_module, hdr->ue_level, text);

	return 0;
}

static int
mdlog_console_read(struct mdlog *mdlog, const void *src, void *buf,
		   u16_t offset, u16_t len)
{
	/* You don't read console, console read you */
	return -EINVAL;
}

static int
mdlog_console_walk(struct mdlog *mdlog, mdlog_walk_fn *walk_func,
		   struct mdlog_offset *mdlog_offset)
{
	/* You don't walk console, console walk you. */
	return -EINVAL;
}

static int
mdlog_console_flush(struct mdlog *mdlog)
{
	/* You don't flush console, console flush you. */
	return -EINVAL;
}

const struct mdlog_handler mdlog_console_handler = {
	.type = MDLOG_TYPE_STREAM,
	.read = mdlog_console_read,
	.append = mdlog_console_append,
	.walk = mdlog_console_walk,
	.flush = mdlog_console_flush,
};
