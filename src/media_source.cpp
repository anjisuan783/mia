//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#include "media_source.h"

#include "rtmp/media_req.h"
#include "live/media_consumer.h"
#include "live/media_live_source.h"
#include "rtc/media_rtc_source.h"

namespace ma {

MDEFINE_LOGGER(MediaSource, "MediaSource");

MediaSource::MediaSource(std::shared_ptr<MediaRequest> r)
  : req_{std::move(r)} {
  MLOG_TRACE(req_->stream);
}

MediaSource::~MediaSource() {
  MLOG_TRACE(req_->stream)
}

void MediaSource::Initialize(Config& c) {
  config_ = c;
}

std::shared_ptr<MediaConsumer> MediaSource::create_consumer() {
  CheckLiveSource();
  return live_source_->create_consumer();
}

srs_error_t MediaSource::consumer_dumps(
    MediaConsumer* consumer, 
    bool dump_seq_header, 
    bool dump_meta, 
    bool dump_gop) {
  CheckLiveSource();

  return live_source_->consumer_dumps(
      consumer, dump_seq_header, dump_meta, dump_gop);
}

void MediaSource::CheckLiveSource() {
  if (!live_source_) {
    live_source_ = std::make_shared<MediaLiveSource>();
    live_source_->initialize(config_.worker.get(), 
                             config_.gop,
                             config_.atc,
                             config_.jitter_algorithm);
  }
}

void MediaSource::CheckRtcSource() {
  if (!rtc_source_) {
    rtc_source_ = std::move(std::make_shared<MediaRtcSource>());
    rtc_source_->Open(config_.rtc_api, config_.worker.get());
  }
}

srs_error_t MediaSource::Publish(const std::string& s, 
                                 std::shared_ptr<IHttpResponseWriter> w,
                                 std::string& publisher_id) {
  CheckRtcSource();
  return rtc_source_->Publish(s, std::move(w), publisher_id);
}

srs_error_t MediaSource::Subscribe(const std::string& s, 
                                   std::shared_ptr<IHttpResponseWriter> w,
                                   std::string& subscriber_id) {
  CheckRtcSource();
  return rtc_source_->Subscribe(s, std::move(w), subscriber_id);
}

JitterAlgorithm MediaSource::jitter() {
  return live_source_->jitter();
}

} //namespace ma

