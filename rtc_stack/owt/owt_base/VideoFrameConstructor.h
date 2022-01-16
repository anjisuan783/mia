// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

#ifndef VideoFrameConstructor_h
#define VideoFrameConstructor_h

#include <memory>
#include "common/logger.h"

#include "utils/Worker.h"
#include "erizo/MediaDefinitions.h"
#include "owt_base/MediaDefinitionExtra.h"
#include "owt_base/MediaFramePipeline.h"
#include "rtc_adapter/RtcAdapter.h"

namespace owt_base {

class VideoInfoListener {
public:
  virtual ~VideoInfoListener(){};
  virtual void onVideoInfo(const std::string& videoInfoJSON) = 0;
};

/**
* A class to process the incoming streams by leveraging video coding module from
* webrtc engine, which will framize the frames.
*/
class VideoFrameConstructor  
    : public erizo::MediaSink,
      public erizo::FeedbackSource,
      public FrameSource,
      public rtc_adapter::AdapterFrameListener,
      public rtc_adapter::AdapterStatsListener,
      public rtc_adapter::AdapterDataListener {
  DECLARE_LOGGER();
public:
  struct config {
    uint32_t ssrc{0};
    uint32_t rtx_ssrc{0};
    bool rtcp_rsize{false};
    int rtp_payload_type{0};
    int ulpfec_payload{-1};
    bool flex_fec = false;
    int transportcc{-1};
    int red_payload{-1};
    wa::Worker* worker{nullptr};
    rtc_adapter::RtcAdapterFactory* factory{nullptr};
  };

  VideoFrameConstructor(VideoInfoListener*, const config& _config);
  virtual ~VideoFrameConstructor();

  void bindTransport(erizo::MediaSource* source, erizo::FeedbackSink* fbSink);
  void unbindTransport();
  void enable(bool enabled);

  //timer 1s
  void onTimeout();

  // Implements the FrameSource interfaces.
  void onFeedback(const FeedbackMsg& msg) override;

  // Implements the AdapterFrameListener interfaces.
  void onAdapterFrame(std::shared_ptr<Frame>) override;
  // Implements the AdapterStatsListener interfaces.
  void onAdapterStats(const rtc_adapter::AdapterStats& stats) override;
  // Implements the AdapterDataListener interfaces.
  void onAdapterData(char* data, int len) override;

  int32_t RequestKeyFrame();

  bool setBitrate(uint32_t kbps);

private:
  config config_;

  void createReceiveVideo(uint32_t);

  // Implement erizo::MediaSink
  int deliverAudioData_(std::shared_ptr<erizo::DataPacket> audio_packet) override;
  int deliverVideoData_(std::shared_ptr<erizo::DataPacket> video_packet) override;
  int deliverEvent_(erizo::MediaEventPtr event) override { return 0; }
  void close();

  bool enable_{true};
  uint32_t ssrc_{0};

  erizo::MediaSource* transport_{nullptr};
  uint32_t pendingKeyFrameRequests_{0};

  VideoInfoListener* videoInfoListener_;

  std::shared_ptr<rtc_adapter::RtcAdapter> rtcAdapter_;
  rtc_adapter::VideoReceiveAdapter* videoReceive_{nullptr};

  wa::Worker* worker_;
};

} // namespace owt_base

#endif /* VideoFrameConstructor_h */
