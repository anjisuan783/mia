/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_RTP_RTCP_SOURCE_RTP_SENDER_AUDIO_H_
#define MODULES_RTP_RTCP_SOURCE_RTP_SENDER_AUDIO_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include <string_view>
#include "audio/audio_coding_module_typedefs.h"
#include "rtp_rtcp/dtmf_queue.h"
#include "rtp_rtcp/rtp_sender.h"
#include "rtc_base/constructor_magic.h"
#include "rtc_base/one_time_event.h"
#include "rtc_base/thread_annotations.h"
#include "rtc_base/clock.h"

namespace webrtc {

class RTPSenderAudio {
 public:
  RTPSenderAudio(Clock* clock, RTPSender* rtp_sender);
  ~RTPSenderAudio();

  int32_t RegisterAudioPayload(std::string_view payload_name,
                               int8_t payload_type,
                               uint32_t frequency,
                               size_t channels,
                               uint32_t rate);

  bool SendAudio(AudioFrameType frame_type,
                 int8_t payload_type,
                 uint32_t capture_timestamp,
                 const uint8_t* payload_data,
                 size_t payload_size);

  // Store the audio level in dBov for
  // header-extension-for-audio-level-indication.
  // Valid range is [0,100]. Actual value is negative.
  int32_t SetAudioLevel(uint8_t level_dbov);

  // Send a DTMF tone using RFC 2833 (4733)
  int32_t SendTelephoneEvent(uint8_t key, uint16_t time_ms, uint8_t level);

 protected:
  bool SendTelephoneEventPacket(
      bool ended,
      uint32_t dtmf_timestamp,
      uint16_t duration,
      bool marker_bit);  // set on first packet in talk burst

  bool MarkerBit(AudioFrameType frame_type, int8_t payload_type);

 private:
  bool LogAndSendToNetwork(std::unique_ptr<RtpPacketToSend> packet);

  Clock* const clock_ = nullptr;
  RTPSender* const rtp_sender_ = nullptr;

  // DTMF.
  bool dtmf_event_is_on_ = false;
  bool dtmf_event_first_packet_sent_ = false;
  int8_t dtmf_payload_type_ = -1;
  uint32_t dtmf_payload_freq_ = 8000;
  uint32_t dtmf_timestamp_ = 0;
  uint32_t dtmf_length_samples_ = 0;
  int64_t dtmf_time_last_sent_ = 0;
  uint32_t dtmf_timestamp_last_sent_ = 0;
  DtmfQueue::Event dtmf_current_event_;
  DtmfQueue dtmf_queue_;

  // VAD detection, used for marker bit.
  bool inband_vad_active_ = false;
  int8_t cngnb_payload_type_ = -1;
  int8_t cngwb_payload_type_ = -1;
  int8_t cngswb_payload_type_ = -1;
  int8_t cngfb_payload_type_ = -1;
  int8_t last_payload_type_ = -1;

  // Audio level indication.
  // (https://datatracker.ietf.org/doc/draft-lennox-avt-rtp-audio-level-exthdr/)
  uint8_t audio_level_dbov_ = 0;
  OneTimeEvent first_packet_sent_;

  RTC_DISALLOW_IMPLICIT_CONSTRUCTORS(RTPSenderAudio);
};

}  // namespace webrtc

#endif  // MODULES_RTP_RTCP_SOURCE_RTP_SENDER_AUDIO_H_
