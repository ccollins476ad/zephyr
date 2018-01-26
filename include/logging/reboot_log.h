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

/**
 * @file
 * @brief Functionality for persisting reboot records.
 *
 * If CONFIG_REBOOT_LOG is not defined, reboot log write attempts compile to
 * no-ops.  Only one entry can be written to the log per reboot; subsequent
 * attempts fail with a return code of -EALREADY.
 */

#ifndef H_REBOOT_LOG_
#define H_REBOOT_LOG_

#include <zephyr/types.h>
struct mdlog;

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_REBOOT_LOG

/**
 * @brief Configures the specified log to be used for reboot entries.
 *
 * @param mdlog                 The log to use for reboot entries.
 */
void reboot_log_configure(struct mdlog *mdlog);

/**
 * @brief Writes a generic entry to the reboot log.
 *
 * @param reason                The "reason" string to include in the reboot
 *                                  entry.
 *
 * @return                      0 on success; negative error code on failure.
 */
int reboot_log_write(const char *reason);

/**
 * @brief Writes a fault entry to the reboot log.
 *
 * @param fault_type            A _NANO_ERR_[...] code describing the fault.
 * @param pc                    The value of the PC register at the time of the
 *                                  fault.
 *
 * @return                      0 on success; negative error code on failure.
 */
int reboot_log_write_fault(int fault_type, u32_t pc);

/**
 * @brief Writes a failed assertion entry to the reboot log.
 *
 * @param file                  The filename where the assertion failed.
 * @param line                  The line number where the assertion failed.
 *
 * @return                      0 on success; negative error code on failure.
 */
int reboot_log_write_assert(const char *file, int line);

#else

static inline int reboot_log_write(const char *reason)
{
	return 0;
}

static inline int reboot_log_write_fault(int fault_type, u32_t pc)
{
	return 0;
}

static inline int reboot_log_write_assert(const char *file, int line)
{
	return 0;
}

#endif

#ifdef __cplusplus
}
#endif

#endif
