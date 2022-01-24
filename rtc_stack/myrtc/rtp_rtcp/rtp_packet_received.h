/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef MODULES_RTP_RTCP_SOURCE_RTP_PACKET_RECEIVED_H_
#define MODULES_RTP_RTCP_SOURCE_RTP_PACKET_RECEIVED_H_

#include <stdint.h>

#include <vector>

#include "rtc_base/array_view.h"
#include "api/rtp_headers.h"
#include "rtp_rtcp/rtp_packet.h"
#include "rtc_base/ntp_time.h"

namespace webrtc {
// Class to hold rtp packet with metadata for receiver side.
class RtpPacketReceived : public RtpPacket {
 public:
  RtpPacketReceived(bool readonly = false);
  explicit RtpPacketReceived(const ExtensionManager* extensions);
  RtpPacketReceived(const RtpPacketReceived& packet);
  RtpPacketReceived(RtpPacketReceived&& packet);

  RtpPacketReceived& operator=(const RtpPacketReceived& packet);
  RtpPacketReceived& operator=(RtpPacketReceived&& packet);

  ~RtpPacketReceived() override;

  // TODO(danilchap): Remove this function when all code update to use RtpPacket
  // directly. Function is there just for easier backward compatibilty.
  void GetHeader(RTPHeader* header) const;

  // Time in local time base as close as it can to packet arrived on the
  // network.
  int64_t arrival_time_ms() const { return arrival_time_ms_; }
  void set_arrival_time_ms(int64_t time) { arrival_time_ms_ = time; }

  // Estimated from Timestamp() using rtcp Sender Reports.
  NtpTime capture_ntp_time() const { return capture_time_; }
  void set_capture_ntp_time(NtpTime time) { capture_time_ = time; }

  // Flag if packet was recovered via RTX or FEC.
  bool recovered() const { return recovered_; }
  void set_recovered(bool value) { recovered_ = value; }

  int payload_type_frequency() const { return payload_type_frequency_; }
  void set_payload_type_frequency(int value) {
    payload_type_frequency_ = value;
  }

  bool Parse(const uint8_t* buffer, size_t size) override;
  bool Parse(rtc::ArrayView<const uint8_t> packet) override;
  const uint8_t* ReadAt(size_t offset) const override { 
    return view_.data() + offset; 
  }
  const uint8_t* data() const override { return view_.data(); }
 private:
  NtpTime capture_time_;
  int64_t arrival_time_ms_ = 0;
  int payload_type_frequency_ = 0;
  bool recovered_ = false;
  rtc::ArrayView<const uint8_t> view_;
};

}  // namespace webrtc
#endif  // MODULES_RTP_RTCP_SOURCE_RTP_PACKET_RECEIVED_H_

