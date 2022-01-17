// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

#include "rtc_adapter/AudioSendAdapter.h"

#include <memory>

#include "rtc_base/logging.h"
#include "rtp_rtcp/rtp_packet.h"
#include "owt_base/AudioUtilitiesNew.h"
#include "owt_base/TaskRunnerPool.h"
#include "common/rtputils.h"
#include "rtc_adapter/thread/ProcessThreadMock.h"

using namespace owt_base;

namespace rtc_adapter {

const uint32_t kSeqNoStep = 10;

AudioSendAdapterImpl::AudioSendAdapterImpl(
    CallOwner* callowner, const RtcAdapter::Config& config)
    : frameFormat_(FRAME_FORMAT_UNKNOWN),
      lastOriginSeqNo_(0),
      seqNo_(0),
      ssrc_(0),
      ssrcGenerator_(SsrcGenerator::GetSsrcGenerator()), 
      config_(config), 
      rtpListener_(config.rtp_listener), 
      statsListener_(config.stats_listener), 
      taskRunner_{std::make_unique<ProcessThreadMock>(
          callowner->taskQueue().get())} {
  ssrc_ = ssrcGenerator_->CreateSsrc();
  ssrcGenerator_->RegisterSsrc(ssrc_);
  init();
}

AudioSendAdapterImpl::~AudioSendAdapterImpl() {
  close();
  ssrcGenerator_->ReturnSsrc(ssrc_);
}

int AudioSendAdapterImpl::onRtcpData(char* data, int len) {
  if (rtpRtcp_) {
    rtpRtcp_->IncomingRtcpPacket(reinterpret_cast<uint8_t*>(data), len);
  }
  return len;
}

void AudioSendAdapterImpl::onFrame(std::shared_ptr<owt_base::Frame> frame) {
  if (frame->format != frameFormat_) {
    frameFormat_ = frame->format;
    setSendCodec(frameFormat_);
  }

  if (frame->additionalInfo.audio.isRtpPacket) {
    // FIXME: Temporarily use Frame to carry rtp-packets
    // due to the premature AudioFrameConstructor implementation.
    updateSeqNo(frame->payload);
    if (rtpListener_) {
      if (!mid_.empty()) {
        webrtc::RtpPacket packet(&extensions_);
        packet.Parse(frame->payload, frame->length);
        packet.SetExtension<webrtc::RtpMid>(mid_);
        rtpListener_->onAdapterData(
            reinterpret_cast<char*>(const_cast<uint8_t*>(packet.data())), 
            packet.size());
      } else {
        rtpListener_->onAdapterData(
            reinterpret_cast<char*>(frame->payload), frame->length);
      }
    }
  } else {
    int payloadType = getAudioPltype(frame->format);
    if (payloadType != INVALID_PT) {
      if (rtpRtcp_->OnSendingRtpFrame(frame->timeStamp, 
                                       -1, payloadType, false)) {
        const uint32_t rtp_timestamp = 
            frame->timeStamp + rtpRtcp_->StartTimestamp();
        // TODO: The frame type information is lost.
        // We treat every frame a kAudioFrameSpeech frame for now.
        if (!senderAudio_->SendAudio(webrtc::AudioFrameType::kAudioFrameSpeech,
                payloadType, rtp_timestamp,
                frame->payload, frame->length)) {
          RTC_DLOG(LS_ERROR) << 
              "ChannelSend failed to send data to RTP/RTCP module";
        }
      } else {
         RTC_DLOG(LS_WARNING) << "OnSendingRtpFrame return false";
      }
    }
  }
}

bool AudioSendAdapterImpl::init() {
  clock_ = webrtc::Clock::GetRealTimeClock();
  eventLog_ = std::make_unique<webrtc::RtcEventLogNull>();

  webrtc::RtpRtcp::Configuration configuration;
  configuration.clock = clock_;
  configuration.audio = true; // Audio.
  configuration.receiver_only = false; //send
  configuration.outgoing_transport = this;
  configuration.event_log = eventLog_.get();
  configuration.local_media_ssrc = ssrc_;
  configuration.extmap_allow_mixed = true;

  if (config_.rtx_ssrc) {
    configuration.rtx_send_ssrc = config_.rtx_ssrc;
  }
  
  rtpRtcp_ = webrtc::RtpRtcp::Create(configuration);
  rtpRtcp_->SetSendingStatus(true);
  rtpRtcp_->SetSendingMediaStatus(true);
  
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
    // TODO: Remove extensions_ if frames do not carry rtp
    extensions_.Register<webrtc::RtpMid>(config_.mid_ext);
    mid_ = mid;
  }
  senderAudio_ = std::make_unique<webrtc::RTPSenderAudio>(
      configuration.clock, rtpRtcp_->RtpSender());
  taskRunner_->RegisterModule(rtpRtcp_.get(), RTC_FROM_HERE);

  return true;
}

bool AudioSendAdapterImpl::setSendCodec(FrameFormat format) {
  CodecInst codec;

  if (!getAudioCodecInst(frameFormat_, codec)) {
    return false;
  }

  rtpRtcp_->RegisterSendPayloadFrequency(codec.pltype, codec.plfreq);
  senderAudio_->RegisterAudioPayload("audio", codec.pltype,
      codec.plfreq, codec.channels, 0);
  return true;
}

void AudioSendAdapterImpl::close() {
  taskRunner_->DeRegisterModule(rtpRtcp_.get());
}

bool AudioSendAdapterImpl::SendRtp(const uint8_t* packet,
    size_t length,
    const webrtc::PacketOptions&) {
  if (rtpListener_) {
    rtpListener_->onAdapterData(
        reinterpret_cast<char*>(const_cast<uint8_t*>(packet)), length);
    return true;
  }
  return false;
}

bool AudioSendAdapterImpl::SendRtcp(const uint8_t* packet, size_t length) {
  const RTCPHeader* chead = reinterpret_cast<const RTCPHeader*>(packet);
  uint8_t packetType = chead->getPacketType();
  RTC_DLOG_F(LS_WARNING) << "pt:" << packetType;
  if (packetType == RTCP_Sender_PT) {
    if (rtpListener_) {
      rtpListener_->onAdapterData(
          reinterpret_cast<char*>(const_cast<uint8_t*>(packet)), length);
      return true;
    }
  }
  return false;
}

void AudioSendAdapterImpl::updateSeqNo(uint8_t* rtp) {
  uint16_t originSeqNo = *(reinterpret_cast<uint16_t*>(&rtp[2]));
  if ((lastOriginSeqNo_ == seqNo_) && (seqNo_ == 0)) {
  } else {
    uint16_t step = ((originSeqNo < lastOriginSeqNo_) ? 
                    (UINT16_MAX - lastOriginSeqNo_ + originSeqNo + 1) : 
                    (originSeqNo - lastOriginSeqNo_));
    if ((step == 1) || (step > kSeqNoStep)) {
      seqNo_ += 1;
    } else {
      seqNo_ += step;
    }
  }
  lastOriginSeqNo_ = originSeqNo;
  *(reinterpret_cast<uint16_t*>(&rtp[2])) = htons(seqNo_);
}

} //namespacertc_adapter
