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

#ifndef H_ZEPHYR_SMP_
#define H_ZEPHYR_SMP_

#ifdef __cplusplus
extern "C" {
#endif

struct zephyr_smp_transport;
struct net_buf;

/** @typedef zephyr_smp_transport_out_fn
 * @brief SMP transmit function for Zephyr.
 *
 * The supplied net_buf is always consumed, regardless of return code.
 *
 * @param mst                   The transport to send via.
 * @param nb                    The net_buf to transmit.
 *
 * @return                      0 on success, MGMT_ERR_[...] code on failure.
 */
typedef int zephyr_smp_transport_out_fn(struct zephyr_smp_transport *zst,
					struct net_buf *nb);

/** @typedef zephyr_smp_transport_get_mtu_fn
 * @brief SMP MTU query function for Zephyr.
 *
 * The supplied net_buf should contain a request received from the peer whose
 * MTU is being queried.  This function takes a net_buf parameter because some
 * transports store connection-specific information in the net_buf user header
 * (e.g., the BLE transport stores the peer address).
 *
 * @param nb                    Contains a request from the relevant peer.
 *
 * @return                      The transport's MTU;
 *                              0 if transmission is currently not possible.
 */
typedef uint16_t zephyr_smp_transport_get_mtu_fn(const struct net_buf *nb);

/**
 * @brief Provides Zephyr-specific functionality for sending SMP responses.
 */
struct zephyr_smp_transport {
	/* Must be the first member. */
	struct k_work zst_work;

	/* FIFO containing incoming requests to be processed. */
	struct k_fifo zst_fifo;

	zephyr_smp_transport_out_fn *zst_output;
	zephyr_smp_transport_get_mtu_fn *zst_get_mtu;
};

/**
 * @brief Initializes a Zephyr SMP transport object.
 *
 * @param mst                   The transport to construct.
 * @param output_func           The transport's send function.
 * @param get_mtu_func          The transport's get-MTU function.
 *
 * @return                      0 on success, MGMT_ERR_[...] code on failure.
 */
void zephyr_smp_transport_init(struct zephyr_smp_transport *zst,
			       zephyr_smp_transport_out_fn *output_func,
			       zephyr_smp_transport_get_mtu_fn *get_mtu_func);

/**
 * @brief Enqueues an incoming SMP request packet for processing.
 *
 * This function always consumes the supplied net_buf.
 *
 * @param mst                   The transport to use to send the corresponding
 *                                  response(s).
 * @param req                   The request packet to process.
 */
void zephyr_smp_rx_req(struct zephyr_smp_transport *zst, struct net_buf *nb);

#ifdef __cplusplus
}
#endif

#endif
