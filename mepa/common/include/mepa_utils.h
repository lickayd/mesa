// Copyright (c) 2004-2021 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: MIT

#ifndef MICROCHIP_ETHERNET_PHY_API_TRACE_H
#define MICROCHIP_ETHERNET_PHY_API_TRACE_H

#include "mepa_driver.h"

#if defined(MEPA_OPSYS_VELOCITYSP)

#include <stdbool.h>

#define MEPA_TRACE_TYPES                                                                           \
    MEPA_TRACE_TYPE_X(uint64_t, lmu_fmt_u64, mepa_trace_single_u64, mepa_trace_first_u64,          \
                      mepa_trace_last_u64)                                                         \
    MEPA_TRACE_TYPE_X(uint32_t, lmu_fmt_u32, mepa_trace_single_u32, mepa_trace_first_u32,          \
                      mepa_trace_last_u32)                                                         \
    MEPA_TRACE_TYPE_X(uint16_t, lmu_fmt_u16, mepa_trace_single_u16, mepa_trace_first_u16,          \
                      mepa_trace_last_u16)                                                         \
    MEPA_TRACE_TYPE_X(uint8_t, lmu_fmt_u8, mepa_trace_single_u8, mepa_trace_first_u8,              \
                      mepa_trace_last_u8)                                                          \
    MEPA_TRACE_TYPE_X(int64_t, lmu_fmt_i64, mepa_trace_single_i64, mepa_trace_first_i64,           \
                      mepa_trace_last_i64)                                                         \
    MEPA_TRACE_TYPE_X(int32_t, lmu_fmt_i32, mepa_trace_single_i32, mepa_trace_first_i32,           \
                      mepa_trace_last_i32)                                                         \
    MEPA_TRACE_TYPE_X(int16_t, lmu_fmt_i16, mepa_trace_single_i16, mepa_trace_first_i16,           \
                      mepa_trace_last_i16)                                                         \
    MEPA_TRACE_TYPE_X(int8_t, lmu_fmt_i8, mepa_trace_single_i8, mepa_trace_first_i8,               \
                      mepa_trace_last_i8)                                                          \
    MEPA_TRACE_TYPE_X(char, lmu_fmt_char, mepa_trace_single_char, mepa_trace_first_char,           \
                      mepa_trace_last_char)                                                        \
    MEPA_TRACE_TYPE_X(char *, lmu_fmt_czstr, mepa_trace_single_czstr, mepa_trace_first_czstr,      \
                      mepa_trace_last_czstr)                                                       \
    MEPA_TRACE_TYPE_X(lmu_str_t *, lmu_fmt_str, mepa_trace_single_str, mepa_trace_first_str,       \
                      mepa_trace_last_str)                                                         \
    MEPA_TRACE_TYPE_X(lmu_cstr_t *, lmu_fmt_cstr, mepa_trace_single_cstr, mepa_trace_first_cstr,   \
                      mepa_trace_last_cstr)                                                        \
    MEPA_TRACE_TYPE_X(lm_ipv4_t *, lmu_fmt_ipv4, mepa_trace_single_ipv4, mepa_trace_first_ipv4,    \
                      mepa_trace_last_ipv4)                                                        \
    MEPA_TRACE_TYPE_X(lm_ipv4_prefix_t *, lmu_fmt_ipv4_prefix, mepa_trace_single_ipv4_prefix,      \
                      mepa_trace_first_ipv4_prefix, mepa_trace_last_ipv4_prefix)                   \
    MEPA_TRACE_TYPE_X(lmu_bin4_t *, lmu_fmt_bin4, mepa_trace_single_bin4, mepa_trace_first_bin4,   \
                      mepa_trace_last_bin4)                                                        \
    MEPA_TRACE_TYPE_X(lm_port_list_t *, lmu_fmt_port_list, mepa_trace_single_port_list,            \
                      mepa_trace_first_port_list, mepa_trace_last_port_list)                       \
    MEPA_TRACE_TYPE_X(lm_mac_t *, lmu_fmt_mac, mepa_trace_single_mac, mepa_trace_first_mac,        \
                      mepa_trace_last_mac)

