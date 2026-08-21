// Copyright (c) 2004-2026 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#ifndef AS2XXXX_BITFIELDS_H_
#define AS2XXXX_BITFIELDS_H_

#include <stdint.h>

// ------------------------------------------------------------------------------------------------
// Bit fields
// ------------------------------------------------------------------------------------------------

/** @brief Create a 16-bit value with bit set. */
#define BIT16(nr) ((uint16_t)(1u << (nr)))
/** @brief Create a 32-bit value with bit set. */
#define BIT32(nr) ((uint32_t)(1u << (nr)))
/** @brief Return the 0-based position of the least-significant set bit in x (used internally). */
#define __bf_shf(x) (__builtin_ffs(x) - 1)

/** @brief Set or clear the single bit (16-bit). Must be a single-bit mask. */
#define BIT_MODIFY16(reg, mask, val)                                                               \
    ((reg) = ((reg) & ~BIT16(__bf_shf(mask))) | ((val) ? BIT16(__bf_shf(mask)) : 0u))
/** @brief Set or clear the single bit (32-bit). Must be a single-bit mask. */
#define BIT_MODIFY32(reg, mask, val)                                                               \
    ((reg) = ((reg) & ~BIT32(__bf_shf(mask))) | ((val) ? BIT32(__bf_shf(mask)) : 0u))

/** @brief Shift left into the bit field described by mask (16-bit). */
#define FIELD_PREP16(mask, val) (((uint16_t)(val) << __bf_shf(mask)) & (mask))
/** @brief Shift left into the bit field described by mask (32-bit). */
#define FIELD_PREP32(mask, val) (((uint32_t)(val) << __bf_shf(mask)) & (mask))

/** @brief Create a contiguous 16-bit mask spanning bits l through h (inclusive). */
#define GENMASK16(h, l)                                                                            \
    ((uint16_t)((uint16_t)(0xFFFFu >> (15u - (unsigned)(h))) &                                     \
                (uint16_t)(0xFFFFu << (unsigned)(l))))
/** @brief Create a contiguous 32-bit mask spanning bits l through h (inclusive). */
#define GENMASK32(h, l)                                                                            \
    ((uint32_t)((uint32_t)(0xFFFFFFFFu >> (31u - (unsigned)(h))) &                                 \
                (uint32_t)(0xFFFFFFFFu << (unsigned)(l))))

/** @brief Low byte of a 16-bit value. */
#define U16_LOW_BYTE GENMASK16(7, 0)
/** @brief High byte of a 16-bit value. */
#define U16_HIGH_BYTE GENMASK16(15, 8)
/** @brief Low 16-bit word of a 32-bit value. */
#define U32_LOW_WORD GENMASK32(15, 0)
/** @brief High 16-bit word of a 32-bit value. */
#define U32_HIGH_WORD GENMASK32(31, 16)

/** @brief Extract the bit field described by mask from reg and right-shift to bit 0 (16-bit). */
#define FIELD_GET16(mask, reg) ((uint16_t)(((reg) & (mask)) >> __bf_shf(mask)))
/** @brief Extract the bit field described by mask from reg and right-shift to bit 0 (32-bit). */
#define FIELD_GET32(mask, reg) ((uint32_t)(((reg) & (mask)) >> __bf_shf(mask)))

#endif /* AS2XXXX_BITFIELDS_H_ */
