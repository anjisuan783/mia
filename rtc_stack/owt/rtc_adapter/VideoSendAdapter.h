// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

#ifndef RTC_ADAPTER_VIDEO_SEND_ADAPTER_
#define RTC_ADAPTER_VIDEO_SEND_ADAPTER_

#include <memory>

#include "api/field_trial_based_config.h"
#include "rtp_rtcp/rtp_rtcp.h"
#include "rtp_rtcp/rtp_rtcp_defines.h"
#include "rtp_rtcp/rtp_sender_video.h"
#include "rtc_base/rate_limiter.h"
#include "rtc_base/time_utils.h"
#include "rtc_adapter/RtcAdapter.h"
#include "rtc_adapter/AdapterInternalDefinitions.h"

#include "owt_base/MediaFramePipeline.h"
#include "owt_base/SsrcGenerator.h"
#include "rtc_base/location.h"
#include "utility/process_thread.h"


namespace rtc_adapter {

class VideoSendAdapterImpl : public VideoSendAdapter,
                             public webrtc::Transport,
                             public webrtc::RtcpIntraFrameObserver {
 public:
  VideoSendAdapterImpl(CallOwner* owner, const RtcAdapter::Config& config);
  ~VideoSendAdapterImpl();

  // Implement VideoSendAdapter
  void onFrame(std::shared_ptr<owt_base::Frame>) override;
  int onRtcpData(char* data, int len) override;
  void reset() override;

  uint32_t ssrc() { return ssrc_; }

  // Implement webrtc::Transport
  bool SendRtp(const uint8_t* packet,
               size_t length,
               const webrtc::PacketOptions& options) override;
  bool SendRtcp(const uint8_t* packet, size_t length) override;

  // Implements webrtc::RtcpIntraFrameObserver.
  void OnReceivedIntraFrameRequest(uint32_t ssrc) override;

 private:
  bool init();

  bool enableDump_{false};
  RtcAdapter::Config config_;

  bool keyFrameArrived_{false};
  std::unique_ptr<webrtc::RateLimiter> retransmissionRateLimiter_;
  std::unique_ptr<webrtc::RtpRtcp> rtpRtcp_;

  owt_base::FrameFormat frameFormat_;
  uint16_t frameWidth_{0};
  uint16_t frameHeight_{0};
  uint32_t ssrc_{0};
  owt_base::SsrcGenerator* const ssrcGenerator_;

  webrtc::Clock* m_clock{nullptr};
  int64_t timeStampOffset_{0};

  webrtc::RtcEventLog* eventLog_;
  std::unique_ptr<webrtc::RTPSenderVideo> senderVideo_;
  std::unique_ptr<webrtc::PlayoutDelayOracle> playoutDelayOracle_;
  std::unique_ptr<webrtc::FieldTrialBasedConfig> fieldTrialConfig_;

  // Listeners
  AdapterFeedbackListener* feedbackListener_;
  AdapterDataListener* dataListener_;
  AdapterStatsListener* statsListener_;

  std::unique_ptr<webrtc::ProcessThread> taskRunner_;
};

} //namespace owt

#endif /* RTC_ADAPTER_VIDEO_SEND_ADAPTER_ */
