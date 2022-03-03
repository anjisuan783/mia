//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#include "example_rtmp_publisher.h"

#include <iostream>

#include "config.h"
#include "rtc_base/thread.h"

extern rtc::Thread* g_thdMain;

ExpRtmpPublish::ExpRtmpPublish() { }

srs_error_t ExpRtmpPublish::Open(const std::string& v) {
  srs_error_t err = srs_success;
  MIA_LOG("RtmpPublish::Open source:%s", v.c_str());

  reader_.reset(new ExpFlvLoopReader);
  err = reader_->Open(this, v, g_thdMain);
  if (err != srs_success) {
    return srs_error_wrap(err, "loop reader open");
  }

  api_ = std::move(ma::MediaRtmpPublisherFactory().Create());
  api_->OnPublish("rtmp://127.0.0.1/live", "livestream");

  return err;
}

void ExpRtmpPublish::Close() {
  MIA_LOG("RtcPublish::Close");
  reader_->Close();
  api_->OnUnpublish();
}

void ExpRtmpPublish::OnFlvVideo(const uint8_t* data, int32_t len, uint32_t ts) {
  api_->OnVideo(data, len, ts);
}
void ExpRtmpPublish::OnFlvAudio(const uint8_t* data, int32_t len, uint32_t ts) {
  api_->OnAudio(data, len, ts);
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

