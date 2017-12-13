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
	$(CC) -c $(CFLAGS) -Iinclude -I$(CURDIR)/../tinycbor/src src/cborattr.c -o $(OBJ_DIR)/cborattr.o
	$(AR) -rcs $(LIB_DIR)/libcborattr.a $(OBJ_DIR)/cborattr.o

clean:
	rm -rf $(OBJ_DIR) $(LIB_DIR)
