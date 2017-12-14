include($ENV{ZEPHYR_BASE}/cmake/app/boilerplate.cmake NO_POLICY_SCOPE)
project(NONE)

target_sources(app PRIVATE
    src/cborattr/src/cborattr.c
    src/main.c
    src/mcumgr/mgmt/src/mgmt.c
    src/mcumgr/newtmgr/src/newtmgr.c
    src/mcumgr/newtmgr/port/zephyr/src/zephyr_nmgr.c
    src/mgmt_os/src/mgmt_os.c
    src/mgmt_os/src/mgmt_os.c
    src/tinycbor/src/cbor_buf_reader.c
    src/tinycbor/src/cbor_buf_writer.c
    src/tinycbor/src/cborencoder.c
    src/tinycbor/src/cborerrorstrings.c
    src/tinycbor/src/cborparser.c
    src/tinycbor/src/cborparser_dup_string.c
    #src/tinycbor/src/cborpretty.c
    src/tinycbor/src/cbortojson.c
    src/tinycbor/src/cborvalidation.c
    src/znp/src/znp.c
)

target_include_directories(app PRIVATE
    src/mgmt_os/include
    src/cborattr/include
    src/mcumgr/mgmt/include
    src/mcumgr/newtmgr/include
    src/mcumgr/newtmgr/port/zephyr/include
    src/tinycbor/src
    src/znp/include
)
