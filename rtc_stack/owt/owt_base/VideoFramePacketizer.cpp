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
    : m_rtcAdapter{std::move(config.factory->CreateRtcAdapter())} {
  auto factory = rtc_adapter::createDummyTaskQueueFactory(config.task_queue);
  auto task_queue = factory->CreateTaskQueue(
      "deliver_frame", webrtc::TaskQueueFactory::Priority::NORMAL);
  task_queue_ = std::move(
      std::make_unique<rtc::TaskQueue>(std::move(task_queue)));
  
  init(config);
}

VideoFramePacketizer::~VideoFramePacketizer() {
  close();
  if (m_videoSend) {
    m_rtcAdapter->destoryVideoSender(m_videoSend);
    m_videoSend = nullptr;
  }
}

bool VideoFramePacketizer::init(VideoFramePacketizer::Config& config) {
  if (m_videoSend) {
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
  m_videoSend = m_rtcAdapter->createVideoSender(sendConfig);
  m_ssrc = m_videoSend->ssrc();
  return true;
}

void VideoFramePacketizer::bindTransport(erizo::MediaSink* sink) {
  video_sink_ = sink;
  video_sink_->setVideoSinkSSRC(m_videoSend->ssrc());
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
  m_enabled = enabled;
  if (m_enabled) {
    m_sendFrameCount = 0;
    if (m_videoSend) {
      m_videoSend->reset();
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
  
  task_queue_->PostTask([this, weak_ptr = weak_from_this(), frame = std::move(f)] () {
    if (auto shared_this = weak_ptr.lock()) {
      if (!m_enabled) {
        return;
      }

      if (m_videoSend) {
        m_videoSend->onFrame(std::move(frame));
      }
    }
  });
}

void VideoFramePacketizer::onVideoSourceChanged() {
  ELOG_TRACE("onVideoSourceChanged");
  if (m_videoSend) {
    m_videoSend->reset();
  }
}

void VideoFramePacketizer::close() {
  unbindTransport();
}

int VideoFramePacketizer::deliverFeedback_(std::shared_ptr<erizo::DataPacket> data_packet) {
  if (m_videoSend) {
    m_videoSend->onRtcpData(data_packet->data, data_packet->length);
    return data_packet->length;
  }
  return 0;
}

} //namespace owt

