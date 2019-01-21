/*
 * Copyright (c) 2017 Solarflare Communications Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of the FreeBSD Project.
 */

/* This File is a copy of ef10_image.c from
 * v5 source repo with changeset 63775
 */

#include "fetch_image.h"

/*
 * Utility routines to support limited parsing of ASN.1 tags. This is not a
 * general purpose ASN.1 parser, but is sufficient to locate the required
 * objects in a signed image with CMS headers.
 */

/* Parse header of DER encoded ASN.1 TLV and match tag */
static int asn1_parse_header_match_tag( asn1_cursor_t    *cursor, uint8_t  tag)
{
    int rc = 0;

    if (cursor == NULL || cursor->buffer == NULL || cursor->length < 2)
    {
        rc = EINVAL;
        goto fail;
    }

    cursor->tag = cursor->buffer[0];
    if (cursor->tag != tag)
    {
        /* Tag not matched */
        rc = ENOENT;
        goto fail;
    }

    if ((cursor->tag & 0x1F) == 0x1F)
    {
        /* Long tag format not used in CMS syntax */
        rc = EINVAL;
        goto fail;
    }

    if ((cursor->buffer[1] & 0x80) == 0)
    {
        /* Short form: length is 0..127 */
        cursor->hdr_size = 2;
        cursor->val_size = cursor->buffer[1];
    }
    else
    {
        /* Long form: length encoded as [0x80+nbytes][length bytes] */
        uint32_t nbytes = cursor->buffer[1] & 0x7F;
        uint32_t offset;

        if (nbytes == 0)
        {
            /* Indefinite length not allowed in DER encoding */
            rc = EINVAL;
            goto fail;
        }
        if (2 + nbytes > cursor->length)
        {
            /* Header length overflows image buffer */
            rc = EINVAL;
            goto fail;
        }
        if (nbytes > sizeof (uint32_t))
        {
            /* Length encoding too big */
            rc = E2BIG;
            goto fail;
        }
        cursor->hdr_size = 2 + nbytes;
        cursor->val_size = 0;
        for (offset = 2; offset < cursor->hdr_size; offset++)
        {
            cursor->val_size =
                (cursor->val_size << 8) | cursor->buffer[offset];
        }
    }

    if ((cursor->hdr_size + cursor->val_size) > cursor->length)
    {
        /* Length overflows image buffer */
        rc = E2BIG;
        goto fail;
    }

fail:
    return (rc);
}

/* Enter nested ASN.1 TLV (contained in value of current TLV) */
static int asn1_enter_tag(asn1_cursor_t    *cursor, uint8_t  tag)
{
    int rc = 0;

    if (cursor == NULL)
    {
        rc = EINVAL;
        goto fail;
    }

    if (ASN1_TAG_IS_PRIM(tag))
    {
        /* Cannot enter a primitive tag */
        rc = ENOTSUP;
        goto fail;
    }
    rc = asn1_parse_header_match_tag(cursor, tag);
    if (rc != 0)
    {
        /* Invalid TLV or wrong tag */
        goto fail;
    }

    /* Limit cursor range to nested TLV */
    cursor->buffer += cursor->hdr_size;
    cursor->length = cursor->val_size;

fail:
    return (rc);
}

/*
 * Check that the current ASN.1 TLV matches the given tag and value.
 * Advance cursor to next TLV on a successful match.
 */
static int asn1_match_tag_value(
               asn1_cursor_t    *cursor,
               uint8_t        tag,
               const void        *valp,
               uint32_t        val_size)
{
    int rc = 0;

    if (cursor == NULL)
    {
        rc = EINVAL;
        goto fail;
    }
    rc = asn1_parse_header_match_tag(cursor, tag);
    if (rc != 0)
    {
        /* Invalid TLV or wrong tag */
        goto fail;
    }
    if (cursor->val_size != val_size)
    {
        /* Value size is different */
        rc = EINVAL;
        goto fail;
    }
    if (memcmp(cursor->buffer + cursor->hdr_size, valp, val_size) != 0)
    {
        /* Value content is different */
        rc = EINVAL;
        goto fail;
    }
    cursor->buffer += cursor->hdr_size + cursor->val_size;
    cursor->length -= cursor->hdr_size + cursor->val_size;

fail:
    return (rc);
}