////////////////////////////////////////////////////////////////////////////////
// Declare the the single/first/last prototype for each type.
#define MEPA_TRACE_TYPE_X(TYPE, BASE, SINGLE, FIRST, LAST)                                         \
    void SINGLE(const mepa_trace_group_t group,                    \
                const mepa_trace_level_t level, const char *func, uint32_t line,                  \
                const char *file, const char *fmt, const TYPE val);                                \
    bool FIRST(const mepa_trace_group_t group,                     \
               const mepa_trace_level_t level, const char *fmt, lmu_fmt_state_buf128_t *state,     \
               const TYPE val);                                                                    \
    void LAST(const mepa_trace_group_t group,                      \
              const mepa_trace_level_t level, const char *func, uint32_t line, const char *file,  \
              lmu_fmt_state_t *state, const TYPE val);
MEPA_TRACE_TYPES
#undef MEPA_TRACE_TYPE_X

// Special handling of uintptr_t formating
#if defined(__arm__) && !defined(__linux__)
#define MEPA_TRACE_GENERIC_SINGLE_UINTPTR                                                          \
    uintptr_t:                                                                                     \
    mepa_trace_single_i32,
#define MEPA_TRACE_GENERIC_FIRST_UINTPTR                                                           \
    uintptr_t:                                                                                     \
    mepa_trace_first_i32,
#define MEPA_TRACE_GENERIC_LAST_UINTPTR                                                            \
    uintptr_t:                                                                                     \
    mepa_trace_last_i32,
#else
// Need to be empty to avoid clashing with uint32_t/uint64_t
#define MEPA_TRACE_GENERIC_SINGLE_UINTPTR
#define MEPA_TRACE_GENERIC_FIRST_UINTPTR
#define MEPA_TRACE_GENERIC_LAST_UINTPTR
#endif

////////////////////////////////////////////////////////////////////////////////
// Using the _Generic keyword introduce in C11 to choose a fucntion based on the
// type of an expression.
#define MEPA_TRACE_GENERIC_SINGLE(GRP, LVL, FMT, X)                                           \
    _Generic((X),                                                                                  \
        MEPA_TRACE_GENERIC_SINGLE_UINTPTR uint64_t: mepa_trace_single_u64,                         \
        uint32_t: mepa_trace_single_u32,                                                           \
        uint16_t: mepa_trace_single_u16,                                                           \
        uint8_t: mepa_trace_single_u8,                                                             \
        int64_t: mepa_trace_single_i64,                                                            \
        int32_t: mepa_trace_single_i32,                                                            \
        int16_t: mepa_trace_single_i16,                                                            \
        int8_t: mepa_trace_single_i8,                                                              \
        char: mepa_trace_single_char,                                                              \
        char *: mepa_trace_single_czstr,                                                           \
        const char *: mepa_trace_single_czstr,                                                     \
        lmu_str_t *: mepa_trace_single_str,                                                        \
        lmu_cstr_t *: mepa_trace_single_cstr,                                                      \
        const lmu_cstr_t *: mepa_trace_single_cstr,                                                \
        lm_ipv4_t *: mepa_trace_single_ipv4,                                                       \
        lm_ipv4_prefix_t *: mepa_trace_single_ipv4_prefix,                                         \
        lmu_bin4_t *: mepa_trace_single_bin4,                                                      \
        lm_port_list_t *: mepa_trace_single_port_list,                                             \
        lm_mac_t *: mepa_trace_single_mac)(GRP, LVL, __FILE__, __LINE__, __FUNCTION__, FMT,   \
                                           X)

