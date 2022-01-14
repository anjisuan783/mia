// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

#include "rtc_adapter/VideoSendAdapter.h"

#include "api/rtc_event_log.h"
#include "api/video_codec_type.h"
#include "api/video_codec.h"
#include "module/module_common_types.h"
#include "rtp_rtcp/rtp_video_header.h"
#include "rtc_base/logging.h"

#include "common/rtputils.h"
#include "owt_base/MediaUtilities.h"
#include "owt_base/TaskRunnerPool.h"
#include "rtc_adapter/thread/ProcessThreadMock.h"

using namespace owt_base;

namespace rtc_adapter {
  
static void dump(void* index, FrameFormat format, uint8_t* buf, int len) {
  char dumpFileName[128];

  snprintf(dumpFileName, 128, "/tmp/prePacketizer-%p.%s", index, getFormatStr(format));
  FILE* bsDumpfp = fopen(dumpFileName, "ab");
  if (bsDumpfp) {
      fwrite(buf, 1, len, bsDumpfp);
      fclose(bsDumpfp);
  }
}

//VideoSendAdapterImpl
VideoSendAdapterImpl::VideoSendAdapterImpl(CallOwner* callowner, 
                                           const RtcAdapter::Config& config)
    : config_(config),
      frameFormat_(FRAME_FORMAT_UNKNOWN),
      ssrcGenerator_(SsrcGenerator::GetSsrcGenerator()),
      feedbackListener_(config.feedback_listener),
      dataListener_(config.rtp_listener),
      statsListener_(config.stats_listener),
      taskRunner_(std::make_unique<ProcessThreadMock>(
          callowner->taskQueue().get())) {
    ssrc_ = ssrcGenerator_->CreateSsrc();
    ssrcGenerator_->RegisterSsrc(ssrc_);
    init();
}

VideoSendAdapterImpl::~VideoSendAdapterImpl() {
    taskRunner_->DeRegisterModule(rtpRtcp_.get());
    ssrcGenerator_->ReturnSsrc(ssrc_);
}

bool VideoSendAdapterImpl::init() {
  m_clock = webrtc::Clock::GetRealTimeClock();
  retransmissionRateLimiter_ = std::move(std::make_unique<webrtc::RateLimiter>(
      m_clock, 1000));

  //configure rtp_rtcp
  eventLog_ = std::make_unique<webrtc::RtcEventLogNull>();
  webrtc::RtpRtcp::Configuration configuration;
  configuration.clock = m_clock;
  configuration.audio = false;
  configuration.receiver_only = false;
  configuration.outgoing_transport = this;
  configuration.intra_frame_callback = this;
  configuration.event_log = eventLog_.get();
  configuration.retransmission_rate_limiter = retransmissionRateLimiter_.get();
  configuration.local_media_ssrc = ssrc_;
  configuration.extmap_allow_mixed = true;

  if (config_.rtx_ssrc) {
    configuration.rtx_send_ssrc = config_.rtx_ssrc;
  }

  rtpRtcp_ = webrtc::RtpRtcp::Create(configuration);
  rtpRtcp_->SetSendingStatus(true);
  rtpRtcp_->SetSendingMediaStatus(true);
  rtpRtcp_->SetRtcpXrRrtrStatus(true);

  webrtc::RtcpMode mode = webrtc::RtcpMode::kCompound;
  if (config_.rtcp_rsize) {
    mode = webrtc::RtcpMode::kReducedSize;
  }
  rtpRtcp_->SetRTCPStatus(mode);
  
  // Set NACK.
  rtpRtcp_->SetStorePacketsStatus(true, 600);
  
  if (config_.transport_cc != -1) {
    rtpRtcp_->RegisterRtpHeaderExtension(
      webrtc::RtpExtension::kTransportSequenceNumberUri, config_.transport_cc);
  }
  
  if (config_.mid_ext) {
    config_.mid[sizeof(config_.mid) - 1] = '\0';
    std::string mid(config_.mid);
    // Register MID extension
    rtpRtcp_->RegisterRtpHeaderExtension(
        webrtc::RtpExtension::kMidUri, config_.mid_ext);
    rtpRtcp_->SetMid(mid);
  }

  // configure rtp sender
  webrtc::RTPSenderVideo::Config video_config;
  playoutDelayOracle_ = std::make_unique<webrtc::PlayoutDelayOracle>();
  fieldTrialConfig_ = std::make_unique<webrtc::FieldTrialBasedConfig>();
  video_config.clock = configuration.clock;
  video_config.rtp_sender = rtpRtcp_->RtpSender();
  video_config.field_trials = fieldTrialConfig_.get();
  video_config.playout_delay_oracle = playoutDelayOracle_.get();
  if (config_.red_payload != -1) {
    video_config.red_payload_type = config_.red_payload;
  }
  if (config_.ulpfec_payload != -1) {
    video_config.ulpfec_payload_type = config_.ulpfec_payload;
  }

  senderVideo_ = std::make_unique<webrtc::RTPSenderVideo>(video_config);
  taskRunner_->RegisterModule(rtpRtcp_.get(), RTC_FROM_HERE);

  return true;
}

void VideoSendAdapterImpl::reset() {
  keyFrameArrived_ = false;
  timeStampOffset_ = 0;
}

void VideoSendAdapterImpl::onFrame(const Frame& frame) {
  if (frame.format != FRAME_FORMAT_H264) {
    assert(false);
    return;
  }
  
  using namespace webrtc;

  if (!keyFrameArrived_) {
    if (!frame.additionalInfo.video.isKeyFrame) {
      if (feedbackListener_) {
        FeedbackMsg feedback = {.type = VIDEO_FEEDBACK, .cmd = REQUEST_KEY_FRAME };
        feedbackListener_->onFeedback(feedback);
      }
      return;
    }
    
    // Recalculate timestamp offset
    static constexpr uint32_t kMsToRtpTimestamp = 90;
    timeStampOffset_ = 
        kMsToRtpTimestamp * m_clock->TimeInMilliseconds() - frame.timeStamp;
    keyFrameArrived_ = true;
  }

  // Recalculate timestamp for stream substitution
  uint32_t timeStamp = frame.timeStamp + timeStampOffset_;
  webrtc::RTPVideoHeader h;
  memset(&h, 0, sizeof(webrtc::RTPVideoHeader));

  if (frame.format != frameFormat_
      || frame.additionalInfo.video.width != frameWidth_
      || frame.additionalInfo.video.height != frameHeight_) {
      frameFormat_ = frame.format;
      frameWidth_ = frame.additionalInfo.video.width;
      frameHeight_ = frame.additionalInfo.video.height;
  }

  h.frame_type = frame.additionalInfo.video.isKeyFrame ? 
                 VideoFrameType::kVideoFrameKey : 
                 VideoFrameType::kVideoFrameDelta;
 
  h.width = frameWidth_;
  h.height = frameHeight_;

  int frame_length = frame.length;
  if (enableDump_) {
    dump(this, frame.format, frame.payload, frame_length);
  }

  h.codec = webrtc::VideoCodecType::kVideoCodecH264;
  
  int nalu_found_length = 0;
  uint8_t* buffer_start = frame.payload;
  int buffer_length = frame_length;
  int nalu_start_offset = 0;
  int nalu_end_offset = 0;
  int sc_len = 0;
  RTPFragmentationHeader frag_info;
  
  while (buffer_length > 0) {
    nalu_found_length = findNALU(buffer_start,
                                 buffer_length,
                                 &nalu_start_offset,
                                 &nalu_end_offset,
                                 &sc_len);
    if (nalu_found_length >= 0) {
      /* SPS, PPS, I, P*/
      uint16_t last = frag_info.fragmentationVectorSize;
      frag_info.VerifyAndAllocateFragmentationHeader(last + 1);
      frag_info.fragmentationOffset[last] = 
          nalu_start_offset + (buffer_start - frame.payload);
      frag_info.fragmentationLength[last] = nalu_found_length;
      buffer_start += (nalu_start_offset + nalu_found_length);
      buffer_length -= (nalu_start_offset + nalu_found_length);
    } else {
      /* Error, should never happen */
      assert(false);
      break;
    }
  }

  h.video_type_header.emplace<RTPVideoHeaderH264>();
  senderVideo_->SendVideo(
      H264_90000_PT,
      webrtc::kVideoCodecH264,
      timeStamp,
      timeStamp,
      rtc::ArrayView<const uint8_t>(frame.payload, frame.length),
      &frag_info,
      h,
      rtpRtcp_->ExpectedRetransmissionTimeMs());
}

int VideoSendAdapterImpl::onRtcpData(char* data, int len) {
  if (rtpRtcp_) {
    rtpRtcp_->IncomingRtcpPacket(reinterpret_cast<uint8_t*>(data), len);
    return len;
  }
  return 0;
}

bool VideoSendAdapterImpl::SendRtp(
    const uint8_t* data,
    size_t length,
    const webrtc::PacketOptions&) {
  if (dataListener_) {
    dataListener_->onAdapterData(
        reinterpret_cast<char*>(const_cast<uint8_t*>(data)), length);
    return true;
  }
  return false;
}

bool VideoSendAdapterImpl::SendRtcp(const uint8_t* data, size_t length) {
  if (dataListener_) {
    dataListener_->onAdapterData(
        reinterpret_cast<char*>(const_cast<uint8_t*>(data)), length);
    return true;
  }
  return false;
}

void VideoSendAdapterImpl::OnReceivedIntraFrameRequest(uint32_t ssrc) {
  if (feedbackListener_) {
    FeedbackMsg feedback = {.type = VIDEO_FEEDBACK, .cmd = REQUEST_KEY_FRAME };
    feedbackListener_->onFeedback(feedback);
  }
}

} // namespace rtc_adapter

