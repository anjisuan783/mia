//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#include "example_rtc_publisher.h"

#include <iostream>

#include "config.h"

ExpRtcPublish::ExpRtcPublish() {
}

int ExpRtcPublish::Open(const std::string& v) {
  MIA_LOG("RtcPublish::Open source:%s", v.c_str());
}

int ExpRtcPublish::Close() {

  MIA_LOG("RtcPublish::Close");
}

void ExpRtcPublish::OnMessage(rtc::Message* msg) {
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
  g_publish->Open(flv_path);
  return 0;
}

void ServiceStop() {
  g_publish->Close();

  delete g_publish;
  g_publish = nullptr;
}