#define MEPA_TRACE_GENERIC_FIRST(GRP, LVL, FMT, X)                                            \
    _Generic((X),                                                                                  \
        MEPA_TRACE_GENERIC_FIRST_UINTPTR uint64_t: mepa_trace_first_u64,                           \
        uint32_t: mepa_trace_first_u32,                                                            \
        uint16_t: mepa_trace_first_u16,                                                            \
        uint8_t: mepa_trace_first_u8,                                                              \
        int64_t: mepa_trace_first_i64,                                                             \
        int32_t: mepa_trace_first_i32,                                                             \
        int16_t: mepa_trace_first_i16,                                                             \
        int8_t: mepa_trace_first_i8,                                                               \
        char: mepa_trace_first_char,                                                               \
        char *: mepa_trace_first_czstr,                                                            \
        const char *: mepa_trace_first_czstr,                                                      \
        lmu_str_t *: mepa_trace_first_str,                                                         \
        lmu_cstr_t *: mepa_trace_first_cstr,                                                       \
        const lmu_cstr_t *: mepa_trace_first_cstr,                                                 \
        lm_ipv4_t *: mepa_trace_first_ipv4,                                                        \
        lm_ipv4_prefix_t *: mepa_trace_first_ipv4_prefix,                                          \
        lmu_bin4_t *: mepa_trace_first_bin4,                                                       \
        lm_port_list_t *: mepa_trace_first_port_list,                                              \
        lm_mac_t *: mepa_trace_first_mac)(GRP, LVL, FMT, &lmu_fmt_state__, X)

#define MEPA_TRACE_GENERIC_LAST(GRP, LVL, X)                                                  \
    _Generic((X),                                                                                  \
        MEPA_TRACE_GENERIC_LAST_UINTPTR uint64_t: mepa_trace_last_u64,                             \
        uint32_t: mepa_trace_last_u32,                                                             \
        uint16_t: mepa_trace_last_u16,                                                             \
        uint8_t: mepa_trace_last_u8,                                                               \
        int64_t: mepa_trace_last_i64,                                                              \
        int32_t: mepa_trace_last_i32,                                                              \
        int16_t: mepa_trace_last_i16,                                                              \
        int8_t: mepa_trace_last_i8,                                                                \
        char: mepa_trace_last_char,                                                                \
        char *: mepa_trace_last_czstr,                                                             \
        const char *: mepa_trace_last_czstr,                                                       \
        lmu_str_t *: mepa_trace_last_str,                                                          \
        lmu_cstr_t *: mepa_trace_last_cstr,                                                        \
        const lmu_cstr_t *: mepa_trace_last_cstr,                                                  \
        lm_ipv4_t *: mepa_trace_last_ipv4,                                                         \
        lm_ipv4_prefix_t *: mepa_trace_last_ipv4_prefix,                                           \
        lmu_bin4_t *: mepa_trace_last_bin4,                                                        \
        lm_port_list_t *: mepa_trace_last_port_list,                                               \
        lm_mac_t *: mepa_trace_last_mac)(GRP, LVL, __FILE__, __LINE__, __FUNCTION__,          \
                                         &lmu_fmt_state__.state, X)

////////////////////////////////////////////////////////////////////////////////
// PRE-PROCESSOR Iterator over arguments. Will "call" LMU_FMT_GENERIC on each
// argument except the last where it "calls" MEPA_TRACE_GENERIC_LAST
#define MEPA_TRACE_FOREACH_0()
#define MEPA_TRACE_FOREACH_1(GRP, LVL, X) MEPA_TRACE_GENERIC_LAST(GRP, LVL, X);
#define MEPA_TRACE_FOREACH_2(GRP, LVL, X, ...)                                                \
    LMU_FMT_GENERIC(&lmu_fmt_state__.state, X);                                                    \
    MEPA_TRACE_FOREACH_1(GRP, LVL, __VA_ARGS__)
#define MEPA_TRACE_FOREACH_3(GRP, LVL, X, ...)                                                \
    LMU_FMT_GENERIC(&lmu_fmt_state__.state, X);                                                    \
    MEPA_TRACE_FOREACH_2(GRP, LVL, __VA_ARGS__)
#define MEPA_TRACE_FOREACH_4(GRP, LVL, X, ...)                                                \
    LMU_FMT_GENERIC(&lmu_fmt_state__.state, X);                                                    \
    MEPA_TRACE_FOREACH_3(GRP, LVL, __VA_ARGS__)
#define MEPA_TRACE_FOREACH_5(GRP, LVL, X, ...)                                                \
    LMU_FMT_GENERIC(&lmu_fmt_state__.state, X);                                                    \
    MEPA_TRACE_FOREACH_4(GRP, LVL, __VA_ARGS__)
