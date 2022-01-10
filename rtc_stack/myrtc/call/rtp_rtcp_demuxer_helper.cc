/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "call/rtp_rtcp_demuxer_helper.h"

#include "rtp_rtcp/byte_io.h"
#include "rtp_rtcp/bye.h"
#include "rtp_rtcp/common_header.h"
#include "rtp_rtcp/extended_reports.h"
#include "rtp_rtcp/psfb.h"
#include "rtp_rtcp/receiver_report.h"
#include "rtp_rtcp/rtpfb.h"
#include "rtp_rtcp/sender_report.h"

namespace webrtc {

std::optional<uint32_t> ParseRtcpPacketSenderSsrc(
    rtc::ArrayView<const uint8_t> packet) {
  rtcp::CommonHeader header;
  for (const uint8_t* next_packet = packet.begin(); next_packet < packet.end();
       next_packet = header.NextPacket()) {
    if (!header.Parse(next_packet, packet.end() - next_packet)) {
      return std::nullopt;
    }

    switch (header.type()) {
      case rtcp::Bye::kPacketType:
      case rtcp::ExtendedReports::kPacketType:
      case rtcp::Psfb::kPacketType:
      case rtcp::ReceiverReport::kPacketType:
      case rtcp::Rtpfb::kPacketType:
      case rtcp::SenderReport::kPacketType: {
        // Sender SSRC at the beginning of the RTCP payload.
        if (header.payload_size_bytes() >= sizeof(uint32_t)) {
          const uint32_t ssrc_sender =
              ByteReader<uint32_t>::ReadBigEndian(header.payload());
          return ssrc_sender;
        } else {
          return std::nullopt;
        }
      }
    }
  }

  return std::nullopt;
}

}  // namespace webrtc
