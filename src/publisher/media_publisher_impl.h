//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_PUBLISHER_IMPL_H__
#define __MEDIA_PUBLISHER_IMPL_H__

#include "h/media_publisher_api.h"

#include <memory>

namespace ma {

class MediaRequest;
class SrsFileWriter;
class SrsFlvStreamEncoder;
class MediaSource;

class MediaRtcPublisher : public MediaRtcPublisherApi {
 public:
  MediaRtcPublisher();
  ~MediaRtcPublisher() override;
  
 private:
  void OnPublish(const std::string& tcUrl, 
                 const std::string& stream) override;
  void OnUnpublish() override;
  void OnFrame(owt_base::Frame&) override;
 private:
  bool active_{false};
  std::shared_ptr<MediaSource> source_;
  std::shared_ptr<MediaRequest> req_;
};

class MediaRtmpPublisher : public MediaRtmpPublisherApi {
 public:
  MediaRtmpPublisher() = default;
  ~MediaRtmpPublisher() override = default;
  
 private:
  void OnPublish(const std::string& tcUrl, 
                 const std::string& stream) override;
  void OnUnpublish() override;
 
  void OnVideo(const uint8_t*, uint32_t len, uint32_t timestamp) override;
  void OnAudio(const uint8_t*, uint32_t len, uint32_t timestamp) override;

  void ToFile();
 private:
  bool active_;
  std::shared_ptr<MediaSource> source_;
  std::shared_ptr<MediaRequest> req_;  
  std::unique_ptr<SrsFileWriter> file_writer_;
  std::unique_ptr<SrsFlvStreamEncoder> flv_encoder_;
  bool debug_{false};
};

}

#endif //!__MEDIA_PUBLISHER_IMPL_H__
