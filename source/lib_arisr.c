/**
 * @attention

    Copyright (C) 2025  - ARIS Alliance

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

 **********************************************************************************
 * @file lib_arisr.c
 * @brief This file contains the implementation of the ARISr library.
 * @date 2025-01-30
 * @authors ARIS Alliance
*/

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h> // For malloc, free

#include "lib_arisr_base.h"
#include "lib_arisr_interface.h"
#include "lib_arisr_comm.h"
#include "lib_arisr_err.h"
#include "lib_arisr_crypt.h"
#include "lib_arisr.h"


// Default key value
const ARISR_AES128_KEY ARISR_DEFAULT_NULL_KEY = { 0x00 };

// =============================================
ARISR_UINT8 ARISR_proto_ctrl_getField(const ARISR_UINT8 *ctrl, ARISR_UINT32 mask, ARISR_UINT8 shift)
{
    if (!ctrl) {
        return -1;
    }
    // Interpret 'ctrl' as a uint32_t and apply 'mask'
    return (ARISR_UINT8)((
    (
        (
            ((ARISR_UINT32)(ARISR_UINT8)ctrl[0] << 24) |  // Byte 0 (MSB)
            ((ARISR_UINT32)(ARISR_UINT8)ctrl[1] << 16) |  // Byte 1
            ((ARISR_UINT32)(ARISR_UINT8)ctrl[2] <<  8) |  // Byte 2
            ((ARISR_UINT32)(ARISR_UINT8)ctrl[3])          // Byte 3 (LSB)
        )
    ) & mask) >> shift);
}

// =============================================
ARISR_ERR ARISR_proto_ctrl_setField(ARISR_UINT8 *ctrl, ARISR_UINT8 data, ARISR_UINT8 shift)
{
    if (!ctrl) {
        return (ARISR_ERR)kARISR_ERR_GENERIC;
    }

    // Combine the 4 bytes from the array into a 32-bit integer (big-endian).
    ARISR_UINT32 ctrl32 = ((ARISR_UINT32)ctrl[0] << 24) |
                          ((ARISR_UINT32)ctrl[1] << 16) |
                          ((ARISR_UINT32)ctrl[2] <<  8) |
                          ((ARISR_UINT32)ctrl[3]);

    // Insert the bits from 'data' at position 'shift'.
    ctrl32 |= ((ARISR_UINT32)data << shift);

    // Write back to the array in big-endian format.
    ctrl[0] = (ARISR_UINT8)(ctrl32 >> 24);
    ctrl[1] = (ARISR_UINT8)(ctrl32 >> 16);
    ctrl[2] = (ARISR_UINT8)(ctrl32 >>  8);
    ctrl[3] = (ARISR_UINT8)(ctrl32);

    return kARISR_OK;
}

// =============================================
ARISR_ERR ARISR_proto_raw_chunk_clean(ARISR_CHUNK_RAW *buffer)
{
    if (!buffer) {
        return kARISR_ERR_GENERIC;
    }

    // Free destinationsB array if allocated
    if (buffer->destinationsB) {
        free(buffer->destinationsB);
        buffer->destinationsB = NULL;
    }

    // Free destinationsC if allocated
    if (buffer->destinationC) {
        free(buffer->destinationC);
        buffer->destinationC = NULL;
    }

    // Free ctrl2 array if allocated
    if (buffer->ctrl2) {
        free(buffer->ctrl2);
        buffer->ctrl2 = NULL;
    }

    // Free data array if allocated
    if (buffer->data) {
        free(buffer->data);
        buffer->data = NULL;
    }

    // Reset the structure to zero
    memset(buffer, 0, sizeof(ARISR_CHUNK_RAW));
    return kARISR_OK;
}

// =============================================
ARISR_ERR ARISR_proto_chunk_clean(ARISR_CHUNK *buffer)
{
    if (!buffer) {
        return kARISR_ERR_GENERIC;
    }

    // Free destinationsB array if allocated
    if (buffer->destinationsB) {
        free(buffer->destinationsB);
        buffer->destinationsB = NULL;
    }

    // Free data array if allocated
    if (buffer->data) {
        free(buffer->data);
        buffer->data = NULL;
    }

    // Reset the structure to zero
    memset(buffer, 0, sizeof(ARISR_CHUNK_RAW));
    return kARISR_OK;
}

