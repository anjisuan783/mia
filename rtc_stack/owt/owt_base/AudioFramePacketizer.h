// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

#ifndef AudioFramePacketizer_h
#define AudioFramePacketizer_h

#include "erizo/MediaDefinitions.h"
#include "rtc_base/task_queue.h"
#include "rtc_adapter/RtcAdapter.h"
#include "common/logger.h"
#include "owt_base/MediaFramePipeline.h"
#include "owt_base/MediaDefinitionExtra.h"
#include "rtc_adapter/RtcAdapter.h"

namespace owt_base {

/**
 * This is the class to send out the audio frame with a given format.
 */
class AudioFramePacketizer 
    : public FrameDestination,
      public erizo::MediaSource,
      public erizo::FeedbackSink,
      public erizoExtra::RTPDataReceiver,
      public rtc_adapter::AdapterStatsListener,
      public rtc_adapter::AdapterDataListener,
      public std::enable_shared_from_this<AudioFramePacketizer> {
  DECLARE_LOGGER();

 public:
  struct Config {
      std::string mid = "";
      uint32_t midExtId = 0;
      rtc_adapter::RtcAdapterFactory* factory{nullptr};
      webrtc::TaskQueueBase* task_queue = nullptr;
  };
  AudioFramePacketizer(Config& config);
  ~AudioFramePacketizer();

  void bindTransport(erizo::MediaSink* sink);
  void unbindTransport();
  void enable(bool enable) { enable_ = enable; }
  uint32_t getSsrc() { return ssrc_; }

  // Implements FrameDestination.
  void onFrame(std::shared_ptr<Frame>);

  // Implements RTPDataReceiver.
  void receiveRtpData(char*, int len, erizoExtra::DataType, uint32_t channelId);

  // Implements the AdapterStatsListener interfaces.
  void onAdapterStats(const rtc_adapter::AdapterStats& stats) override;
  // Implements the AdapterDataListener interfaces.
  void onAdapterData(char* data, int len) override;

 private:
  bool init(Config& config);
  void close();

  // Implement erizo::FeedbackSink
  int deliverFeedback_(std::shared_ptr<erizo::DataPacket> data_packet);
  // Implement erizo::MediaSource
  int sendPLI();

  bool enable_{true};
  uint32_t ssrc_{0};

  std::shared_ptr<rtc_adapter::RtcAdapter> rtcAdapter_;
  rtc_adapter::AudioSendAdapter* audioSend_{nullptr};

  std::unique_ptr<rtc::TaskQueue> task_queue_;
};

} //namespace owt_base

#endif /* AudioFramePacketizer_h */

