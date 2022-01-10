// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

#include "owt_base/VideoFrameConstructor.h"

#include <iostream>
#include <future>
#include <random>
#include "common/rtputils.h"
#include "myrtc/api/task_queue_base.h"

using namespace rtc_adapter;

namespace owt_base {

DEFINE_LOGGER(VideoFrameConstructor, "owt.VideoFrameConstructor");

VideoFrameConstructor::VideoFrameConstructor(
    VideoInfoListener* vil, const config& config)
  : config_{config},
    videoInfoListener_{vil},
    rtcAdapter_{std::move(config.factory->CreateRtcAdapter())},
    worker_{config.worker} {
}

VideoFrameConstructor::~VideoFrameConstructor() {
  unbindTransport();
  if (videoReceive_) {
    rtcAdapter_->destoryVideoReceiver(videoReceive_);
    videoReceive_ = nullptr;
  }
}

void VideoFrameConstructor::bindTransport(
    erizo::MediaSource* source, erizo::FeedbackSink* fbSink) {
  transport_ = source;
  transport_->setVideoSink(this);
  transport_->setEventSink(this);
  setFeedbackSink(fbSink);
}

void VideoFrameConstructor::unbindTransport() {
  if (transport_) {
    setFeedbackSink(nullptr);
    transport_ = nullptr;
  }
}

void VideoFrameConstructor::enable(bool enabled) {
  enable_ = enabled;
  RequestKeyFrame();
}

int32_t VideoFrameConstructor::RequestKeyFrame() {
  if (!enable_) {
    return 0;
  }
  if (videoReceive_) {
    videoReceive_->requestKeyFrame();
  }
  return 0;
}

bool VideoFrameConstructor::setBitrate(uint32_t kbps) {
  // At present we do not react on this request
  return true;
}

void VideoFrameConstructor::onAdapterFrame(const Frame& frame) {
  if (enable_) {
    deliverFrame(frame);
  }
}

void VideoFrameConstructor::onAdapterStats(const AdapterStats& stats) {
  if (videoInfoListener_) {
    std::ostringstream json_str;
    json_str.str("");
    json_str << "{\"video\": {\"parameters\": {\"resolution\": {"
             << "\"width\":" << stats.width << ", "
             << "\"height\":" << stats.height
             << "}}}}";
    videoInfoListener_->onVideoInfo(json_str.str().c_str());
  }
}

void VideoFrameConstructor::onAdapterData(char* data, int len) {
  if (len <= 0) {
    return;
  }
  // Data come from video receive stream is RTCP
  if (fb_sink_) {
    fb_sink_->deliverFeedback(
      std::make_shared<erizo::DataPacket>(0, data, len, erizo::VIDEO_PACKET));
  }
}

int VideoFrameConstructor::deliverVideoData_(
    std::shared_ptr<erizo::DataPacket> video_packet) {
  RTCPHeader* chead = reinterpret_cast<RTCPHeader*>(video_packet->data);
  uint8_t packetType = chead->getPacketType();

  assert(packetType != RTCP_Receiver_PT && 
         packetType != RTCP_PS_Feedback_PT && 
         packetType != RTCP_RTP_Feedback_PT);
  if (videoReceive_ && 
      (packetType == RTCP_SDES_PT || 
       packetType == RTCP_Sender_PT || 
       packetType == RTCP_XR_PT) ) {
    videoReceive_->onRtpData(video_packet->data, video_packet->length);
    return video_packet->length;
  }

  if (packetType >= RTCP_MIN_PT && packetType <= RTCP_MAX_PT) {
    return 0;
  }

  RTPHeader* head = reinterpret_cast<RTPHeader*>(video_packet->data);
  if (!ssrc_ && head->getSSRC()) {
    createReceiveVideo(head->getSSRC());
  }
  if (videoReceive_) {
    videoReceive_->onRtpData(video_packet->data, video_packet->length);
  }

  return video_packet->length;
}

int VideoFrameConstructor::deliverAudioData_(
    std::shared_ptr<erizo::DataPacket> audio_packet) {
  assert(false);
  return 0;
}

void VideoFrameConstructor::onTimeout() {
  if (pendingKeyFrameRequests_ > 1) {
      RequestKeyFrame();
  }
  pendingKeyFrameRequests_ = 0;
}

void VideoFrameConstructor::onFeedback(const FeedbackMsg& msg) {
  if (msg.type != owt_base::VIDEO_FEEDBACK) {
    return;
  }
  auto share_this = 
      std::dynamic_pointer_cast<VideoFrameConstructor>(shared_from_this());
  std::weak_ptr<VideoFrameConstructor> weak_this = share_this;
  
  worker_->task([msg, weak_this, this]() {
    if (auto share_this = weak_this.lock()) {
      if (msg.cmd == REQUEST_KEY_FRAME) {
        if (!pendingKeyFrameRequests_) {
          RequestKeyFrame();
        }
        ++pendingKeyFrameRequests_;
      } else if (msg.cmd == SET_BITRATE) {
        setBitrate(msg.data.kbps);
      }      
    }
  });
}

void VideoFrameConstructor::close() {
  unbindTransport();
}

void VideoFrameConstructor::createReceiveVideo(uint32_t ssrc) {
  if (videoReceive_) {
    return;
  }
  
  auto share_this = 
        std::dynamic_pointer_cast<VideoFrameConstructor>(shared_from_this());
  std::weak_ptr<VideoFrameConstructor> weak_this = share_this;
  worker_->scheduleEvery([weak_this] () {
    if (auto p = weak_this.lock()) {
      p->onTimeout();
      return true;
    }
    return false;
  }, std::chrono::seconds(1));
  
  ssrc_ = config_.ssrc;

  // Create Receive Video Stream
  rtc_adapter::RtcAdapter::Config recvConfig;

  recvConfig.ssrc = config_.ssrc;
  recvConfig.rtx_ssrc = config_.rtx_ssrc;
  recvConfig.rtcp_rsize = config_.rtcp_rsize;
  recvConfig.rtp_payload_type = config_.rtp_payload_type;
  recvConfig.transport_cc = config_.transportcc;
  recvConfig.red_payload = config_.red_payload;
  recvConfig.ulpfec_payload = config_.ulpfec_payload;
  recvConfig.flex_fec = config_.flex_fec;
  recvConfig.rtp_listener = this;
  recvConfig.stats_listener = this;
  recvConfig.frame_listener = this;

  videoReceive_ = rtcAdapter_->createVideoReceiver(recvConfig);
}

}