// =============================================
ARISR_ERR ARISR_proto_parse(ARISR_CHUNK *buffer, const ARISR_UINT8 *data, const ARISR_AES128_KEY key, ARISR_UINT8 *id)
{
    if (!buffer || !data) {
        return kARISR_ERR_GENERIC;
    }

    // Clean up the buffer first in case it has leftover data
    // if (ARISR_proto_raw_chunk_clean(buffer) != kARISR_OK) {
    //     return kARISR_ERR_GENERIC;
    // }
    memset(buffer, 0, sizeof(ARISR_CHUNK));

    // Reading pointer (index) for the 'data' buffer
    unsigned int p = 0, err;

    // Field placeholders
    ARISR_UINT8 ctrl[4];
    ARISR_UINT16 crc;

    /* =============== ID & ARIS ================= */
    // 1- Copy the first 8 bytes (ID + ARIS), known as ARISR_PROTO_CRYPT_SIZE
    //    directly into the 'id' and 'aris' fields of the buffer.
    //    Then increase 'p' accordingly.
    memcpy(buffer->id, data, ARISR_PROTO_CRYPT_SIZE);

    // Check if id is the same as the provided 'id'
    if (memcmp(buffer->id, id, ARISR_PROTO_ID_SIZE) != 0) {
        return kARISR_ERR_NOT_SAME_ID;
    }

    // Decrypt the 'aris' field using the last byte of the key
    if (ARISR_aes_aris_decrypt(key, buffer->aris) != kARISR_OK) {
        return kARISR_ERR_NOT_SAME_ARIS;
    }

    p += ARISR_PROTO_CRYPT_SIZE;

    /* =============== CTRL 1 ================= */
    // 2- Allocate and copy the first control section (CTRL1).
    memcpy(ctrl, data + p, ARISR_CTRL_SECTION_SIZE);
    p += ARISR_CTRL_SECTION_SIZE;

    // Extract required bits from CTRL1
    buffer->ctrl.version        = ARISR_proto_ctrl_getField(ctrl, ARISR_CTRL_VERSION_MASK, ARISR_CTRL_VERSION_SHIFT);
    buffer->ctrl.destinations   = ARISR_proto_ctrl_getField(ctrl, ARISR_CTRL_DESTS_MASK, ARISR_CTRL_DESTS_SHIFT);
    buffer->ctrl.from           = ARISR_proto_ctrl_getField(ctrl, ARISR_CTRL_FROM_MASK, ARISR_CTRL_FROM_SHIFT);
    buffer->ctrl.option         = ARISR_proto_ctrl_getField(ctrl, ARISR_CTRL_OPTION_MASK, ARISR_CTRL_OPTION_SHIFT);
    buffer->ctrl.sequence       = ARISR_proto_ctrl_getField(ctrl, ARISR_CTRL_SEQUENCE_MASK, ARISR_CTRL_SEQUENCE_SHIFT);
    buffer->ctrl.retry          = ARISR_proto_ctrl_getField(ctrl, ARISR_CTRL_RETRY_MASK, ARISR_CTRL_RETRY_SHIFT);
    buffer->ctrl.more_data      = ARISR_proto_ctrl_getField(ctrl, ARISR_CTRL_MD_MASK, ARISR_CTRL_MD_SHIFT);
    buffer->ctrl.identifier     = ARISR_proto_ctrl_getField(ctrl, ARISR_CTRL_ID_MASK, ARISR_CTRL_ID_SHIFT);
    buffer->ctrl.more_header    = ARISR_proto_ctrl_getField(ctrl, ARISR_CTRL_MH_MASK, ARISR_CTRL_MH_SHIFT);

    /* =============== ORIGIN & DESTINATION ================= */
    // 3- Copy origin (6 bytes) and destinationA (6 bytes)
    memcpy(buffer->origin, data + p, ARISR_ADDRESS_SIZE * 2);
    p += ARISR_ADDRESS_SIZE * 2;

    /* =============== DESTINATIONS B ================= */
    // 4- Copy the next 'destinations' addresses (each 6 bytes)
    if (buffer->ctrl.destinations > 0) {
        buffer->destinationsB = (ARISR_UINT8 (*)[ARISR_ADDRESS_SIZE])
                                 malloc(buffer->ctrl.destinations * (sizeof(ARISR_UINT8) * ARISR_ADDRESS_SIZE));
        if (!buffer->destinationsB) {
            ARISR_CLEAN_AND_RETURN(kARISR_ERR_GENERIC);
        }
        memcpy(buffer->destinationsB, data + p, buffer->ctrl.destinations * ARISR_ADDRESS_SIZE);
        p += buffer->ctrl.destinations * ARISR_ADDRESS_SIZE;
    }

    /* =============== DESTINATION C ================= */
    // 5- If 'from_relay' is set, copy the 'destinationC' field (6 bytes)
    memset(buffer->destinationC, '\0', ARISR_ADDRESS_SIZE);
    if (buffer->ctrl.from) {
        memcpy(buffer->destinationC, data + p, ARISR_ADDRESS_SIZE);
        p += ARISR_ADDRESS_SIZE;
    }

    /* ================= CTRL 2 ===================== */
    // 6- If 'more_headers' is set, we allocate and copy CTRL2
    if (buffer->ctrl.more_header) {
        memcpy(ctrl, data + p, ARISR_CTRL_SECTION_SIZE);
        p += ARISR_CTRL2_SECTION_SIZE;
        buffer->ctrl2.data_length = ARISR_proto_ctrl_getField(ctrl, ARISR_CTRL2_DATA_LENGTH_MASK, ARISR_CTRL2_DATA_LENGTH_SHIFT) * ARISR_DATA_MULT;
        buffer->ctrl2.feature     = ARISR_proto_ctrl_getField(ctrl, ARISR_CTRL2_FEATURE_MASK, ARISR_CTRL2_FEATURE_SHIFT);
        buffer->ctrl2.neg_answer  = ARISR_proto_ctrl_getField(ctrl, ARISR_CTRL2_NEG_ANSWER_MASK, ARISR_CTRL2_NEG_ANSWER_SHIFT);
        buffer->ctrl2.freq_switch = ARISR_proto_ctrl_getField(ctrl, ARISR_CTRL2_FREQ_SWITCH_MASK, ARISR_CTRL2_FREQ_SWITCH_SHIFT);
    } else {
        memset(&buffer->ctrl2, '\0', ARISR_CTRL2_SECTION_SIZE);
    }

    /* =============== CRC HEADER ================= */
    // 7- Copy the 2-byte CRC for the header
    memcpy(buffer->crc_header, data + p, ARISR_CRC_SIZE);

    // Convert 'crc_header' (2 bytes) into an ARISR_UINT16 for comparison
    ARISR_UINT16 expected_crc_header = ((ARISR_UINT16)buffer->crc_header[0] << 8) 
                                     | buffer->crc_header[1];

    // Calculate CRC over the entire header portion from index 0 to p-1
    // 'p' currently points at the start of the CRC header, so the header size is 'p'.
    crc = ARISR_crypt_crc16_calculate((const ARISR_UINT8*)data, p);
    if (crc != expected_crc_header) {
        ARISR_CLEAN_AND_RETURN(kARISR_ERR_NOT_SAME_CRC_HEADER);
    }
    p += ARISR_CRC_SIZE;

    /* =============== DATA ================= */
    // 8- If 'more_headers' is set, we parse the data section and decrypt it with its CRC
    if (buffer->ctrl.more_header && buffer->ctrl2.data_length > 0) {
        // Copy the 2-byte CRC for the data
        memcpy(buffer->crc_data, data + p + buffer->ctrl2.data_length, ARISR_CRC_SIZE);

        // Convert 'crc_data' (2 bytes) into an ARISR_UINT16 for comparison
        ARISR_UINT16 expected_crc_data = ((ARISR_UINT16)buffer->crc_data[0] << 8) 
                                       | buffer->crc_data[1];

        // Calculate CRC over the entire data portion from index 0 to p-1
        // 'p' currently points at the start of the CRC data, so the data size is 'p'.
        crc = ARISR_crypt_crc16_calculate((const ARISR_UINT8*)data + p, buffer->ctrl2.data_length);
        if (crc != expected_crc_data) {
            ARISR_CLEAN_AND_RETURN(kARISR_ERR_NOT_SAME_CRC_DATA);
        }

        // Data
        ARISR_UINT8 *decrypted_data;
        ARISR_UINT32 decrypted_length;
        if ((err = ARISR_aes_data_decrypt(
            (!ARISR_AES_IS_ZERO_KEY(key)) ? key : ARISR_DEFAULT_NULL_KEY
            , data + p, buffer->ctrl2.data_length, &decrypted_data, &decrypted_length)) != kARISR_OK) {

            ARISR_CLEAN_AND_RETURN(err);
        }

        // Copy data to buffer
        buffer->data = decrypted_data;

        // Move the pointer to the next section after the data
        p += buffer->ctrl2.data_length + ARISR_CRC_SIZE;

        // Update real data length decrypted
        buffer->ctrl2.data_length = decrypted_length;
    }

    /* =============== END ================= */
    // 9- Finally, copy the 4-byte 'end' field
    memcpy(buffer->end, data + p, ARISR_PROTO_ID_SIZE);

    // Check if 'end' is the same as the provided 'id'
    if (memcmp(buffer->end, id, ARISR_PROTO_ID_SIZE) != 0) {
        return kARISR_ERR_NOT_SAME_END;
    }

    return kARISR_OK;
}

