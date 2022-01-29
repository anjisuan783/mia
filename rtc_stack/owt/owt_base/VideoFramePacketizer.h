// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

#ifndef VideoFramePacketizer_h
#define VideoFramePacketizer_h

#include <memory>
#include "common/logger.h"

#include "erizo/MediaDefinitions.h"
#include "rtc_adapter/RtcAdapter.h"
#include "rtc_base/task_queue.h"
#include "owt_base/MediaFramePipeline.h"
#include "owt_base/MediaDefinitionExtra.h"


namespace owt_base {
/**
 * This is the class to accept the encoded frame with the given format,
 * packetize the frame and send them out via the given WebRTCTransport.
 * It also gives the feedback to the encoder based on the feedback from the remote.
 */
class VideoFramePacketizer 
    : public FrameDestination,
      public erizo::MediaSource,
      public erizo::FeedbackSink,
      public rtc_adapter::AdapterFeedbackListener,
      public rtc_adapter::AdapterStatsListener,
      public rtc_adapter::AdapterDataListener,
      public std::enable_shared_from_this<VideoFramePacketizer> {
  DECLARE_LOGGER();

 public:
  struct Config {
      int Red{false};
      int Ulpfec{-1};
      int transportccExt{-1};
      bool selfRequestKeyframe{false};
      std::string mid{""};
      uint32_t midExtId{0};
      rtc_adapter::RtcAdapterFactory* factory{nullptr};
      webrtc::TaskQueueBase* task_queue{nullptr};
  };
  VideoFramePacketizer(Config& config);
  ~VideoFramePacketizer() override;

  void close();
    
  void bindTransport(erizo::MediaSink* sink);
  void unbindTransport();
  void enable(bool enabled);
  uint32_t getSsrc() { return ssrc_; }

  // Implements FrameDestination.
  void onFrame(std::shared_ptr<Frame>);
  void onVideoSourceChanged() override;
  
  // Implements the AdapterFeedbackListener interfaces.
  void onFeedback(const FeedbackMsg& msg) override;
  // Implements the AdapterStatsListener interfaces.
  void onAdapterStats(const rtc_adapter::AdapterStats& stats) override { }
  // Implements the AdapterDataListener interfaces.
  void onAdapterData(char* data, int len) override;

 private:
  bool init(Config& config);

  // Implement erizo::FeedbackSink
  int deliverFeedback_(std::shared_ptr<erizo::DataPacket> data_packet);
  // Implement erizo::MediaSource
  int sendPLI() { return 0; }

 private:
  bool enabled_{true};
  uint32_t ssrc_{0};
  uint16_t sendFrameCount_{0};
  std::shared_ptr<rtc_adapter::RtcAdapter> rtcAdapter_;
  rtc_adapter::VideoSendAdapter* videoSend_{nullptr};
  std::unique_ptr<rtc::TaskQueue> task_queue_;
};

} //namespace owt_base

#endif /* EncodedVideoFrameSender_h */

