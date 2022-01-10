// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

#ifndef MediaFramePipeline_h
#define MediaFramePipeline_h

#include <list>
#include <unordered_map>
#include <stdint.h>
#include <string>
#include <memory>

#include "h/rtc_media_frame.h"

namespace owt_base {

enum MetaDataType {
  META_DATA_OWNER_ID = 0,
};

struct MetaData {
  MetaDataType type;
  uint8_t* payload;
  uint32_t length;
};

inline FrameFormat getFormat(const std::string& codec) {
  if (codec == "vp8") {
    return owt_base::FRAME_FORMAT_VP8;
  } else if (codec == "h264") {
    return owt_base::FRAME_FORMAT_H264;
  } else if (codec == "vp9") {
    return owt_base::FRAME_FORMAT_VP9;
  } else if (codec == "h265") {
    return owt_base::FRAME_FORMAT_H265;
  } else if (codec == "pcm_48000_2" || codec == "pcm_raw") {
    return owt_base::FRAME_FORMAT_PCM_48000_2;
  } else if (codec == "pcmu") {
    return owt_base::FRAME_FORMAT_PCMU;
  } else if (codec == "pcma") {
    return owt_base::FRAME_FORMAT_PCMA;
  } else if (codec == "isac_16000") {
    return owt_base::FRAME_FORMAT_ISAC16;
  } else if (codec == "isac_32000") {
    return owt_base::FRAME_FORMAT_ISAC32;
  } else if (codec == "ilbc") {
    return owt_base::FRAME_FORMAT_ILBC;
  } else if (codec == "g722_16000_1") {
    return owt_base::FRAME_FORMAT_G722_16000_1;
  } else if (codec == "g722_16000_2") {
    return owt_base::FRAME_FORMAT_G722_16000_2;
  } else if (codec == "opus_48000_2") {
    return owt_base::FRAME_FORMAT_OPUS;
  } else if (codec.compare(0, 3, "aac") == 0) {
    if (codec == "aac_48000_2")
      return owt_base::FRAME_FORMAT_AAC_48000_2;
    else
      return owt_base::FRAME_FORMAT_AAC;
  } else if (codec.compare(0, 3, "ac3") == 0) {
    return owt_base::FRAME_FORMAT_AC3;
  } else if (codec.compare(0, 10, "nellymoser") == 0) {
    return owt_base::FRAME_FORMAT_NELLYMOSER;
  } else {
    return owt_base::FRAME_FORMAT_UNKNOWN;
  }
}

inline const char *getFormatStr(const FrameFormat &format) {
  switch(format) {
    case FRAME_FORMAT_UNKNOWN:
        return "UNKNOWN";
    case FRAME_FORMAT_I420:
        return "I420";
    case FRAME_FORMAT_MSDK:
        return "MSDK";
    case FRAME_FORMAT_VP8:
        return "VP8";
    case FRAME_FORMAT_VP9:
        return "VP9";
    case FRAME_FORMAT_H264:
        return "H264";
    case FRAME_FORMAT_H265:
        return "H265";
    case FRAME_FORMAT_PCM_48000_2:
        return "PCM_48000_2";
    case FRAME_FORMAT_PCMU:
        return "PCMU";
    case FRAME_FORMAT_PCMA:
        return "PCMA";
    case FRAME_FORMAT_OPUS:
        return "OPUS";
    case FRAME_FORMAT_ISAC16:
        return "ISAC16";
    case FRAME_FORMAT_ISAC32:
        return "ISAC32";
    case FRAME_FORMAT_ILBC:
        return "ILBC";
    case FRAME_FORMAT_G722_16000_1:
        return "G722_16000_1";
    case FRAME_FORMAT_G722_16000_2:
        return "G722_16000_2";
    case FRAME_FORMAT_AAC:
        return "AAC";
    case FRAME_FORMAT_AAC_48000_2:
        return "AAC_48000_2";
    case FRAME_FORMAT_AC3:
        return "AC3";
    case FRAME_FORMAT_NELLYMOSER:
        return "NELLYMOSER";
    default:
        return "INVALID";
  }
}

enum FeedbackType {
  VIDEO_FEEDBACK,
  AUDIO_FEEDBACK
};

enum FeedbackCmd {
  REQUEST_KEY_FRAME,
  SET_BITRATE,
  REQUEST_OWNER_ID,
  RTCP_PACKET  
  // FIXME: Temporarily use FeedbackMsg to carry audio rtcp-packets 
  // due to the premature AudioFrameConstructor implementation.
};

struct FeedbackMsg {
  FeedbackType type;
  FeedbackCmd  cmd;
  union {
    unsigned short kbps;
    struct RtcpPacket{
      // FIXME: Temporarily use FeedbackMsg to carry audio rtcp-packets 
      // due to the premature AudioFrameConstructor implementation.
      uint32_t len;
      char     buf[128];
    } rtcp;
  } data;
  FeedbackMsg(FeedbackType t, FeedbackCmd c) : type{t}, cmd{c} {}
};

class FrameDestination;
class FrameSource : public std::enable_shared_from_this<FrameSource> {
 public:
  FrameSource() = default;
  virtual ~FrameSource();

