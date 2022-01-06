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
#include "media_source_mgr.h"

namespace ma {

MDEFINE_LOGGER(MediaSource, "ma.source");

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
  ActiveLiveSource();
  return live_source_->create_consumer();
}

srs_error_t MediaSource::consumer_dumps(
    MediaConsumer* consumer, 
    bool dump_seq_header, 
    bool dump_meta, 
    bool dump_gop) {
  ActiveLiveSource();

  return live_source_->consumer_dumps(
      consumer, dump_seq_header, dump_meta, dump_gop);
}

void MediaSource::ActiveLiveSource() {
  std::lock_guard<std::mutex> guard(live_source_lock_);
  
  if (live_source_) {
    return;
  }
  live_source_ = std::make_shared<MediaLiveSource>();
  live_source_->Initialize(config_.worker.get(), 
                           config_.gop,
                           config_.jitter_algorithm);
  live_source_->OnPublish();
 
  live_source_->signal_live_no_consumer_.connect(
                    this, &MediaSource::UnactiveLiveSource);

  ActiveAdapter();                  
}

void MediaSource::UnactiveLiveSource() {
  std::lock_guard<std::mutex> guard(live_source_lock_);
  
  if (!live_source_) {
    return;
  }

  live_source_->OnUnpublish();
  live_source_ = nullptr;
  UnactiveAdapter();
}

void MediaSource::CheckRtcSource() {
  if (rtc_source_) {
    return;
  }
  
  rtc_source_ = std::move(std::make_shared<MediaRtcSource>());
  rtc_source_->Open(config_.rtc_api, config_.worker.get());
  rtc_source_->signal_rtc_first_suber_.connect(
                   this, &MediaSource::OnRtcFirstSubscriber);
  rtc_source_->signal_rtc_nobody_.connect(
                   this, &MediaSource::OnRtcNobody);
  rtc_source_->signal_rtc_first_packet_.connect(
                   this, &MediaSource::OnRtcFirstPacket);
  rtc_source_->signal_rtc_publisher_left_.connect(
                   this, &MediaSource::OnRtcPublisherLeft);
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
  ActiveLiveSource();
  return live_source_->jitter();
}

void MediaSource::OnRtcFirstPacket() {
  rtc_active_ = true;
  g_server_.OnPublish(shared_from_this(), req_);
  rtc_source_->SetMediaSink(this);
  ActiveAdapter();
}

void MediaSource::OnRtcPublisherLeft() {
  rtc_active_ = false;
  publisher_in_.clear();
  g_server_.OnUnpublish(shared_from_this(), req_);
  UnactiveAdapter();
}

void MediaSource::OnRtcFirstSubscriber() {
}

void MediaSource::OnRtcNobody() {
  MLOG_INFO("onbody, destroy source:" << req_->get_stream_url());
  auto self = shared_from_this();
  g_source_mgr_.RemoveSource(req_);
}

void MediaSource::OnMediaFrame(const owt_base::Frame& frm) {
  if (live_adapter_ ) {
    live_adapter_->onFrame(frm);
  }
}

void MediaSource::ActiveAdapter() {
  if (!live_source_ || !rtc_active_) {
    return;
  }

  assert(live_adapter_.get() == nullptr);
  live_adapter_.reset(new MediaRtcLiveAdaptor(req_->stream));
  live_adapter_->SetSink(live_source_.get());
}

void MediaSource::UnactiveAdapter() {
  if (!live_adapter_) {
    return;
  }
  live_adapter_.reset(nullptr);
}

} //namespace ma

