// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

// This file is borrowed from lynckia/licode with some modifications.


#ifndef ERIZO_SRC_ERIZO_RTP_RTPEXTENSIONPROCESSOR_H_
#define ERIZO_SRC_ERIZO_RTP_RTPEXTENSIONPROCESSOR_H_

#include <array>
#include <map>
#include <string>
#include <vector>

#include "erizo/MediaDefinitions.h"
#include "erizo/SdpInfo.h"
#include "erizo/rtp/RtpHeaders.h"

namespace erizo {

enum RTPExtensions {
  UNKNOWN = 0,
  SSRC_AUDIO_LEVEL,     // urn:ietf:params:rtp-hdrext:ssrc-audio-level
  ABS_SEND_TIME,        // http:// www.webrtc.org/experiments/rtp-hdrext/abs-send-time
  TOFFSET,              // urn:ietf:params:rtp-hdrext:toffset
  VIDEO_ORIENTATION,    // urn:3gpp:video-orientation
  TRANSPORT_CC,         // http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01
  PLAYBACK_TIME,        // http:// www.webrtc.org/experiments/rtp-hdrext/playout-delay
  MEDIA_ID,             // urn:ietf:params:rtp-hdrext:sdes:mid
  RTP_ID,               // urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id
  REPARIED_RTP_ID       // urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id
};

constexpr uint32_t kRtpExtSize = 20;

class RtpExtensionProcessor final {
  DECLARE_LOGGER();

 public:
  explicit RtpExtensionProcessor(const std::vector<erizo::ExtMap>& ext_mappings);
  ~RtpExtensionProcessor() = default;

  void setSdpInfo(std::shared_ptr<SdpInfo> theInfo);
  
  uint32_t processRtpExtensions(std::shared_ptr<DataPacket> p);
  
  // return new RID:ssrc in extension if detected
  std::pair<std::string, uint32_t> checkNewRid(std::shared_ptr<DataPacket> p);
  
  VideoRotation getVideoRotation();

  const std::array<RTPExtensions, kRtpExtSize>& getVideoExtensionMap() {
    return ext_map_video_;
  }
  const std::array<RTPExtensions, kRtpExtSize>& getAudioExtensionMap() {
    return ext_map_audio_;
  }
  const std::vector<ExtMap>& getSupportedExtensionMap() {
    return ext_mappings_;
  }
  bool isValidExtension(const std::string& uri);

  const std::string& lastMid() { return last_mid_; }
  const std::string& lastRid() { return last_rid_; }

 private:
  std::vector<ExtMap> ext_mappings_;
  std::array<RTPExtensions, kRtpExtSize> ext_map_video_, ext_map_audio_;
  std::map<std::string, uint8_t> translationMap_;
  VideoRotation video_orientation_;
  std::map<std::string, uint32_t> rids_;
  std::string last_mid_;
  std::string last_rid_;

  uint32_t processAbsSendTime(char* buf);
  uint32_t processVideoOrientation(char* buf);
  uint32_t processMid(char* buf);
  uint32_t processRid(char* buf);
  uint32_t stripExtension(char* buf, int len);
};

}  // namespace erizo

#endif  // ERIZO_SRC_ERIZO_RTP_RTPEXTENSIONPROCESSOR_H_

