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
	$(CC) -c $(CFLAGS) -Iinclude -I$(CURDIR)/../mgmt/include -I$(CURDIR)/../../tinycbor/src src/newtmgr.c -o $(OBJ_DIR)/newtmgr.o
	$(AR) -rcs $(LIB_DIR)/libnewtmgr.a $(OBJ_DIR)/newtmgr.o

clean:
	rm -rf $(OBJ_DIR) $(LIB_DIR)
