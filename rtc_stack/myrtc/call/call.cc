/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "call.h"

#include <string.h>
#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>
#include <optional>

#include "api/rtc_event_log.h"
#include "api/network_control.h"
#include "call/bitrate_allocator.h"
#include "call/receive_time_calculator.h"
#include "call/rtp_stream_receiver_controller.h"
#include "logging/rtc_event_audio_receive_stream_config.h"
#include "logging/rtc_event_rtcp_packet_incoming.h"
#include "logging/rtc_event_rtp_packet_incoming.h"
#include "logging/rtc_event_video_receive_stream_config.h"
#include "logging/rtc_stream_config.h"
#include "cc/receive_side_congestion_controller.h"
#include "rtp_rtcp/flexfec_receiver.h"
#include "rtp_rtcp//rtp_header_extension_map.h"
#include "rtp_rtcp/byte_io.h"
#include "rtp_rtcp/rtp_packet_received.h"
#include "rtp_rtcp/rtp_utility.h"
#include "utility/process_thread.h"
#include "video/fec_controller_default.h"
#include "rtc_base/checks.h"
#include "rtc_base/constructor_magic.h"
#include "rtc_base/location.h"
#include "rtc_base/logging.h"
#include "rtc_base/string_builder.h"
#include "rtc_base/sequence_checker.h"
#include "rtc_base/thread_annotations.h"
#include "rtc_base/time_utils.h"
#include "rtc_base/trace_event.h"
#include "rtc_base/cpu_info.h"
#include "rtc_base/field_trial.h"
#include "rtc_base/metrics.h"
#include "audio/audio_receive_stream.h"
#include "video/call_stats.h"
#include "video/stats_counter.h"
#include "video/video_receive_stream.h"
#include "pacing/packet_router.h"

