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
 * @brief mdlog - Managed log.
 *
 * mdlog is a generic logging mechanism.  The particular medium that backs an
 * mdlog instance is specified at creation time.  In addition, mdlogs are
 * accessible via mcumgr.
 */

#ifndef H_MDLOG_
#define H_MDLOG_

#include <zephyr/types.h>
#include <ignore.h>
struct mdlog;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Current log format version.  Indicates the medium-specific entry
 * format.
 */
#define MDLOG_VERSION                   2

/* Predefined log level IDs. */
#define MDLOG_LEVEL_DEBUG               0
#define MDLOG_LEVEL_INFO                1
#define MDLOG_LEVEL_WARN                2
#define MDLOG_LEVEL_ERROR               3
#define MDLOG_LEVEL_CRITICAL            4
#define MDLOG_LEVEL_MAX                 UINT8_MAX

/* Predefined log module IDs. */
#define MDLOG_MODULE_DEFAULT            0
#define MDLOG_MODULE_OS                 1
#define MDLOG_MODULE_MCUMGR             2
#define MDLOG_MODULE_BLUETOOTH_CTLR     3
#define MDLOG_MODULE_BLUETOOTH_HOST     4
#define MDLOG_MODULE_FILESYSTEM         5
#define MDLOG_MODULE_REBOOT             6
#define MDLOG_MODULE_TEST               7
#define MDLOG_MODULE_PERUSER            64
#define MDLOG_MODULE_MAX                255

/* Logging medium */
#define MDLOG_TYPE_STREAM               0
#define MDLOG_TYPE_MEMORY               1
#define MDLOG_TYPE_STORAGE              2

/** @brief Used for walks; indicates part of mdlog to access. */
struct mdlog_offset {
	/* If   lo_ts == -1: Only access last mdlog entry;
	 * Elif lo_ts == 0:  Don't filter by timestamp;
	 * Else:             Only access entries whose ts >= lo_ts.
	 */
	s64_t lo_ts;

	/* Only access entries whose index >= lo_index. */
	u32_t lo_index;

	/* On read, lo_data_len gets populated with the number of bytes read. */
	u32_t lo_data_len;

	/* Specific to walk / read function. */
	void *lo_arg;
};

/** @typedef mdlog_walk_fn
 * @brief Function that gets applied to every entry during a log walk.
 *
 * @param mdlog                 The log being walked.
 * @param mdlog_offset          Indicates which entries to process.
 * @param descriptor            Medium-specific descriptor for the entry being
 *                                  processed.
 * @param len                   The size, in bytes, of the log entry.
 *
 * @return                      0 if the walk should proceed;
 *                              nonzero to abort the walk.
 */
typedef int mdlog_walk_fn(struct mdlog *mdlog,
			  struct mdlog_offset *mdlog_offset,
			  const void *descriptor, u16_t len);

/** @typedef lh_read_fn
 * @brief Read handler for a specific log medium.
 *
 * @param mdlog                 The log to read from.
 * @param descriptor            Medium-specific descriptor for the entry to
 *                                  read.
 * @param buf                   The buffer to read into.
 * @param offset                The offset from the entry base to read from.
 * @param len                   The number of bytes to read.
 *
 * @return                      The number of bytes read on success;
 *                              Negative error code on failure.
 */
typedef int lh_read_fn(struct mdlog *mdlog, const void *descriptor, void *buf,
		       u16_t offset, u16_t len);

/** @typedef lh_append_fn
 * @brief Append handler for a specific log medium.
 *
 * Appends a new entry to the specified log.
 *
 * @param mdlog                 The log to append to.
 * @param buf                   The data to append.
 * @param len                   The number of bytes to append.
 *
 * @return                      0 on success; negative error code on failure.
 */
typedef int lh_append_fn(struct mdlog *mdlog, const void *buf, int len);


/** @typedef lh_append_fn
 * @brief Walk handler for a specific log medium.
 *
 * Applies the specified function to every entry in a log.
 *
 * @param mdlog                 The log to walk.
 * @param walk_cb               The function to apply to each log entry.
 * @param mdlog_offset          Indicates which entries to process.
 *
 * @return                      0 if the walk completed successfully;
 *                              nonzero if the walk was aborted.
 */
