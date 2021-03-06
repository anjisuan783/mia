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

#define RTMP_SIG_SRS_SERVER "ma server"
#define RTMP_SIG_SRS_VERSION "test"

// Time and duration unit, in us.
typedef int64_t srs_utime_t;

// The time unit in ms, for example 100 * SRS_UTIME_MILLISECONDS means 100ms.
#define SRS_UTIME_MILLISECONDS 1000

// Convert srs_utime_t as ms.
#define srsu2ms(us) ((us) / SRS_UTIME_MILLISECONDS)
#define srsu2msi(us) int((us) / SRS_UTIME_MILLISECONDS)

// The time unit in ms, for example 120 * SRS_UTIME_SECONDS means 120s.
#define SRS_UTIME_SECONDS 1000000LL

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

#define MA_MAX_PACKET_SIZE 16*1024

constexpr int OPUS_SAMPLE_RATE = 48000;
constexpr int OPUS_SAMPLES_PER_MS = OPUS_SAMPLE_RATE / SRS_UTIME_MILLISECONDS;

constexpr int AAC_SAMPLE_RATE = 44100;
constexpr int AAC_SAMPLE_PER_MS = AAC_SAMPLE_RATE / SRS_UTIME_MILLISECONDS;

constexpr int AUDIO_STREAM_BITRATE = 48000;

constexpr int AUDIO_STERO = 2;

constexpr int VIDEO_SAMPLE_RATE = 90000;
constexpr int VIDEO_SAMPLES_PER_MS = 90000 / SRS_UTIME_MILLISECONDS;

}
#endif //!__MEDIA_COMMON_DEFINE_H__

