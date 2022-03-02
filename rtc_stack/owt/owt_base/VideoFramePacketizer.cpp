// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

#include "owt_base/VideoFramePacketizer.h"
#include "owt_base/MediaUtilities.h"
#include "common/rtputils.h"
#include "myrtc/api/task_queue_base.h"
#include "rtc_adapter/thread/StaticTaskQueueFactory.h"

using namespace rtc_adapter;

namespace owt_base {

// To make it consistent with the webrtc library, 
// we allow packets to be transmitted
// in up to 2 times max video bitrate if the bandwidth estimate allows it.
static const int TRANSMISSION_MAXBITRATE_MULTIPLIER = 2;

DEFINE_LOGGER(VideoFramePacketizer, "owt.VideoFramePacketizer");

VideoFramePacketizer::VideoFramePacketizer(VideoFramePacketizer::Config& config)
    : rtcAdapter_{std::move(config.factory->CreateRtcAdapter())} {
  auto factory = rtc_adapter::createDummyTaskQueueFactory(config.task_queue);
  auto task_queue = factory->CreateTaskQueue(
      "deliver_frame", webrtc::TaskQueueFactory::Priority::NORMAL);
  task_queue_ = std::move(
      std::make_unique<rtc::TaskQueue>(std::move(task_queue)));
  
  init(config);
}

VideoFramePacketizer::~VideoFramePacketizer() {
  close();
}

void VideoFramePacketizer::close() {
  unbindTransport();
  if (videoSend_) {
    rtcAdapter_->destoryVideoSender(videoSend_);
    videoSend_ = nullptr;
  }
  rtcAdapter_ = nullptr;
}

bool VideoFramePacketizer::init(VideoFramePacketizer::Config& config) {
  if (videoSend_) {
    return false;
  }
  
  // Create Send Video Stream
  rtc_adapter::RtcAdapter::Config sendConfig;

  sendConfig.transport_cc = config.transportccExt;
  sendConfig.red_payload = config.Red;
  sendConfig.ulpfec_payload = config.Ulpfec;
  if (!config.mid.empty()) {
    strncpy(sendConfig.mid, config.mid.c_str(), sizeof(sendConfig.mid) - 1);
    sendConfig.mid_ext = config.midExtId;
  }
  sendConfig.feedback_listener = this;
  sendConfig.rtp_listener = this;
  sendConfig.stats_listener = this;
  videoSend_ = rtcAdapter_->createVideoSender(sendConfig);
  ssrc_ = videoSend_->ssrc();
  return true;
}

void VideoFramePacketizer::bindTransport(erizo::MediaSink* sink) {
  video_sink_ = sink;
  video_sink_->setVideoSinkSSRC(videoSend_->ssrc());
  erizo::FeedbackSource* fbSource = video_sink_->getFeedbackSource();
  if (fbSource)
      fbSource->setFeedbackSink(this);
}

void VideoFramePacketizer::unbindTransport() {
  if (video_sink_) {
    video_sink_ = nullptr;
  }
}

void VideoFramePacketizer::enable(bool enabled) {
  enabled_ = enabled;
  if (enabled_) {
    sendFrameCount_ = 0;
    if (videoSend_) {
      videoSend_->reset();
    }
  }
}

void VideoFramePacketizer::onFeedback(const FeedbackMsg& msg) {
  deliverFeedbackMsg(msg);
}

void VideoFramePacketizer::onAdapterData(char* data, int len) {
  if (!video_sink_) {
    return;
  }

  video_sink_->deliverVideoData(std::make_shared<erizo::DataPacket>(
      0, data, len, erizo::VIDEO_PACKET));
}

void VideoFramePacketizer::onFrame(std::shared_ptr<Frame> f) {
  if (f->length <= 0) {
    return ;
  }
  
  task_queue_->PostTask(
      [this, weak_ptr = weak_from_this(), frame = std::move(f)] () {
        if (auto shared_this = weak_ptr.lock()) {
          if (!enabled_) {
            return;
          }

          if (videoSend_) {
            videoSend_->onFrame(std::move(frame));
          }
        }
      }
  );
}

void VideoFramePacketizer::onVideoSourceChanged() {
  ELOG_TRACE("onVideoSourceChanged");
  if (videoSend_) {
    videoSend_->reset();
  }
}

int VideoFramePacketizer::deliverFeedback_(std::shared_ptr<erizo::DataPacket> data_packet) {
  if (videoSend_) {
    videoSend_->onRtcpData(data_packet->data, data_packet->length);
    return data_packet->length;
  }
  return 0;
}

} //namespace owt

