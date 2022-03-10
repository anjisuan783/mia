//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#include "media_source.h"


#include "media_server.h"
#include "rtmp/media_req.h"
#include "live/media_consumer.h"
#include "live/media_live_source.h"
#include "rtc/media_rtc_source.h"
#include "rtc/media_rtc_live_adaptor.h"
#include "live/media_live_rtc_adaptor.h"

#include "media_source_mgr.h"

namespace ma {

MDEFINE_LOGGER(MediaSource, "ma.source");

MediaSource::MediaSource(std::shared_ptr<MediaRequest> r)
  : req_{std::move(r)} {
  thread_check_.Detach();
  MLOG_TRACE(req_->get_stream_url());
}

MediaSource::~MediaSource() {
  MLOG_TRACE(req_->get_stream_url())
}

void MediaSource::Open(Config& c) {
  config_ = c;
  worker_ = config_.worker.get();

  // active rtc source here, avoid using lock 
  // when publisher or subscriber will join
  ActiveRtcSource();
  ActiveLiveSource();
  closed_ = false;
}

void MediaSource::Close() {
  closed_ = true; 
  
  worker_->task([p = shared_from_this()](){
    p->UnactiveRtcSource();
    p->UnactiveLiveSource();
    p->UnactiveRtcAdapter();
    p->UnactiveRtmpAdapter();
  });

  worker_ = nullptr;
}

std::shared_ptr<MediaConsumer> MediaSource::CreateConsumer() {
  RTC_DCHECK_RUN_ON(&thread_check_);
  return live_source_->CreateConsumer();
}

srs_error_t MediaSource::ConsumerDumps(
    MediaConsumer* consumer, 
    bool dump_seq_header, 
    bool dump_meta, 
    bool dump_gop) {
  RTC_DCHECK_RUN_ON(&thread_check_);
  return live_source_->ConsumerDumps(
      consumer, dump_seq_header, dump_meta, dump_gop);
}

srs_error_t MediaSource::Publish(std::string_view sdp, 
                                 std::shared_ptr<IHttpResponseWriter> w,
                                 std::string& publisher_id,
                                 std::shared_ptr<MediaRequest> req) {
  srs_error_t err = nullptr;
  if (closed_) {
    return srs_error_wrap(err, "source closed.");
  }

  rtc_publisher_in_ = true;
  
  err = rtc_source_->Publish(
      sdp, std::move(w), publisher_id, std::move(req));
  if (err != srs_success) {
    rtc_publisher_in_ = false;
  }
  
  return err;
}

srs_error_t MediaSource::Subscribe(std::string_view s, 
                                   std::shared_ptr<IHttpResponseWriter> w,
                                   std::string& subscriber_id,
                                   std::shared_ptr<MediaRequest> req) {
  if (closed_) {
    return srs_error_wrap(nullptr, "source closed.");
  }
  return rtc_source_->Subscribe(
      s, std::move(w), subscriber_id, std::move(req));
}

void MediaSource::OnPublish(PublisherType t) {
  auto func = [t, this](std::shared_ptr<MediaSource> p) {
    if (active_) {
      return;
    }

    publiser_type_ = t;

    if (eLocalRtc == t) {
      srs_error_t err = rtc_source_->OnLocalPublish(req_->get_stream_url());
      if (err != nullptr) {
        MLOG_ERROR("rtc local publish failed, desc:" << srs_error_desc(err));
        delete err;
      }
    }

    if (isRtmp(t)) {
      live_source_->OnPublish();
    }

    active_ = true;
    
    g_server_.OnPublish(shared_from_this(), req_);
  };

  // prevent remote publisher
  if (eLocalRtc == t) {
    rtc_publisher_in_ = true;
  }
  
  if (worker_->IsCurrent()) {
    // attach checker thread
    RTC_DCHECK_RUN_ON(&thread_check_);
    func(shared_from_this());
  } else {
   async_task(func);
  }
}

void MediaSource::OnUnpublish() {
  auto func = [this](std::shared_ptr<MediaSource> p) {
    RTC_DCHECK_RUN_ON(&thread_check_);
    if (!active_) {
      return;
    }
    
    if (eLocalRtc == publiser_type_) {
      srs_error_t err = rtc_source_->OnLocalUnpublish();
      if (err != nullptr) {
        MLOG_ERROR("rtc local unpublish failed, desc:" << srs_error_desc(err));
        delete err;
      }
    }

    if (isRtmp(publiser_type_)) {
      live_source_->OnUnpublish();
    }

    active_ = false;
    g_server_.OnUnpublish(shared_from_this(), req_);
  };

  // remote publisher can join
  if (eLocalRtc == publiser_type_) {
    rtc_publisher_in_ = false;
  }

  if (worker_->IsCurrent()) {
    RTC_DCHECK_RUN_ON(&thread_check_);
    func(shared_from_this());
  } else {
   async_task(func);
  }
}

JitterAlgorithm MediaSource::jitter() {
  RTC_DCHECK_RUN_ON(&thread_check_);
  return live_source_->jitter();
}

void MediaSource::OnRtcFirstPacket() {
  RTC_DCHECK_RUN_ON(&thread_check_);
}

void MediaSource::OnRtcPublisherJoin() {
  RTC_DCHECK_RUN_ON(&thread_check_);
  //remote rtc publisher
  OnPublish(eRemoteRtc);
}

void MediaSource::OnRtcPublisherLeft() {
  RTC_DCHECK_RUN_ON(&thread_check_);
  rtc_publisher_in_ = false;
  //remote rtc publisher
  OnUnpublish();
}

void MediaSource::OnRtcFirstSubscriber() {
  RTC_DCHECK_RUN_ON(&thread_check_);
  // transform media from rtmp to rtc
  if (isRtmp(publiser_type_)) {
    ActiveRtcAdapter();
  }
}

void MediaSource::OnRtcNobody() {
  RTC_DCHECK_RUN_ON(&thread_check_);

  MLOG_INFO("rtc onbody:" << req_->get_stream_url());
  auto self = shared_from_this();  
  // no players destroy rtc adaptor
  if (isRtmp(publiser_type_)) {
    UnactiveRtcAdapter();
  }
}

void MediaSource::OnRtmpNoConsumer() {
  RTC_DCHECK_RUN_ON(&thread_check_);
  MLOG_INFO("rtmp onbody:" << req_->get_stream_url());
  // no players destroy rtmp adaptor
  if (isRtc(publiser_type_)) {
    UnactiveRtmpAdapter();
  }
}

void MediaSource::OnRtmpFirstConsumer() {
  RTC_DCHECK_RUN_ON(&thread_check_);
  // transform media from rtc to rtmp
  if (isRtc(publiser_type_)) {
    ActiveRtmpAdapter();
  }
}

void MediaSource::OnRtmpFirstPacket() {
  RTC_DCHECK_RUN_ON(&thread_check_);
}

void MediaSource::ActiveRtcSource() {
  if (rtc_source_) {
    return;
  }
  
  rtc_source_.reset(new MediaRtcSource(req_->get_stream_url()));
  rtc_source_->Open(config_.rtc_api, worker_);
  rtc_source_->signal_rtc_first_suber_.connect(
                   this, &MediaSource::OnRtcFirstSubscriber);
  rtc_source_->signal_rtc_nobody_.connect(
                   this, &MediaSource::OnRtcNobody);
  rtc_source_->signal_rtc_first_packet_.connect(
                   this, &MediaSource::OnRtcFirstPacket);
  rtc_source_->signal_rtc_publisher_left_.connect(
                   this, &MediaSource::OnRtcPublisherLeft);
  rtc_source_->signal_rtc_publisher_join_.connect( 
                   this, &MediaSource::OnRtcPublisherJoin);
}

void MediaSource::UnactiveRtcSource() {
  if (!rtc_source_) {
    return;
  }
  
  rtc_source_->Close();
  rtc_source_->signal_rtc_first_suber_.disconnect(this);
  rtc_source_->signal_rtc_nobody_.disconnect(this);
  rtc_source_->signal_rtc_first_packet_.disconnect(this);
  rtc_source_->signal_rtc_publisher_left_.disconnect(this);
  rtc_source_->signal_rtc_publisher_join_.disconnect(this);

  rtc_source_ = nullptr;
}

void MediaSource::ActiveLiveSource() {
  if (live_source_) {
    return;
  }
  
  live_source_.reset(new MediaLiveSource(req_->get_stream_url()));
  live_source_->Initialize(config_.gop, config_.jitter_algorithm,
      config_.mix_correct_, config_.consumer_queue_size_);
  
  live_source_->signal_live_no_consumer_.connect(
                    this, &MediaSource::OnRtmpNoConsumer);
  live_source_->signal_live_fisrt_consumer_.connect(
                    this, &MediaSource::OnRtmpFirstConsumer);
  live_source_->signal_live_first_packet_.connect(
                    this, &MediaSource::OnRtmpFirstPacket);
}

void MediaSource::UnactiveLiveSource() {
  if (!live_source_) {
    return;
  }

  live_source_->signal_live_no_consumer_.disconnect(this);
  live_source_->signal_live_fisrt_consumer_.disconnect(this);
  live_source_->signal_live_first_packet_.disconnect(this);
  live_source_ = nullptr;
}

void MediaSource::ActiveRtcAdapter() {
  RTC_DCHECK_RUN_ON(&thread_check_);

  if (rtc_adapter_) {
    return;
  }

  if (!config_.enable_rtmp2rtc_) {
    return;
  }

  rtc_adapter_ = std::make_shared<MediaLiveRtcAdaptor>(req_->get_stream_url());
  srs_error_t err = rtc_adapter_->Open(worker_, live_source_.get(), 
      rtc_source_.get(), config_.enable_rtmp2rtc_debug_);

  if (err != nullptr) {
    MLOG_ERROR("rtc_adapter open failed, desc:" << srs_error_desc(err));
    delete err;
    rtc_adapter_->Close();
    rtc_adapter_ = nullptr;
  }
}

void MediaSource::UnactiveRtcAdapter() {
  RTC_DCHECK_RUN_ON(&thread_check_);

  if (!rtc_adapter_) {
    return;
  }

  rtc_adapter_->Close();
  rtc_adapter_ = nullptr;
}

void MediaSource::ActiveRtmpAdapter() {
  RTC_DCHECK_RUN_ON(&thread_check_);

  if (live_adapter_) {
    return;
  }

  if (!config_.enable_rtc2rtmp_) {
    return;
  }
  
  live_adapter_.reset(new MediaRtcLiveAdaptor(req_->get_stream_url()));
  live_adapter_->Open(rtc_source_.get(), live_source_.get(), 
      config_.enable_rtc2rtmp_debug_);
}

void MediaSource::UnactiveRtmpAdapter() {
  if (!live_adapter_) {
    return;
  }

  live_adapter_->Close();  
  live_adapter_.reset(nullptr);
}

void MediaSource::OnMessage(std::shared_ptr<MediaMessage> msg) {
  async_task([msg](std::shared_ptr<MediaSource> p) {
    if (p->live_source_) {
      if (msg->is_audio()) {
        p->live_source_->OnAudio(msg, false);
      }
      else if (msg->is_video()) {
        p->live_source_->OnVideo(msg, false);
      } else {
        // TODO support meta 
      }
    }
  });
}

void MediaSource::OnFrame(std::shared_ptr<owt_base::Frame> msg) {
  async_task([frm = std::move(msg)](std::shared_ptr<MediaSource> p){
    if (p->rtc_source_) {
      p->rtc_source_->OnFrame(std::move(frm));
    }
  });
}

void MediaSource::async_task
    (std::function<void(std::shared_ptr<MediaSource>)> f) {
  worker_->task([weak_this = weak_from_this(), f] {
    if (auto this_ptr = weak_this.lock()) {
      f(this_ptr);
    }
  });
}

} //namespace ma
