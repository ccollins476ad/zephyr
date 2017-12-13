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
	$(CC) -c $(CFLAGS) -Iinclude -I$(CURDIR)/../../tinycbor/src src/mgmt.c -o $(OBJ_DIR)/mgmt.o
	$(AR) -rcs $(LIB_DIR)/libmgmt.a $(OBJ_DIR)/mgmt.o

clean:
	rm -rf $(OBJ_DIR) $(LIB_DIR)