typedef int lh_walk_fn(struct mdlog *mdlog,
		       mdlog_walk_fn walk_cb,
		       struct mdlog_offset *mdlog_offset);

/** @typedef lh_flush_fn
 * @brief Flush handler for a specific log medium.
 *
 * Clears the specified log.
 *
 * @param mdlog                 The log to clear.
 *
 * @return                      0 on success; negative error code on failure.
 */
typedef int lh_flush_fn(struct mdlog *mdlog);

/**
 * @brief Fills in the generic functionality for a particular log medium.
 */
struct mdlog_handler {
	u8_t type;
	lh_read_fn *read;
	lh_append_fn *append;
	lh_walk_fn *walk;
	lh_flush_fn *flush;
};

/**
 * @brief Header that accompanies every log entry.
 */
struct mdlog_entry_hdr {
	s64_t ue_ts;
	u32_t ue_index;
	u8_t ue_module;
	u8_t ue_level;
} __attribute__((__packed__));

/**
 * @brief A generic logger with a medium-specific handler.
 */
struct mdlog {
	const char *l_name;
	const struct mdlog_handler *l_handler;
	void *l_arg;
	struct mdlog *l_next;
	u8_t l_level;
};

#define MDLOG_ENTRY_HDR_SIZE            (sizeof(struct mdlog_entry_hdr))

/* Logging is disabled by default. */
#ifndef CONFIG_MDLOG_LEVEL
#define CONFIG_MDLOG_LEVEL  MDLOG_LEVEL_MAX
#endif

#if CONFIG_MDLOG_LEVEL <= MDLOG_LEVEL_DEBUG
#define MDLOG_DEBUG(log__, mod__, msg__, ...) mdlog_printf(log__, mod__, \
		    MDLOG_LEVEL_DEBUG, msg__, ##__VA_ARGS__)
#else
#define MDLOG_DEBUG(log__, mod__, ...) IGNORE(__VA_ARGS__)
#endif

#if CONFIG_MDLOG_LEVEL <= MDLOG_LEVEL_INFO
#define MDLOG_INFO(log__, mod__, msg__, ...) mdlog_printf(log__, mod__,	\
		   MDLOG_LEVEL_INFO, msg__, ##__VA_ARGS__)
#else
#define MDLOG_INFO(log__, mod__, ...) IGNORE(__VA_ARGS__)
#endif

#if CONFIG_MDLOG_LEVEL <= MDLOG_LEVEL_WARN
#define MDLOG_WARN(log__, mod__, msg__, ...) mdlog_printf(log__, mod__,	\
		   MDLOG_LEVEL_WARN, msg__, ##__VA_ARGS__)
#else
#define MDLOG_WARN(log__, mod__, ...) IGNORE(__VA_ARGS__)
#endif

#if CONFIG_MDLOG_LEVEL <= MDLOG_LEVEL_ERROR
#define MDLOG_ERROR(log__, mod__, msg__, ...) mdlog_printf(log__, mod__, \
		    MDLOG_LEVEL_ERROR, msg__, ##__VA_ARGS__)
#else
#define MDLOG_ERROR(log__, mod__, ...) IGNORE(__VA_ARGS__)
#endif

#if CONFIG_MDLOG_LEVEL <= MDLOG_LEVEL_CRITICAL
#define MDLOG_CRITICAL(log__, mod__, msg__, ...) mdlog_printf(log__, mod__, \
		       MDLOG_LEVEL_CRITICAL, msg__, ##__VA_ARGS__)
#else
#define MDLOG_CRITICAL(log__, mod__, ...) IGNORE(__VA_ARGS__)
#endif

/**
 * @brief Retrieves the name of the specified module ID.
 *
 * @param module_id             The ID of the module to look up.
 *
 * @return                      The name of the specified module on success;
 *                              NULL if no match was found.
 */
const char *mdlog_module_name(u8_t module_id);

/**
 * @brief Retrieves the name of the specified level ID.
 *
 * @param level_id              The ID of the level to look up.
 *
 * @return                      The name of the specified level on success;
 *                              NULL if no match was found.
 */
