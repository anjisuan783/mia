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
#include "media_server.h"
#include "rtc/media_rtc_live_adaptor.h"

namespace ma {

MDEFINE_LOGGER(MediaSource, "MediaSource");

MediaSource::MediaSource(std::shared_ptr<MediaRequest> r)
  : req_{std::move(r)} {
  MLOG_TRACE(req_->get_stream_url());
}

MediaSource::~MediaSource() {
  MLOG_TRACE(req_->get_stream_url())
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
    live_source_->Initialize(config_.worker.get(), 
                             config_.gop,
                             config_.atc,
                             config_.jitter_algorithm);
    live_adapter_ = std::move(
        std::make_unique<MediaRtcLiveAdaptor>(req_->stream));
    live_adapter_->SetSink(live_source_.get());
  }
}

void MediaSource::CheckRtcSource() {
  if (!rtc_source_) {
    rtc_source_ = std::move(std::make_shared<MediaRtcSource>());
    rtc_source_->Open(config_.rtc_api, config_.worker.get());
    rtc_source_->signal_rtc_first_suber_.connect(
                     this, &MediaSource::OnRtcFirstSubscriber);
    rtc_source_->signal_rtc_nobody_.connect(
                     this, &MediaSource::OnRtcNobody);
    rtc_source_->signal_rtc_publish_.connect(
                     this, &MediaSource::OnRtcPublish);
    rtc_source_->signal_rtc_unpublish_.connect(
                     this, &MediaSource::OnRtcUnPublish);
  }
}

srs_error_t MediaSource::Publish(const std::string& s, 
                                 std::shared_ptr<IHttpResponseWriter> w,
                                 std::string& publisher_id) {
  CheckRtcSource();
  return rtc_source_->Publish(s, std::move(w), req_->stream, publisher_id);
}

srs_error_t MediaSource::Subscribe(const std::string& s, 
                                   std::shared_ptr<IHttpResponseWriter> w,
                                   std::string& subscriber_id) {
  CheckRtcSource();
  return rtc_source_->Subscribe(s, std::move(w), req_->stream, subscriber_id);
}

JitterAlgorithm MediaSource::jitter() {
  return live_source_->jitter();
}

void MediaSource::OnRtcPublish() {
  CheckLiveSource();
  g_server_.OnPublish(shared_from_this(), req_);
  live_source_->OnPublish();
  rtc_source_->SetMediaSink(this);
}

void MediaSource::OnRtcUnPublish() {
  g_server_.OnUnpublish(shared_from_this(), req_);
  live_source_->OnUnpublish();
}

void MediaSource::OnRtcFirstSubscriber() {
}

void MediaSource::OnRtcNobody() {
}

void MediaSource::OnMediaFrame(const owt_base::Frame& frm) {
  if (live_adapter_ ) {
    live_adapter_->onFrame(frm);
  }
}

} //namespace ma

