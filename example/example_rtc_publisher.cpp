//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#include "example_rtc_publisher.h"

#include "config.h"
#include "rtc_base/thread.h"
#include "common/media_message.h"
#include "rtmp/media_rtmp_const.h"

#include <iostream>

using namespace ma;

extern rtc::Thread* g_thdMain;

ExpRtcPublish::ExpRtcPublish() {
}

srs_error_t ExpRtcPublish::Open(const std::string& v) {
  MIA_LOG("RtcPublish::Open source:%s", v.c_str());
  
  reader_.reset(new ExpFlvLoopReader);
  srs_error_t err = reader_->Open(this, v, g_thdMain);
  if (err != srs_success) {
    return srs_error_wrap(err, "loop reader open");
  }

  audio_.reset(new AudioTransform);
  if (srs_success != (err = audio_->Open(this, true, "/tmp/a.aac"))) {
    return err;
  }

  video_.reset(new Videotransform);
  if (srs_success != (err = video_->Open(this, true, "/tmp/a.264"))) {
    return err;
  }

  api_ = std::move(MediaRtcPublisherFactory().Create());
  api_->OnPublish("rtmp://127.0.0.1/live", "livestream");

  return err;
}

void ExpRtcPublish::Close() {
  MIA_LOG("RtcPublish::Close");
  reader_->Close();
  audio_.reset(nullptr);
  video_.reset(nullptr);
  api_->OnUnpublish();
}

void ExpRtcPublish::OnFlvVideo(const uint8_t* data, int32_t len, uint32_t ts) {
  MessageHeader header{.payload_length = len, 
      .message_type = RTMP_MSG_VideoMessage, .timestamp = ts, 
      .stream_id = 0, .perfer_cid = 0};
  auto msg = MediaMessage::create(&header, (const char*)data);
  srs_error_t err = video_->OnData(std::move(msg));
  if (nullptr != err) {
    std::cout << "transform video error, desc:" << 
        srs_error_desc(err) << std::endl;
    delete err;
  }
}

void ExpRtcPublish::OnFlvAudio(const uint8_t* data, int32_t len, uint32_t ts) {
  MessageHeader header{.payload_length = len, 
      .message_type = RTMP_MSG_AudioMessage, .timestamp = ts, 
      .stream_id = 0, .perfer_cid = 0};
  auto msg = MediaMessage::create(&header, (const char*)data);
  srs_error_t err = audio_->OnData(std::move(msg));
  if (nullptr != err) {
    std::cout << "transform audio error, desc:" << 
        srs_error_desc(err) << std::endl;
    delete err;
  }
}

void ExpRtcPublish::OnFrame(owt_base::Frame& frm) {
  api_->OnFrame(frm);
}

static ExpRtcPublish* g_publish = nullptr;

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
  g_publish = new ExpRtcPublish;
  srs_error_t err = g_publish->Open(flv_path);
  if (err != nullptr) {
    std::cout << "rtc publisher open source file:" << flv_path
        << " error, desc:" << srs_error_desc(err) << std::endl;
    delete err;
    return -1;
  }
  return 0;
}

void ServiceStop() {
  g_publish->Close();

  delete g_publish;
  g_publish = nullptr;
}
