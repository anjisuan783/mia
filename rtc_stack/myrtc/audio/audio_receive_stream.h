/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AUDIO_AUDIO_RECEIVE_STREAM_H_
#define AUDIO_AUDIO_RECEIVE_STREAM_H_

#include <memory>
#include <vector>

#include "api/rtp_headers.h"
#include "call/audio_receive_stream.h"
#include "call/syncable.h"
#include "rtc_base/constructor_magic.h"
#include "rtc_base/clock.h"
#include "rtp_rtcp/receive_statistics.h"
#include "rtp_rtcp/rtp_rtcp.h"
#include "rtp_rtcp/rtp_rtcp_config.h"
#include "utility/process_thread.h"

namespace webrtc {
class PacketRouter;
class ProcessThread;
class RtcEventLog;
class RtpPacketReceived;
class RtpStreamReceiverControllerInterface;
class RtpStreamReceiverInterface;

namespace internal {

class AudioReceiveStream final : public webrtc::AudioReceiveStream,
                                 public Syncable,
                                 public RtpPacketSinkInterface {
 public:
  // For unit tests, which need to supply a mock channel receive.
  AudioReceiveStream(
      Clock* clock,
      RtpStreamReceiverControllerInterface* receiver_controller,
      PacketRouter* packet_router,
      ProcessThread* module_process_thread,
      const webrtc::AudioReceiveStream::Config& config,
      webrtc::RtcEventLog* event_log);
  ~AudioReceiveStream() override;

  // webrtc::AudioReceiveStream implementation.
  void Reconfigure(const webrtc::AudioReceiveStream::Config& config) override;
  void Start() override;
  void Stop() override;
  webrtc::AudioReceiveStream::Stats GetStats() const override;
  void SetSink(AudioSinkInterface* sink) override;
  void SetGain(float gain) override;
  bool SetBaseMinimumPlayoutDelayMs(int delay_ms) override;
  int GetBaseMinimumPlayoutDelayMs() const override;
  std::vector<webrtc::RtpSource> GetSources() const override;

  //RtpPacketSinkInterface
  void OnRtpPacket(const RtpPacketReceived& packet) override;

  // Syncable
  int id() const override;
  absl::optional<Syncable::Info> GetInfo() const override { 
    return absl::nullopt;
  }
  uint32_t GetPlayoutTimestamp() const override { return 0; }
  void SetMinimumPlayoutDelay(int delay_ms) override { }

  void DeliverRtcp(const uint8_t* packet, size_t length);
  const webrtc::AudioReceiveStream::Config& config() const;

 private:
  static void ConfigureStream(AudioReceiveStream* stream,
                              const Config& new_config,
                              bool first_time);

  webrtc::AudioReceiveStream::Config config_;

  bool playing_ = false;

  std::unique_ptr<RtpStreamReceiverInterface> rtp_stream_receiver_;

  std::unique_ptr<ReceiveStatistics> rtp_receive_statistics_;
  std::unique_ptr<RtpRtcp> rtpRtcpModule_;
  PacketRouter* packet_router_ = nullptr;
  ProcessThread* moduleProcessThreadPtr_;

  // Indexed by payload type.
  std::map<uint8_t, int> payload_type_frequencies_;

  RTC_DISALLOW_IMPLICIT_CONSTRUCTORS(AudioReceiveStream);
};
}  // namespace internal
}  // namespace webrtc

#endif  // AUDIO_AUDIO_RECEIVE_STREAM_H_
