//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#include "live/media_live_source.h"

#include <inttypes.h>

#include "common/media_message.h"
#include "media_source_mgr.h"
#include "encoder/media_codec.h"
#include "live/media_gop_cache.h"

#include "live/media_meta_cache.h"
#include "media_server.h"

namespace ma {

MDEFINE_LOGGER(MediaLiveSource, "MediaLiveSource");

MediaLiveSource::MediaLiveSource() {
  thread_check_.Detach();
}

MediaLiveSource::~MediaLiveSource() = default;

bool MediaLiveSource::Initialize(wa::Worker* worker, 
    bool gop, bool atc, JitterAlgorithm algorithm) {
  worker_ = worker;
  enable_gop_ = gop;
  atc_ = atc;
  jitter_algorithm_ = algorithm;
  return true;
}

void MediaLiveSource::OnPublish() {
  if (active_) {
    MLOG_CERROR("wrong publish status, active");
    return;
  }
  is_monotonically_increase_ = true;
  active_ = true;

  assert(nullptr == gop_cache_.get());
  gop_cache_.reset(new SrsGopCache);
  gop_cache_->set(enable_gop_);

  assert(nullptr == meta_.get());
  meta_.reset(new MediaMetaCache);
}

void MediaLiveSource::OnUnpublish() {
  if (!active_) {
    MLOG_CERROR("wrong publish status, unactive");
    return;
  }

  gop_cache_.reset(nullptr);
  meta_.reset(nullptr);

  last_packet_time_ = 0;
  active_ = false;
}

std::shared_ptr<MediaConsumer> MediaLiveSource::create_consumer() {
  RTC_DCHECK_RUN_ON(&thread_check_); 
  auto consumer = std::make_shared<MediaConsumer>(this); 
  consumers_.emplace_back(consumer);
  return consumer;
}

/*
void MediaLiveSource::destroy_consumer(MediaConsumer* consumer) {
  std::lock_guard<std::mutex> guard(consumer_lock_);

  auto it = std::find_if(consumers_.begin(), consumers_.end(), 
      [consumer](std::weak_ptr<MediaConsumer> c)->bool {
        return c.lock().get() == consumer;
      });
  if (it != consumers_.end()) {
    consumers_.erase(it);
  }
}
*/

void MediaLiveSource::on_audio_async(
    std::shared_ptr<MediaMessage> shared_audio) {
  RTC_DCHECK_RUN_ON(&thread_check_);
  srs_error_t err = srs_success;
  
  bool is_sequence_header = 
        SrsFlvAudio::sh(shared_audio->payload_->GetFirstMsgReadPtr(),
                        shared_audio->payload_->GetFirstMsgLength());

  // whether consumer should drop for the duplicated sequence header.
  bool drop_for_reduce = false;
  if (is_sequence_header && meta_->previous_ash()) {
    drop_for_reduce = 
            *(meta_->previous_vsh()->payload_)==*(shared_audio->payload_);
  }

  // cache the sequence header of aac, or first packet of mp3.
  // for example, the mp3 is used for hls to write the "right" audio codec.
  // TODO: FIXME: to refine the stream info system.
  if (is_sequence_header || !meta_->ash()) {
    std::shared_ptr<MediaMessage> sh_update;
    if (atc_) {
      auto cp = std::make_shared<MediaMessage>(*shared_audio.get());
      sh_update = std::move(cp);
    } else {
      sh_update = shared_audio;
    }
  
    if ((err = meta_->update_ash(sh_update)) != srs_success) {
      MLOG_CERROR("meta consume audio, code:%d desc:%s", 
          srs_error_code(err), srs_error_desc(err).c_str());
      delete err;
      return ;
    }

    if (is_sequence_header) {
      MLOG_TRACE("audio seq header update, size=" << shared_audio->size_);
    }
  }
  
  if (!drop_for_reduce) {
    for (auto i = consumers_.begin(); i != consumers_.end();) {
      if (auto c_ptr = i->lock()) {
        c_ptr->enqueue(shared_audio, atc_, jitter_algorithm_);
        ++i;
      } else {
        consumers_.erase(i++);
      }
    }
  }
  
  // when sequence header, donot push to gop cache and adjust the timestamp.
  if (!is_sequence_header) {
    return ;
  }

  if ((err = gop_cache_->cache(shared_audio)) != srs_success) {
    MLOG_CERROR("gop cache consume audio, desc:%s",srs_error_desc(err).c_str());
    delete err;
    return;
  }

  // if atc, update the sequence header to abs time.
  if (atc_ && !gop_cache_->enabled()) {
    if (meta_->ash()) {
      meta_->ash()->timestamp_ = shared_audio->timestamp_;
    }
    if (meta_->data()) {
      meta_->data()->timestamp_ = shared_audio->timestamp_;
    }
  }
}

srs_error_t MediaLiveSource::OnAudio(
    std::shared_ptr<MediaMessage> shared_audio) {
  srs_error_t err = srs_success;
  if (!active_) { 
    return err;
  }

  // monotically increase detect.
  if (is_monotonically_increase_) {
    if (last_packet_time_ > 0 && shared_audio->timestamp_ < last_packet_time_) {
      is_monotonically_increase_ = false;
      MLOG_WARN("AUDIO: stream not monotonically increase, TODO(mix_correct).");
    }
  }
  last_packet_time_ = shared_audio->timestamp_;

  async_task([shared_audio, this](std::shared_ptr<MediaLiveSource> p) {
    p->on_audio_async(shared_audio);
  });

  return err;
}

void MediaLiveSource::on_video_async(
    std::shared_ptr<MediaMessage> shared_video) {
  RTC_DCHECK_RUN_ON(&thread_check_);

  srs_error_t err = srs_success;

  bool is_sequence_header = 
      SrsFlvVideo::sh(shared_video->payload_->GetFirstMsgReadPtr(), 
                      shared_video->payload_->GetFirstMsgLength());

  // whether consumer should drop for the duplicated sequence header.
  bool drop_for_reduce = false;
  if (is_sequence_header && meta_->previous_vsh()) {
    drop_for_reduce = 
          *(meta_->previous_vsh()->payload_) == *(shared_video->payload_);
  }

  // cache the sequence header if h264
  // donot cache the sequence header to gop_cache, return here.
  if(is_sequence_header) {
    std::shared_ptr<MediaMessage> sh_update;
    if (atc_) {
      auto cp = std::make_shared<MediaMessage>(*shared_video.get());
      sh_update = std::move(cp);
    } else {
      sh_update = shared_video;
    }

    if (!drop_for_reduce) {
      if ((err = meta_->update_vsh(sh_update)) != srs_success) {
        MLOG_CERROR("meta update video, code:%d desc:%s", 
            srs_error_code(err), srs_error_desc(err).c_str());
        delete err;
        return ;
      }
    
      if (meta_->vsh_format()->is_avc_sequence_header()) {
        SrsVideoCodecConfig* c = meta_->vsh_format()->vcodec;
        srs_assert(c);

        if (last_width_ != c->width || last_height_ != c->height) {
          MLOG_CINFO("%dB video sh, "
              "codec[%d, profile=%s, level=%s, %dx%d, %dkbps, %.1ffps, %.1fs]",
              shared_video->size_, 
              c->id, 
              srs_avc_profile2str(c->avc_profile).c_str(),
              srs_avc_level2str(c->avc_level).c_str(), 
              c->width, 
              c->height,
              c->video_data_rate / 1000, 
              c->frame_rate, 
              c->duration);
          last_width_ = c->width;
          last_height_ = c->height;
        }
      }
    }
  }
  
  // copy to all consumer asynchronously
  if (!drop_for_reduce) {
    for (auto i = consumers_.begin(); i != consumers_.end();) {
      if (auto c_ptr = i->lock()) {
        c_ptr->enqueue(shared_video, atc_, jitter_algorithm_);
        ++i;
      } else {
        consumers_.erase(i++);
      }
    }
  }
  
  // when sequence header, donot push to gop cache and adjust the timestamp.
  if (is_sequence_header) {
    return ;
  }

  // cache the last gop packets
  if ((err = gop_cache_->cache(shared_video)) != srs_success) {
    MLOG_CERROR("gop cache consume vdieo, desc:%s",srs_error_desc(err).c_str());
    delete err;
    return ;
  }

  // if atc, update the sequence header to abs time.
  if (atc_ && !gop_cache_->enabled()) {
    if (meta_->vsh()) {
      meta_->vsh()->timestamp_ = shared_video->timestamp_;
    }
    if (meta_->data()) {
      meta_->data()->timestamp_ = shared_video->timestamp_;
    }
  }
}

srs_error_t MediaLiveSource::OnVideo(
    std::shared_ptr<MediaMessage> shared_video) {
  srs_error_t err = srs_success;
  if (!active_) { 
    return err;
  }

  // monotically increase detect.
  if (is_monotonically_increase_) {
    if (last_packet_time_ > 0 && shared_video->timestamp_ < last_packet_time_) {
      is_monotonically_increase_ = false;
      MLOG_WARN("VIDEO: stream not monotonically increase, TODO(mix_correct)");
    }
  }
  last_packet_time_ = shared_video->timestamp_;
  
  // drop any unknown header video.
  // @see https://github.com/ossrs/srs/issues/421
  if (!SrsFlvVideo::acceptable(shared_video->payload_->GetFirstMsgReadPtr(), 
                               shared_video->payload_->GetFirstMsgLength())) {
    char b0 = 0x00;
    if (shared_video->size_ > 0) {
      shared_video->payload_->Peek(&b0, 1);
    }
    MLOG_CWARN("drop unknown header video, size=%d, bytes[0]=%#x", 
               shared_video->size_, b0);
    return err;
  }

  async_task([shared_video, this](std::shared_ptr<MediaLiveSource> p) {
    p->on_video_async(shared_video);
  });

  return err;
}

srs_error_t MediaLiveSource::consumer_dumps(MediaConsumer* consumer, 
                                        bool dump_seq_header, 
                                        bool dump_meta, 
                                        bool dump_gop) {
  RTC_DCHECK_RUN_ON(&thread_check_);

  srs_error_t err = srs_success;

  srs_utime_t queue_size = 
        g_server_.config_.consumer_queue_size_ * SRS_UTIME_MILLISECONDS;
  consumer->set_queue_size(queue_size);
 
  // if atc, update the sequence header to gop cache time.
  if (/*atc_ && */!gop_cache_->empty()) {
    if (meta_->data()) {
      meta_->data()->timestamp_ = srsu2ms(gop_cache_->start_time());
    }
    if (meta_->vsh()) {
      meta_->vsh()->timestamp_ = srsu2ms(gop_cache_->start_time());
    }
    if (meta_->ash()) {
      meta_->ash()->timestamp_ = srsu2ms(gop_cache_->start_time());
    }
  }

  // If stream is publishing, dumps the sequence header and gop cache.
  if (active_) {
    // Copy metadata and sequence header to consumer.
    if ((err = meta_->dumps(
          consumer, 
          atc_, 
          jitter_algorithm_, 
          dump_meta, 
          dump_seq_header)) != srs_success) {
      return srs_error_wrap(err, "meta dumps");
    }

    // copy gop cache to client.
    if (dump_gop && 
        (err = gop_cache_->dump(consumer, 
                                atc_, 
                                jitter_algorithm_)) != srs_success) {
      return srs_error_wrap(err, "gop cache dumps");
    }
  }

  // print status.
  if (dump_gop) {
    MLOG_CTRACE("consumer_dumps, active=%d, queue_size=%" PRId64 ", jitter=%d", 
        (active_?1:0), queue_size, jitter_algorithm_);
  } else {
    MLOG_CTRACE("consumer_dumps, active=%d, ignore gop cache, jitter=%d", 
       (active_?1:0), jitter_algorithm_);
  }

  return err;
}

void MediaLiveSource::async_task
    (std::function<void(std::shared_ptr<MediaLiveSource>)> f) {
  std::weak_ptr<MediaLiveSource> weak_this = shared_from_this();
  worker_->task([weak_this, f] {
    if (auto this_ptr = weak_this.lock()) {
      f(this_ptr);
    }
  });
}

} //namespace ma

