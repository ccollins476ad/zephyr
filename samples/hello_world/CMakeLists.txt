# Top-level CMakeLists.txt for the skeleton application.
#
# Copyright (c) 2017 Open Source Foundries Limited
#
# SPDX-License-Identifier: Apache-2.0
#
# This provides a basic application structure suitable for loading by
# mcuboot, which is easy to customize on a per-board basis. It can be
# used as a starting point for new applications.

# The board should be set to a supported target. The default is
# frdm_k64f.
#if (NOT DEFINED BOARD)
  #set(BOARD frdm_k64f)
#endif()

# The default top-level application configuration is prj.conf.
# You can place additional board-specific files in boards/${BOARD}.conf,
# and they will be merged into the configuration along with prj.conf.
macro(set_conf_file)
  if(EXISTS ${APPLICATION_SOURCE_DIR}/boards/${BOARD}.conf)
    set(CONF_FILE "prj.conf ${APPLICATION_SOURCE_DIR}/boards/${BOARD}.conf")
  else()
    set(CONF_FILE "prj.conf")
  endif()
endmacro()

# Zephyr uses Device Tree (DT) to describe some board hardware
# configuration.
#
# For this simple example, all we need to tell DT is where on the chip
# flash to link this application image so mcuboot can find it. We do
# this with a device tree overlay file.
#
# See the Zephyr documentation for more information on DT:
# http://docs.zephyrproject.org/devices/dts/device_tree.html
set(DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/dts.overlay")

# Standard Zephyr application boilerplate.
include($ENV{ZEPHYR_BASE}/cmake/app/boilerplate.cmake NO_POLICY_SCOPE)
project(NONE)

assert_exists(DTC_OVERLAY_FILE)

# This string ends up getting printed in the device console
if (NOT DEFINED FROM_WHO)
  set(FROM_WHO Zephyr)
endif()

target_sources(app PRIVATE
    src/cborattr/src/cborattr.c
    src/main.c
    src/mcumgr/img/port/zephyr/src/zephyr_img.c
    src/mcumgr/img/src/img.c
    src/mcumgr/img/src/img_state.c
    src/mcumgr/img/src/img_util.c
    src/mcumgr/mgmt/src/mgmt.c
    src/mcumgr/newtmgr/port/zephyr/src/zephyr_nmgr.c
    src/mcumgr/newtmgr/src/newtmgr.c
    src/mgmt_os/src/mgmt_os.c
    src/mgmt_os/src/mgmt_os.c
    src/tinycbor/src/cbor_buf_reader.c
    src/tinycbor/src/cbor_buf_writer.c
    src/tinycbor/src/cborencoder.c
    src/tinycbor/src/cborerrorstrings.c
    src/tinycbor/src/cborparser.c
    src/tinycbor/src/cborparser_dup_string.c
    #src/tinycbor/src/cborpretty.c
    #src/tinycbor/src/cbortojson.c
    #src/tinycbor/src/cborvalidation.c
    src/znp/src/znp.c

)

target_include_directories(app PRIVATE
    src/mgmt_os/include
    src/cborattr/include
    src/mcumgr/img/include
    src/mcumgr/mgmt/include
    src/mcumgr/newtmgr/include
    src/mcumgr/newtmgr/port/zephyr/include
    src/tinycbor/src
    src/znp/include
    src/mcuboot/boot/bootutil/include
    src/mcuboot/boot/bootutil/src
    src/mcuboot/boot/zephyr/include
)
