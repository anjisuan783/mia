//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#include "example_rtmp_publisher.h"

#include <iostream>

#include "config.h"
#include "rtc_base/thread.h"
#include "rtc_base/clock.h"
#include "rtc_base/time_utils.h"
#include "encoder/media_codec.h"

extern rtc::Thread* g_thdMain;

const int TIME_OUT_LONG = 20;

using namespace ma;

ExpRtmpPublish::ExpRtmpPublish() {
}

srs_error_t ExpRtmpPublish::Open(const std::string& v) {
  MIA_LOG("RtmpPublish::Open source:%s", v.c_str());

  srs_error_t err = reader_.open(v);
  if (err != ERROR_SUCCESS) {
    return err;
  }
  file_size_ = reader_.filesize();
  
  if ((err = decoder_.initialize(&reader_)) != srs_success) {
    return srs_error_wrap(err, "init decoder");
  }
  
  char header[9];
  if ((err = decoder_.read_header(header)) != srs_success) {
    return srs_error_wrap(err, "read header");
  }
  
  char no_use[4];
  if ((err = decoder_.read_previous_tag_size(no_use)) != srs_success) {
    return srs_error_wrap(err, "read pts");
  }

  last_round_ts_ = 0;

  begin_pos_ = reader_.tellg();

  api_ = std::move(ma::MediaRtmpPublisherFactory().Create());
  api_->OnPublish("rtmp://127.0.0.1/live", "livestream");

  g_thdMain->PostDelayed(RTC_FROM_HERE, 100, this, MSG_TIMEOUT, nullptr);
}

int ExpRtmpPublish::Close() {

  MIA_LOG("RtcPublish::Close");
  reader_.close();
  api_->OnUnpublish();
}

srs_error_t ExpRtmpPublish::ReadTags() {

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
        assert(SrsFlvVideo::sh(data, size));
      }
      api_->OnVideo((const uint8_t*)data, size, media_ts);
    } else if (type == 8) {
      // audio
      if (first_audio_pkt) {
        first_audio_pkt = false;
        assert(SrsFlvAudio::sh(data, size));
      }
      //std::cout << "ExpRtmpPublish: len:" << size << 
      //", media_ts:" << media_ts << ", ts:" << time << std::endl;
      api_->OnAudio((const uint8_t*)data, size, media_ts);
    } else if(type == 18) {
      // script
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

void ExpRtmpPublish::OnMessage(rtc::Message* msg) {
  srs_error_t err = srs_success;
  if (MSG_TIMEOUT == msg->message_id) {
    if ((err = ReadTags()) == srs_success) {
      g_thdMain->PostDelayed(RTC_FROM_HERE, TIME_OUT_LONG, this, MSG_TIMEOUT, nullptr);
    }
  }
}

static ExpRtmpPublish* g_publish = nullptr;

int Usage() {
  printf("usage: ./rtc_push -l [log4cxx.properties] -c [mia.cfg] -s [flv] \n");
  return -1;
}

extern std::string g_log4cxx_config_path;
extern std::string g_mia_config_path;

static std::string flv_path;

int ParseArgs(int argc, char* argv[]) {
  if (argc < 7) {
    return -1;
  }
  
  int c;
  while ((c = getopt(argc, argv, "l:c:s:")) != -1) {
    switch (c) {
      case 's':
        flv_path = optarg;
        break;
  		case 'l':
        g_log4cxx_config_path = optarg;
        break;
      case 'c':
        g_mia_config_path = optarg;
        break;
      default:
        std::cout << "RtcPublish service" << std::endl;
        return -1;
    }
  }

  return 0;
}

int ServiceStart() {
  g_publish = new ExpRtmpPublish;
  srs_error_t err = g_publish->Open(flv_path);
  if (err != ERROR_SUCCESS) {
    std::cout << "rtmp publisher open source file:" << flv_path
        << " error, desc:" << srs_error_desc(err) << std::endl;
  }
  return 0;
}

void ServiceStop() {
  g_publish->Close();

  delete g_publish;
  g_publish = nullptr;
}