// =============================================
ARISR_ERR ARISR_proto_build(ARISR_UINT8 **buffer, ARISR_UINT32 *length, ARISR_CHUNK *data, const ARISR_AES128_KEY key)
{
    ARISR_ERR err;
    if (!data || !buffer || !length) {
        return kARISR_ERR_GENERIC;
    }

    // Pointer to save size of the buffer
    unsigned int p, size;
    ARISR_UINT16 crc;
    ARISR_UINT32 encrypted_length = 0;
    ARISR_UINT8 *encrypted_data = NULL, ctrl[4];

    // Minimun size of the buffer
    size = ARISR_PROTO_ID_SIZE + ARISR_PROTO_ARIS_SIZE + ARISR_CTRL_SECTION_SIZE + ARISR_ADDRESS_SIZE * 2 + ARISR_CRC_SIZE + ARISR_PROTO_ID_SIZE;
    // Calculate the destinations
    if (data->ctrl.destinations > 0) {
        size += data->ctrl.destinations * ARISR_ADDRESS_SIZE;
    }

    // Get from
    if (data->ctrl.from) {
        size += ARISR_ADDRESS_SIZE;
    }

    // Get more headers
    if (data->ctrl.more_header) {
        size += ARISR_CTRL2_SECTION_SIZE;

        /* =============== DATA ================= */// -----> Calculate first the apply to ctrl2
        // Calculate the data length
        if (data->ctrl2.data_length > 0) {
            // Encrypt the data section using AES key
            if ((err = ARISR_aes_data_encrypt(
                (!ARISR_AES_IS_ZERO_KEY(key)) ? key : ARISR_DEFAULT_NULL_KEY
                , data->data, data->ctrl2.data_length, &encrypted_data, &encrypted_length)) != kARISR_OK) {
                
                return err;
            }

            size += encrypted_length + ARISR_CRC_SIZE;
        }
    }

    // Allocate the buffer
    *buffer = (ARISR_UINT8*)malloc(sizeof(ARISR_UINT8) * size);

    // Check if the buffer was allocated
    if (!*buffer) {
        return kARISR_ERR_GENERIC;
    }

    memset(*buffer, '\0', size);

    // Assign the size to the length
    *length = size;

    // Start writing the buffer
    p = 0;

    /* ================  ID  ================== */
    memcpy(*buffer, data, ARISR_PROTO_CRYPT_SIZE);
    p += ARISR_PROTO_CRYPT_SIZE;

    /* =============== ARIS ================= */
    if ((err = ARISR_aes_aris_encrypt(key, *buffer + ARISR_PROTO_ID_SIZE)) != kARISR_OK) {
        free(*buffer);
        return err;
    }


    /* =============== CTRL 1 ================= */
    memset(ctrl, '\0', ARISR_CTRL_SECTION_SIZE);
    ARISR_proto_ctrl_setField(ctrl, data->ctrl.version, ARISR_CTRL_VERSION_SHIFT);
    ARISR_proto_ctrl_setField(ctrl, data->ctrl.destinations, ARISR_CTRL_DESTS_SHIFT);
    ARISR_proto_ctrl_setField(ctrl, data->ctrl.from, ARISR_CTRL_FROM_SHIFT);
    ARISR_proto_ctrl_setField(ctrl, data->ctrl.option, ARISR_CTRL_OPTION_SHIFT);
    ARISR_proto_ctrl_setField(ctrl, data->ctrl.sequence, ARISR_CTRL_SEQUENCE_SHIFT);
    ARISR_proto_ctrl_setField(ctrl, data->ctrl.retry, ARISR_CTRL_RETRY_SHIFT);
    ARISR_proto_ctrl_setField(ctrl, data->ctrl.more_data, ARISR_CTRL_MD_SHIFT);
    ARISR_proto_ctrl_setField(ctrl, data->ctrl.identifier, ARISR_CTRL_ID_SHIFT);
    ARISR_proto_ctrl_setField(ctrl, data->ctrl.more_header, ARISR_CTRL_MH_SHIFT);

    memcpy(*buffer + p, ctrl, ARISR_CTRL_SECTION_SIZE);
    p += ARISR_CTRL_SECTION_SIZE;

    /* =============== ORIGIN & DESTINATION ================= */
    memcpy(*buffer + p, data->origin, ARISR_ADDRESS_SIZE * 2);
    p += ARISR_ADDRESS_SIZE * 2;

    /* =============== DESTINATIONS B ================= */
    if (data->ctrl.destinations > 0) {
        size = data->ctrl.destinations * ARISR_ADDRESS_SIZE;
        memcpy(*buffer + p, data->destinationsB, size);
        p += size;
    }

    /* =============== DESTINATION C ================= */
    if (data->ctrl.from) {
        memcpy(*buffer + p, data->destinationC, ARISR_ADDRESS_SIZE);
        p += ARISR_ADDRESS_SIZE;
    }

    /* =============== CTRL 2 ================= */
    if (data->ctrl.more_header) {
        // Write the data length
        memset(ctrl, '\0', ARISR_CTRL2_SECTION_SIZE);
        ARISR_proto_ctrl_setField(ctrl, encrypted_length / ARISR_DATA_MULT, ARISR_CTRL2_DATA_LENGTH_SHIFT);
        ARISR_proto_ctrl_setField(ctrl, data->ctrl2.feature, ARISR_CTRL2_FEATURE_SHIFT);
        ARISR_proto_ctrl_setField(ctrl, data->ctrl2.neg_answer, ARISR_CTRL2_NEG_ANSWER_SHIFT);
        ARISR_proto_ctrl_setField(ctrl, data->ctrl2.freq_switch, ARISR_CTRL2_FREQ_SWITCH_SHIFT);

        memcpy(*buffer + p, ctrl, ARISR_CTRL2_SECTION_SIZE);
        p += ARISR_CTRL2_SECTION_SIZE;
    }

    /* =============== CRC HEADER ================= */
    // Calculate CRC over the entire header portion from index 0 to p-1
    crc = ARISR_crypt_crc16_calculate(*buffer, p);
    (*buffer)[p++] = (ARISR_UINT8)(crc >> 8) & 0xFF;
    (*buffer)[p++] = (ARISR_UINT8)(crc) & 0xFF;

    /* =============== DATA ================= */
    memcpy(*buffer + p, encrypted_data, encrypted_length);
    p += encrypted_length;

    /* =============== CRC DATA ================= */
    if (data->ctrl.more_header && data->ctrl2.data_length > 0) {
        // Calculate CRC over the entire data portion from index 0 to p-1
        crc = ARISR_crypt_crc16_calculate(encrypted_data, encrypted_length);
        (*buffer)[p++] = (ARISR_UINT8)(crc >> 8) & 0xFF;
        (*buffer)[p++] = (ARISR_UINT8)(crc) & 0xFF;
    }

    /* =============== END ================= */
    // Write the end field
    memcpy(*buffer + p, data->id, ARISR_PROTO_ID_SIZE);

    // Free the encrypted data
    free(encrypted_data);

    return kARISR_OK;
}

