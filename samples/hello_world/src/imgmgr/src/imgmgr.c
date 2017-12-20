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

#include "bootutil/image.h"
#include "imgmgr/imgmgr.h"
#include "imgmgr_priv.h"

//static int imgr_upload(struct mgmt_cbuf *);
//static int imgr_erase(struct mgmt_cbuf *);

static const struct mgmt_handler imgr_nmgr_handlers[] = {
    [IMGMGR_NMGR_ID_STATE] = {
        .mh_read = imgmgr_state_read,
        //.mh_write = imgmgr_state_write,
    },
#if 0
    [IMGMGR_NMGR_ID_UPLOAD] = {
        .mh_read = NULL,
        .mh_write = imgr_upload
    },
    [IMGMGR_NMGR_ID_ERASE] = {
        .mh_read = NULL,
        .mh_write = imgr_erase
    },
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

struct imgr_state imgr_state;
static struct device *imgmgr_flash_dev;

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

    if (data_end > bounds->size) {
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

#if 0
static int
imgmgr_erase_slot(int slot_idx)
{
    const struct imgmgr_bounds *bounds;
    int rc;

    bounds = imgmgr_get_slot_bounds(slot);
    if (bounds == NULL) {
        return MGMT_ERR_EUNKNOWN;
    }

    /* XXX: Call in to mcuboot? */

    rc = flash_write_protection_set(imgmgr_flash_dev, false);
    if (rc != 0) {
        return MGMT_ERR_EUNKNOWN;
    }

    rc = flash_erase(imgmgr_flash_dev, bounds->offset, bounds->size);
    if (rc != 0) {
        return MGMT_ERR_EUNKNOWN;
    }

    rc = flash_write_protection_set(imgmgr_flash_dev, true);
    if (rc != 0) {
        return MGMT_ERR_EUNKNOWN;
    }

    return 0;
}

static int
imgr_erase(struct mgmt_cbuf *cb)
{
    CborError g_err:
    int slot;
    int rc;

    slot = imgmgr_find_best_slot();
    if (slot == -1) {
        /* No slot to erase. */
        return MGMT_ERR_ENOMEM;
    }

    /* XXX: Deal with upload state. */

    rc = imgmgr_erase_slot(slot);

    g_err = 0;
    g_err |= cbor_encode_text_stringz(&cb->encoder, "rc");
    g_err |= cbor_encode_int(&cb->encoder, rc);

    if (g_err != 0) {
        return MGMT_ERR_ENOMEM;
    }

    return 0;
}

static int
imgr_upload(struct mgmt_cbuf *cb)
{
    uint8_t img_data[MYNEWT_VAL(IMGMGR_MAX_CHUNK_SIZE)];
    long long unsigned int off = UINT_MAX;
    long long unsigned int size = UINT_MAX;
    size_t data_len = 0;
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
    struct image_header *hdr;
    int area_id;
    int rc;
    bool empty = false;
    CborError g_err = CborNoError;

    rc = cbor_read_object(&cb->it, off_attr);
    if (rc || off == UINT_MAX) {
        return MGMT_ERR_EINVAL;
    }

    if (off == 0) {
        if (data_len < sizeof(struct image_header)) {
            /*
             * Image header is the first thing in the image.
             */
            return MGMT_ERR_EINVAL;
        }
        hdr = (struct image_header *)img_data;
        if (hdr->ih_magic != IMAGE_MAGIC) {
            return MGMT_ERR_EINVAL;
        }

        /*
         * New upload.
         */
        imgr_state.upload.off = 0;
        imgr_state.upload.size = size;

        area_id = imgmgr_find_best_slot();
        if (area_id >= 0) {
            if (imgr_state.upload.fa) {
                flash_area_close(imgr_state.upload.fa);
                imgr_state.upload.fa = NULL;
            }
            rc = flash_area_open(area_id, &imgr_state.upload.fa);
            if (rc) {
                return MGMT_ERR_EINVAL;
            }

            rc = flash_area_is_empty(imgr_state.upload.fa, &empty);
            if (rc) {
                return MGMT_ERR_EINVAL;
            }

            if(!empty) {
                rc = flash_area_erase(imgr_state.upload.fa, 0,
                  imgr_state.upload.fa->fa_size);
            }
        } else {
            /*
             * No slot where to upload!
             */
            return MGMT_ERR_ENOMEM;
        }
    } else if (off != imgr_state.upload.off) {
        /*
         * Invalid offset. Drop the data, and respond with the offset we're
         * expecting data for.
         */
        goto out;
    }

    if (!imgr_state.upload.fa) {
        return MGMT_ERR_EINVAL;
    }
    if (data_len) {
        rc = flash_area_write(imgr_state.upload.fa, imgr_state.upload.off,
          img_data, data_len);
        if (rc) {
            rc = MGMT_ERR_EINVAL;
            goto err_close;
        }
        imgr_state.upload.off += data_len;
        if (imgr_state.upload.size == imgr_state.upload.off) {
            /* Done */
            flash_area_close(imgr_state.upload.fa);
            imgr_state.upload.fa = NULL;
        }
    }

out:
    g_err |= cbor_encode_text_stringz(&cb->encoder, "rc");
    g_err |= cbor_encode_int(&cb->encoder, MGMT_ERR_EOK);
    g_err |= cbor_encode_text_stringz(&cb->encoder, "off");
    g_err |= cbor_encode_int(&cb->encoder, imgr_state.upload.off);

    if (g_err) {
        return MGMT_ERR_ENOMEM;
    }
    return 0;
err_close:
    flash_area_close(imgr_state.upload.fa);
    imgr_state.upload.fa = NULL;
    return rc;
}
#endif

int
imgmgr_group_register(void)
{
    extern struct device *boot_flash_device;

    imgmgr_flash_dev = device_get_binding(FLASH_DRIVER_NAME);
    if (imgmgr_flash_dev == NULL) {
        return -ENODEV;
    }

    boot_flash_device = imgmgr_flash_dev;

    return mgmt_group_register(&imgr_nmgr_group);
}
