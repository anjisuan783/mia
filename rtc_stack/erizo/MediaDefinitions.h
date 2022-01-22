// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

// This file is borrowed from lynckia/licode with some modifications.

#ifndef ERIZO_SRC_ERIZO_MEDIADEFINITIONS_H_
#define ERIZO_SRC_ERIZO_MEDIADEFINITIONS_H_

#include <vector>
#include <algorithm>
#include <memory>
#include <cstring>

#include "utils/Clock.h"
#include "rtc_base/copy_on_write_buffer.h"

namespace erizo {

enum packetType {
  VIDEO_PACKET,
  AUDIO_PACKET,
  OTHER_PACKET
};

struct DataPacket {
  static const size_t MTU_SIZE = 1500;
  DataPacket() : buffer_{0, MTU_SIZE}, data{(char*)buffer_.data()} { }
  DataPacket(const DataPacket&) = delete;
  DataPacket(DataPacket&& dpk)
      : comp{dpk.comp}, 
        type{dpk.type}, 
        received_time_ms{dpk.received_time_ms},
        buffer_{std::move(dpk.buffer_)},
        length{dpk.length},
        data{dpk.data} {
    dpk.length = 0;
    dpk.data = nullptr;
  }

  DataPacket(int _comp, 
             const char *_data, 
             int _length, 
             packetType _type, 
             uint64_t _received_time_ms) 
             : comp{_comp},
               type{_type},
               received_time_ms{_received_time_ms},
               buffer_{_data, (size_t)_length, MTU_SIZE},
               length{_length},
               data{(char*)buffer_.data()} { }

  DataPacket(int _comp, const char *_data, int _length, packetType _type)
    : DataPacket(_comp, _data, _length, _type, 0) { }

  DataPacket(int _comp, const char *_data, int _length)
    : DataPacket(_comp, _data, _length, VIDEO_PACKET, 0) { }

  void Init(int _comp, 
            const char *_data, 
            int _length, 
            packetType _type, 
            uint64_t _received_time_ms) {
    comp = _comp;
    type = _type;
    received_time_ms = _received_time_ms;
    buffer_.SetData(_data, _length);
    length = _length;
    data = (char*)buffer_.data();
  }

  int comp{-1};         //component_id
  packetType type{VIDEO_PACKET};
  uint64_t received_time_ms{0};
  rtc::CopyOnWriteBuffer buffer_;
  int length{0};
  char* data{nullptr};
};

class MediaEvent {
public:
  MediaEvent() = default;
  virtual ~MediaEvent() = default;
  virtual std::string getType() const {
    return "event";
  }
};

using MediaEventPtr = std::shared_ptr<MediaEvent>;

class FeedbackSink {
public:
  virtual ~FeedbackSink() = default;
  inline int deliverFeedback(std::shared_ptr<DataPacket> data_packet) {
    return this->deliverFeedback_(data_packet);
  }
private:
  virtual int deliverFeedback_(std::shared_ptr<DataPacket> data_packet) = 0;
};

class FeedbackSource {
protected:
  FeedbackSink* fb_sink_;
public:
  FeedbackSource(): fb_sink_{nullptr} {}
  virtual ~FeedbackSource() {}
  void setFeedbackSink(FeedbackSink* sink) {
    fb_sink_ = sink;
  }
};

/*
* A MediaSink
*/
class MediaSink {
 public:
  MediaSink() : audio_sink_ssrc_{0},
              video_sink_ssrc_{0}, 
              sink_fb_source_{nullptr} {
  }
  
  virtual ~MediaSink() = default;

  inline int deliverAudioData(std::shared_ptr<DataPacket> data_packet) {
    return this->deliverAudioData_(std::move(data_packet));
  }
  
  inline int deliverVideoData(std::shared_ptr<DataPacket> data_packet) {
    return this->deliverVideoData_(std::move(data_packet));
  }
  
  inline uint32_t getVideoSinkSSRC() {
    return video_sink_ssrc_;
  }
  
  inline void setVideoSinkSSRC(uint32_t ssrc) {
    video_sink_ssrc_ = ssrc;
  }
  inline uint32_t getAudioSinkSSRC() {
    return audio_sink_ssrc_;
  }
  
