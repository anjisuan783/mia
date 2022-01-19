// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

#include "VideoReceiveAdapter.h"
#include <future>

#include "common_types.h"
#include "video/video_error_codes.h"
#include "video/timing.h"
#include "rtc_base/time_utils.h"
#include "common/rtputils.h"

// using namespace webrtc;
using namespace owt_base;

namespace rtc_adapter {

DEFINE_LOGGER(VideoReceiveAdapterImpl, "VideoReceiveAdapterImpl")

const uint32_t kBufferSize = 8192;
// Local SSRC has no meaning for receive stream here
const uint32_t kLocalSsrc = 1;

static void dump(void* index, FrameFormat format, uint8_t* buf, int len) {
  char dumpFileName[128];

  snprintf(dumpFileName, 128, "/tmp/postConstructor-%p.%s", index, getFormatStr(format));
  FILE* bsDumpfp = fopen(dumpFileName, "ab");
  if (bsDumpfp) {
      fwrite(buf, 1, len, bsDumpfp);
      fclose(bsDumpfp);
  }
}

///////////////////////////////////
//AdapterDecoder
int32_t VideoReceiveAdapterImpl::AdapterDecoder::InitDecode(
    const webrtc::VideoCodec* config, int32_t) {
  RTC_DLOG(LS_INFO) << "AdapterDecoder InitDecode";
  if (config) {
      codec_ = config->codecType;
  }
  if (!frameBuffer_) {
      bufferSize_ = kBufferSize;
      frameBuffer_.reset(new uint8_t[bufferSize_]);
  }
  return 0;
}

int32_t VideoReceiveAdapterImpl::AdapterDecoder::Decode(
    const webrtc::EncodedImage& encodedImage,
    bool missing_frames, int64_t render_time_ms) {
  owt_base::FrameFormat format = FRAME_FORMAT_UNKNOWN;

  switch (codec_) {
  case webrtc::VideoCodecType::kVideoCodecVP8:
      format = FRAME_FORMAT_VP8;
      break;
  case webrtc::VideoCodecType::kVideoCodecVP9:
      format = FRAME_FORMAT_VP9;
      break;
  case webrtc::VideoCodecType::kVideoCodecH264:
      format = FRAME_FORMAT_H264;
      break;
  default:
      OLOG_ERROR_THIS("Unknown FORMAT : " << codec_);
      return WEBRTC_VIDEO_CODEC_OK;
  }

  if (encodedImage.size() > bufferSize_) {
      bufferSize_ = encodedImage.size() * 2;
      frameBuffer_.reset(new uint8_t[bufferSize_]);
  }

  if (encodedImage._encodedWidth > 0 && encodedImage._encodedHeight > 0) {
      width_ = encodedImage._encodedWidth;
      height_ = encodedImage._encodedHeight;
  }

  memcpy(frameBuffer_.get(), encodedImage.data(), encodedImage.size());
  Frame frame;
  memset(&frame, 0, sizeof(frame));
  frame.format = format;
  frame.payload = frameBuffer_.get();
  frame.length = encodedImage.size();
  frame.timeStamp = encodedImage.Timestamp();
  
  // something wrong with ntp time, av timestamp async
  frame.ntpTimeMs = encodedImage.ntp_time_ms_;
  frame.additionalInfo.video.width = width_;
  frame.additionalInfo.video.height = height_;
  frame.additionalInfo.video.isKeyFrame = 
      (encodedImage._frameType == webrtc::VideoFrameType::kVideoFrameKey);
  auto copy = std::make_shared<Frame>(frame);
  if (parent_) {
    if (parent_->frameListener_) {
      parent_->frameListener_->onAdapterFrame(std::move(copy));
    }
    // Check video update
    if (parent_->statsListener_) {
      bool statsChanged = false;
      if (format != parent_->format_) {
        // Update format
        parent_->format_ = format;
        statsChanged = true;
      }
      if ((parent_->width_ != width_) || (parent_->height_ != height_)) {
        // Update width and height
        parent_->width_ = width_;
        parent_->height_ = height_;
        statsChanged = true;
      }
      if (statsChanged) {
        // Notify the stats
        AdapterStats stats = {parent_->width_,
                              parent_->height_,
                              parent_->format_,
                              0};
        parent_->statsListener_->onAdapterStats(stats);
      }
    }
    // Dump for debug use
    if (parent_->enableDump_ && 
        (frame.format == FRAME_FORMAT_H264 || 
         frame.format == FRAME_FORMAT_H265)) {
      dump(this, frame.format, frame.payload, frame.length);
    }
    // Request key frame
    if (parent_->reqKeyFrame_) {
      // set reqKeyFrame_ to false, 
      // see @https://github.com/anjisuan783/media_lib/issues/2
      parent_->reqKeyFrame_ = false;
      return WEBRTC_VIDEO_CODEC_OK_REQUEST_KEYFRAME;
    }
  }
  return WEBRTC_VIDEO_CODEC_OK;
}

///////////////////////////////////
//VideoReceiveAdapterImpl
VideoReceiveAdapterImpl::VideoReceiveAdapterImpl(
    CallOwner* owner, const RtcAdapter::Config& config)
  : config_(config), 
    format_(owt_base::FRAME_FORMAT_UNKNOWN), 
    frameListener_(config.frame_listener), 
    rtcpListener_(config.rtp_listener), 
    statsListener_(config.stats_listener), 
    owner_(owner) {
    assert(owner_ != nullptr);
    CreateReceiveVideo();
}

VideoReceiveAdapterImpl::~VideoReceiveAdapterImpl() {
  if (videoRecvStream_) {
    videoRecvStream_->Stop();
    call()->DestroyVideoReceiveStream(videoRecvStream_);
    videoRecvStream_ = nullptr;
  }
}

void VideoReceiveAdapterImpl::OnFrame(const webrtc::VideoFrame& video_frame) {
}

void VideoReceiveAdapterImpl::CreateReceiveVideo() {
  if (videoRecvStream_) {
    return;
  }
  // Create Receive Video Stream
  webrtc::VideoReceiveStream::Config video_recv_config(this);

  //config rtp 
  video_recv_config.rtp.remote_ssrc = config_.ssrc;
  video_recv_config.rtp.local_ssrc = kLocalSsrc;
  
  video_recv_config.rtp.rtcp_mode = webrtc::RtcpMode::kCompound;
    
  if (config_.transport_cc != -1) {
    video_recv_config.rtp.transport_cc = true;
    video_recv_config.rtp.extensions.emplace_back(
      webrtc::RtpExtension::kTransportSequenceNumberUri, config_.transport_cc);
  } else {
    video_recv_config.rtp.transport_cc = false;
  }

  if (config_.mid_ext) {
    video_recv_config.rtp.extensions.emplace_back(
       webrtc::RtpExtension::kMidUri, config_.mid_ext);
  }

  video_recv_config.rtp.ulpfec_payload_type = config_.ulpfec_payload;   
  video_recv_config.rtp.red_payload_type = config_.red_payload;
  video_recv_config.rtp.rtx_ssrc = config_.rtx_ssrc;

  video_recv_config.rtp.protected_by_flexfec = config_.flex_fec;
  
  video_recv_config.rtp.rtx_associated_payload_types[config_.rtx_ssrc] = 
      config_.rtp_payload_type;

  //config decoder
  video_recv_config.renderer = this;
  webrtc::VideoReceiveStream::Decoder decoder;
  decoder.decoder_factory = this;

  decoder.payload_type = config_.rtp_payload_type; 
  decoder.video_format = webrtc::SdpVideoFormat(
      webrtc::CodecTypeToPayloadString(webrtc::VideoCodecType::kVideoCodecH264));
  OLOG_INFO_THIS("Config add decoder:" << decoder.ToString());
  video_recv_config.decoders.push_back(decoder);

  videoRecvStream_ = call()->CreateVideoReceiveStream(std::move(video_recv_config));
  videoRecvStream_->Start();
  call()->SignalChannelNetworkState(webrtc::MediaType::VIDEO, webrtc::NetworkState::kNetworkUp);
}

void VideoReceiveAdapterImpl::requestKeyFrame() {
  reqKeyFrame_ = true;
}

std::vector<webrtc::SdpVideoFormat> 
VideoReceiveAdapterImpl::GetSupportedFormats() const {
  return std::vector<webrtc::SdpVideoFormat>{
      webrtc::SdpVideoFormat(
          webrtc::CodecTypeToPayloadString(webrtc::VideoCodecType::kVideoCodecH264)),
  };
}

std::unique_ptr<webrtc::VideoDecoder> 
VideoReceiveAdapterImpl::CreateVideoDecoder(
  const webrtc::SdpVideoFormat& format) {
  return std::make_unique<AdapterDecoder>(this);
}

int VideoReceiveAdapterImpl::onRtpData(char* data, int len) {
  auto rv = call()->Receiver()->DeliverPacket(
          webrtc::MediaType::VIDEO,
          rtc::CopyOnWriteBuffer(data, len),
          rtc::TimeUTCMicros());
  if (webrtc::PacketReceiver::DELIVERY_OK != rv) {
    OLOG_ERROR_THIS("VideoReceiveAdapterImpl DeliverPacket failed code:" << rv);
  }
  return len;
}

bool VideoReceiveAdapterImpl::SendRtp(const uint8_t* data, size_t len, 
    const webrtc::PacketOptions& options) {
  OLOG_WARN_THIS("VideoReceiveAdapterImpl SendRtp called");
  return true;
}

bool VideoReceiveAdapterImpl::SendRtcp(const uint8_t* data, size_t len) {
  if (rtcpListener_) {
    rtcpListener_->onAdapterData(
        reinterpret_cast<char*>(const_cast<uint8_t*>(data)), len);
    return true;
  }
  return false;
}

} // namespace rtc_adapter