/**
 * @brief Those functions are used step by step to pack, send, receive and unpack the data.
 * 
 * If you need this functios, please consider to define ARISR_PROTO_PARTIAL_FUNCTIONS
 * 
 * Otherwise you can use the function ARISR_proto_parse to do all the process in one function.
 * And also you can use the function ARISR_proto_build to do all the process in one function.
 */
#if defined(ARISR_PROTO_PARTIAL_FUNCTIONS)

// =============================================
ARISR_ERR ARISR_proto_recv(ARISR_CHUNK_RAW *buffer, const ARISR_UINT8 *data, const ARISR_AES128_KEY key, ARISR_UINT8 *id)
{
    if (!buffer || !data) {
        return kARISR_ERR_GENERIC;
    }

    // Limpiar la estructura antes de usarla
    memset(buffer, 0, sizeof(ARISR_CHUNK_RAW));

    unsigned int p = 0;
    ARISR_UINT8 destinations, from_relay, more_headers, data_length;
    ARISR_UINT16 crc;

    memcpy(buffer->id, data, ARISR_PROTO_CRYPT_SIZE);

    if (memcmp(buffer->id, id, ARISR_PROTO_ID_SIZE) != 0) {
        return kARISR_ERR_NOT_SAME_ID;
    }

    if (ARISR_aes_aris_decrypt(key, buffer->aris) != kARISR_OK) {
        return kARISR_ERR_NOT_SAME_ARIS;
    }

    p += ARISR_PROTO_CRYPT_SIZE;

    memcpy(buffer->ctrl, data + p, ARISR_CTRL_SECTION_SIZE);
    p += ARISR_CTRL_SECTION_SIZE;

    destinations = ARISR_proto_ctrl_getField(buffer->ctrl, ARISR_CTRL_DESTS_MASK, ARISR_CTRL_DESTS_SHIFT);
    from_relay   = ARISR_proto_ctrl_getField(buffer->ctrl, ARISR_CTRL_FROM_MASK, ARISR_CTRL_FROM_SHIFT);
    more_headers = ARISR_proto_ctrl_getField(buffer->ctrl, ARISR_CTRL_MH_MASK, ARISR_CTRL_MH_SHIFT);

    memcpy(buffer->origin, data + p, ARISR_ADDRESS_SIZE * 2);
    p += ARISR_ADDRESS_SIZE * 2;

    if (destinations > 0) {
        buffer->destinationsB = (ARISR_UINT8 (*)[ARISR_ADDRESS_SIZE]) malloc(destinations * ARISR_ADDRESS_SIZE);
        if (!buffer->destinationsB) {
            ARISR_RAW_CLEAN_AND_RETURN(kARISR_ERR_GENERIC);
        }
        memcpy(buffer->destinationsB, data + p, destinations * ARISR_ADDRESS_SIZE);
        p += destinations * ARISR_ADDRESS_SIZE;
    }

    if (from_relay) {
        buffer->destinationC = (ARISR_UINT8*)malloc(ARISR_ADDRESS_SIZE);
        if (!buffer->destinationC) {
            ARISR_RAW_CLEAN_AND_RETURN(kARISR_ERR_GENERIC);
        }
        memcpy(buffer->destinationC, data + p, ARISR_ADDRESS_SIZE);
        p += ARISR_ADDRESS_SIZE;
    }

    if (more_headers) {
        buffer->ctrl2 = (ARISR_UINT8*)malloc(ARISR_CTRL2_SECTION_SIZE);
        if (!buffer->ctrl2) {
            ARISR_RAW_CLEAN_AND_RETURN(kARISR_ERR_GENERIC);
        }
        memcpy(buffer->ctrl2, data + p, ARISR_CTRL2_SECTION_SIZE);
        p += ARISR_CTRL2_SECTION_SIZE;
    } else {
        buffer->ctrl2 = NULL;
    }

    memcpy(buffer->crc_header, data + p, ARISR_CRC_SIZE);

    ARISR_UINT16 expected_crc_header = ((ARISR_UINT16)buffer->crc_header[0] << 8) | buffer->crc_header[1];

    crc = ARISR_crypt_crc16_calculate((const ARISR_UINT8*)data, p);
    if (crc != expected_crc_header) {
        ARISR_RAW_CLEAN_AND_RETURN(kARISR_ERR_NOT_SAME_CRC_HEADER);
    }
    p += ARISR_CRC_SIZE;

    if (more_headers && buffer->ctrl2) {
        data_length = ARISR_proto_ctrl_getField(buffer->ctrl2, ARISR_CTRL2_DATA_LENGTH_MASK, ARISR_CTRL2_DATA_LENGTH_SHIFT);
        data_length *= ARISR_DATA_MULT;

        if (data_length > 0) {
            buffer->data = (ARISR_UINT8*)malloc(data_length);
            if (!buffer->data) {
                ARISR_RAW_CLEAN_AND_RETURN(kARISR_ERR_GENERIC);
            }
            memcpy(buffer->data, data + p, data_length);
            p += data_length;

            memcpy(buffer->crc_data, data + p, ARISR_CRC_SIZE);
            ARISR_UINT16 expected_crc_data = ((ARISR_UINT16)buffer->crc_data[0] << 8) | buffer->crc_data[1];

            crc = ARISR_crypt_crc16_calculate(buffer->data, data_length);
            if (crc != expected_crc_data) {
                ARISR_RAW_CLEAN_AND_RETURN(kARISR_ERR_NOT_SAME_CRC_DATA);
            }
            p += ARISR_CRC_SIZE;
        }
    }

    memcpy(buffer->end, data + p, ARISR_PROTO_ID_SIZE);

    if (memcmp(buffer->end, id, ARISR_PROTO_ID_SIZE) != 0) {
        ARISR_RAW_CLEAN_AND_RETURN(kARISR_ERR_NOT_SAME_END);
    }

    return kARISR_OK;
}

