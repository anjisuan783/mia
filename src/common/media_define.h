//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
// This file is borrowed from srs with some modifications.

#ifndef __MEDIA_COMMON_DEFINE_H__
#define __MEDIA_COMMON_DEFINE_H__

#include "stdint.h"

namespace ma {

#define RTMP_SIG_SERVER                         "mia server"
#define RTMP_SIG_VERSION                        "test"
#define RTMP_SIG_KEY                            "mia"
#define RTMP_SIG_FMS_VER                        "1.0.0"
#define RTMP_SIG_LICENSE                        "MIT"
#define RTMP_SIG_SRS_AUTHORS                    "anjisuan783@sina.com"

// Time and duration unit, in us.
typedef int64_t srs_utime_t;

// The time unit in ms, for example 100 * SRS_UTIME_MILLISECONDS means 100ms.
#define SRS_UTIME_MILLISECONDS 1000

// Convert srs_utime_t as ms.
#define srsu2ms(us) ((us) / SRS_UTIME_MILLISECONDS)
#define srsu2msi(us) int((us) / SRS_UTIME_MILLISECONDS)

// The time unit in ms, for example 120 * SRS_UTIME_SECONDS means 120s.
#define SRS_UTIME_SECONDS 1000000LL

#define Media_ONE_SECOND_IN_MSECS SRS_UTIME_MILLISECONDS
#define Media_ONE_SECOND_IN_USECS SRS_UTIME_SECONDS

// The time unit in minutes, for example 3 * SRS_UTIME_MINUTES means 3m.
#define SRS_UTIME_MINUTES 60000000LL

// The time unit in hours, for example 2 * SRS_UTIME_HOURS means 2h.
#define SRS_UTIME_HOURS 3600000000LL

// Never timeout.
#define SRS_UTIME_NO_TIMEOUT ((srs_utime_t) -1LL)

#ifdef __GNUC__
# define LIKELY(X) __builtin_expect(!!(X), 1)
# define UNLIKELY(X) __builtin_expect(!!(X), 0)
#else
# define LIKELY(X) (X)
# define UNLIKELY(X) (X)
#endif

// mainly copied from ace/Basic_Types.h
// Byte-order (endian-ness) determination.
# if defined (BYTE_ORDER)
#   if (BYTE_ORDER == LITTLE_ENDIAN)
#     define OS_LITTLE_ENDIAN 0x0123
#     define OS_BYTE_ORDER OS_LITTLE_ENDIAN
#   elif (BYTE_ORDER == BIG_ENDIAN)
#     define OS_BIG_ENDIAN 0x3210
#     define OS_BYTE_ORDER OS_BIG_ENDIAN
#   else
#     error: unknown BYTE_ORDER!
#   endif /* BYTE_ORDER */
# elif defined (_BYTE_ORDER)
#   if (_BYTE_ORDER == _LITTLE_ENDIAN)
#     define OS_LITTLE_ENDIAN 0x0123
#     define OS_BYTE_ORDER OS_LITTLE_ENDIAN
#   elif (_BYTE_ORDER == _BIG_ENDIAN)
#     define OS_BIG_ENDIAN 0x3210
#     define OS_BYTE_ORDER OS_BIG_ENDIAN
#   else
#     error: unknown _BYTE_ORDER!
#   endif /* _BYTE_ORDER */
# elif defined (__BYTE_ORDER)
#   if (__BYTE_ORDER == __LITTLE_ENDIAN)
#     define OS_LITTLE_ENDIAN 0x0123
#     define OS_BYTE_ORDER OS_LITTLE_ENDIAN
#   elif (__BYTE_ORDER == __BIG_ENDIAN)
#     define OS_BIG_ENDIAN 0x3210
#     define OS_BYTE_ORDER OS_BIG_ENDIAN
#   else
#     error: unknown __BYTE_ORDER!
#   endif /* __BYTE_ORDER */
# else /* ! BYTE_ORDER && ! __BYTE_ORDER */
  // We weren't explicitly told, so we have to figure it out . . .
#   if defined (i386) || defined (__i386__) || defined (_M_IX86) || \
     defined (vax) || defined (__alpha) || defined (__LITTLE_ENDIAN__) ||\
     defined (ARM) || defined (_M_IA64)
    // We know these are little endian.
#     define OS_LITTLE_ENDIAN 0x0123
#     define OS_BYTE_ORDER OS_LITTLE_ENDIAN
#   else
    // Otherwise, we assume big endian.
#     define OS_BIG_ENDIAN 0x3210
#     define OS_BYTE_ORDER OS_BIG_ENDIAN
#   endif
# endif /* ! BYTE_ORDER && ! __BYTE_ORDER */

#ifdef WIN32
#	define OS_LL_PREFIX "I64"
#else
#	define OS_LL_PREFIX "ll"
#endif

#define MA_MAX_PACKET_SIZE 16*1024

constexpr int OPUS_SAMPLE_RATE = 48000;
constexpr int OPUS_SAMPLES_PER_MS = OPUS_SAMPLE_RATE / SRS_UTIME_MILLISECONDS;

constexpr int AAC_SAMPLE_RATE = 44100;
constexpr int AAC_SAMPLE_PER_MS = AAC_SAMPLE_RATE / SRS_UTIME_MILLISECONDS;

constexpr int AUDIO_STREAM_BITRATE = 48000;

constexpr int AUDIO_STERO = 2;

constexpr int VIDEO_SAMPLE_RATE = 90000;
constexpr int VIDEO_SAMPLES_PER_MS = 90000 / SRS_UTIME_MILLISECONDS;


typedef int MEDIA_HANDLE;
#define MEDIA_INVALID_HANDLE -1

#define MEDIA_SOCK_IOBUFFER_SIZE 131072

 #if !defined (IOV_MAX)
  #define IOV_MAX 64
 #endif // !IOV_MAX

}
#endif //!__MEDIA_COMMON_DEFINE_H__