#define MEPA_TRACE_FOREACH_6(GRP, LVL, X, ...)                                                \
    LMU_FMT_GENERIC(&lmu_fmt_state__.state, X);                                                    \
    MEPA_TRACE_FOREACH_5(GRP, LVL, __VA_ARGS__)
#define MEPA_TRACE_FOREACH_7(GRP, LVL, X, ...)                                                \
    LMU_FMT_GENERIC(&lmu_fmt_state__.state, X);                                                    \
    MEPA_TRACE_FOREACH_6(GRP, LVL, __VA_ARGS__)
#define MEPA_TRACE_FOREACH_8(GRP, LVL, X, ...)                                                \
    LMU_FMT_GENERIC(&lmu_fmt_state__.state, X);                                                    \
    MEPA_TRACE_FOREACH_7(GRP, LVL, __VA_ARGS__)
#define MEPA_TRACE_FOREACH_9(GRP, LVL, X, ...)                                                \
    LMU_FMT_GENERIC(&lmu_fmt_state__.state, X);                                                    \
    MEPA_TRACE_FOREACH_8(GRP, LVL, __VA_ARGS__)
#define MEPA_TRACE_FOREACH_10(GRP, LVL, X, ...)                                               \
    LMU_FMT_GENERIC(&lmu_fmt_state__.state, X);                                                    \
    MEPA_TRACE_FOREACH_9(GRP, LVL, __VA_ARGS__)
#define MEPA_TRACE_FOREACH_11(GRP, LVL, X, ...)                                               \
    LMU_FMT_GENERIC(&lmu_fmt_state__.state, X);                                                    \
    MEPA_TRACE_FOREACH_10(GRP, LVL, __VA_ARGS__)
#define MEPA_TRACE_FOREACH_12(GRP, LVL, X, ...)                                               \
    LMU_FMT_GENERIC(&lmu_fmt_state__.state, X);                                                    \
    MEPA_TRACE_FOREACH_11(GRP, LVL, __VA_ARGS__)
#define MEPA_TRACE_FOREACH_13(GRP, LVL, X, ...)                                               \
    LMU_FMT_GENERIC(&lmu_fmt_state__.state, X);                                                    \
    MEPA_TRACE_FOREACH_12(GRP, LVL, __VA_ARGS__)
#define MEPA_TRACE_FOREACH_14(GRP, LVL, X, ...)                                               \
    LMU_FMT_GENERIC(&lmu_fmt_state__.state, X);                                                    \
    MEPA_TRACE_FOREACH_13(GRP, LVL, __VA_ARGS__)
#define MEPA_TRACE_FOREACH_15(GRP, LVL, X, ...)                                               \
    LMU_FMT_GENERIC(&lmu_fmt_state__.state, X);                                                    \
    MEPA_TRACE_FOREACH_14(GRP, LVL, __VA_ARGS__)

