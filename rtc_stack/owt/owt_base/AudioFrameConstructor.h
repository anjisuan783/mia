// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

#ifndef AudioFrameConstructor_h
#define AudioFrameConstructor_h

#include "rtc_base/rtp_to_ntp_estimator.h"
#include "owt_base/MediaFramePipeline.h"
#include "owt_base/MediaDefinitionExtra.h"
#include "erizo/MediaDefinitions.h"
#include "common/logger.h"
#include "rtc_adapter/RtcAdapter.h"
#include "erizo/rtp/RtpHeaders.h"

namespace owt_base {

/**
 * A class to process the incoming streams by leveraging video coding module from
 * webrtc engine, which will framize the frames.
 */
class AudioFrameConstructor final : public erizo::MediaSink,
                                    public erizo::FeedbackSource,
                                    public FrameSource,
                                    public rtc_adapter::AdapterDataListener {
  DECLARE_LOGGER();

 public:
  struct config {
    uint32_t ssrc{0};
    bool rtcp_rsize{false};
    int rtp_payload_type{0};
    int transportcc{-1};
    rtc_adapter::RtcAdapterFactory* factory{nullptr};
  };
  
  AudioFrameConstructor(const config&);
  virtual ~AudioFrameConstructor();

  void bindTransport(erizo::MediaSource* source, erizo::FeedbackSink* fbSink);
  void unbindTransport();
  void enable(bool enabled) { enabled_ = enabled; }

  // Implements the FrameSource interfaces.
  void onFeedback(const FeedbackMsg& msg);

 private:
  // Implement erizo::MediaSink
  int deliverAudioData_(std::shared_ptr<erizo::DataPacket> audio_packet) override;
  int deliverVideoData_(std::shared_ptr<erizo::DataPacket> video_packet) override;
  int deliverEvent_(erizo::MediaEventPtr event) override;

  void onAdapterData(char* data, int len) override;
  void close();
  void createAudioReceiver();

  void onSr(erizo::RtcpHeader *chead);
  int64_t getNtpTimestamp(uint32_t ts);
 private:
  bool enabled_{true};
  uint32_t ssrc_{0};
  erizo::MediaSource* transport_{nullptr};
  config config_;
  std::shared_ptr<rtc_adapter::RtcAdapter> rtcAdapter_;
  rtc_adapter::AudioReceiveAdapter* audioReceive_{nullptr};
  webrtc::RtpToNtpEstimator ntp_estimator_;
};

}
#endif /* AudioFrameConstructor_h */

