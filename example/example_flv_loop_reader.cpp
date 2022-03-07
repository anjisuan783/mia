//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#include "example_flv_loop_reader.h"

#include "rtc_base/clock.h"
#include "rtc_base/time_utils.h"
#include "encoder/media_codec.h"

const int TIME_OUT_LONG = 10;

srs_error_t ExpFlvLoopReader::Open(ExpFlvLoopReaderSink* sink, 
    const std::string& v, rtc::Thread* thread) {
  srs_error_t err = reader_.open(v);
  if (err != ERROR_SUCCESS) {
    return err;
  }
  file_size_ = reader_.filesize();
  
  if ((err = decoder_.initialize(&reader_)) != srs_success) {
    return srs_error_wrap(err, "init decoder");
  }

  reader_.seek2(begin_pos_);

  last_round_ts_ = 0;

  thread->PostDelayed(RTC_FROM_HERE, 100, this, MSG_TIMEOUT, nullptr);
  sink_ = sink;
  thread_ = thread;

  return err;
}

void ExpFlvLoopReader::Close() {
  reader_.close();
}

srs_error_t ExpFlvLoopReader::ReadTags() {
  int64_t cur_ts = rtc::TimeMillis();
  
  if (loop_begin_ts_ == -1) {
    loop_begin_ts_ = cur_ts;
  }
  
  srs_error_t err = srs_success;
  char no_use[4];

  int64_t end_ts = cur_ts - loop_begin_ts_;

  while (true) {  
    char type;
    int32_t size;
    uint32_t time;

    int64_t pre_offset = reader_.tellg();
    
    if ((err = decoder_.read_tag_header(&type, &size, &time)) != srs_success) {
      return srs_error_wrap(err, "read tag header");
    }
    
    if ((int64_t)time > end_ts) {
      // read from tag header next time
      reader_.seek2(pre_offset);
      break;
    }

    char* data = new char[size];
    std::unique_ptr<char> data_deleter(data); 
    if ((err = decoder_.read_tag_data(data, size)) != srs_success) {
      return srs_error_wrap(err, "read tag data");
    }

    uint32_t media_ts = uint32_t(last_round_ts_ + time);
    
    // create message
    if (type == 9) {
      // video
      if (first_video_pkt) {
        first_video_pkt = false;
        assert(ma::SrsFlvVideo::sh(data, size));
      }

      // ignore AVC end of sequence
      if (size > 0 && (data[0] != 0x17 || data[1] != 0x02)) {
        sink_->OnFlvVideo((const uint8_t*)data, size, media_ts);
      }
    } else if (type == 8) {
      // audio
      if (first_audio_pkt) {
        first_audio_pkt = false;
        assert(ma::SrsFlvAudio::sh(data, size));
      }
      sink_->OnFlvAudio((const uint8_t*)data, size, media_ts);
    } else if(type == 18) {
      // script
      sink_->OnFlvMeta((const uint8_t*)data, size, media_ts);
    }

    if ((err = decoder_.read_previous_tag_size(no_use)) != srs_success) {
      return srs_error_wrap(err, "read pts");
    }

    // reach the file end, set to data begin position
    int64_t tel_g = reader_.tellg();
    if (tel_g >= file_size_) {
      last_round_ts_ += time;
      loop_begin_ts_ = cur_ts;
      reader_.seek2(begin_pos_);
      break;
    }
  }

  return err;
}

void ExpFlvLoopReader::OnMessage(rtc::Message* msg) {
  srs_error_t err = srs_success;
  if (MSG_TIMEOUT == msg->message_id) {
    if ((err = ReadTags()) == srs_success) {
      thread_->PostDelayed(
          RTC_FROM_HERE, TIME_OUT_LONG, this, MSG_TIMEOUT, nullptr);
    } else {
      delete err;
    }
  }
}
