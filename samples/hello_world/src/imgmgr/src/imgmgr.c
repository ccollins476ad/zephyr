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

#include <limits.h>
#include <assert.h>
#include <string.h>

#include <flash.h>
#include <zephyr.h>
#include <soc.h>
#include "cborattr/cborattr.h"
#include "mgmt/mgmt.h"

#include "dfu/mcuboot.h"
#include "dfu/flash_img.h"
#include "imgmgr/imgmgr.h"
#include "imgmgr/image.h"
#include "imgmgr_priv.h"

#define IMGMGR_MAX_CHUNK_SIZE 512

static int imgr_upload(struct mgmt_cbuf *);
static int imgr_erase(struct mgmt_cbuf *);

static const struct mgmt_handler imgr_nmgr_handlers[] = {
    [IMGMGR_NMGR_ID_STATE] = {
        .mh_read = imgmgr_state_read,
        .mh_write = imgmgr_state_write,
    },
    [IMGMGR_NMGR_ID_UPLOAD] = {
        .mh_read = NULL,
        .mh_write = imgr_upload
    },
    [IMGMGR_NMGR_ID_ERASE] = {
        .mh_read = NULL,
        .mh_write = imgr_erase
    },
#if 0
    [IMGMGR_NMGR_ID_CORELIST] = {
#if MYNEWT_VAL(IMGMGR_COREDUMP)
        .mh_read = imgr_core_list,
        .mh_write = NULL
#else
        .mh_read = NULL,
        .mh_write = NULL
#endif
    },
    [IMGMGR_NMGR_ID_CORELOAD] = {
#if MYNEWT_VAL(IMGMGR_COREDUMP)
        .mh_read = imgr_core_load,
        .mh_write = imgr_core_erase,
#else
        .mh_read = NULL,
        .mh_write = NULL
#endif
    },
#endif
};

#define IMGR_HANDLER_CNT                                                \
    sizeof(imgr_nmgr_handlers) / sizeof(imgr_nmgr_handlers[0])

static struct mgmt_group imgr_nmgr_group = {
    .mg_handlers = (struct mgmt_handler *)imgr_nmgr_handlers,
    .mg_handlers_count = IMGR_HANDLER_CNT,
    .mg_group_id = MGMT_GROUP_ID_IMAGE,
};

static struct device *imgmgr_flash_dev;

static struct {
    struct flash_img_context flash_ctxt;
    off_t off;
    size_t image_len;
    bool uploading;
} imgmgr_ctxt;

struct imgmgr_bounds {
    off_t offset;
    size_t size;
};

static const struct imgmgr_bounds imgmgr_slot_bounds[2] = {
    [0] = {
        .offset = FLASH_AREA_IMAGE_0_OFFSET,
        .size = FLASH_AREA_IMAGE_0_SIZE,
    },
    [1] = {
        .offset = FLASH_AREA_IMAGE_1_OFFSET,
        .size = FLASH_AREA_IMAGE_1_SIZE,
    },
};

const struct imgmgr_bounds *
imgmgr_get_slot_bounds(int idx)
{
    if (idx < 0 ||
        idx >= sizeof imgmgr_slot_bounds / sizeof imgmgr_slot_bounds[0]) {

        return NULL;
    }

    return imgmgr_slot_bounds + idx;
}

static int
imgmgr_flash_check_empty(off_t offset, size_t size, bool *out_empty)
{
    uint32_t data[17];
    off_t addr;
    off_t end;
    int bytes_to_read;
    int rc;
    int i;

    assert(size % 4 == 0);

    end = offset + size;
    for (addr = offset; addr < end; addr += sizeof data) {
        if (end - addr < sizeof data) {
            bytes_to_read = end - addr;
        } else {
            bytes_to_read = sizeof data;
        }

        rc = flash_read(imgmgr_flash_dev, addr, data, bytes_to_read);
        if (rc != 0) {
            return MGMT_ERR_EUNKNOWN;
        }

        for (i = 0; i < bytes_to_read / 4; i++) {
            if (data[i] != 0xffffffff) {
                *out_empty = false;
                return 0;
            }
        }
    }

    *out_empty = true;
    return 0;
}

static int
imgr_img_tlvs(const struct image_header *hdr,
              off_t *start_off, off_t *end_off)
{
    struct image_tlv_info tlv_info;
    int rc;

    rc = flash_read(imgmgr_flash_dev, *start_off, &tlv_info, sizeof tlv_info);
    if (rc != 0) {
        return -1;
    }

    if (tlv_info.it_magic != IMAGE_TLV_INFO_MAGIC) {
        return 1;
    }

    *start_off += sizeof tlv_info;
    *end_off = *start_off + tlv_info.it_tlv_tot;

    return 0;
}

/*
 * Read version and build hash from image located slot "image_slot".  Note:
 * this is a slot index, not a flash area ID.
 *
 * @param image_slot
 * @param ver (optional)
 * @param hash (optional)
 * @param flags
 *
 * Returns -1 if area is not readable.
 * Returns 0 if image in slot is ok, and version string is valid.
 * Returns 1 if there is not a full image.
 * Returns 2 if slot is empty. XXXX not there yet
 * XXX Define return code macros.
 */
