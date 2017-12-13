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

#ifndef H_MYNEWT_NEWTMGR_
#define H_MYNEWT_NEWTMGR_

#include <inttypes.h>
#include "mgmt/mgmt.h"
#include "os/os_mbuf.h"
struct mynewt_nmgr_transport;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Transmit function.  The supplied mbuf is always consumed, regardless of
 * return code.
 */
typedef int mynewt_nmgr_transport_out_fn(struct mynewt_nmgr_transport *mnt,
                                         struct os_mbuf *m);

/**
 * MTU query function.  The supplied mbuf should contain a request received
 * from the peer whose MTU is being queried.  This function takes an mbuf
 * parameter because some transports store connection-specific information in
 * the mbuf user header (e.g., the BLE transport stores the connection handle).
 *
 * @return                      The transport's MTU;
 *                              0 if transmission is currently not possible.
 */
typedef uint16_t mynewt_nmgr_transport_get_mtu_fn(struct os_mbuf *m);

struct mynewt_nmgr_transport {
    struct os_mqueue mnt_imq;
    mynewt_nmgr_transport_out_fn *mnt_output;
    mynewt_nmgr_transport_get_mtu_fn *mnt_get_mtu;
};

int mynewt_nmgr_transport_init(struct mynewt_nmgr_transport *mnt,
                               mynewt_nmgr_transport_out_fn *output_func,
                               mynewt_nmgr_transport_get_mtu_fn *get_mtu_func);

int mynewt_nmgr_rx_req(struct mynewt_nmgr_transport *mnt, struct os_mbuf *req);

#ifdef __cplusplus
}
#endif

#endif /* H_MYNEWT_NETMGR_ */
