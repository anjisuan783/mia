// Copyright (C) <2020> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

#ifndef RTC_ADAPTER_RTC_ADAPTER_H_
#define RTC_ADAPTER_RTC_ADAPTER_H_

#include "myrtc/api/task_queue_base.h"
#include "owt_base/MediaFramePipeline.h"
#include "erizo/MediaDefinitions.h"

namespace rtc_adapter {

class AdapterDataListener {
public:
  virtual void onAdapterData(char* data, int len) = 0;

  virtual ~AdapterDataListener() = default;
};

class AdapterFrameListener {
public:
  virtual void onAdapterFrame(std::shared_ptr<owt_base::Frame> frame) = 0;

  virtual ~AdapterFrameListener() = default;
};

class AdapterFeedbackListener {
public:
  virtual void onFeedback(const owt_base::FeedbackMsg& msg) = 0;

  virtual ~AdapterFeedbackListener() = default;
};

struct AdapterStats {
  int width = 0;
  int height = 0;
  owt_base::FrameFormat format = owt_base::FRAME_FORMAT_UNKNOWN;
  int estimatedBandwidth = 0;
};

class AdapterStatsListener {
public:
  virtual void onAdapterStats(const AdapterStats& stat) = 0;

  virtual ~AdapterStatsListener() = default;
};

class VideoReceiveAdapter {
public:
  virtual int onRtpData(erizo::DataPacket*) = 0;
  virtual void requestKeyFrame() = 0;

  virtual ~VideoReceiveAdapter() = default;
};

class VideoSendAdapter {
public:
  virtual void onFrame(std::shared_ptr<owt_base::Frame>) = 0;
  virtual int onRtcpData(char* data, int len) = 0;
  virtual uint32_t ssrc() = 0;
  virtual void reset() = 0;

  virtual ~VideoSendAdapter() = default;
};

class AudioReceiveAdapter {
public:
  virtual int onRtpData(erizo::DataPacket*) = 0;

  virtual ~AudioReceiveAdapter() = default;
};

class AudioSendAdapter {
public:
  virtual void onFrame(std::shared_ptr<owt_base::Frame>) = 0;
  virtual int onRtcpData(char* data, int len) = 0;
  virtual uint32_t ssrc() = 0;

  virtual ~AudioSendAdapter() = default;
};

class RtcAdapter {
public:
  struct Config {
    // SSRC of target stream
    uint32_t ssrc = 0;
    uint32_t rtx_ssrc = 0;

    bool rtcp_rsize = false;
    int rtp_payload_type = 0;
    // Transport-cc extension ID
    int transport_cc = -1;
    int red_payload = -1;
    int ulpfec_payload = -1;
    bool flex_fec = false;
    // MID of target stream
    char mid[32];
    // MID extension ID
    int mid_ext = 0;

    //TODO(nisse) register all extertion to rtc, now only transport-cc
    //std::vector<externmap>
    
    AdapterDataListener* rtp_listener = nullptr;
    AdapterStatsListener* stats_listener = nullptr;
    AdapterFrameListener* frame_listener = nullptr;
    AdapterFeedbackListener* feedback_listener = nullptr;
  };
  virtual VideoReceiveAdapter* createVideoReceiver(const Config&) = 0;
  virtual void destoryVideoReceiver(VideoReceiveAdapter*) = 0;
  virtual VideoSendAdapter* createVideoSender(const Config&) = 0;
  virtual void destoryVideoSender(VideoSendAdapter*) = 0;

  virtual AudioReceiveAdapter* createAudioReceiver(const Config&) = 0;
  virtual void destoryAudioReceiver(AudioReceiveAdapter*) = 0;
  virtual AudioSendAdapter* createAudioSender(const Config&) = 0;
  virtual void destoryAudioSender(AudioSendAdapter*) = 0;

  virtual ~RtcAdapter() = default;
};

class RtcAdapterFactory {
 public:
  RtcAdapterFactory(webrtc::TaskQueueBase*);
  std::shared_ptr<RtcAdapter> CreateRtcAdapter();
 private:
  std::shared_ptr<RtcAdapter> adapter_;
  webrtc::TaskQueueBase* task_queue_;
};

} // namespace rtc_adapter

#endif