/* Advance cursor to next TLV */
static int asn1_skip_tag(
                asn1_cursor_t    *cursor,
                uint8_t          tag)
{
    int rc = 0;

    if (cursor == NULL)
    {
        rc = EINVAL;
        goto fail;
    }

    rc = asn1_parse_header_match_tag(cursor, tag);
    if (rc != 0)
    {
        /* Invalid TLV or wrong tag */
        goto fail;
    }
    cursor->buffer += cursor->hdr_size + cursor->val_size;
    cursor->length -= cursor->hdr_size + cursor->val_size;

fail:
    return (rc);
}

/* Return pointer to value octets and value size from current TLV */
static int asn1_get_tag_value(
        asn1_cursor_t    *cursor,
        uint8_t          tag,
        uint8_t          **valp,
        uint32_t         *val_sizep)
{
    int rc = 0;

    if (cursor == NULL || valp == NULL || val_sizep == NULL)
    {
        rc = EINVAL;
        goto fail;
    }

    rc = asn1_parse_header_match_tag(cursor, tag);
    if (rc != 0)
    {
        /* Invalid TLV or wrong tag */
        goto fail;
    }
    *valp = cursor->buffer + cursor->hdr_size;
    *val_sizep = cursor->val_size;
fail:
    return (rc);
}


/*
 * Utility routines for parsing CMS headers (see RFC2315, PKCS#7)
 */

/* OID 1.2.840.113549.1.7.2 */
static const uint8_t PKCS7_SignedData[] =
{ 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x07, 0x02 };

/* OID 1.2.840.113549.1.7.1 */
static const uint8_t PKCS7_Data[] =
{ 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x07, 0x01 };

/* SignedData structure version */
static const uint8_t SignedData_Version[] =
{ 0x03 };

/*
 * Check for a valid image in signed image format. This uses CMS syntax
 * (see RFC2315, PKCS#7) to provide signatures, and certificates required
 * to validate the signatures. The encapsulated content is in unsigned image
 * format (reflash header, image code, trailer checksum).
 */
static int check_signed_image_header(
        void        *bufferp,
        uint32_t    buffer_size,
        uint32_t    *content_offsetp,
        uint32_t    *content_lengthp)
{
    asn1_cursor_t cursor;
    uint8_t *valp;
    uint32_t val_size;
    int rc = 0;

    if (content_offsetp == NULL || content_lengthp == NULL)
    {
        rc = EINVAL;
        goto fail;
    }
    cursor.buffer = (uint8_t *)bufferp;
    cursor.length = buffer_size;

    /* ContextInfo */
    rc = asn1_enter_tag(&cursor, ASN1_TAG_SEQUENCE);
    if (rc != 0)
        goto fail;

    /* ContextInfo.contentType */
    rc = asn1_match_tag_value(&cursor, ASN1_TAG_OBJ_ID,
        PKCS7_SignedData, sizeof (PKCS7_SignedData));
    if (rc != 0)
        goto fail;

    /* ContextInfo.content */
    rc = asn1_enter_tag(&cursor, ASN1_TAG_CONS_CONTEXT(0));
    if (rc != 0)
        goto fail;

    /* SignedData */
    rc = asn1_enter_tag(&cursor, ASN1_TAG_SEQUENCE);
    if (rc != 0)
        goto fail;

    /* SignedData.version */
    rc = asn1_match_tag_value(&cursor, ASN1_TAG_INTEGER,
        SignedData_Version, sizeof (SignedData_Version));
    if (rc != 0)
        goto fail;

    /* SignedData.digestAlgorithms */
    rc = asn1_skip_tag(&cursor, ASN1_TAG_SET);
    if (rc != 0)
        goto fail;

    /* SignedData.encapContentInfo */
    rc = asn1_enter_tag(&cursor, ASN1_TAG_SEQUENCE);
    if (rc != 0)
        goto fail;

    /* SignedData.encapContentInfo.econtentType */
    rc = asn1_match_tag_value(&cursor, ASN1_TAG_OBJ_ID,
        PKCS7_Data, sizeof (PKCS7_Data));
    if (rc != 0)
        goto fail;

    /* SignedData.encapContentInfo.econtent */
    rc = asn1_enter_tag(&cursor, ASN1_TAG_CONS_CONTEXT(0));
    if (rc != 0)
        goto fail;

    /*
     * The octet string contains the image header, image code bytes and
     * image trailer CRC (same as unsigned image layout).
     */
    valp = NULL;
    val_size = 0;
    rc = asn1_get_tag_value(&cursor, ASN1_TAG_OCTET_STRING,
        &valp, &val_size);
    if (rc != 0)
        goto fail;

    if ((valp == NULL) || (val_size == 0))
    {
        rc = EINVAL;
        goto fail;
    }
    if (valp < (uint8_t *)bufferp)
    {
        rc = EINVAL;
        goto fail;
    }
    if ((valp + val_size) > ((uint8_t *)bufferp + buffer_size))
    {
        rc = EINVAL;
        goto fail;
    }

    *content_offsetp = (uint32_t)(valp - (uint8_t *)bufferp);
    *content_lengthp = val_size;

fail:
    return (rc);
}

