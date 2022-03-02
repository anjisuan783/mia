// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

#include "owt_base/VideoFrameConstructor.h"

#include <future>
#include <random>
#include <math.h>

#include "common/rtputils.h"
#include "myrtc/api/task_queue_base.h"
#include "rtp_rtcp/byte_io.h"

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
  close();
}

void VideoFrameConstructor::close() {
  unbindTransport();
  if (videoReceive_) {
    rtcAdapter_->destoryVideoReceiver(videoReceive_);
    videoReceive_ = nullptr;
  }
  rtcAdapter_ = nullptr;
}

void VideoFrameConstructor::bindTransport(
    erizo::MediaSource* source, erizo::FeedbackSink* fbSink) {
  if (!source) return ;
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

void VideoFrameConstructor::onAdapterFrame(std::shared_ptr<Frame> frame) {
  if (enable_) {
    frame->ntpTimeMs = getNtpTimestamp(frame->timeStamp);
    deliverFrame(std::move(frame));
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
  char* data = video_packet->data;
  int len = video_packet->length;
  erizo::RtcpHeader* chead = 
      reinterpret_cast<erizo::RtcpHeader*>(data);

  if (chead->isRtcp()) {
    if (chead->getPacketType() == RTCP_Sender_PT)
      onSr(chead);

    if (videoReceive_)
      videoReceive_->onRtpData(video_packet.get());
    return len;
  }

  RTPHeader* head = reinterpret_cast<RTPHeader*>(data);
  if (!ssrc_ && head->getSSRC()) {
    createReceiveVideo(head->getSSRC());
  }
  if (videoReceive_) {
    videoReceive_->onRtpData(video_packet.get());
  }

  return len;
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

void VideoFrameConstructor::onSr(erizo::RtcpHeader *chead) {
  const uint8_t* const payload = reinterpret_cast<const uint8_t*>(&(chead->ssrc));
  uint32_t ntp_secs = webrtc::ByteReader<uint32_t>::ReadBigEndian(&payload[4]);
  uint32_t ntp_frac = webrtc::ByteReader<uint32_t>::ReadBigEndian(&payload[8]);
  webrtc::NtpTime ntp;
  ntp.Set(ntp_secs, ntp_frac);
  
  uint32_t rtp_timestamp = webrtc::ByteReader<uint32_t>::ReadBigEndian(&payload[12]);  
  bool new_rtcp_sr = false;
  ntp_estimator_.UpdateMeasurements(ntp_secs, ntp_frac, rtp_timestamp, &new_rtcp_sr);
}

int64_t VideoFrameConstructor::getNtpTimestamp(uint32_t ts) {
  int64_t ntp = 0;

  if(!ntp_estimator_.Estimate(ts, &ntp))
    return -1;

  return ntp;
}

}