// =============================================
ARISR_ERR ARISR_proto_unpack(ARISR_CHUNK *buffer, ARISR_CHUNK_RAW *data, const ARISR_AES128_KEY key)
{
    ARISR_ERR err;

    if (!buffer || !data) {
        return kARISR_ERR_GENERIC;
    }

    // Control check previous to unpacking
    // ID
    // ARIS
    // CRC Header
    // CRC Data

    memset(buffer, 0, sizeof(ARISR_CHUNK));

    // Copy the ID and ARIS fields
    memcpy(buffer->id, data->id, ARISR_PROTO_ID_SIZE);
    memcpy(buffer->aris, data->aris, ARISR_PROTO_ARIS_SIZE);

    // Create struct ARISR_CTRL_CHUNK and copy the control section
    buffer->ctrl.version = ARISR_proto_ctrl_getField(data->ctrl, ARISR_CTRL_VERSION_MASK, ARISR_CTRL_VERSION_SHIFT);
    buffer->ctrl.destinations = ARISR_proto_ctrl_getField(data->ctrl, ARISR_CTRL_DESTS_MASK, ARISR_CTRL_DESTS_SHIFT);
    buffer->ctrl.option = ARISR_proto_ctrl_getField(data->ctrl, ARISR_CTRL_OPTION_MASK, ARISR_CTRL_OPTION_SHIFT);
    buffer->ctrl.from = ARISR_proto_ctrl_getField(data->ctrl, ARISR_CTRL_FROM_MASK, ARISR_CTRL_FROM_SHIFT);
    buffer->ctrl.sequence = ARISR_proto_ctrl_getField(data->ctrl, ARISR_CTRL_SEQUENCE_MASK, ARISR_CTRL_SEQUENCE_SHIFT);
    buffer->ctrl.retry = ARISR_proto_ctrl_getField(data->ctrl, ARISR_CTRL_RETRY_MASK, ARISR_CTRL_RETRY_SHIFT);
    buffer->ctrl.more_data = ARISR_proto_ctrl_getField(data->ctrl, ARISR_CTRL_MD_MASK, ARISR_CTRL_MD_SHIFT);
    buffer->ctrl.identifier = ARISR_proto_ctrl_getField(data->ctrl, ARISR_CTRL_ID_MASK, ARISR_CTRL_ID_SHIFT);
    buffer->ctrl.more_header = ARISR_proto_ctrl_getField(data->ctrl, ARISR_CTRL_MH_MASK, ARISR_CTRL_MH_SHIFT);

    // Copy the origin and destinationA fields
    memcpy(buffer->origin, data->origin, ARISR_ADDRESS_SIZE);
    memcpy(buffer->destinationA, data->destinationA, ARISR_ADDRESS_SIZE);

    // Copy the destinationsB array if allocated
    if (buffer->ctrl.destinations > 0 && data->destinationsB) {

        buffer->destinationsB = (ARISR_UINT8 (*)[ARISR_ADDRESS_SIZE])
                                 malloc(buffer->ctrl.destinations * (sizeof(ARISR_UINT8) * ARISR_ADDRESS_SIZE));
        if (!buffer->destinationsB) {
            ARISR_CLEAN_AND_RETURN(kARISR_ERR_GENERIC);
        }
        memcpy(buffer->destinationsB, data->destinationsB, buffer->ctrl.destinations * ARISR_ADDRESS_SIZE);
    }

    // Copy the destinationC field if allocated
    if (buffer->ctrl.from && data->destinationC) {
        memcpy(buffer->destinationC, data->destinationC, ARISR_ADDRESS_SIZE);
    }

    // Copy the control section 2 if allocated
    if (buffer->ctrl.more_header && data->ctrl2) {
        // Real data length is 'data_length' as n * ARISR_DATA_MULT Bytes
        buffer->ctrl2.data_length = ARISR_proto_ctrl_getField(data->ctrl2, ARISR_CTRL2_DATA_LENGTH_MASK, ARISR_CTRL2_DATA_LENGTH_SHIFT) * ARISR_DATA_MULT;
        buffer->ctrl2.feature = ARISR_proto_ctrl_getField(data->ctrl2, ARISR_CTRL2_FEATURE_MASK, ARISR_CTRL2_FEATURE_SHIFT);
        buffer->ctrl2.neg_answer = ARISR_proto_ctrl_getField(data->ctrl2, ARISR_CTRL2_NEG_ANSWER_MASK, ARISR_CTRL2_NEG_ANSWER_SHIFT);
        buffer->ctrl2.freq_switch = ARISR_proto_ctrl_getField(data->ctrl2, ARISR_CTRL2_FREQ_SWITCH_MASK, ARISR_CTRL2_FREQ_SWITCH_SHIFT);
    }

    // Copy the CRC header
    memcpy(buffer->crc_header, data->crc_header, ARISR_CRC_SIZE);

    // Copy the CRC data
    if (buffer->ctrl2.data_length > 0 && data->data) {
        memcpy(buffer->crc_data, data->crc_data, ARISR_CRC_SIZE);
        
        // Data
        ARISR_UINT8 *decrypted_data;
        ARISR_UINT32 decrypted_length;
        // Decrypt the data section using AES key
        if ((err = ARISR_aes_data_decrypt(
            (!ARISR_AES_IS_ZERO_KEY(key)) ? key : ARISR_DEFAULT_NULL_KEY
            , data->data, buffer->ctrl2.data_length, &decrypted_data, &decrypted_length)) != kARISR_OK) {

            ARISR_CLEAN_AND_RETURN(err);
        }

        // Copy the decrypted data
        free(buffer->data);
        buffer->data = decrypted_data;
        buffer->ctrl2.data_length = decrypted_length;
    }

    // Copy the end fieldç
    memcpy(buffer->end, data->end, ARISR_PROTO_ID_SIZE);    

    return kARISR_OK;
}

