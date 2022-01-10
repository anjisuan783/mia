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

namespace erizo {

enum packetType {
  VIDEO_PACKET,
  AUDIO_PACKET,
  OTHER_PACKET
};

struct DataPacket {
  DataPacket() = default;

  DataPacket(int comp_, 
             const char *data_, 
             int length_, 
             packetType type_, 
             uint64_t received_time_ms_) 
             : comp{comp_}, 
               length{length_}, 
               type{type_}, 
               received_time_ms{received_time_ms_}, 
               is_keyframe{false},
               ending_of_layer_frame{false}, 
               picture_id{-1}, 
               tl0_pic_idx{-1} {
    std::memcpy(data, data_, length_);
  }

  DataPacket(int comp_, const char *data_, int length_, packetType type_)
    : comp{comp_}, 
      length{length_}, 
      type{type_}, 
      received_time_ms{wa::ClockUtils::timePointToMs(wa::clock::now())},
      is_keyframe{false}, 
      ending_of_layer_frame{false},
      picture_id{-1}, 
      tl0_pic_idx{-1} {
    std::memcpy(data, data_, length_);
  }

  DataPacket(int comp_, const unsigned char *data_, int length_)
    : comp{comp_}, 
      length{length_}, 
      type{VIDEO_PACKET}, 
      received_time_ms{wa::ClockUtils::timePointToMs(wa::clock::now())},
      is_keyframe{false}, 
      ending_of_layer_frame{false}, 
      picture_id{-1}, 
      tl0_pic_idx{-1} {
    std::memcpy(data, data_, length_);
  }

  bool belongsToSpatialLayer(int spatial_layer_) {
    std::vector<int>::iterator item = std::find(compatible_spatial_layers.begin(),
                                              compatible_spatial_layers.end(),
                                              spatial_layer_);

    return item != compatible_spatial_layers.end();
  }

  bool belongsToTemporalLayer(int temporal_layer_) {
    std::vector<int>::iterator item = std::find(compatible_temporal_layers.begin(),
                                              compatible_temporal_layers.end(),
                                              temporal_layer_);

    return item != compatible_temporal_layers.end();
  }

  int comp;         //component_id
  char data[1500];
  int length;
  packetType type;
  uint64_t received_time_ms;
  std::vector<int> compatible_spatial_layers;
  std::vector<int> compatible_temporal_layers;
  bool is_keyframe;  // Note: It can be just a keyframe first packet in VP8
  bool ending_of_layer_frame;
  int picture_id;
  int tl0_pic_idx;
  std::string codec;
  unsigned int clock_rate = 0;
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