static int check_unsigned_image(
    char        *bufferp,
    uint32_t    buffer_size)
{
    image_header_t *header;
    image_trailer_t *trailer;
    uint32_t crc;
    int rc = 0;

    /* Must have at least enough space for required image header fields */
    if (buffer_size < (FIELD_OFFSET(image_header_t, eih_size) +
        sizeof (header->eih_size)))
    {
        rc = ENOSPC;
        goto fail;
    }
    header = (image_header_t *)bufferp;
    if (header->eih_magic != IMAGE_HEADER_MAGIC)
    {
        rc = EINVAL;
        goto fail;
    }
    /*
     * Check image header version is same or higher than lowest required
     * version.
     */
    if (header->eih_version < IMAGE_HEADER_VERSION)
    {
        rc = EINVAL;
        goto fail;
    }

    /* Buffer must have space for image header, code and image trailer. */
    if (buffer_size < (header->eih_size + header->eih_code_size +
        IMAGE_TRAILER_SIZE))
    {
        rc = ENOSPC;
        goto fail;
    }

fail:
    return (rc);
}

int check_reflash_image(
    void            *bufferp,
    uint32_t        buffer_size,
    uint32_t        *type)
{
    image_info_t    infop;
    image_format_t format = IMAGE_FORMAT_NO_IMAGE;
    uint32_t image_offset;
    uint32_t image_size;
    void *imagep;
    int rc = 0;

    memset(&infop, 0, sizeof (infop));

    if (bufferp == NULL || buffer_size == 0)
    {
        rc = EINVAL;
        goto fail;
    }

    /*
     * Check if the buffer contains an image in signed format, and if so,
     * locate the image header.
     */
    rc = check_signed_image_header(bufferp, buffer_size,
        &image_offset, &image_size);
    if (rc == 0)
    {
        /*
         * Buffer holds signed image format. Check that the encapsulated
         * content is in unsigned image format.
         */
        format = IMAGE_FORMAT_SIGNED;
    }
    else
    {
        /* Check if the buffer holds image in unsigned image format */
        format = IMAGE_FORMAT_UNSIGNED;
        image_offset = 0;
        image_size = buffer_size;
    }
    if (image_offset + image_size > buffer_size)
    {
        rc = E2BIG;
        goto fail;
    }
    imagep = bufferp + image_offset;
    /* Check unsigned image layout (image header, code, image trailer) */
    rc = check_unsigned_image(imagep, image_size);
    if (rc != 0)
        goto fail;
    /* Return image details */
    infop.eii_format = format;
    infop.eii_imagep = bufferp;
    infop.eii_image_size = buffer_size;
    infop.eii_headerp = (image_header_t *)imagep;

    *type = (infop.eii_headerp->eih_type);
    type++;
    *type = (infop.eii_headerp->eih_subtype);

fail:
    return (rc);
}

