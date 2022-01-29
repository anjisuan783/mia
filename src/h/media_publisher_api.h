//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_PUBLISHER_H__
#define __MEDIA_PUBLISHER_H__

#include <string>
#include <memory>

#include "h/rtc_media_frame.h"

namespace ma {

class MediaRtcPublisherApi {
 public:
  virtual ~MediaRtcPublisherApi() = default;

  virtual void OnPublish(const std::string& tcUrl, 
                         const std::string& stream) = 0;
  virtual void OnUnpublish() = 0;

  virtual void OnFrame(owt_base::Frame&) = 0;
};

class MediaRtcPublisherFactory {
 public:
  /*
   * rtmp : true local rtmp publisher will be created.
   *        rtmp publisher need aac(44.1khz 16bit stero), h264(without B frame)
   * rtmp : false local webrrtc publisher will be created.
            webrtc publisher need opus(48khz 16bit stero), h264(without B frame)
   */
  std::shared_ptr<MediaRtcPublisherApi> Create();
};

class MediaRtmpPublisherApi {
 public:
  virtual ~MediaRtmpPublisherApi() = default;

  virtual void OnPublish(const std::string& tcUrl, 
                         const std::string& stream) = 0;
  virtual void OnUnpublish() = 0;

  virtual void OnVideo(const uint8_t*, uint32_t len, uint32_t timestamp) = 0;
  virtual void OnAudio(const uint8_t*, uint32_t len, uint32_t timestamp) = 0;
};

class MediaRtmpPublisherFactory {
 public:
  /*
   * rtmp : true local rtmp publisher will be created.
   *        rtmp publisher need aac(44.1khz 16bit stero), h264(without B frame)
   * rtmp : false local webrrtc publisher will be created.
            webrtc publisher need opus(48khz 16bit stero), h264(without B frame)
   */
  std::shared_ptr<MediaRtmpPublisherApi> Create();
};


}

#endif //!__MEDIA_PUBLISHER_H__
