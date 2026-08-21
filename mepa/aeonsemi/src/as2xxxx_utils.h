// Copyright (c) 2004-2026 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#ifndef AS2XXXX_UTILS_H_
#define AS2XXXX_UTILS_H_

#include "mepa_os_linux.h"
#include "mepa_utils.h" // For the T_D/T_I/T_W/T_E trace macros

// ------------------------------------------------------------------------------------------------
// Logging modules
// ------------------------------------------------------------------------------------------------

/** @brief Per-module log enable and line tag. The MEPA API layer is not a module,
 *  it logs unconditionally through the MEPA T_D/T_I/T_W/T_E macros. */
#define AS2XXXX_LOG_ENABLE_PRIV 0
#define AS2XXXX_LOG_ENABLE_IPC  0

/** @brief 0: log through the MEPA trace system, filtered by the trace level set over the CLI.
 *         1: print to stdout unconditionally, no CLI setup needed per boot. */
#define AS2XXXX_LOG_TO_PRINTF 0

// ------------------------------------------------------------------------------------------------
// Log sink
// ------------------------------------------------------------------------------------------------

#if (AS2XXXX_LOG_TO_PRINTF == 1)

#include <stdio.h>

/** @brief printf back end. The trace group is accepted and ignored, so the emitters
 *  stay interchangeable with the MEPA ones. */
#define AS2XXXX_PRINTF(_lvl, _grp, fmt, ...)                                                       \
    do {                                                                                           \
        printf("AS2XXXX [" _lvl "] %s:%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__);          \
        fflush(stdout);                                                                            \
    } while (0)

#define AS2XXXX_T_D(_grp, fmt, ...) AS2XXXX_PRINTF("D", _grp, fmt, ##__VA_ARGS__)
#define AS2XXXX_T_I(_grp, fmt, ...) AS2XXXX_PRINTF("I", _grp, fmt, ##__VA_ARGS__)
#define AS2XXXX_T_W(_grp, fmt, ...) AS2XXXX_PRINTF("W", _grp, fmt, ##__VA_ARGS__)
#define AS2XXXX_T_E(_grp, fmt, ...) AS2XXXX_PRINTF("E", _grp, fmt, ##__VA_ARGS__)

#else

#define AS2XXXX_T_D(_grp, fmt, ...) T_D(_grp, fmt, ##__VA_ARGS__)
#define AS2XXXX_T_I(_grp, fmt, ...) T_I(_grp, fmt, ##__VA_ARGS__)
#define AS2XXXX_T_W(_grp, fmt, ...) T_W(_grp, fmt, ##__VA_ARGS__)
#define AS2XXXX_T_E(_grp, fmt, ...) T_E(_grp, fmt, ##__VA_ARGS__)

#endif /* AS2XXXX_LOG_TO_PRINTF */

// ------------------------------------------------------------------------------------------------
// Central logger
// ------------------------------------------------------------------------------------------------

/** @brief Emit one message for a module. Implementation of AS2XXXX_LOG_D/I/W/E. */
#define AS2XXXX_LOG_EMIT(_mod, _emit, fmt, ...)                                                    \
    do {                                                                                           \
        if (AS2XXXX_LOG_ENABLE_##_mod == 1) {                                                      \
            _emit(MEPA_TRACE_GRP_GEN, fmt, ##__VA_ARGS__);                                         \
        }                                                                                          \
    } while (0)

/** @brief Log for a module, one macro per severity. A source file binds its module
 *  once: #define LOG_E(fmt, ...) AS2XXXX_LOG_E(PRIV, fmt, ##__VA_ARGS__) */
#define AS2XXXX_LOG_D(_mod, fmt, ...) AS2XXXX_LOG_EMIT(_mod, AS2XXXX_T_D, fmt, ##__VA_ARGS__)
#define AS2XXXX_LOG_I(_mod, fmt, ...) AS2XXXX_LOG_EMIT(_mod, AS2XXXX_T_I, fmt, ##__VA_ARGS__)
#define AS2XXXX_LOG_W(_mod, fmt, ...) AS2XXXX_LOG_EMIT(_mod, AS2XXXX_T_W, fmt, ##__VA_ARGS__)
#define AS2XXXX_LOG_E(_mod, fmt, ...) AS2XXXX_LOG_EMIT(_mod, AS2XXXX_T_E, fmt, ##__VA_ARGS__)

// ------------------------------------------------------------------------------------------------
// Return code handling
// ------------------------------------------------------------------------------------------------

/** @brief Evaluate EXPR. On failure log at error level for a module and return the code. */
// Assigns to a local named "rc", which the enclosing function must declare - the value is what
// gets returned, so a function using this macro cannot keep its return code under another name.
#define AS2XXXX_RC_LOG(_mod, EXPR, MSG, ...)                                                       \
    do {                                                                                           \
        rc = (EXPR);                                                                               \
        if (rc != MEPA_RC_OK) {                                                                    \
            AS2XXXX_LOG_E(_mod, MSG " (rc=%d)", ##__VA_ARGS__, (int)rc);                           \
            return rc;                                                                             \
        }                                                                                          \
    } while (0)

// ------------------------------------------------------------------------------------------------
// Misc
// ------------------------------------------------------------------------------------------------

/** @brief Sleeps for a given number of microseconds */
#define sleep_us(us) MEPA_NSLEEP((us) * 1000)
/** @brief Returns a size of an array */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))
/** @brief Indicates unused variable and suppress any compiler warnings */
#define UNUSED(var) ((void)var)
/** @brief Stringify helpers, for building compile-time format strings */
#define STRINGIFY_(x) #x
#define STRINGIFY(x)  STRINGIFY_(x)

/** @brief Returns MEPA_RC_ERR_PARM if ptr is NULL. Logged unconditionally: a NULL
 *  argument is a violation, not module diagnostics. */
#define NULL_CHECK(ptr)                                                                            \
    do {                                                                                           \
        if (ptr == NULL) {                                                                         \
            AS2XXXX_T_E(MEPA_TRACE_GRP_GEN, #ptr " ptr is NULL");                                  \
            return MEPA_RC_ERR_PARM;                                                               \
        }                                                                                          \
    } while (0)

#endif /* AS2XXXX_UTILS_H_ */
