/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtp_rtcp/playout_delay_oracle.h"

#include <algorithm>

#include "rtp_rtcp/rtp_header_extensions.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"

namespace webrtc {

PlayoutDelayOracle::PlayoutDelayOracle() = default;

PlayoutDelayOracle::~PlayoutDelayOracle() = default;

std::optional<PlayoutDelay> PlayoutDelayOracle::PlayoutDelayToSend(
    PlayoutDelay requested_delay) const {
  if (requested_delay.min_ms > PlayoutDelayLimits::kMaxMs ||
      requested_delay.max_ms > PlayoutDelayLimits::kMaxMs) {
    RTC_DLOG(LS_ERROR)
        << "Requested playout delay values out of range, ignored";
    return std::nullopt;
  }
  if (requested_delay.max_ms != -1 &&
      requested_delay.min_ms > requested_delay.max_ms) {
    RTC_DLOG(LS_ERROR) << "Requested playout delay values out of order";
    return std::nullopt;
  }
  if ((requested_delay.min_ms == -1 ||
       requested_delay.min_ms == latest_delay_.min_ms) &&
      (requested_delay.max_ms == -1 ||
       requested_delay.max_ms == latest_delay_.max_ms)) {
    // Unchanged.
    return unacked_sequence_number_ ? std::make_optional(latest_delay_)
                                    : std::nullopt;
  }
  if (requested_delay.min_ms == -1) {
    RTC_DCHECK_GE(requested_delay.max_ms, 0);
    requested_delay.min_ms =
        std::min(latest_delay_.min_ms, requested_delay.max_ms);
  }
  if (requested_delay.max_ms == -1) {
    requested_delay.max_ms =
        std::max(latest_delay_.max_ms, requested_delay.min_ms);
  }
  return requested_delay;
}

void PlayoutDelayOracle::OnSentPacket(uint16_t sequence_number,
                                      std::optional<PlayoutDelay> delay) {
  int64_t unwrapped_sequence_number = unwrapper_.Unwrap(sequence_number);

  if (!delay) {
    return;
  }

  RTC_DCHECK_LE(0, delay->min_ms);
  RTC_DCHECK_LE(delay->max_ms, PlayoutDelayLimits::kMaxMs);
  RTC_DCHECK_LE(delay->min_ms, delay->max_ms);

  if (delay->min_ms != latest_delay_.min_ms ||
      delay->max_ms != latest_delay_.max_ms) {
    latest_delay_ = *delay;
    unacked_sequence_number_ = unwrapped_sequence_number;
  }
}

// If an ACK is received on the packet containing the playout delay extension,
// we stop sending the extension on future packets.
void PlayoutDelayOracle::OnReceivedAck(
    int64_t extended_highest_sequence_number) {
  if (unacked_sequence_number_ &&
      extended_highest_sequence_number > *unacked_sequence_number_) {
    unacked_sequence_number_ = std::nullopt;
  }
}

}  // namespace webrtc