int
imgr_read_info(int image_slot, struct image_version *ver, uint8_t *hash,
               uint32_t *flags)
{
    struct image_tlv tlv;
    int rc = -1;
    int rc2;
    struct image_header hdr;
    uint32_t data_off, data_end;
    const struct imgmgr_bounds *bounds;

    bounds = imgmgr_get_slot_bounds(image_slot);
    if (bounds == NULL) {
        return -1;
    }

    rc2 = flash_read(imgmgr_flash_dev, bounds->offset, &hdr, sizeof hdr);
    if (rc2) {
        return -1;
    }

    if (ver != NULL) {
        memset(ver, 0xff, sizeof(*ver));
    }
    if (hdr.ih_magic == IMAGE_MAGIC) {
        if (ver) {
            memcpy(ver, &hdr.ih_ver, sizeof(*ver));
        }
    } else if (hdr.ih_magic == 0xffffffff) {
        return 2;
    } else {
        return 1;
    }

    if (flags) {
        *flags = hdr.ih_flags;
    }

    /*
     * Build ID is in a TLV after the image.
     */
    data_off = bounds->offset + hdr.ih_hdr_size + hdr.ih_img_size;
    rc = imgr_img_tlvs(&hdr, &data_off, &data_end);
    if (rc != 0) {
        return rc;
    }

    if (data_end > bounds->offset + bounds->size) {
        return 1;
    }

    while (data_off + sizeof tlv <= data_end) {
        rc2 = flash_read(imgmgr_flash_dev, data_off, &tlv,
                         sizeof tlv);
        if (rc2 != 0) {
            return 0;
        }
        if (tlv.it_type == 0xff && tlv.it_len == 0xffff) {
            return 1;
        }
        if (tlv.it_type != IMAGE_TLV_SHA256 || tlv.it_len != IMGMGR_HASH_LEN) {
            data_off += sizeof tlv + tlv.it_len;
            continue;
        }
        data_off += sizeof tlv;
        if (hash != NULL) {
            if (data_off + IMGMGR_HASH_LEN > data_end) {
                return 0;
            }
            rc2 = flash_read(imgmgr_flash_dev, data_off,
                             hash, IMGMGR_HASH_LEN);
            if (rc2 != 0) {
                return 0;
            }
        }
        return 0;
    }

    return 1;
}

int
imgr_my_version(struct image_version *ver)
{
    return imgr_read_info(boot_current_slot, ver, NULL, NULL);
}

/*
 * Finds image given version number. Returns the slot number image is in,
 * or -1 if not found.
 */
int
imgr_find_by_ver(struct image_version *find, uint8_t *hash)
{
    int i;
    struct image_version ver;

    for (i = 0; i < 2; i++) {
        if (imgr_read_info(i, &ver, hash, NULL) != 0) {
            continue;
        }
        if (!memcmp(find, &ver, sizeof(ver))) {
            return i;
        }
    }
    return -1;
}

/*
 * Finds image given hash of the image. Returns the slot number image is in,
 * or -1 if not found.
 */
int
imgr_find_by_hash(uint8_t *find, struct image_version *ver)
{
    int i;
    uint8_t hash[IMGMGR_HASH_LEN];

    for (i = 0; i < 2; i++) {
        if (imgr_read_info(i, ver, hash, NULL) != 0) {
            continue;
        }
        if (!memcmp(hash, find, IMGMGR_HASH_LEN)) {
            return i;
        }
    }
    return -1;
}

int
imgmgr_find_best_slot(void)
{
    struct image_version ver;
    int best = -1;
    int i;
    int rc;

    for (i = 0; i < 2; i++) {
        rc = imgr_read_info(i, &ver, NULL, NULL);
        if (rc < 0) {
            continue;
        }
        if (rc == 0) {
            /* Image in slot is ok. */
            if (imgmgr_state_slot_in_use(i)) {
                /* Slot is in use; can't use this. */
                continue;
            } else {
                /*
                 * Not active slot, but image is ok. Use it if there are
                 * no better candidates.
                 */
                best = i;
            }
            continue;
        }
        best = i;
        break;
    }

    return best;
}

static int
imgmgr_erase_slot(int slot_idx)
{
    const struct imgmgr_bounds *bounds;
    int rc;

    bounds = imgmgr_get_slot_bounds(slot_idx);
    if (bounds == NULL) {
        return MGMT_ERR_EUNKNOWN;
    }

    rc = boot_erase_img_bank(bounds->offset);
    if (rc != 0) {
        return MGMT_ERR_EUNKNOWN;
    }

    return 0;
}

static int
imgmgr_ensure_slot_erased(int slot_idx)
{
    const struct imgmgr_bounds *bounds;
    bool empty;
    int rc;

    bounds = imgmgr_get_slot_bounds(slot_idx);
    if (bounds == NULL) {
        return MGMT_ERR_EUNKNOWN;
    }

    rc = imgmgr_flash_check_empty(bounds->offset, bounds->size, &empty);
    if (rc != 0) {
        return rc;
    }

    if (!empty) {
        rc = imgmgr_erase_slot(slot_idx);
        if (rc != 0) {
            return rc;
        }
    }

    return 0;
}

