/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtp_rtcp/rtp_packet_received.h"

#include <stddef.h>

#include <cstdint>
#include <vector>

#include "rtp_rtcp/rtp_header_extensions.h"
#include "rtc_base/safe_conversions.h"

namespace webrtc {

RtpPacketReceived::RtpPacketReceived(bool readonly)
    : RtpPacket(true) {
}

RtpPacketReceived::RtpPacketReceived(const ExtensionManager* extensions)
    : RtpPacket(extensions) {}
RtpPacketReceived::RtpPacketReceived(const RtpPacketReceived& packet) = default;
RtpPacketReceived::RtpPacketReceived(RtpPacketReceived&& packet) = default;

RtpPacketReceived& RtpPacketReceived::operator=(
    const RtpPacketReceived& packet) = default;
RtpPacketReceived& RtpPacketReceived::operator=(RtpPacketReceived&& packet) =
    default;

RtpPacketReceived::~RtpPacketReceived() {}

void RtpPacketReceived::GetHeader(RTPHeader* header) const {
  header->markerBit = Marker();
  header->payloadType = PayloadType();
  header->sequenceNumber = SequenceNumber();
  header->timestamp = Timestamp();
  header->ssrc = Ssrc();
  std::vector<uint32_t> csrcs = Csrcs();
  header->numCSRCs = rtc::dchecked_cast<uint8_t>(csrcs.size());
  for (size_t i = 0; i < csrcs.size(); ++i) {
    header->arrOfCSRCs[i] = csrcs[i];
  }
  header->paddingLength = padding_size();
  header->headerLength = headers_size();
  header->payload_type_frequency = payload_type_frequency();
  header->extension.hasTransmissionTimeOffset =
      GetExtension<TransmissionOffset>(
          &header->extension.transmissionTimeOffset);
  header->extension.hasAbsoluteSendTime =
      GetExtension<AbsoluteSendTime>(&header->extension.absoluteSendTime);
  header->extension.absolute_capture_time =
      GetExtension<AbsoluteCaptureTimeExtension>();
  header->extension.hasTransportSequenceNumber =
      GetExtension<TransportSequenceNumberV2>(
          &header->extension.transportSequenceNumber,
          &header->extension.feedback_request) ||
      GetExtension<TransportSequenceNumber>(
          &header->extension.transportSequenceNumber);
  header->extension.hasAudioLevel = GetExtension<AudioLevel>(
      &header->extension.voiceActivity, &header->extension.audioLevel);
  header->extension.hasVideoRotation =
      GetExtension<VideoOrientation>(&header->extension.videoRotation);
  header->extension.hasVideoContentType =
      GetExtension<VideoContentTypeExtension>(
          &header->extension.videoContentType);
  header->extension.has_video_timing =
      GetExtension<VideoTimingExtension>(&header->extension.video_timing);
  header->extension.has_frame_marking =
      GetExtension<FrameMarkingExtension>(&header->extension.frame_marking);
  GetExtension<RtpStreamId>(&header->extension.stream_id);
  GetExtension<RepairedRtpStreamId>(&header->extension.repaired_stream_id);
  GetExtension<RtpMid>(&header->extension.mid);
  GetExtension<PlayoutDelayLimits>(&header->extension.playout_delay);
  header->extension.color_space = GetExtension<ColorSpaceExtension>();
}

bool RtpPacketReceived::Parse(const uint8_t* buffer, size_t buffer_size) {
  if (!ParseBuffer(buffer, buffer_size)) {
    return false;
  }
  view_ = rtc::MakeArrayView(buffer, buffer_size);
  RTC_DCHECK_EQ(size(), buffer_size);
  return true;
}

bool RtpPacketReceived::Parse(rtc::ArrayView<const uint8_t> packet) {
  return Parse(packet.data(), packet.size());
}

}  // namespace webrtc
