#
# Copyright (c) 2017 Intel Corporation
#
# SPDX-License-Identifier: Apache-2.0
#

PREFIX ?= .
OBJ_DIR ?= $(PREFIX)/obj
LIB_DIR ?= $(PREFIX)/lib

all:
	mkdir -p $(OBJ_DIR) $(LIB_DIR)
	$(CC) -c $(CFLAGS) -Iinclude -I$(CURDIR)/../tinycbor/src -I$(CURDIR)/../cborattr/include -I$(CURDIR)/../mcumgr/mgmt/include src/mgmt_os.c -o $(OBJ_DIR)/mgmt_os.o
	$(AR) -rcs $(LIB_DIR)/libmgmt_os.a $(OBJ_DIR)/mgmt_os.o

clean:
	rm -rf $(OBJ_DIR) $(LIB_DIR)
