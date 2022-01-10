/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "audio/audio_receive_stream.h"

#include <string>
#include <utility>

#include "absl/memory/memory.h"
#include "api/audio_format.h"
#include "api/rtp_parameters.h"
#include "rtc_base/array_view.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/time_utils.h"
#include "rtc_base/location.h"
#include "pacing/packet_router.h"
#include "rtp_rtcp/rtp_packet_received.h"
#include "rtp_rtcp/receive_statistics.h"
#include "call/rtp_config.h"
#include "call/rtp_stream_receiver_controller_interface.h"


namespace webrtc {

std::string AudioReceiveStream::Config::Rtp::ToString() const {
  char ss_buf[1024];
  rtc::SimpleStringBuilder ss(ss_buf);
  ss << "{remote_ssrc: " << remote_ssrc;
  ss << ", local_ssrc: " << local_ssrc;
  ss << ", transport_cc: " << (transport_cc ? "on" : "off");
  ss << ", nack: " << nack.ToString();
  ss << ", extensions: [";
  for (size_t i = 0; i < extensions.size(); ++i) {
    ss << extensions[i].ToString();
    if (i != extensions.size() - 1) {
      ss << ", ";
    }
  }
  ss << ']';
  ss << '}';
  return ss.str();
}

std::string AudioReceiveStream::Config::ToString() const {
  char ss_buf[1024];
  rtc::SimpleStringBuilder ss(ss_buf);
  ss << "{rtp: " << rtp.ToString();
  ss << ", rtcp_send_transport: "
     << (rtcp_send_transport ? "(Transport)" : "null");
  ss << ", media_transport_config: " << media_transport_config.DebugString();
  if (!sync_group.empty()) {
    ss << ", sync_group: " << sync_group;
  }
  ss << '}';
  return ss.str();
}

namespace internal {

AudioReceiveStream::AudioReceiveStream(
    Clock* clock,
    RtpStreamReceiverControllerInterface* receiver_controller,
    PacketRouter* packet_router,
    ProcessThread* module_process_thread,
    const webrtc::AudioReceiveStream::Config& config,
    webrtc::RtcEventLog* event_log)
    : rtp_receive_statistics_(ReceiveStatistics::Create(clock)),
      moduleProcessThreadPtr_(module_process_thread) {
  RTC_LOG(LS_INFO) << "AudioReceiveStream: " << config.rtp.remote_ssrc;
  //RTC_DCHECK(config.decoder_factory);
  RTC_DCHECK(config.rtcp_send_transport);

  RtpRtcp::Configuration configuration;
  configuration.clock = clock;
  configuration.audio = true;
  configuration.receiver_only = true;
  configuration.outgoing_transport = config.rtcp_send_transport;
  configuration.receive_statistics = rtp_receive_statistics_.get();
  configuration.event_log = event_log;
  configuration.local_media_ssrc = config.rtp.local_ssrc;

  rtpRtcpModule_ = RtpRtcp::Create(configuration);
  rtpRtcpModule_->SetSendingMediaStatus(false);
  rtpRtcpModule_->SetRemoteSSRC(config.rtp.remote_ssrc);

  moduleProcessThreadPtr_->RegisterModule(rtpRtcpModule_.get(), RTC_FROM_HERE);

  // Ensure that RTCP is enabled for the created channel.
  rtpRtcpModule_->SetRTCPStatus(RtcpMode::kCompound);

  packet_router->AddReceiveRtpModule(rtpRtcpModule_.get(), false);
  packet_router_ = packet_router;

  rtp_stream_receiver_ = receiver_controller->CreateReceiver(
        config.rtp.remote_ssrc, this);
  ConfigureStream(this, config, true);
}

AudioReceiveStream::~AudioReceiveStream() {
  RTC_LOG(LS_INFO) << "~AudioReceiveStream: " << config_.rtp.remote_ssrc;
  Stop();
  moduleProcessThreadPtr_->DeRegisterModule(rtpRtcpModule_.get());
  RTC_DCHECK(packet_router_);
  packet_router_->RemoveReceiveRtpModule(rtpRtcpModule_.get());
  packet_router_ = nullptr;
}

void AudioReceiveStream::Reconfigure(
    const webrtc::AudioReceiveStream::Config& config) {
  ConfigureStream(this, config, false);
}

void AudioReceiveStream::Start() {
  if (playing_) {
    return;
  }
  playing_ = true;
}

void AudioReceiveStream::Stop() {
  if (!playing_) {
    return;
  }
  playing_ = false;
}

webrtc::AudioReceiveStream::Stats AudioReceiveStream::GetStats() const {
  webrtc::AudioReceiveStream::Stats stats;
  stats.remote_ssrc = config_.rtp.remote_ssrc;

  return stats;
}

void AudioReceiveStream::SetSink(AudioSinkInterface*) {
}

void AudioReceiveStream::SetGain(float) {
}

bool AudioReceiveStream::SetBaseMinimumPlayoutDelayMs(int) {
  return true;
}

int AudioReceiveStream::GetBaseMinimumPlayoutDelayMs() const {
  return 1;
}

std::vector<RtpSource> AudioReceiveStream::GetSources() const {
  return std::vector<RtpSource>();
}

int AudioReceiveStream::id() const {
  return config_.rtp.remote_ssrc;
}

void AudioReceiveStream::DeliverRtcp(const uint8_t* packet, size_t length) {
  // Deliver RTCP packet to RTP/RTCP module for parsing
  rtpRtcpModule_->IncomingRtcpPacket(packet, length);
}

void AudioReceiveStream::OnRtpPacket(const RtpPacketReceived& packet) {

  const auto& it = payload_type_frequencies_.find(packet.PayloadType());
  if (it == payload_type_frequencies_.end())
    return;
  
  // TODO(nisse): Set payload_type_frequency earlier, when packet is parsed.
  RtpPacketReceived packet_copy(packet);
  
  packet_copy.set_payload_type_frequency(it->second);

  rtp_receive_statistics_->OnRtpPacket(packet_copy);
}

const webrtc::AudioReceiveStream::Config& AudioReceiveStream::config() const {
  return config_;
}

void AudioReceiveStream::ConfigureStream(AudioReceiveStream* stream,
                                         const Config& new_config,
                                         bool first_time) {
  RTC_LOG(LS_INFO) << "AudioReceiveStream::ConfigureStream: "
                   << new_config.ToString();
  RTC_DCHECK(stream);
  const auto& old_config = stream->config_;

  // Configuration parameters which cannot be changed.
  RTC_DCHECK(first_time ||
             old_config.rtp.remote_ssrc == new_config.rtp.remote_ssrc);
  RTC_DCHECK(first_time ||
             old_config.rtcp_send_transport == new_config.rtcp_send_transport);
  // Decoder factory cannot be changed because it is configured at
  // voe::Channel construction time.
  RTC_DCHECK(first_time ||
             old_config.decoder_factory == new_config.decoder_factory);

  if (!first_time) {
    // SSRC can't be changed mid-stream.
    RTC_DCHECK_EQ(old_config.rtp.local_ssrc, new_config.rtp.local_ssrc);
    RTC_DCHECK_EQ(old_config.rtp.remote_ssrc, new_config.rtp.remote_ssrc);
  }

  // TODO(solenberg): Config NACK history window (which is a packet count),
  // using the actual packet size for the configured codec.
  if (first_time || old_config.rtp.nack.rtp_history_ms !=
                        new_config.rtp.nack.rtp_history_ms) {
    if (new_config.rtp.nack.rtp_history_ms != 0) {
      int max_packets = new_config.rtp.nack.rtp_history_ms / 20;
      stream->rtp_receive_statistics_->SetMaxReorderingThreshold(max_packets);
    } else {
      stream->rtp_receive_statistics_->SetMaxReorderingThreshold(
          kDefaultMaxReorderingThreshold);
    }
  }
                        
  if (first_time || old_config.decoder_map != new_config.decoder_map) {
    for (const auto& kv : new_config.decoder_map) {
      RTC_DCHECK_GE(kv.second.clockrate_hz, 1000);
      stream->payload_type_frequencies_[kv.first] = kv.second.clockrate_hz;
    }
  }
  stream->config_ = new_config;
}
                                         
}  // namespace internal

}  // namespace webrtc