  inline void setAudioSinkSSRC(uint32_t ssrc) {
    audio_sink_ssrc_ = ssrc;
  }
  
  inline bool isVideoSinkSSRC(uint32_t ssrc) {
    return ssrc == video_sink_ssrc_;
  }
  
  inline bool isAudioSinkSSRC(uint32_t ssrc) {
    return ssrc == audio_sink_ssrc_;
  }
  
  inline FeedbackSource* getFeedbackSource() {
    return sink_fb_source_;
  }
  
  inline int deliverEvent(MediaEventPtr event) {
    return this->deliverEvent_(event);
  }
  
  virtual void close() = 0;

 private:
  virtual int deliverAudioData_(std::shared_ptr<DataPacket> data_packet) = 0;
  virtual int deliverVideoData_(std::shared_ptr<DataPacket> data_packet) = 0;
  virtual int deliverEvent_(MediaEventPtr event) = 0;

 protected:
  // SSRCs received by the SINK
  uint32_t audio_sink_ssrc_;
  uint32_t video_sink_ssrc_;
  // Is it able to provide Feedback
  FeedbackSource* sink_fb_source_;
};

/**
* A MediaSource is any class that produces audio or video data.
*/
class MediaSource {
public:
  MediaSource() 
  : audio_source_ssrc_{0},
    video_source_ssrc_list_{std::vector<uint32_t>(1, 0)},
    video_sink_{nullptr}, 
    audio_sink_{nullptr}, 
    event_sink_{nullptr}, 
    source_fb_sink_{nullptr} {
  }
  virtual ~MediaSource() = default;
    
  inline void setAudioSink(MediaSink* audio_sink) {
    this->audio_sink_ = audio_sink;
  }
  inline void setVideoSink(MediaSink* video_sink) {
    this->video_sink_ = video_sink;
  }
  inline void setEventSink(MediaSink* event_sink) {
    this->event_sink_ = event_sink;
  }

  inline FeedbackSink* getFeedbackSink() {
    return source_fb_sink_;
  }

  uint32_t getVideoSourceSSRC() {
    if (video_source_ssrc_list_.empty()) {
      return 0;
    }
    return video_source_ssrc_list_[0];
  }
  
  void setVideoSourceSSRC(uint32_t ssrc) {
    if (video_source_ssrc_list_.empty()) {
      video_source_ssrc_list_.push_back(ssrc);
      return;
    }
    video_source_ssrc_list_[0] = ssrc;
  }
  
  inline std::vector<uint32_t> getVideoSourceSSRCList() {
    return video_source_ssrc_list_;  //  return by copy to avoid concurrent access
  }
  
  inline void setVideoSourceSSRCList(const std::vector<uint32_t>& new_ssrc_list) {
    video_source_ssrc_list_ = new_ssrc_list;
  }
  
  inline uint32_t getAudioSourceSSRC() {
    return audio_source_ssrc_;
  }
  
  inline void setAudioSourceSSRC(uint32_t ssrc) {
    audio_source_ssrc_ = ssrc;
  }

  bool isVideoSourceSSRC(uint32_t ssrc) {
    auto found_ssrc = std::find_if(video_source_ssrc_list_.begin(), 
        video_source_ssrc_list_.end(),
        [ssrc](uint32_t known_ssrc) {
          return known_ssrc == ssrc;
        });
    return (found_ssrc != video_source_ssrc_list_.end());
  }

  inline bool isAudioSourceSSRC(uint32_t ssrc) {
    return audio_source_ssrc_ == ssrc;
  }
  
  virtual int sendPLI() = 0;
 
  virtual void close() = 0;

 protected:
  // SSRCs coming from the source
  uint32_t audio_source_ssrc_;
  std::vector<uint32_t> video_source_ssrc_list_;
  MediaSink* video_sink_{nullptr};
  MediaSink* audio_sink_{nullptr};
  MediaSink* event_sink_{nullptr};
  // can it accept feedback
  FeedbackSink* source_fb_sink_{nullptr};
};

}  // namespace erizo

#endif  // ERIZO_SRC_ERIZO_MEDIADEFINITIONS_H_

