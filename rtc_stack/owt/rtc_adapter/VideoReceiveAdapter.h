// Copyright (C) <2020> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

#ifndef RTC_ADAPTER_VIDEO_RECEIVE_ADAPTER_
#define RTC_ADAPTER_VIDEO_RECEIVE_ADAPTER_

#include <atomic>
#include "common/logger.h"

#include "api/transport.h"
#include "api/video_codec.h"
#include "api/video_decoder.h"
#include "api/video_decoder_factory.h"
#include "call/call.h"
#include "rtc_base/task_queue.h"

#include "rtc_adapter/AdapterInternalDefinitions.h"
#include "rtc_adapter/RtcAdapter.h"

namespace rtc_adapter {

class VideoReceiveAdapterImpl : public VideoReceiveAdapter,
                                public rtc::VideoSinkInterface<webrtc::VideoFrame>,
                                public webrtc::VideoDecoderFactory,
                                public webrtc::Transport {
  DECLARE_LOGGER();
public:
  VideoReceiveAdapterImpl(CallOwner* owner, const RtcAdapter::Config& config);
  virtual ~VideoReceiveAdapterImpl();
  // Implement VideoReceiveAdapter
  int onRtpData(erizo::DataPacket*) override;
  void requestKeyFrame() override;

  // Implements rtc::VideoSinkInterface<VideoFrame>.
  void OnFrame(const webrtc::VideoFrame& video_frame) override;

  // Implements the webrtc::VideoDecoderFactory interface.
  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;
  std::unique_ptr<webrtc::VideoDecoder> 
    CreateVideoDecoder(const webrtc::SdpVideoFormat& format) override;

  // Implements webrtc::Transport
  bool SendRtp(const uint8_t* packet,
      size_t length,
      const webrtc::PacketOptions& options) override;
  bool SendRtcp(const uint8_t* packet, size_t length) override;

private:
  class AdapterDecoder : public webrtc::VideoDecoder {
  public:
    AdapterDecoder(VideoReceiveAdapterImpl* parent)
        : parent_(parent){
    }

    int32_t InitDecode(const webrtc::VideoCodec* config, 
                       int32_t number_of_cores) override;

    int32_t Decode(const webrtc::EncodedImage& input,
                   bool missing_frames,
                   int64_t render_time_ms) override;

    int32_t RegisterDecodeCompleteCallback(
        webrtc::DecodedImageCallback* callback) override { return 0; }

    int32_t Release() override { return 0; }
  private:
    VideoReceiveAdapterImpl* parent_;
    webrtc::VideoCodecType codec_;
    uint16_t width_;
    uint16_t height_;
    std::unique_ptr<uint8_t[]> frameBuffer_;
    uint32_t bufferSize_;
  };

  void CreateReceiveVideo();

  std::shared_ptr<webrtc::Call> call() {
    return owner_ ? owner_->call() : nullptr;
  }

  bool enableDump_{false};
  RtcAdapter::Config config_;
  // Video Statistics collected in decoder thread
  owt_base::FrameFormat format_;
  uint16_t width_{0};
  uint16_t height_{0};
  // Listeners
  AdapterFrameListener* frameListener_;
  AdapterDataListener* rtcpListener_;
  AdapterStatsListener* statsListener_;

  bool reqKeyFrame_{false};
  CallOwner* owner_{nullptr};

  webrtc::VideoReceiveStream* videoRecvStream_{nullptr};
};

} // namespace rtc_adapter

#endif /* RTC_ADAPTER_VIDEO_RECEIVE_ADAPTER_ */