  virtual void onFeedback(const FeedbackMsg&) { };

  void addAudioDestination(std::shared_ptr<FrameDestination>);
  void removeAudioDestination(FrameDestination*);

  void addVideoDestination(std::shared_ptr<FrameDestination>);
  void removeVideoDestination(FrameDestination*);

  void addDataDestination(std::shared_ptr<FrameDestination>);
  void removeDataDestination(FrameDestination*);

 protected:
  void deliverFrame(const Frame&);
  void deliverMetaData(const MetaData&);

 private:
  std::unordered_map<FrameDestination*, std::weak_ptr<FrameDestination>> m_audio_dests;
  std::unordered_map<FrameDestination*, std::weak_ptr<FrameDestination>> m_video_dests;
  std::unordered_map<FrameDestination*, std::weak_ptr<FrameDestination>> m_data_dests;
};

class FrameDestination {
 public:
  FrameDestination() = default;
  virtual ~FrameDestination() { }

  virtual void onFrame(const Frame&) = 0;
  virtual void onMetaData(const MetaData&) {}
  virtual void onVideoSourceChanged() {}

  void setAudioSource(std::weak_ptr<FrameSource>);
  void unsetAudioSource();

  void setVideoSource(std::weak_ptr<FrameSource>);
  void unsetVideoSource();

  void setDataSource(std::weak_ptr<FrameSource>);
  void unsetDataSource();

  bool hasAudioSource() { return !m_audio_src.expired(); }
  bool hasVideoSource() { return !m_video_src.expired(); }
  bool hasDataSource() { return !m_data_src.expired(); }

 protected:
  void deliverFeedbackMsg(const FeedbackMsg& msg);

 private:
  std::weak_ptr<FrameSource> m_audio_src;
  std::weak_ptr<FrameSource> m_video_src;
  std::weak_ptr<FrameSource> m_data_src;
};

class VideoFrameDecoder : public FrameSource, public FrameDestination {
 public:
  virtual ~VideoFrameDecoder() { }
  virtual bool init(FrameFormat) = 0;
};

class VideoFrameProcesser : public FrameSource, public FrameDestination {
 public:
  virtual ~VideoFrameProcesser() { }
  virtual bool init(FrameFormat format, const uint32_t width, const uint32_t height, const uint32_t frameRate) = 0;
  virtual void drawText(const std::string& textSpec) = 0;
  virtual void clearText() = 0;
};

class VideoFrameAnalyzer : public FrameSource, public FrameDestination {
 public:
  virtual ~VideoFrameAnalyzer() { }
  virtual bool init(FrameFormat format, const uint32_t width, const uint32_t height, const uint32_t frameRate, const std::string& pluginName) = 0;
};

class VideoFrameEncoder : public FrameDestination {
 public:
  virtual ~VideoFrameEncoder() { }

  virtual FrameFormat getInputFormat() = 0;

  virtual bool canSimulcast(FrameFormat, uint32_t width, uint32_t height) = 0;
  virtual bool isIdle() = 0;
  virtual int32_t generateStream(uint32_t width, uint32_t height, uint32_t frameRate, uint32_t bitrateKbps, uint32_t keyFrameIntervalSeconds, FrameDestination*) = 0;
  virtual void degenerateStream(int32_t streamId) = 0;
  virtual void setBitrate(unsigned short kbps, int32_t streamId) = 0;
  virtual void requestKeyFrame(int32_t streamId) = 0;
};

}
#endif
