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

#define SRS_PERF_MW_SLEEP 100 //ms

/**
 * how many msgs can be send entirely.
 * for play clients to get msgs then totally send out.
 * for the mw sleep set to 1800, the msgs is about 133.
 * @remark, recomment to 128.
 */
constexpr int SRS_PERF_MW_MSGS = 10;

} //namespace ma

#endif //!__MEDIA_PERFORMACE_H__

