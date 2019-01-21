#ifndef FETCH_IMAGE_H
#define FETCH_IMAGE_H

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>

/* DER encodings for ASN.1 tags (see ITU-T X.690) */
#define	ASN1_TAG_INTEGER	    (0x02)
#define	ASN1_TAG_OCTET_STRING	    (0x04)
#define	ASN1_TAG_OBJ_ID		    (0x06)
#define	ASN1_TAG_SEQUENCE	    (0x30)
#define	ASN1_TAG_SET		    (0x31)

#define	ASN1_TAG_IS_PRIM(tag)	    ((tag & 0x20) == 0)

#define	ASN1_TAG_PRIM_CONTEXT(n)    (0x80 + (n))
#define	ASN1_TAG_CONS_CONTEXT(n)    (0xA0 + (n))

#define STATIC_ASSERT(_cond)                \
        ((void)sizeof (char[(_cond) ? 1 : -1]))

#define ARRAY_SIZE(_array)                  \
        (sizeof (_array) / sizeof ((_array)[0]))

#define FIELD_OFFSET(_type, _field)         \
        ((size_t)&(((_type *)0)->_field))

typedef struct image_header_s {
        uint32_t        eih_magic;
        uint32_t        eih_version;
        uint32_t        eih_type;
        uint32_t        eih_subtype;
        uint32_t        eih_code_size;
        uint32_t        eih_size;
        union {
                uint32_t        eih_controller_version_min;
                struct {
                        uint16_t        eih_controller_version_min_short;
                        uint8_t         eih_extra_version_a;
                        uint8_t         eih_extra_version_b;
                };
        };
        union {
                uint32_t        eih_controller_version_max;
                struct {
                        uint16_t        eih_controller_version_max_short;
                        uint8_t         eih_extra_version_c;
                        uint8_t         eih_extra_version_d;
                };
        };
        uint16_t        eih_code_version_a;
        uint16_t        eih_code_version_b;
        uint16_t        eih_code_version_c;
        uint16_t        eih_code_version_d;
} image_header_t;

#define IMAGE_HEADER_SIZE           (40)
#define IMAGE_HEADER_VERSION        (4)
#define IMAGE_HEADER_MAGIC          (0x106F1A5)


typedef struct image_trailer_s {
        uint32_t        eit_crc;
} image_trailer_t;

#define IMAGE_TRAILER_SIZE          (4)

typedef enum image_format_e {
        IMAGE_FORMAT_NO_IMAGE,
        IMAGE_FORMAT_INVALID,
        IMAGE_FORMAT_UNSIGNED,
        IMAGE_FORMAT_SIGNED,
} image_format_t;

typedef struct image_info_s {
        image_format_t      eii_format;
        uint8_t *           eii_imagep;
        size_t              eii_image_size;
        image_header_t *    eii_headerp;
} image_info_t;

extern  int
check_reflash_image(
                   void           *bufferp,
                   uint32_t       buffer_size,
                   uint32_t       *type);

typedef struct asn1_cursor_s {
        uint8_t         *buffer;
        uint32_t        length;

        uint8_t         tag;
        uint32_t        hdr_size;
        uint32_t        val_size;
} asn1_cursor_t;
#endif
