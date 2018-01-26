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

#include <stdbool.h>
#include <stdio.h>
#include <zephyr.h>
#include "dfu/mcuboot.h"
#include "logging/mdlog.h"

static struct mdlog *reboot_log_mdlog;
static bool reboot_log_written;

void
reboot_log_configure(struct mdlog *mdlog)
{
	reboot_log_mdlog = mdlog;

	/* XXX: If a reboot entry was not written on the previous reboot, write
	 * an "unknown" entry now.
	 */
}

static const char *
reboot_log_ver_str(char *buf)
{
#ifdef CONFIG_MCUBOOT_IMG_MANAGER
	struct image_version ver;
	int rc;

	rc = boot_current_image_version(&ver);
	if (rc == 0) {
		sprintf(buf, "%u.%u.%u.%u",
		        ver.iv_major, ver.iv_minor, ver.iv_revision,
		        (unsigned int)ver.iv_build_num);
		return buf;
	}
#endif

	return "???";
}

int
reboot_log_write(const char *reason)
{
	char ver_str[BOOT_IMG_VER_STRLEN_MAX];

	if (reboot_log_mdlog == NULL) {
		return -EBADF;
	}

	if (reboot_log_written) {
		return -EALREADY;
	}
	reboot_log_written = true;

	/* XXX: No reboot counter. */

	MDLOG_CRITICAL(reboot_log_mdlog, MDLOG_MODULE_REBOOT, 
	               "rsn:%s cnt:0 img:%s", reason,
	               reboot_log_ver_str(ver_str));

	/* XXX: Persist the fact that a reboot entry has been written. */

	return 0;
}

int
reboot_log_write_fault(int fault_type, u32_t pc)
{
	char buf[32];

	snprintf(buf, sizeof buf, "fault,type=%d,$pc=0x%x", fault_type, pc);
	return reboot_log_write(buf);
}

int
reboot_log_write_assert(const char *file, int line)
{
	char buf[128];

	snprintf(buf, sizeof buf, "assert,%s:%d", file, line);
	return reboot_log_write(buf);
}
