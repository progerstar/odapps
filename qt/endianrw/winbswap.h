/*
 * MinGW does not have byteswap.
 */
#ifndef BSWAP_H
#define BSWAP_H

/* ex: set ts=4 et: */
/*
 * Portable definitions for byte-swapping
 *
 * Copyright (c) 2012 Ryan Flynn <parseerror+bswap@gmail.com> <parseerror.com> <rflynn.com>
 *
 * uppercase macros suitable for compile-time constants
 * lowercase functions for run-time
 *
 * byte-swapping:
 *  BSWAP32(x), BSWAP64(x)
 *  bswap32(x), bswap64(x)
 *
 * endianness:
 *  LITTLEENDIAN32(x), LITTLEENDIAN64(x)
 *  BIGENDIAN32(x), BIGENDIAN64(x)
 */

/* use efficient builtins if possible... */

#if defined(__GNUC__) || defined(__clang__)
#   ifdef __has_builtin
#       if __has_builtin(__builtin_bswap32)
#           define WINBSWAP32(x) __builtin_bswap32(x)
#       endif
#       if __has_builtin(__builtin_bswap64)
#           define WINBSWAP64(x) __builtin_bswap64(x)
#       endif
#   endif
#elif defined(__INTEL_COMPILER)
    /* TODO: */
#endif

/* fall back to doing things the hard (slow) way */

#ifndef WINBSWAP32
#define WINBSWAP32(x)                                     \
    ((((uint32_t)(x) & 0x000000FF) << 24) |            \
     (((uint32_t)(x) & 0x0000FF00) << 8)  |            \
     (((uint32_t)(x) >> 8) & 0x0000FF00)  |            \
     (((uint32_t)(x) >> 24) & 0x000000FF))
#endif

#ifndef WINBSWAP64
#define WINBSWAP64(x)                                     \
    (((uint64_t)(x) << 56) |                           \
     (((uint64_t)(x) << 40) & 0X00FF000000000000ULL) | \
     (((uint64_t)(x) << 24) & 0X0000FF0000000000ULL) | \
     (((uint64_t)(x) << 8)  & 0X000000FF00000000ULL) | \
     (((uint64_t)(x) >> 8)  & 0X00000000FF000000ULL) | \
     (((uint64_t)(x) >> 24) & 0X0000000000FF0000ULL) | \
     (((uint64_t)(x) >> 40) & 0X000000000000FF00ULL) | \
     ((uint64_t)(x)  >> 56))
#endif

#endif // BSWAP_H
