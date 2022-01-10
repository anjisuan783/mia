/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VIDEO_RTP_VIDEO_STREAM_RECEIVER_H_
#define VIDEO_RTP_VIDEO_STREAM_RECEIVER_H_

#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <optional>

#include "api/color_space.h"
#include "api/video_codec.h"
#include "call/rtp_packet_sink_interface.h"
#include "call/syncable.h"
#include "call/video_receive_stream.h"
#include "rtp_rtcp/receive_statistics.h"
#include "rtp_rtcp/remote_ntp_time_estimator.h"
#include "rtp_rtcp/rtp_header_extension_map.h"
#include "rtp_rtcp/rtp_rtcp.h"
#include "rtp_rtcp/rtp_rtcp_defines.h"
#include "video/h264_sps_pps_tracker.h"
#include "video/loss_notification_controller.h"
#include "video/packet_buffer.h"
#include "video/rtp_frame_reference_finder.h"
#include "rtc_base/constructor_magic.h"
#include "rtc_base/sequence_number_util.h"
#include "rtc_base/sequence_checker.h"
#include "rtc_base/thread_annotations.h"
#include "rtc_base/thread_checker.h"

namespace webrtc {

class NackModule;
class PacketRouter;
class ProcessThread;
class ReceiveStatistics;
class ReceiveStatisticsProxy;
class RtcpRttStats;
class RtpPacketReceived;
class Transport;
class UlpfecReceiver;

class RtpVideoStreamReceiver : public LossNotificationSender,
                               public RecoveredPacketReceiver,
                               public RtpPacketSinkInterface,
                               public KeyFrameRequestSender,
                               public video_coding::OnAssembledFrameCallback,
                               public video_coding::OnCompleteFrameCallback {
 public:
  /* The PacketRouter is optional; if provided, the RtpRtcp module for this
   * stream is registered as a candidate for sending REMB and transport feedback.
   *
   * The KeyFrameRequestSender is optional; if not provided, key frame
   * requests are sent via the internal RtpRtcp module.
   */
  RtpVideoStreamReceiver(
      Clock* clock,
      Transport* transport,
      RtcpRttStats* rtt_stats,
      PacketRouter* packet_router,
      const VideoReceiveStream::Config* config,
      ReceiveStatistics* rtp_receive_statistics,
      ReceiveStatisticsProxy* receive_stats_proxy,
      ProcessThread* process_thread,
      NackSender* nack_sender,
      KeyFrameRequestSender*,
      video_coding::OnCompleteFrameCallback* complete_frame_callback);
  ~RtpVideoStreamReceiver() override;

  void AddReceiveCodec(const VideoCodec& video_codec,
                       const std::map<std::string, std::string>& codec_params,
                       bool raw_payload);

  void StartReceive();
  void StopReceive();

  // Produces the transport-related timestamps; current_delay_ms is left unset.
  std::optional<Syncable::Info> GetSyncInfo() const;

  bool DeliverRtcp(const uint8_t* rtcp_packet, size_t rtcp_packet_length);

  void FrameContinuous(int64_t seq_num);

  void FrameDecoded(int64_t seq_num);

  void SignalNetworkState(NetworkState state);

  // Returns number of different frames seen in the packet buffer.
  int GetUniqueFramesSeen() const;

  // Implements RtpPacketSinkInterface.
  void OnRtpPacket(const RtpPacketReceived& packet) override;

  // TODO(philipel): Stop using VCMPacket in the new jitter buffer and then
  //                 remove this function. Public only for tests.
  int32_t OnReceivedPayloadData(
      const uint8_t* payload_data,
      size_t payload_size,
      const RTPHeader& rtp_header,
      const RTPVideoHeader& video_header,
      const std::optional<RtpGenericFrameDescriptor>& generic_descriptor,
      bool is_recovered);

  // Implements RecoveredPacketReceiver.
  void OnRecoveredPacket(const uint8_t* packet, size_t packet_length) override;

  // Send an RTCP keyframe request.
  void RequestKeyFrame() override;

  // Implements LossNotificationSender.
  void SendLossNotification(uint16_t last_decoded_seq_num,
                            uint16_t last_received_seq_num,
                            bool decodability_flag,
                            bool buffering_allowed) override;

  bool IsUlpfecEnabled() const;
  bool IsRetransmissionsEnabled() const;

  // Returns true if a decryptor is attached and frames can be decrypted.
  // Updated by OnDecryptionStatusChangeCallback. Note this refers to Frame
  // Decryption not SRTP.
  //bool IsDecryptable() const;

  // Don't use, still experimental.
  void RequestPacketRetransmit(const std::vector<uint16_t>& sequence_numbers);

  // Implements OnAssembledFrameCallback.
  void OnAssembledFrame(
      std::unique_ptr<video_coding::RtpFrameObject> frame) override;

  // Implements OnCompleteFrameCallback.
  void OnCompleteFrame(
      std::unique_ptr<video_coding::EncodedFrame> frame) override;

  // Called by VideoReceiveStream when stats are updated.
  void UpdateRtt(int64_t max_rtt_ms);

  std::optional<int64_t> LastReceivedPacketMs() const;
  std::optional<int64_t> LastReceivedKeyframePacketMs() const;

 private:
  // Used for buffering RTCP feedback messages and sending them all together.
  // Note:
  // 1. Key frame requests and NACKs are mutually exclusive, with the
  //    former taking precedence over the latter.
  // 2. Loss notifications are orthogonal to either. (That is, may be sent
  //    alongside either.)
  class RtcpFeedbackBuffer : public KeyFrameRequestSender,
                             public NackSender,
                             public LossNotificationSender {
   public:
    RtcpFeedbackBuffer(KeyFrameRequestSender* key_frame_request_sender,
                       NackSender* nack_sender,
                       LossNotificationSender* loss_notification_sender);

    ~RtcpFeedbackBuffer() override = default;

    // KeyFrameRequestSender implementation.
    void RequestKeyFrame() override;

    // NackSender implementation.
    void SendNack(const std::vector<uint16_t>& sequence_numbers,
                  bool buffering_allowed) override;

    // LossNotificationSender implementation.
    void SendLossNotification(uint16_t last_decoded_seq_num,
                              uint16_t last_received_seq_num,
                              bool decodability_flag,
                              bool buffering_allowed) override;

    // Send all RTCP feedback messages buffered thus far.
    void SendBufferedRtcpFeedback();

   private:
    KeyFrameRequestSender* const key_frame_request_sender_;
    NackSender* const nack_sender_;
    LossNotificationSender* const loss_notification_sender_;

    // Key-frame-request-related state.
    bool request_key_frame_;

    // NACK-related state.
    std::vector<uint16_t> nack_sequence_numbers_;

    // LNTF-related state.
    struct LossNotificationState {
      LossNotificationState(uint16_t last_decoded_seq_num,
                            uint16_t last_received_seq_num,
                            bool decodability_flag)
          : last_decoded_seq_num(last_decoded_seq_num),
            last_received_seq_num(last_received_seq_num),
            decodability_flag(decodability_flag) {}

      uint16_t last_decoded_seq_num;
      uint16_t last_received_seq_num;
      bool decodability_flag;
    };
    std::optional<LossNotificationState> lntf_state_;
  };

  // Entry point doing non-stats work for a received packet. Called
  // for the same packet both before and after RED decapsulation.
  void ReceivePacket(const RtpPacketReceived& packet);
  // Parses and handles RED headers.
  // This function assumes that it's being called from only one thread.
  void ParseAndHandleEncapsulatingHeader(const RtpPacketReceived& packet);
  void NotifyReceiverOfEmptyPacket(uint16_t seq_num);
  void UpdateHistograms();
  bool IsRedEnabled() const;
  void InsertSpsPpsIntoTracker(uint8_t payload_type);

  Clock* const clock_;
  // Ownership of this object lies with VideoReceiveStream, which owns |this|.
  const VideoReceiveStream::Config& config_;
  PacketRouter* const packet_router_;
  ProcessThread* const process_thread_;

  RemoteNtpTimeEstimator ntp_estimator_;

  RtpHeaderExtensionMap rtp_header_extensions_;
  ReceiveStatistics* const rtp_receive_statistics_;
  std::unique_ptr<UlpfecReceiver> ulpfec_receiver_;

  SequenceChecker worker_task_checker_;
  bool receiving_{false} RTC_GUARDED_BY(worker_task_checker_);
  int64_t last_packet_log_ms_{-1} RTC_GUARDED_BY(worker_task_checker_);

  const std::unique_ptr<RtpRtcp> rtp_rtcp_;

  video_coding::OnCompleteFrameCallback* complete_frame_callback_;

  RtcpFeedbackBuffer rtcp_feedback_buffer_;
  std::unique_ptr<NackModule> nack_module_;
  std::unique_ptr<LossNotificationController> loss_notification_controller_;

  video_coding::PacketBuffer packet_buffer_;

  std::unique_ptr<video_coding::RtpFrameReferenceFinder> reference_finder_;
  std::optional<VideoCodecType> current_codec_;
  uint32_t last_assembled_frame_rtp_timestamp_;

  std::map<int64_t, uint16_t> last_seq_num_for_pic_id_;
  video_coding::H264SpsPpsTracker tracker_;

  // Maps payload type to codec type, for packetization.
  std::map<uint8_t, std::optional<VideoCodecType>> payload_type_map_;

  // TODO(johan): Remove pt_codec_params_ once
  // https://bugs.chromium.org/p/webrtc/issues/detail?id=6883 is resolved.
  // Maps a payload type to a map of out-of-band supplied codec parameters.
  std::map<uint8_t, std::map<std::string, std::string>> pt_codec_params_;
  int16_t last_payload_type_ = -1;

  bool has_received_frame_ = false;

  std::optional<uint32_t> last_received_rtp_timestamp_;
  std::optional<int64_t> last_received_rtp_system_time_ms_;

  // Used to validate the buffered frame decryptor is always run on the correct
  // thread.
  rtc::ThreadChecker network_tc_;

  std::optional<ColorSpace> last_color_space_;

  int64_t last_completed_picture_id_ = 0;
};

}  // namespace webrtc

#endif  // VIDEO_RTP_VIDEO_STREAM_RECEIVER_H_
