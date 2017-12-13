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

#ifndef _NEWTMGR_H_
#define _NEWTMGR_H_

#include "mgmt/mgmt.h"

#ifdef __cplusplus
extern "C" {
#endif

struct mynewt_nmgr_transport;
struct nmgr_streamer;
struct nmgr_hdr;
typedef int nmgr_tx_rsp_fn(struct nmgr_streamer *ns, void *buf, void *arg);

struct nmgr_streamer {
    struct mgmt_streamer ns_base;
    nmgr_tx_rsp_fn *ns_tx_rsp;
};

void nmgr_ntoh_hdr(struct nmgr_hdr *hdr);
int nmgr_handle_single_payload(struct mgmt_cbuf *cbuf,
                               const struct nmgr_hdr *req_hdr);
int nmgr_process_single_packet(struct nmgr_streamer *streamer, void *req);

#ifdef __cplusplus
}
#endif

#endif /* _NETMGR_H */
