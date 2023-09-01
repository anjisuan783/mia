//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
// This file is borrowed from srs with some modifications.


#ifndef __MEDIA_PERFORMACE_H__
#define __MEDIA_PERFORMACE_H__

#include <chrono>

namespace ma {
// following annotation is useless

/**
 * the MW(merged-write) send cache time in srs_utime_t.
 * the default value, user can override it in config.
 * to improve send performance, cache msgs and send in a time.
 * for example, cache 500ms videos and audios, then convert all these
 * msgs to iovecs, finally use writev to send.
 * @remark this largely improve performance, from 3.5k+ to 7.5k+.
 *       the latency+ when cache+.
 * @remark the socket send buffer default to 185KB, it large enough.
 * @see https://github.com/ossrs/srs/issues/194
 * @see SrsConfig::get_mw_sleep_ms()
 * @remark the mw sleep and msgs to send, maybe:
 *       mw_sleep        msgs        iovs
 *       350             43          86
 *       400             44          88
 *       500             46          92
 *       600             46          92
 *       700             82          164
 *       800             81          162
 *       900             80          160
 *       1000            88          176
 *       1100            91          182
 *       1200            89          178
 *       1300            119         238
 *       1400            120         240
 *       1500            119         238
 *       1600            131         262
 *       1700            131         262
 *       1800            133         266
 *       1900            141         282
 *       2000            150         300
 */

/**
 * how many chunk stream to cache, [0, N].
 * to imporove about 10% performance when chunk size small, and 5% for large chunk.
 * @see https://github.com/ossrs/srs/issues/249
 * @remark 0 to disable the chunk stream cache.
 */
constexpr int SRS_PERF_CHUNK_STREAM_CACHE = 16;

} //namespace ma

#endif //!__MEDIA_PERFORMACE_H__

