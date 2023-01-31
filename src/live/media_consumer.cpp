//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
// This file is borrowed from srs with some modifications.

#include "live/media_consumer.h"

#include <algorithm>

#include "common/media_log.h"
#include "common/media_message.h"
#include "encoder/media_codec.h"
#include "live/media_live_source.h"

namespace ma {

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.live");

#define CONST_MAX_JITTER_MS         250
#define CONST_MAX_JITTER_MS_NEG         -250
#define DEFAULT_FRAME_TIME_MS         10

MessageQueue::MessageQueue(bool ignore_shrink) {
  _ignore_shrink = ignore_shrink;
}

MessageQueue::~MessageQueue() {
  clear();
}

int MessageQueue::size() {
  return (int)msgs.size();
}

srs_utime_t MessageQueue::duration() {
  return (av_end_time - av_start_time);
}

void MessageQueue::set_queue_size(srs_utime_t queue_size) {
	max_queue_size = queue_size;
}

void MessageQueue::enqueue(std::shared_ptr<MediaMessage> msg, bool* is_overflow) {

  if (msg->is_av()) {
    if (av_start_time == -1) {
      av_start_time = srs_utime_t(msg->timestamp_ * UTIME_MILLISECONDS);
    }
    
    av_end_time = srs_utime_t(msg->timestamp_ * UTIME_MILLISECONDS);
  }
  
  msgs.emplace_back(std::move(msg));

  //MLOG_CTRACE("start:%lld, end:%lld, diff:%lld", av_start_time, av_end_time, av_end_time-av_start_time);
  while (av_end_time - av_start_time > max_queue_size) {
    // notice the caller queue already overflow and shrinked.
    if (is_overflow) {
      *is_overflow = true;
    }
    
    shrink();
  }
}

void MessageQueue::fetch_packets(int max_count, 
    std::vector<std::shared_ptr<MediaMessage>>& pmsgs, int& count) {
  int nb_msgs = (int)msgs.size();
  if (nb_msgs <= 0) {
    return;
  }
  
  assert(max_count > 0);
  count = std::min(max_count, nb_msgs);
  
  for (int i = 0; i < count; i++) {
    pmsgs.push_back(msgs[i]);
  }
  
  std::shared_ptr<MediaMessage> last = msgs[count - 1];
  av_start_time = srs_utime_t(last->timestamp_ * UTIME_MILLISECONDS);
  
  if (count >= nb_msgs) {
    // the pmsgs is big enough and clear msgs at most time.
    msgs.clear();
  } else {
    // erase some vector elements may cause memory copy,
    // maybe can use more efficient vector.swap to avoid copy.
    // @remark for the pmsgs is big enough, for instance, PERF_MW_MSGS 128,
    //      the rtmp play client will get 128msgs once, so this branch rarely execute.
    msgs.erase(msgs.begin(), msgs.begin() + count);
  }
}

void MessageQueue::fetch_packets(
    MediaConsumer* consumer, JitterAlgorithm ag) {

  int nb_msgs = (int)msgs.size();
  if (nb_msgs <= 0) {
    return;
  }
  
  for (int i = 0; i < nb_msgs; i++) {
    std::shared_ptr<MediaMessage> msg = msgs[i];
    consumer->enqueue(msg, ag);
  }
}

void MessageQueue::shrink() {
  std::shared_ptr<MediaMessage> video_sh;
  std::shared_ptr<MediaMessage> audio_sh;
  int msgs_size = (int)msgs.size();
  
  // remove all msg
  // igone the sequence header
  for (size_t i = 0; i < msgs.size(); i++) {
    auto& msg = msgs.at(i);
    
    if (msg->is_video() && SrsFlvVideo::sh(
        msg->payload_->GetFirstMsgReadPtr(), msg->payload_->GetFirstMsgLength())) {
      video_sh = msg;
      continue;
    }
    else if (msg->is_audio() && SrsFlvAudio::sh(
        msg->payload_->GetFirstMsgReadPtr(), msg->payload_->GetFirstMsgLength())) {
      audio_sh = msg;
      continue;
    }
  }
  msgs.clear();
  
  // update av_start_time
  av_start_time = av_end_time;
  //push_back secquence header and update timestamp
  if (video_sh) {
    video_sh->timestamp_ = srsu2ms(av_end_time);
    msgs.emplace_back(std::move(video_sh));
  }
  if (audio_sh) {
    audio_sh->timestamp_ = srsu2ms(av_end_time);
    msgs.emplace_back(std::move(audio_sh));
  }
  
  if (!_ignore_shrink) {
    MLOG_CTRACE("shrinking, size=%d, removed=%d, max=%dms", 
      (int)msgs.size(), msgs_size - (int)msgs.size(), srsu2msi(max_queue_size));
  }
}

void MessageQueue::clear() {
  msgs.clear();
  
  av_start_time = av_end_time = -1;
}

//MediaJitter
MediaConsumer::MediaJitter::MediaJitter() = default;

void MediaConsumer::MediaJitter::correct(
    std::shared_ptr<MediaMessage> msg, JitterAlgorithm ag) {
  // for performance issue
  if (JitterAlgorithmFULL != ag) {
    // all jitter correct features is disabled, ignore.
    if (JitterAlgorithmOFF == ag) {
      return ;
    }
    
    // start at zero, but donot ensure monotonically increasing.
    if (ag == JitterAlgorithmZERO) {
      if (last_pkt_correct_time == -1) {
        last_pkt_correct_time = msg->timestamp_;
      }
      msg->timestamp_ -= last_pkt_correct_time;
      return ;
    }
    
    // other algorithm, ignore.
    return ;
  }

  // full jitter algorithm, do jitter correct.
  // set to 0 for metadata.
  if (!msg->is_av()) {
    msg->timestamp_ = 0;
    return ;
  }
  
  /**
   * we use a very simple time jitter detect/correct algorithm:
   * 1. delta: ensure the delta is positive and valid,
   *     we set the delta to DEFAULT_FRAME_TIME_MS,
   *     if the delta of time is nagative or greater than CONST_MAX_JITTER_MS.
   * 2. last_pkt_time: specifies the original packet time,
   *     is used to detect next jitter.
   * 3. last_pkt_correct_time: simply add the positive delta,
   *     and enforce the time monotonically.
   */
  int64_t time = msg->timestamp_;
  int64_t delta = time - last_pkt_time;
  
  // if jitter detected, reset the delta.
  if (delta < CONST_MAX_JITTER_MS_NEG || delta > CONST_MAX_JITTER_MS) {
    // use default 10ms to notice the problem of stream.
    // @see https://github.com/ossrs/srs/issues/425
    delta = DEFAULT_FRAME_TIME_MS;
  }
  
  last_pkt_correct_time = std::max<int64_t>(0, last_pkt_correct_time + delta);

  msg->timestamp_ = last_pkt_correct_time;
  last_pkt_time = time;
}

int64_t MediaConsumer::MediaJitter::get_time() {
  return last_pkt_correct_time;
}

//MediaConsumer
MediaConsumer::MediaConsumer(MediaLiveSource* s) {
  source_ = s;
  paused_ = false;
}

MediaConsumer::~MediaConsumer() {
}

void MediaConsumer::set_queue_size(srs_utime_t queue_size) {
  queue_.set_queue_size(queue_size);
}

int64_t MediaConsumer::get_time() {
  return jitter_.get_time();
}

void MediaConsumer::enqueue(std::shared_ptr<MediaMessage> msg, 
                            JitterAlgorithm jitter_algo) {
  auto cp_msg = msg->Copy();
  
  jitter_.correct(cp_msg, jitter_algo);

  queue_.enqueue(cp_msg, nullptr);
}

void MediaConsumer::fetch_packets(int get_max,
    std::vector<std::shared_ptr<MediaMessage>>& msgs, int& count) {
  assert(count >= 0);
  assert(get_max > 0);
  
  // the count used as input to reset the max if positive.
  int max = count? std::min(count, get_max) : get_max;
  
  // the count specifies the max acceptable count,
  // here maybe 1+, and we must set to 0 when got nothing.
  count = 0;
  
  // paused, return nothing.
  if (paused_) {
    return;
  }
  
  queue_.fetch_packets(max, msgs, count);
}

void MediaConsumer::on_play_client_pause(bool is_pause) {
  MLOG_CTRACE("stream consumer change pause state %d=>%d", paused_, is_pause);
  paused_ = is_pause;
}

}

