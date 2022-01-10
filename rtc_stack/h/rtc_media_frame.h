// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

#ifndef MediaFrameDefine_h
#define MediaFrameDefine_h

#include <stdint.h>
#include <cstring>

namespace owt_base{

enum FrameFormat {
  FRAME_FORMAT_UNKNOWN      = 0,

  FRAME_FORMAT_I420,

  FRAME_FORMAT_VP8,
  FRAME_FORMAT_VP9,
  FRAME_FORMAT_H264,
  FRAME_FORMAT_H265,

  FRAME_FORMAT_MSDK,

  FRAME_FORMAT_PCM_48000_2,
  FRAME_FORMAT_PCMU,
  FRAME_FORMAT_PCMA,
  FRAME_FORMAT_OPUS,
  FRAME_FORMAT_ISAC16,
  FRAME_FORMAT_ISAC32,
  FRAME_FORMAT_ILBC,
  FRAME_FORMAT_G722_16000_1,
  FRAME_FORMAT_G722_16000_2,

  FRAME_FORMAT_AAC,           // ignore sample rate and channels for decoder, default is 48000_2
  FRAME_FORMAT_AAC_48000_2,   // specify sample rate and channels for encoder

  FRAME_FORMAT_AC3,
  FRAME_FORMAT_NELLYMOSER,

  FRAME_FORMAT_DATA,  // Generic data frame. We don't know its detailed structure.
  FRAME_FORMAT_MAX
};

enum VideoCodecProfile {
  PROFILE_UNKNOWN                     = 0,

  /* AVC Profiles */
  PROFILE_AVC_CONSTRAINED_BASELINE    = 66 + (0x100 << 1),
  PROFILE_AVC_BASELINE                = 66,
  PROFILE_AVC_MAIN                    = 77,
  //PROFILE_AVC_EXTENDED                = 88,
  PROFILE_AVC_HIGH                    = 100,
};

struct VideoFrameSpecificInfo {
  uint16_t width;
  uint16_t height;
  bool isKeyFrame;
};

struct AudioFrameSpecificInfo {
  /*AudioFrameSpecificInfo() : isRtpPacket(false) {}*/
  uint8_t isRtpPacket; // FIXME: Temporarily use Frame to carry rtp-packets due to the premature AudioFrameConstructor implementation.
  uint32_t nbSamples;
  uint32_t sampleRate;
  uint8_t channels;
  uint8_t voice;
  uint8_t audioLevel;
};

typedef union MediaSpecInfo {
  VideoFrameSpecificInfo video;
  AudioFrameSpecificInfo audio;
} MediaSpecInfo;

struct Frame;

bool isAudioFrame(const Frame& frame);
bool isVideoFrame(const Frame& frame);

struct Frame {
  FrameFormat     format;
  uint8_t*        payload;
  uint32_t        length;
  uint32_t        timeStamp;
  int64_t         ntpTimeMs;
  MediaSpecInfo   additionalInfo;

  Frame() = default;
  
  Frame(const Frame& r) {
    format = r.format;
    payload = new uint8_t[r.length];
    std::memcpy(payload, r.payload, r.length);
    length = r.length;
    timeStamp = r.timeStamp;
    ntpTimeMs = r.ntpTimeMs;
    if (isVideoFrame(*this)) {
      additionalInfo.video = r.additionalInfo.video;
    } else {
      additionalInfo.audio = r.additionalInfo.audio;
    }
    need_delete = true;
  }

  Frame(Frame&& r) {
    format = r.format;
    payload = r.payload;
    r.payload = nullptr;
    length = r.length;
    timeStamp = r.timeStamp;
    ntpTimeMs = r.ntpTimeMs;
    if (isVideoFrame(*this)) {
      additionalInfo.video = r.additionalInfo.video;
    } else {
      additionalInfo.audio = r.additionalInfo.audio;
    }
    need_delete = true;
    r.need_delete = false;
  }
  ~Frame() {
    if (need_delete)
      delete[] payload;
  }

  void operator =(const Frame&) = delete;
  void operator =(Frame&&) = delete;
  
 private:
  bool need_delete{false};
};

inline bool isAudioFrame(const Frame& frame) {
  return frame.format == FRAME_FORMAT_PCM_48000_2
      || frame.format == FRAME_FORMAT_PCMU
      || frame.format == FRAME_FORMAT_PCMA
      || frame.format == FRAME_FORMAT_OPUS
      || frame.format == FRAME_FORMAT_ISAC16
      || frame.format == FRAME_FORMAT_ISAC32
      || frame.format == FRAME_FORMAT_ILBC
      || frame.format == FRAME_FORMAT_G722_16000_1
      || frame.format == FRAME_FORMAT_G722_16000_2
      || frame.format == FRAME_FORMAT_AAC
      || frame.format == FRAME_FORMAT_AAC_48000_2
      || frame.format == FRAME_FORMAT_AC3
      || frame.format == FRAME_FORMAT_NELLYMOSER;
}

inline bool isVideoFrame(const Frame& frame) {
  return frame.format == FRAME_FORMAT_I420
      || frame.format == FRAME_FORMAT_MSDK
      || frame.format == FRAME_FORMAT_VP8
      || frame.format == FRAME_FORMAT_VP9
      || frame.format == FRAME_FORMAT_H264
      || frame.format == FRAME_FORMAT_H265;
}

inline bool isDataFrame(const Frame& frame) {
  return frame.format == FRAME_FORMAT_DATA;
}


}
#endif