static int
imgr_erase(struct mgmt_cbuf *cb)
{
    CborError err;
    int slot;
    int rc;

    slot = imgmgr_find_best_slot();
    if (slot == -1) {
        /* No slot to erase. */
        return MGMT_ERR_ENOMEM;
    }

    /* XXX: Deal with upload state. */

    rc = imgmgr_erase_slot(slot);

    err = 0;
    err |= cbor_encode_text_stringz(&cb->encoder, "rc");
    err |= cbor_encode_int(&cb->encoder, rc);

    if (err != 0) {
        return MGMT_ERR_ENOMEM;
    }

    return 0;
}

static int
imgmgr_write_upload_rsp(struct mgmt_cbuf *cb, int status)
{
    CborError err;

    err = 0;
    err |= cbor_encode_text_stringz(&cb->encoder, "rc");
    err |= cbor_encode_int(&cb->encoder, status);
    err |= cbor_encode_text_stringz(&cb->encoder, "off");
    err |= cbor_encode_int(&cb->encoder, imgmgr_ctxt.off);

    if (err != 0) {
        return MGMT_ERR_ENOMEM;
    }
    return 0;
}

/* XXX: Rename */
static int
imgmgr_upload_first(struct mgmt_cbuf *cb, const uint8_t *req_data, size_t len)
{
    struct image_header hdr;
    int slot;
    int rc;

    if (len < sizeof hdr) {
        return MGMT_ERR_EINVAL;
    }

    memcpy(&hdr, req_data, sizeof hdr);
    if (hdr.ih_magic != IMAGE_MAGIC) {
        return MGMT_ERR_EINVAL;
    }

    slot = imgmgr_find_best_slot();
    if (slot == -1) {
        /* No free slot. */
        return MGMT_ERR_ENOMEM;
    }

    rc = imgmgr_ensure_slot_erased(slot);
    if (rc != 0) {
        return rc;
    }

    imgmgr_ctxt.uploading = true;
    imgmgr_ctxt.off = 0;
    imgmgr_ctxt.image_len = 0;
    flash_img_init(&imgmgr_ctxt.flash_ctxt, imgmgr_flash_dev);

    return 0;
}

static int
imgmgr_write_chunk(const uint8_t *data, size_t len)
{
    int rc;

    rc = flash_img_buffered_write(&imgmgr_ctxt.flash_ctxt, (void *)data, len, false);
    if (rc != 0) {
        return MGMT_ERR_EUNKNOWN;
    }

    return 0;
}

static int
imgr_upload(struct mgmt_cbuf *cb)
{
    long long unsigned int off = UINT_MAX;
    long long unsigned int size = UINT_MAX;
    uint8_t img_data[IMGMGR_MAX_CHUNK_SIZE];
    size_t data_len = 0;
    int rc;

    const struct cbor_attr_t off_attr[4] = {
        [0] = {
            .attribute = "data",
            .type = CborAttrByteStringType,
            .addr.bytestring.data = img_data,
            .addr.bytestring.len = &data_len,
            .len = sizeof(img_data)
        },
        [1] = {
            .attribute = "len",
            .type = CborAttrUnsignedIntegerType,
            .addr.uinteger = &size,
            .nodefault = true
        },
        [2] = {
            .attribute = "off",
            .type = CborAttrUnsignedIntegerType,
            .addr.uinteger = &off,
            .nodefault = true
        },
        [3] = { 0 },
    };

    rc = cbor_read_object(&cb->it, off_attr);
    if (rc || off == UINT_MAX) {
        return MGMT_ERR_EINVAL;
    }

    if (off == 0) {
        rc = imgmgr_upload_first(cb, img_data, data_len);
        if (rc != 0) {
            return rc;
        }
        imgmgr_ctxt.image_len = size;
    } else {
        if (!imgmgr_ctxt.uploading) {
            return MGMT_ERR_EINVAL;
        }

        if (off != imgmgr_ctxt.off) {
            /* Invalid offset. Drop the data, and respond with the offset we're
             * expecting data for.
             */
            return imgmgr_write_upload_rsp(cb, 0);
        }
    }

    if (data_len > 0) {
        rc = imgmgr_write_chunk(img_data, data_len);
        if (rc != 0) {
            return rc;
        }

        imgmgr_ctxt.off += data_len;
        if (imgmgr_ctxt.off == imgmgr_ctxt.image_len) {
            rc = flash_img_buffered_write(&imgmgr_ctxt.flash_ctxt, NULL, 0, true);
            if (rc != 0) {
                return MGMT_ERR_EUNKNOWN;
            }
            imgmgr_ctxt.uploading = false;
        }
    }

    return imgmgr_write_upload_rsp(cb, 0);
}

int
imgmgr_group_register(void)
{
    imgmgr_flash_dev = device_get_binding(FLASH_DRIVER_NAME);
    if (imgmgr_flash_dev == NULL) {
        return -ENODEV;
    }

    return mgmt_group_register(&imgr_nmgr_group);
}