const char *mdlog_level_name(u8_t level_id);

/**
 * @brief Retrieves the next registered log.
 *
 * @param cur                   The log whose sucessor is being retrieved, or
 *                                  NULL to retrieve the first log.
 *
 * @return                      Pointer to the retrieved log on success;
 *                              NULL if no more logs remain.
 */
struct mdlog *mdlog_get_next(struct mdlog *cur);

/**
 * @brief Retrieves the log with the specified name.
 *
 * @param name                  The name of the log to look up.
 *
 * @return                      Pointer to the retrieved log on success;
 *                              NULL if there is no matching registered log.
 */
struct mdlog *mdlog_find(const char *name);

/**
 * @brief Registers a log.
 *
 * @param name                  The name of the log to register.
 *                                  This name must be unique among all
 *                                  logs.  If the name is a
 *                                  duplicate, this function will return
 *                                  -EALREADY.
 * @param mdlog                 The log to register.
 * @param lh                    The set of handlers implementing the log
 *                                  medium.
 * @param arg                   Optional argument to pass to the callback.
 * @param level                 The minimum level for messages written to the
 *                                  log.  Only entries with a level >= this
 *                                  argument value get logged.
 *
 * @return                      0 on success; negative error code on failure.
 */
int mdlog_register(const char *name, struct mdlog *mdlog,
		   const struct mdlog_handler *lh, void *arg, u8_t level);

/**
 * @brief Appends a new entry to an mdlog.
 *
 * @param mdlog                 The mdlog to append to.
 * @param module                The module of the entry to append.
 * @param level                 The log level of the entry to append.
 * @param data                  The body of the new log entry.
 * @param len                   The size of `data`, in bytes.
 *
 * @return                      0 on success; negative error code on failure.
 */
int mdlog_append(struct mdlog *mdlog, u16_t module, u16_t level,
		 void *data, u16_t len);

/**
 * @brief Appends a formatted entry to an mdlog.
 *
 * @param mdlog                 The mdlog to append to.
 * @param module                The module of the entry to append.
 * @param level                 The log level of the entry to append.
 * @param msg                   A printf-style format string specifying the
 *                                  entry body.
 *
 * @return                      0 on success; negative error code on failure.
 */
void mdlog_printf(struct mdlog *mdlog, uint16_t module, uint16_t level,
		  const char *restrict msg, ...);

/**
 * @brief Reads an entry from an mdlog.
 *
 * @param mdlog                 The mdlog to read from.
 * @param descriptor            Medium-specific descriptor for the entry to
 * @param buf                   The buffer to read into.
 * @param off                   The offset from the entry base to read from.
 * @param len                   The number of bytes to read.
 *
 * @return                      The number of bytes read on success;
 *                              Negative error code on failure.
 */
int mdlog_read(struct mdlog *mdlog, const void *descriptor, void *buf,
	       u16_t off, u16_t len);

/*
 * @brief Applies a function to every entry in a log.
 *
 * @param mdlog                 The log to apply a function to.
 * @param walk_func             The function to apply to each log entry.
 * @param mdlog_offset          Indicates which entries to process.
 *
 * @return                      0 if the walk completed;
 *                              nonzero if the walk was aborted.
 */
int mdlog_walk(struct mdlog *mdlog, mdlog_walk_fn *walk_func,
	       struct mdlog_offset *mdlog_offset);

/**
 * Clears an mdlog.
 *
 * @param mdlog                 The log to clear.
 *
 * @return                      0 on success; negative error code on failure.
 */
int mdlog_flush(struct mdlog *mdlog);

/**
 * @brief Retrieves the index that the next appended log entry will use.
 *
 * @return                      The next entry index.
 */
u32_t mdlog_get_next_index(void);

/* Handler exports */
#ifdef CONFIG_MDLOG_CONSOLE
extern const struct mdlog_handler mdlog_console_handler;
#endif
#ifdef CONFIG_MDLOG_FCB
extern const struct mdlog_handler mdlog_fcb_handler;
#endif

#ifdef __cplusplus
}
#endif

#endif /* H_MDLOG_ */