// Entry point in the foreach loop to iterate over arguments.
#define MEPA_TRACE_FOREACH(GRP, LVL, FMT, ...)                                                \
    LMU_PP_VA_ARGS_OVERLOAD(MEPA_TRACE_FOREACH_, ##__VA_ARGS__)                                    \
    (GRP, LVL, ##__VA_ARGS__)

#define MEPA_TRACE0(GRP, LVL, FMT, ...)                                                       \
    MEPA_trace(GRP, LVL, __FUNCTION__, __LINE__, __FILE__, FMT)

#define MEPA_TRACE1(GRP, LVL, FMT, X) MEPA_TRACE_GENERIC_SINGLE(GRP, LVL, FMT, X);

#define MEPA_TRACE2(GRP, LVL, FMT, X, ...)                                                    \
    do {                                                                                           \
        lmu_fmt_state_buf128_t lmu_fmt_state__;                                                    \
        if (MEPA_TRACE_GENERIC_FIRST(GRP, LVL, FMT, X)) {                                     \
            MEPA_TRACE_FOREACH(GRP, LVL, FMT, ##__VA_ARGS__)                                  \
        }                                                                                          \
    } while (false)

#define MEPA_TRACE_FMT(GRP, LVL, FMT, ...)                            \
    LMU_PP_VA_ARGS_OVERLOAD_TWO_OR_MORE(MEPA_TRACE, ##__VA_ARGS__)    \
    (GRP, LVL, FMT, ##__VA_ARGS__)

#define MEPA_TRACE_EMIT(grp, lvl, format, ...)                        \
    MEPA_TRACE_FMT(grp, lvl, format, ##__VA_ARGS__)

#define T_N(grp, format, ...) MEPA_TRACE_EMIT((grp), MEPA_TRACE_LVL_NOISE,   format, ##__VA_ARGS__);
#define T_D(grp, format, ...) MEPA_TRACE_EMIT((grp), MEPA_TRACE_LVL_DEBUG,   format, ##__VA_ARGS__);
#define T_I(grp, format, ...) MEPA_TRACE_EMIT((grp), MEPA_TRACE_LVL_INFO,    format, ##__VA_ARGS__);
#define T_W(grp, format, ...) MEPA_TRACE_EMIT((grp), MEPA_TRACE_LVL_WARNING, format, ##__VA_ARGS__);
#define T_E(grp, format, ...) MEPA_TRACE_EMIT((grp), MEPA_TRACE_LVL_ERROR,   format, ##__VA_ARGS__);
#else
#define T_N(grp, format, ...) MEPA_trace((grp), MEPA_TRACE_LVL_NOISE, __FUNCTION__, __LINE__, __FILE__, format, ##__VA_ARGS__);
#define T_D(grp, format, ...) MEPA_trace((grp), MEPA_TRACE_LVL_DEBUG, __FUNCTION__, __LINE__, __FILE__, format, ##__VA_ARGS__);
#define T_I(grp, format, ...) MEPA_trace((grp), MEPA_TRACE_LVL_INFO, __FUNCTION__, __LINE__, __FILE__, format, ##__VA_ARGS__);
#define T_W(grp, format, ...) MEPA_trace((grp), MEPA_TRACE_LVL_WARNING, __FUNCTION__, __LINE__, __FILE__, format, ##__VA_ARGS__);
#define T_E(grp, format, ...) MEPA_trace((grp), MEPA_TRACE_LVL_ERROR, __FUNCTION__, __LINE__, __FILE__, format, ##__VA_ARGS__);
#endif // MEPA_OPSYS_VELOCITYSP

// Debug print
#define pr(fmt_str, ...)                                                                           \
    LMU_PP_VA_ARGS_OVERLOAD_ONE_OR_MORE(LMU_SS_FMT, ##__VA_ARGS__)                                 \
    ((ss), (fmt_str), ##__VA_ARGS__)

#define MEPA_RC(expr) { mepa_rc __rc__ = (expr); if (__rc__ < MEPA_RC_OK) { return __rc__; } }
#define MEPA_RC_GOTO(rc, expr) { { (rc) = (expr); }  if ((rc) != 0) { goto error; } }
#define MEPA_ASSERT(x) if((x)) { return MEPA_RC_ERROR;}

#if defined(MEPA_OPSYS_VELOCITYSP)
#define MEPA_ENTER(dev)
#define MEPA_EXIT(dev)
#else
#define MEPA_ENTER(dev) {                            \
    mepa_lock_t lock;                                \
    lock.function = __FUNCTION__;                    \
    lock.file = __FILE__;                            \
    lock.line = __LINE__;                            \
    if ((dev)->callout->lock_enter != NULL) {        \
        (dev)->callout->lock_enter(&lock);           \
    }                                                \
}

#define MEPA_EXIT(dev) {                             \
    mepa_lock_t lock;                                \
    lock.function = __FUNCTION__;                    \
    lock.file = __FILE__;                            \
    lock.line = __LINE__;                            \
    if ((dev)->callout->lock_exit != NULL) {           \
        (dev)->callout->lock_exit(&lock);              \
    }                                                \
}
#endif

#endif /**< MICROCHIP_ETHERNET_PHY_API_TRACE_H */