namespace webrtc {

//helper function
namespace {
bool SendPeriodicFeedback(const std::vector<RtpExtension>& extensions) {
  for (const auto& extension : extensions) {
    if (extension.uri == RtpExtension::kTransportSequenceNumberV2Uri)
      return false;
  }
  return true;
}

// TODO(nisse): This really begs for a shared context struct.
bool UseSendSideBwe(const std::vector<RtpExtension>& extensions,
                    bool transport_cc) {
  if (!transport_cc)
    return false;
  for (const auto& extension : extensions) {
    if (extension.uri == RtpExtension::kTransportSequenceNumberUri ||
        extension.uri == RtpExtension::kTransportSequenceNumberV2Uri)
      return true;
  }
  return false;
}

bool UseSendSideBwe(const AudioReceiveStream::Config& config) {
  return UseSendSideBwe(config.rtp.extensions, config.rtp.transport_cc);
}

bool UseSendSideBwe(const VideoReceiveStream::Config& config) {
  return UseSendSideBwe(config.rtp.extensions, config.rtp.transport_cc);
}

bool UseSendSideBwe(const FlexfecReceiveStream::Config& config) {
  return UseSendSideBwe(config.rtp_header_extensions, config.transport_cc);
}

const int* FindKeyByValue(const std::map<int, int>& m, int v) {
  for (const auto& kv : m) {
    if (kv.second == v)
      return &kv.first;
  }
  return nullptr;
}

std::unique_ptr<rtclog::StreamConfig> CreateRtcLogStreamConfig(
    const VideoReceiveStream::Config& config) {
  auto rtclog_config = std::make_unique<rtclog::StreamConfig>();
  rtclog_config->remote_ssrc = config.rtp.remote_ssrc;
  rtclog_config->local_ssrc = config.rtp.local_ssrc;
  rtclog_config->rtx_ssrc = config.rtp.rtx_ssrc;
  rtclog_config->rtcp_mode = config.rtp.rtcp_mode;
  rtclog_config->rtp_extensions = config.rtp.extensions;

  for (const auto& d : config.decoders) {
    const int* search =
        FindKeyByValue(config.rtp.rtx_associated_payload_types, d.payload_type);
    rtclog_config->codecs.emplace_back(d.video_format.name, d.payload_type,
                                       search ? *search : 0);
  }
  return rtclog_config;
}

std::unique_ptr<rtclog::StreamConfig> CreateRtcLogStreamConfig(
    const AudioReceiveStream::Config& config) {
  auto rtclog_config = std::make_unique<rtclog::StreamConfig>();
  rtclog_config->remote_ssrc = config.rtp.remote_ssrc;
  rtclog_config->local_ssrc = config.rtp.local_ssrc;
  rtclog_config->rtp_extensions = config.rtp.extensions;
  return rtclog_config;
}

bool IsRtcp(const uint8_t* packet, size_t length) {
  RtpUtility::RtpHeaderParser rtp_parser(packet, length);
  return rtp_parser.RTCP();
}

}  // namespace

namespace internal {

class Call final : public webrtc::Call,
                   public PacketReceiver
                   //public RecoveredPacketReceiver, flex fec disabled 
                   {
 public:
  Call(Clock* clock,
       const Call::Config& config,
       std::unique_ptr<ProcessThread> module_process_thread,
       TaskQueueFactory* task_queue_factory);
  ~Call() override;

  webrtc::AudioReceiveStream* CreateAudioReceiveStream(
      const webrtc::AudioReceiveStream::Config& config) override;
  void DestroyAudioReceiveStream(
      webrtc::AudioReceiveStream* receive_stream) override;

  webrtc::VideoReceiveStream* CreateVideoReceiveStream(
      webrtc::VideoReceiveStream::Config configuration) override;
  void DestroyVideoReceiveStream(
      webrtc::VideoReceiveStream* receive_stream) override;

  FlexfecReceiveStream* CreateFlexfecReceiveStream(
      const FlexfecReceiveStream::Config& config) override;
  void DestroyFlexfecReceiveStream(
      FlexfecReceiveStream* receive_stream) override;

  void SignalChannelNetworkState(MediaType media, NetworkState state) override;
  
  PacketReceiver* Receiver() override;

  // Implements PacketReceiver.
  DeliveryStatus DeliverPacket(MediaType media_type,
                               rtc::ArrayView<const uint8_t> packet,
                               int64_t packet_time_us) override;

  // Implements RecoveredPacketReceiver.
  //void OnRecoveredPacket(const uint8_t* packet, size_t length) override;

 private:
  DeliveryStatus DeliverRtcp(MediaType media_type,
                             const uint8_t* packet,
                             size_t length);
  DeliveryStatus DeliverRtp(MediaType media_type,
                            rtc::ArrayView<const uint8_t> packet,
                            int64_t packet_time_us);

  void NotifyBweOfReceivedPacket(const RtpPacketReceived& packet,
                                 MediaType media_type,
                                 auto& it);

  void UpdateSendHistograms(Timestamp first_sent_packet);
  void UpdateReceiveHistograms();
  void UpdateHistograms();
  void UpdateAggregateNetworkState();

  void RegisterRateObserver();

  rtc::TaskQueue* network_queue() {
    return &task_queue_;
  }

  Clock* const clock_;
  TaskQueueFactory* const task_queue_factory_;

  const int num_cpu_cores_;
  const std::unique_ptr<ProcessThread> module_process_thread_;
  const std::unique_ptr<CallStats> call_stats_;

  Call::Config config_;
  SequenceChecker configuration_sequence_checker_;
  SequenceChecker worker_sequence_checker_;

  NetworkState video_network_state_ = kNetworkDown;
  NetworkState audio_network_state_ = kNetworkDown;
  bool aggregate_network_up_ = false;

  // Audio, Video, and FlexFEC receive streams are owned by the client that
  // creates them.
  std::set<AudioReceiveStream*> audio_receive_streams_;
  std::set<VideoReceiveStream*> video_receive_streams_;

  // TODO(nisse): Should eventually be injected at creation,
  // with a single object in the bundled case.
  RtpStreamReceiverController audio_receiver_controller_;
  RtpStreamReceiverController video_receiver_controller_;

  // This extra map is used for receive processing which is
  // independent of media type.

  // TODO(nisse): In the RTP transport refactoring, we should have a
  // single mapping from ssrc to a more abstract receive stream, with
  // accessor methods for all configuration we need at this level.
  struct ReceiveRtpConfig {
    explicit ReceiveRtpConfig(const webrtc::AudioReceiveStream::Config& config)
        : extensions(config.rtp.extensions),
          use_send_side_bwe(UseSendSideBwe(config)) {}
    explicit ReceiveRtpConfig(const webrtc::VideoReceiveStream::Config& config)
        : extensions(config.rtp.extensions),
          use_send_side_bwe(UseSendSideBwe(config)) {}
    explicit ReceiveRtpConfig(const FlexfecReceiveStream::Config& config)
        : extensions(config.rtp_header_extensions),
          use_send_side_bwe(UseSendSideBwe(config)) {}

    // Registered RTP header extensions for each stream. Note that RTP header
    // extensions are negotiated per track ("m= line") in the SDP, but we have
    // no notion of tracks at the Call level. We therefore store the RTP header
    // extensions per SSRC instead, which leads to some storage overhead.
    const RtpHeaderExtensionMap extensions;
    // Set if both RTP extension the RTCP feedback message needed for
    // send side BWE are negotiated.
    const bool use_send_side_bwe;
  };
  std::map<uint32_t, ReceiveRtpConfig> receive_rtp_config_;

  // Audio and Video send streams are owned by the client that creates them.
  
  webrtc::RtcEventLog* event_log_;

  // The following members are only accessed (exclusively) from one thread and
  // from the destructor, and therefore doesn't need any explicit
  // synchronization.
  RateCounter received_bytes_per_second_counter_;
  RateCounter received_audio_bytes_per_second_counter_;
  RateCounter received_video_bytes_per_second_counter_;
  RateCounter received_rtcp_bytes_per_second_counter_;
  absl::optional<int64_t> first_received_rtp_audio_ms_;
  absl::optional<int64_t> last_received_rtp_audio_ms_;
  std::optional<int64_t> first_received_rtp_video_ms_;
  std::optional<int64_t> last_received_rtp_video_ms_;

  // TODO(holmer): Remove this lock once BitrateController no longer calls
  // OnNetworkChanged from multiple threads.
  uint32_t min_allocated_send_bitrate_bps_ = 0;
  uint32_t configured_max_padding_bitrate_bps_ = 0;
  AvgCounter estimated_send_bitrate_kbps_counter_;
  AvgCounter pacer_bitrate_kbps_counter_;

  //associate with receive video stream, receive audio stream & flex receive stream
  ReceiveSideCongestionController receive_side_cc_;

  const std::unique_ptr<ReceiveTimeCalculator> receive_time_calculator_;

  const int64_t start_ms_;
  bool is_target_rate_observer_registered_ = false;

  PacketRouter packet_router_;

  rtc::TaskQueue task_queue_;

  RTC_DISALLOW_COPY_AND_ASSIGN(Call);
};
}  // namespace internal

std::string Call::Stats::ToString(int64_t time_ms) const {
  char buf[1024];
  rtc::SimpleStringBuilder ss(buf);
  ss << "Call stats: " << time_ms << ", {";
  ss << "send_bw_bps: " << send_bandwidth_bps << ", ";
  ss << "recv_bw_bps: " << recv_bandwidth_bps << ", ";
  ss << "max_pad_bps: " << max_padding_bitrate_bps << ", ";
  ss << "pacer_delay_ms: " << pacer_delay_ms << ", ";
  ss << "rtt_ms: " << rtt_ms;
  ss << '}';
  return ss.str();
}

Call* Call::Create(const Call::Config& config,
                   Clock* clock,
                   std::unique_ptr<ProcessThread> module_thread) {
  RTC_DCHECK(config.task_queue_factory);
  return new internal::Call(clock, 
                            config,
                            std::move(module_thread), 
                            config.task_queue_factory);
}

namespace internal {

Call::Call(Clock* clock,
           const Call::Config& config,
           std::unique_ptr<ProcessThread> module_process_thread,
           TaskQueueFactory* task_queue_factory)
    : clock_(clock),
      task_queue_factory_(task_queue_factory),
      num_cpu_cores_(CpuInfo::DetectNumberOfCores()),
      module_process_thread_(std::move(module_process_thread)),
      call_stats_(new CallStats(clock_, module_process_thread_.get())),
      config_(config),
      event_log_(config.event_log),
      received_bytes_per_second_counter_(clock_, nullptr, true),
      received_audio_bytes_per_second_counter_(clock_, nullptr, true),
      received_video_bytes_per_second_counter_(clock_, nullptr, true),
      received_rtcp_bytes_per_second_counter_(clock_, nullptr, true),
      estimated_send_bitrate_kbps_counter_(clock_, nullptr, true),
      pacer_bitrate_kbps_counter_(clock_, nullptr, true),
      receive_side_cc_(clock_, &packet_router_),
      receive_time_calculator_(ReceiveTimeCalculator::CreateFromFieldTrial()),
      start_ms_(clock_->TimeInMilliseconds()),
      task_queue_(task_queue_factory->CreateTaskQueue(
          "rtp_send_controller",
          TaskQueueFactory::Priority::NORMAL)) {
  worker_sequence_checker_.Detach();

  call_stats_->RegisterStatsObserver(&receive_side_cc_);

  // for transport-cc feedback
  module_process_thread_->RegisterModule(
      receive_side_cc_.GetRemoteBitrateEstimator(true), RTC_FROM_HERE);
  
  module_process_thread_->RegisterModule(call_stats_.get(), RTC_FROM_HERE);
  //module_process_thread_->RegisterModule(&receive_side_cc_, RTC_FROM_HERE);
}

Call::~Call() {
  RTC_DCHECK_RUN_ON(&configuration_sequence_checker_);
  RTC_CHECK(audio_receive_streams_.empty());
  RTC_CHECK(video_receive_streams_.empty());

  module_process_thread_->Stop();
  module_process_thread_->DeRegisterModule(
      receive_side_cc_.GetRemoteBitrateEstimator(true));
  //module_process_thread_->DeRegisterModule(&receive_side_cc_);
  module_process_thread_->DeRegisterModule(call_stats_.get());
  call_stats_->DeregisterStatsObserver(&receive_side_cc_);

  std::optional<Timestamp> first_sent_packet_ms;

  // Only update histograms after process threads have been shut down, so that
  // they won't try to concurrently update stats.
  if (first_sent_packet_ms) {
    UpdateSendHistograms(*first_sent_packet_ms);
  }

  UpdateReceiveHistograms();
  UpdateHistograms();
}

void Call::RegisterRateObserver() {
  RTC_DCHECK_RUN_ON(&configuration_sequence_checker_);

  if (is_target_rate_observer_registered_)
    return;

  is_target_rate_observer_registered_ = true;

  module_process_thread_->Start();
}

void Call::UpdateHistograms() {
  RTC_HISTOGRAM_COUNTS_100000(
      "WebRTC.Call.LifetimeInSeconds",
      (clock_->TimeInMilliseconds() - start_ms_) / 1000);
}

// Called from the dtor.
void Call::UpdateSendHistograms(Timestamp first_sent_packet) {
  int64_t elapsed_sec =
      (clock_->TimeInMilliseconds() - first_sent_packet.ms()) / 1000;
  if (elapsed_sec < metrics::kMinRunTimeInSeconds)
    return;
  const int kMinRequiredPeriodicSamples = 5;
  AggregatedStats send_bitrate_stats =
      estimated_send_bitrate_kbps_counter_.ProcessAndGetStats();
  if (send_bitrate_stats.num_samples > kMinRequiredPeriodicSamples) {
    RTC_HISTOGRAM_COUNTS_100000("WebRTC.Call.EstimatedSendBitrateInKbps",
                                send_bitrate_stats.average);
    RTC_LOG(LS_INFO) << "WebRTC.Call.EstimatedSendBitrateInKbps, "
                     << send_bitrate_stats.ToString();
  }
  AggregatedStats pacer_bitrate_stats =
      pacer_bitrate_kbps_counter_.ProcessAndGetStats();
  if (pacer_bitrate_stats.num_samples > kMinRequiredPeriodicSamples) {
    RTC_HISTOGRAM_COUNTS_100000("WebRTC.Call.PacerBitrateInKbps",
                                pacer_bitrate_stats.average);
    RTC_LOG(LS_INFO) << "WebRTC.Call.PacerBitrateInKbps, "
                     << pacer_bitrate_stats.ToString();
  }
}

void Call::UpdateReceiveHistograms() {
  if (first_received_rtp_audio_ms_) {
    RTC_HISTOGRAM_COUNTS_100000(
        "WebRTC.Call.TimeReceivingAudioRtpPacketsInSeconds",
        (*last_received_rtp_audio_ms_ - *first_received_rtp_audio_ms_) / 1000);
  }

  if (first_received_rtp_video_ms_) {
    RTC_HISTOGRAM_COUNTS_100000(
        "WebRTC.Call.TimeReceivingVideoRtpPacketsInSeconds",
        (*last_received_rtp_video_ms_ - *first_received_rtp_video_ms_) / 1000);
  }
  const int kMinRequiredPeriodicSamples = 5;
  AggregatedStats video_bytes_per_sec =
      received_video_bytes_per_second_counter_.GetStats();
  if (video_bytes_per_sec.num_samples > kMinRequiredPeriodicSamples) {
    RTC_HISTOGRAM_COUNTS_100000("WebRTC.Call.VideoBitrateReceivedInKbps",
                                video_bytes_per_sec.average * 8 / 1000);
    RTC_LOG(LS_INFO) << "WebRTC.Call.VideoBitrateReceivedInBps, "
                     << video_bytes_per_sec.ToStringWithMultiplier(8);
  }

  AggregatedStats audio_bytes_per_sec =
      received_audio_bytes_per_second_counter_.GetStats();
  if (audio_bytes_per_sec.num_samples > kMinRequiredPeriodicSamples) {
    RTC_HISTOGRAM_COUNTS_100000("WebRTC.Call.AudioBitrateReceivedInKbps",
                                audio_bytes_per_sec.average * 8 / 1000);
    RTC_LOG(LS_INFO) << "WebRTC.Call.AudioBitrateReceivedInBps, "
                     << audio_bytes_per_sec.ToStringWithMultiplier(8);
  }

  AggregatedStats rtcp_bytes_per_sec =
      received_rtcp_bytes_per_second_counter_.GetStats();
  if (rtcp_bytes_per_sec.num_samples > kMinRequiredPeriodicSamples) {
    RTC_HISTOGRAM_COUNTS_100000("WebRTC.Call.RtcpBitrateReceivedInBps",
                                rtcp_bytes_per_sec.average * 8);
    RTC_LOG(LS_INFO) << "WebRTC.Call.RtcpBitrateReceivedInBps, "
                     << rtcp_bytes_per_sec.ToStringWithMultiplier(8);
  }
  AggregatedStats recv_bytes_per_sec =
      received_bytes_per_second_counter_.GetStats();
  if (recv_bytes_per_sec.num_samples > kMinRequiredPeriodicSamples) {
    RTC_HISTOGRAM_COUNTS_100000("WebRTC.Call.BitrateReceivedInKbps",
                                recv_bytes_per_sec.average * 8 / 1000);
    RTC_LOG(LS_INFO) << "WebRTC.Call.BitrateReceivedInBps, "
                     << recv_bytes_per_sec.ToStringWithMultiplier(8);
  }
}

PacketReceiver* Call::Receiver() {
  RTC_DCHECK_RUN_ON(&configuration_sequence_checker_);
  return this;
}

webrtc::AudioReceiveStream* Call::CreateAudioReceiveStream(
    const webrtc::AudioReceiveStream::Config& config) {
  RTC_DCHECK_RUN_ON(&configuration_sequence_checker_);
  RegisterRateObserver();
  if (event_log_)
    event_log_->Log(std::make_unique<RtcEventAudioReceiveStreamConfig>(
        CreateRtcLogStreamConfig(config)));
  
  AudioReceiveStream* receive_stream = new AudioReceiveStream(
      clock_, &audio_receiver_controller_, &packet_router_, module_process_thread_.get(), config, event_log_);

  receive_rtp_config_.emplace(config.rtp.remote_ssrc, ReceiveRtpConfig(config));
  audio_receive_streams_.insert(receive_stream);

  UpdateAggregateNetworkState();
  return receive_stream;
}

void Call::DestroyAudioReceiveStream(
    webrtc::AudioReceiveStream* receive_stream) {
  RTC_DCHECK_RUN_ON(&configuration_sequence_checker_);
  RTC_DCHECK(receive_stream != nullptr);
  webrtc::internal::AudioReceiveStream* audio_receive_stream =
      static_cast<webrtc::internal::AudioReceiveStream*>(receive_stream);
  
  const AudioReceiveStream::Config& config = audio_receive_stream->config();
  uint32_t ssrc = config.rtp.remote_ssrc;
  receive_side_cc_.GetRemoteBitrateEstimator(UseSendSideBwe(config))
      ->RemoveStream(ssrc);
  audio_receive_streams_.erase(audio_receive_stream);

  receive_rtp_config_.erase(ssrc);
  
  UpdateAggregateNetworkState();
  delete audio_receive_stream;
}

webrtc::VideoReceiveStream* Call::CreateVideoReceiveStream(
    webrtc::VideoReceiveStream::Config configuration) {
  RTC_DCHECK_RUN_ON(&configuration_sequence_checker_);

  receive_side_cc_.SetSendPeriodicFeedback(
      SendPeriodicFeedback(configuration.rtp.extensions));

  RegisterRateObserver();

  VideoReceiveStream* receive_stream = 
    new VideoReceiveStream(task_queue_factory_, 
                           &video_receiver_controller_, 
                           num_cpu_cores_,
                           &packet_router_, 
                           std::move(configuration),
                           module_process_thread_.get(), 
                           call_stats_.get(), 
                           clock_);

  const webrtc::VideoReceiveStream::Config& config = receive_stream->config();
  
  if (config.rtp.rtx_ssrc) {
    // We record identical config for the rtx stream as for the main
    // stream. Since the transport_send_cc negotiation is per payload
    // type, we may get an incorrect value for the rtx stream, but
    // that is unlikely to matter in practice.
    receive_rtp_config_.emplace(config.rtp.rtx_ssrc,
                                ReceiveRtpConfig(config));
  }
  receive_rtp_config_.emplace(config.rtp.remote_ssrc,
                              ReceiveRtpConfig(config));
  video_receive_streams_.insert(receive_stream);
  
  receive_stream->SignalNetworkState(video_network_state_);
  UpdateAggregateNetworkState();

  if (event_log_)
    event_log_->Log(std::make_unique<RtcEventVideoReceiveStreamConfig>(
        CreateRtcLogStreamConfig(config)));
  return receive_stream;
}

void Call::DestroyVideoReceiveStream(
    webrtc::VideoReceiveStream* receive_stream) {
  RTC_DCHECK_RUN_ON(&configuration_sequence_checker_);
  RTC_DCHECK(receive_stream != nullptr);
  VideoReceiveStream* receive_stream_impl =
      static_cast<VideoReceiveStream*>(receive_stream);
  const VideoReceiveStream::Config& config = receive_stream_impl->config();

  // Remove all ssrcs pointing to a receive stream. As RTX retransmits on a
  // separate SSRC there can be either one or two.
  receive_rtp_config_.erase(config.rtp.remote_ssrc);
  if (config.rtp.rtx_ssrc) {
    receive_rtp_config_.erase(config.rtp.rtx_ssrc);
  }
  video_receive_streams_.erase(receive_stream_impl);

  RemoteBitrateEstimator* estimator = 
    receive_side_cc_.GetRemoteBitrateEstimator(UseSendSideBwe(config));
  if (estimator)
      estimator->RemoveStream(config.rtp.remote_ssrc);

  UpdateAggregateNetworkState();
  delete receive_stream_impl;
}

FlexfecReceiveStream* Call::CreateFlexfecReceiveStream(
    const FlexfecReceiveStream::Config& config) {

#if 0    
  RTC_DCHECK_RUN_ON(&configuration_sequence_checker_);

  RecoveredPacketReceiver* recovered_packet_receiver = this;

  FlexfecReceiveStreamImpl* receive_stream;
  {
    // Unlike the video and audio receive streams,
    // FlexfecReceiveStream implements RtpPacketSinkInterface itself,
    // and hence its constructor passes its |this| pointer to
    // video_receiver_controller_->CreateStream(). Calling the
    // constructor while holding |receive_crit_| ensures that we don't
    // call OnRtpPacket until the constructor is finished and the
    // object is in a valid state.
    // TODO(nisse): Fix constructor so that it can be moved outside of
    // this locked scope.
    receive_stream = new FlexfecReceiveStreamImpl(
        clock_, &video_receiver_controller_, config, recovered_packet_receiver,
        call_stats_.get(), module_process_thread_.get());

    RTC_DCHECK(receive_rtp_config_.find(config.remote_ssrc) ==
               receive_rtp_config_.end());
    receive_rtp_config_.emplace(config.remote_ssrc, ReceiveRtpConfig(config));
  }

  // TODO(brandtr): Store config in RtcEventLog here.

  return receive_stream;
#endif
  return nullptr;
}

void Call::DestroyFlexfecReceiveStream(FlexfecReceiveStream* receive_stream) {

#if 0
  RTC_DCHECK_RUN_ON(&configuration_sequence_checker_);

  RTC_DCHECK(receive_stream != nullptr);

  const FlexfecReceiveStream::Config& config = receive_stream->GetConfig();
  uint32_t ssrc = config.remote_ssrc;
  receive_rtp_config_.erase(ssrc);

  // Remove all SSRCs pointing to the FlexfecReceiveStreamImpl to be
  // destroyed.
  receive_side_cc_.GetRemoteBitrateEstimator(UseSendSideBwe(config))
      ->RemoveStream(ssrc);

  delete receive_stream;

#endif  
}

void Call::SignalChannelNetworkState(MediaType media, NetworkState state) {
  RTC_DCHECK_RUN_ON(&configuration_sequence_checker_);
  switch (media) {
    case MediaType::AUDIO:
      audio_network_state_ = state;
      break;
    case MediaType::VIDEO:
      video_network_state_ = state;
      break;
    default:
      RTC_NOTREACHED();
  }

  UpdateAggregateNetworkState();
  for (VideoReceiveStream* video_receive_stream : video_receive_streams_) {
    video_receive_stream->SignalNetworkState(video_network_state_);
  }
}

void Call::UpdateAggregateNetworkState() {
  RTC_DCHECK_RUN_ON(&configuration_sequence_checker_);
  bool have_video = false;
  bool have_audio = false;

  if (!audio_receive_streams_.empty())
    have_audio = true;

  if (!video_receive_streams_.empty())
    have_video = true;

  bool aggregate_network_up =
      ((have_video && video_network_state_ == kNetworkUp) ||
       (have_audio && audio_network_state_ == kNetworkUp));

  RTC_LOG(LS_INFO) << "UpdateAggregateNetworkState: aggregate_state="
                   << (aggregate_network_up ? "up" : "down");
  aggregate_network_up_ = aggregate_network_up;
}

PacketReceiver::DeliveryStatus Call::DeliverRtcp(MediaType media_type,
                                                 const uint8_t* packet,
                                                 size_t length) {
  // TODO(pbos): Make sure it's a valid packet.
  //             Return DELIVERY_UNKNOWN_SSRC if it can be determined that
  //             there's no receiver of the packet.
  if (received_bytes_per_second_counter_.HasSample()) {
    // First RTP packet has been received.
    received_bytes_per_second_counter_.Add(static_cast<int>(length));
    received_rtcp_bytes_per_second_counter_.Add(static_cast<int>(length));
  }
  bool rtcp_delivered = false;
  if (media_type == MediaType::ANY || media_type == MediaType::VIDEO) {
    for (VideoReceiveStream* stream : video_receive_streams_) {
      if (stream->DeliverRtcp(packet, length))
        rtcp_delivered = true;
    }
  }
  if (media_type == MediaType::ANY || media_type == MediaType::AUDIO) {
    for (AudioReceiveStream* stream : audio_receive_streams_) {
      stream->DeliverRtcp(packet, length);
      rtcp_delivered = true;
    }
  }
  if (rtcp_delivered && event_log_) {
    event_log_->Log(std::make_unique<RtcEventRtcpPacketIncoming>(
        rtc::MakeArrayView(packet, length)));
  }

  return rtcp_delivered ? DELIVERY_OK : DELIVERY_PACKET_ERROR;
}

PacketReceiver::DeliveryStatus Call::DeliverRtp(MediaType media_type,
    rtc::ArrayView<const uint8_t> packet, int64_t packet_time_us) {
  RtpPacketReceived parsed_packet(true);
  if (!parsed_packet.Parse(packet))
    return DELIVERY_PACKET_ERROR;

  if (packet_time_us != -1) {
    if (receive_time_calculator_) {
      // Repair packet_time_us for clock resets by comparing a new read of
      // the same clock (TimeUTCMicros) to a monotonic clock reading.
      packet_time_us = receive_time_calculator_->ReconcileReceiveTimes(
          packet_time_us, rtc::TimeUTCMicros(), clock_->TimeInMicroseconds());
    }
    parsed_packet.set_arrival_time_ms((packet_time_us + 500) / 1000);
  } else {
    parsed_packet.set_arrival_time_ms(clock_->TimeInMilliseconds());
  }

#if 0
  if (parsed_packet.padding_size() != 0) {
    RTC_LOG(LS_ERROR) << "receive padding from ssrc:"<< parsed_packet.Ssrc() << 
      ", padding size:" << parsed_packet.padding_size() <<
      ", payload size:" << parsed_packet.payload_size();
  }
#endif

  // We might get RTP keep-alive packets in accordance with RFC6263 section 4.6.
  // These are empty (zero length payload) RTP packets with an unsignaled
  // payload type.
  const bool is_keep_alive_packet = parsed_packet.payload_size() == 0;

  RTC_DCHECK(media_type == MediaType::AUDIO || media_type == MediaType::VIDEO ||
             is_keep_alive_packet);

  auto it = receive_rtp_config_.find(parsed_packet.Ssrc());
  if (it == receive_rtp_config_.end()) {
    RTC_LOG(LS_ERROR) << "receive_rtp_config_ lookup failed for ssrc "
                      << parsed_packet.Ssrc();
    // Destruction of the receive stream, including deregistering from the
    // RtpDemuxer, is not protected by the |receive_crit_| lock. But
    // deregistering in the |receive_rtp_config_| map is protected by that lock.
    // So by not passing the packet on to demuxing in this case, we prevent
    // incoming packets to be passed on via the demuxer to a receive stream
    // which is being torned down.
    return DELIVERY_UNKNOWN_SSRC;
  }

  parsed_packet.IdentifyExtensions(it->second.extensions);

  NotifyBweOfReceivedPacket(parsed_packet, media_type, it);

  // RateCounters expect input parameter as int, save it as int,
  // instead of converting each time it is passed to RateCounter::Add below.
  int length = static_cast<int>(parsed_packet.size());
  if (media_type == MediaType::AUDIO) {
    if (audio_receiver_controller_.OnRtpPacket(parsed_packet)) {
      received_bytes_per_second_counter_.Add(length);
      received_audio_bytes_per_second_counter_.Add(length);
      if (event_log_) {
        event_log_->Log(
            std::make_unique<RtcEventRtpPacketIncoming>(parsed_packet));
      }
      const int64_t arrival_time_ms = parsed_packet.arrival_time_ms();
      if (!first_received_rtp_audio_ms_) {
        first_received_rtp_audio_ms_.emplace(arrival_time_ms);
      }
      last_received_rtp_audio_ms_.emplace(arrival_time_ms);
      return DELIVERY_OK;
    }
  } else if (media_type == MediaType::VIDEO) {
    parsed_packet.set_payload_type_frequency(kVideoPayloadTypeFrequency);
    if (video_receiver_controller_.OnRtpPacket(parsed_packet)) {
      received_bytes_per_second_counter_.Add(length);
      received_video_bytes_per_second_counter_.Add(length);
      if (event_log_) {
        event_log_->Log(
            std::make_unique<RtcEventRtpPacketIncoming>(parsed_packet));
      }
      const int64_t arrival_time_ms = parsed_packet.arrival_time_ms();
      if (!first_received_rtp_video_ms_) {
        first_received_rtp_video_ms_.emplace(arrival_time_ms);
      }
      last_received_rtp_video_ms_.emplace(arrival_time_ms);
      return DELIVERY_OK;
    }
  }
  return DELIVERY_UNKNOWN_SSRC;
}

PacketReceiver::DeliveryStatus Call::DeliverPacket(
    MediaType media_type,
    rtc::ArrayView<const uint8_t> packet,
    int64_t packet_time_us) {
  RTC_DCHECK_RUN_ON(&configuration_sequence_checker_);
  if (IsRtcp(packet.data(), packet.size()))
    return DeliverRtcp(media_type, packet.data(), packet.size());

  return DeliverRtp(media_type, packet, packet_time_us);
}

#if 0
void Call::OnRecoveredPacket(const uint8_t* packet, size_t length) {
  RtpPacketReceived parsed_packet;
  if (!parsed_packet.Parse(packet, length))
    return;

  parsed_packet.set_recovered(true);

  ReadLockScoped read_lock(*receive_crit_);
  auto it = receive_rtp_config_.find(parsed_packet.Ssrc());
  if (it == receive_rtp_config_.end()) {
    RTC_LOG(LS_ERROR) << "receive_rtp_config_ lookup failed for ssrc "
                      << parsed_packet.Ssrc();
    // Destruction of the receive stream, including deregistering from the
    // RtpDemuxer, is not protected by the |receive_crit_| lock. But
    // deregistering in the |receive_rtp_config_| map is protected by that lock.
    // So by not passing the packet on to demuxing in this case, we prevent
    // incoming packets to be passed on via the demuxer to a receive stream
    // which is being torn down.
    return;
  }
  parsed_packet.IdentifyExtensions(it->second.extensions);

  // TODO(brandtr): Update here when we support protecting audio packets too.
  parsed_packet.set_payload_type_frequency(kVideoPayloadTypeFrequency);
  video_receiver_controller_.OnRtpPacket(parsed_packet);
}
#endif

void Call::NotifyBweOfReceivedPacket(const RtpPacketReceived& packet,
                                     MediaType media_type,
                                     auto& it) {
  bool use_send_side_bwe = it->second.use_send_side_bwe;

  RTPHeader header;
  packet.GetHeader(&header);

  ReceivedPacket packet_msg;
  packet_msg.size = DataSize::bytes(packet.payload_size());
  packet_msg.receive_time = Timestamp::ms(packet.arrival_time_ms());
  if (header.extension.hasAbsoluteSendTime) {
    packet_msg.send_time = header.extension.GetAbsoluteSendTimestamp();
  }

  if (!use_send_side_bwe && header.extension.hasTransportSequenceNumber) {
    // Inconsistent configuration of send side BWE. Do nothing.
    // TODO(nisse): Without this check, we may produce RTCP feedback
    // packets even when not negotiated. But it would be cleaner to
    // move the check down to RTCPSender::SendFeedbackPacket, which
    // would also help the PacketRouter to select an appropriate rtp
    // module in the case that some, but not all, have RTCP feedback
    // enabled.
    return;
  }
  // For audio, we only support send side BWE.
  if (media_type == MediaType::VIDEO ||
      (use_send_side_bwe && header.extension.hasTransportSequenceNumber)) {
    receive_side_cc_.OnReceivedPacket(
        packet.arrival_time_ms(), packet.payload_size() + packet.padding_size(),
        header);
  }
}

}  // namespace internal

}  // namespace webrtc