// =============================================
ARISR_ERR ARISR_proto_pack(ARISR_CHUNK_RAW *buffer, ARISR_CHUNK *data, const ARISR_AES128_KEY key)
{
    ARISR_ERR err;

    if (!buffer || !data) {
        return kARISR_ERR_GENERIC;
    }

    // Copy the ID and ARIS fields
    memcpy(buffer->id, data->id, ARISR_PROTO_ID_SIZE);
    memcpy(buffer->aris, data->aris, ARISR_PROTO_ARIS_SIZE);

    // Encrypt the 'aris' field using the last byte of the key
    // If not set, use the default ARIS
    if (buffer->aris[0] == '\0') {
        memcpy(buffer->aris, ARISR_PROTO_ARIS_TEXT, ARISR_PROTO_ARIS_SIZE);
    } 
    if ((err = ARISR_aes_aris_encrypt(key, buffer->aris)) != kARISR_OK) {
        return err;
    }

    // Check origin and destinationA fields
    if (memcmp(data->origin, (ARISR_UINT8[ARISR_ADDRESS_SIZE]){0}, ARISR_ADDRESS_SIZE) == 0) {
        return kARISR_ERR_NULL_ORIGIN;
    }

    if (memcmp(data->destinationA, (ARISR_UINT8[ARISR_ADDRESS_SIZE]){0}, ARISR_ADDRESS_SIZE) == 0) {
        return kARISR_ERR_NULL_DESTINATION;
    }

    // Version
    ARISR_proto_ctrl_setField(buffer->ctrl, data->ctrl.version, ARISR_CTRL_VERSION_SHIFT);
    // Destinations
    ARISR_proto_ctrl_setField(buffer->ctrl, data->ctrl.destinations, ARISR_CTRL_DESTS_SHIFT);
    // Option
    ARISR_proto_ctrl_setField(buffer->ctrl, data->ctrl.option, ARISR_CTRL_OPTION_SHIFT);
    // From
    ARISR_proto_ctrl_setField(buffer->ctrl, data->ctrl.from, ARISR_CTRL_FROM_SHIFT);
    // Sequence
    ARISR_proto_ctrl_setField(buffer->ctrl, data->ctrl.sequence, ARISR_CTRL_SEQUENCE_SHIFT);
    // Retry
    ARISR_proto_ctrl_setField(buffer->ctrl, data->ctrl.retry, ARISR_CTRL_RETRY_SHIFT);
    // More data
    ARISR_proto_ctrl_setField(buffer->ctrl, data->ctrl.more_data, ARISR_CTRL_MD_SHIFT);
    // Identifier
    ARISR_proto_ctrl_setField(buffer->ctrl, data->ctrl.identifier, ARISR_CTRL_ID_SHIFT);
    // More header
    ARISR_proto_ctrl_setField(buffer->ctrl, data->ctrl.more_header, ARISR_CTRL_MH_SHIFT);

    // Copy the origin and destinationA fields
    memcpy(buffer->origin, data->origin, ARISR_ADDRESS_SIZE);
    memcpy(buffer->destinationA, data->destinationA, ARISR_ADDRESS_SIZE);
    
    // Copy the destinationsB array if allocated
    if (data->ctrl.destinations > 0) {
        buffer->destinationsB = (ARISR_UINT8 (*)[ARISR_ADDRESS_SIZE])
                                malloc(data->ctrl.destinations * (sizeof(ARISR_UINT8) * ARISR_ADDRESS_SIZE));
        if (!buffer->destinationsB) {
            ARISR_RAW_CLEAN_AND_RETURN(kARISR_ERR_GENERIC);
        }
        memcpy(buffer->destinationsB, data->destinationsB, data->ctrl.destinations * ARISR_ADDRESS_SIZE);
    }

    // Copy the destinationC field if allocated
    if (data->ctrl.from) {
        buffer->destinationC = (ARISR_UINT8*)malloc(sizeof(ARISR_UINT8) * ARISR_ADDRESS_SIZE);
        if (!buffer->destinationC) {
            ARISR_RAW_CLEAN_AND_RETURN(kARISR_ERR_GENERIC);
        }
        memcpy(buffer->destinationC, data->destinationC, ARISR_ADDRESS_SIZE);
    }

    // Copy ctrl2 if allocated
    if (data->ctrl.more_header) {
        ARISR_UINT32 encrypted_length = 0;
        // Check if data is set
        if (data->ctrl2.data_length > 0 && data->data) {
            // Prepare first data
            ARISR_UINT8 *encrypted_data;
            // Encrypt the data section using AES key
            if ((err = ARISR_aes_data_encrypt(
                (!ARISR_AES_IS_ZERO_KEY(key)) ? key : ARISR_DEFAULT_NULL_KEY
                , data->data, data->ctrl2.data_length, &encrypted_data, &encrypted_length)) != kARISR_OK) {

                ARISR_RAW_CLEAN_AND_RETURN(err);
            }

            // Copy the encrypted data
            free(buffer->data);
            buffer->data = encrypted_data;
        }

        // Arm the control section 2
        buffer->ctrl2 = (ARISR_UINT8*)malloc(sizeof(ARISR_UINT8) * ARISR_CTRL2_SECTION_SIZE);
        if (!buffer->ctrl2) {
            ARISR_RAW_CLEAN_AND_RETURN(kARISR_ERR_GENERIC);
        }
        memset(buffer->ctrl2, 0, ARISR_CTRL2_SECTION_SIZE);
        ARISR_proto_ctrl_setField(buffer->ctrl2, encrypted_length / ARISR_DATA_MULT, ARISR_CTRL2_DATA_LENGTH_SHIFT);
        ARISR_proto_ctrl_setField(buffer->ctrl2, data->ctrl2.feature, ARISR_CTRL2_FEATURE_SHIFT);
        ARISR_proto_ctrl_setField(buffer->ctrl2, data->ctrl2.neg_answer, ARISR_CTRL2_NEG_ANSWER_SHIFT);
        ARISR_proto_ctrl_setField(buffer->ctrl2, data->ctrl2.freq_switch, ARISR_CTRL2_FREQ_SWITCH_SHIFT);
    }

    // Set null CRCs
    memset(buffer->crc_header, 0, ARISR_CRC_SIZE);
    memset(buffer->crc_data, 0, ARISR_CRC_SIZE);

    // Set end field
    memcpy(buffer->end, data->id, ARISR_PROTO_ID_SIZE);


    // End
    return kARISR_OK;
}

// =============================================
ARISR_ERR ARISR_proto_send(ARISR_UINT8 **buffer, ARISR_CHUNK_RAW *data, ARISR_UINT32 *length)
{
    if (!data || !buffer || !length) {
        return kARISR_ERR_GENERIC;
    }

    // Pointer to save size of the buffer
    unsigned int p, size;
    ARISR_UINT8 dst_num, from, more_headers, data_length;
    ARISR_UINT16 crc;

    // Minimun size of the buffer
    size = ARISR_PROTO_ID_SIZE + ARISR_PROTO_ARIS_SIZE + ARISR_CTRL_SECTION_SIZE + ARISR_ADDRESS_SIZE * 2 + ARISR_CRC_SIZE + ARISR_PROTO_ID_SIZE;

    // Calculate the destinations
    dst_num = ARISR_proto_ctrl_getField(data->ctrl, ARISR_CTRL_DESTS_MASK, ARISR_CTRL_DESTS_SHIFT);
    if (dst_num > 0) {
        size += ARISR_ADDRESS_SIZE * dst_num;
    }   

    // Get from
    from = ARISR_proto_ctrl_getField(data->ctrl, ARISR_CTRL_FROM_MASK, ARISR_CTRL_FROM_SHIFT);
    if (from)
        size += ARISR_ADDRESS_SIZE;

    // Get more headers and check data length
    more_headers = ARISR_proto_ctrl_getField(data->ctrl, ARISR_CTRL_MH_MASK, ARISR_CTRL_MH_SHIFT);
    if (more_headers) {
        data_length = ARISR_proto_ctrl_getField(data->ctrl2, ARISR_CTRL2_DATA_LENGTH_MASK, ARISR_CTRL2_DATA_LENGTH_SHIFT) * ARISR_DATA_MULT;
        size += ARISR_CTRL_SECTION_SIZE;

        if (data_length > 0) {
            size += data_length + ARISR_CRC_SIZE;
        }
    }

    // Create the buffer
    *buffer = (ARISR_UINT8*)malloc(sizeof(ARISR_UINT8) * size);

    // Assign the size of the buffer
    *length = size;

    if (!*buffer) {
        return kARISR_ERR_GENERIC;
    }

    // Start writing the buffer
    p = 0;

    size = ARISR_PROTO_CRYPT_SIZE + ARISR_CTRL_SECTION_SIZE + ARISR_ADDRESS_SIZE * 2;

    memcpy(*buffer, data, size);
    p += size;

    // Write the destinationsB array if allocated
    if (dst_num > 0) {
        size = ARISR_ADDRESS_SIZE * dst_num;
        memcpy(*buffer + p, data->destinationsB, size);
        p += size;
    }

    // Write the destinationC field if allocated
    if (from) {
        memcpy(*buffer + p, data->destinationC, ARISR_ADDRESS_SIZE);
        p += ARISR_ADDRESS_SIZE;
    }

    if (more_headers) {
        memcpy(*buffer + p, data->ctrl2, ARISR_CTRL2_SECTION_SIZE);
        p += ARISR_CTRL2_SECTION_SIZE;
    }

    // Calculate CRC for the header
    crc = ARISR_crypt_crc16_calculate(*buffer, p);
    (*buffer)[p++] = (ARISR_UINT8)(crc >> 8) & 0xFF;
    (*buffer)[p++] = (ARISR_UINT8)(crc) & 0xFF;

    // Write the data section if allocated
    if (more_headers && data_length > 0) {
        size = data_length;
        memcpy(*buffer + p, data->data, size);
        p += size;

        // Calculate CRC for the data
        crc = ARISR_crypt_crc16_calculate(data->data, size);
        (*buffer)[p++] = (ARISR_UINT8)(crc >> 8) & 0xFF;
        (*buffer)[p++] = (ARISR_UINT8)(crc) & 0xFF;
    }

    // Write the end field
    memcpy(*buffer + p, data->end, ARISR_PROTO_ID_SIZE);

    return kARISR_OK;
}

#endif // ARISR_PROTO_PARTIAL_FUNCTIONS

/* COPYRIGHT ARIS Alliance */