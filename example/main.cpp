//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#include <stdio.h>
#include <iostream>
#include <stdlib.h>
#include <signal.h>
#include <memory>
#include <assert.h>

#include "h/media_return_code.h"
#include "h/media_server_api.h"
#include "rtc_base/thread.h"
#include "config.h"

rtc::Thread* g_thdMain;

void SignalExit(int sig) {
  std::cout << "signal:" << sig << std::endl;
	g_thdMain->Stop();
}

int RegisterSignalHandler() {
	if (::signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		::printf("ERROR: main, signal(SIGPIPE) failed!\n");
		return -1;
	}
	
	if (::signal(SIGUSR1, SignalExit) == SIG_ERR) {
		::printf("ERROR: main, signal(SIGINT) failed!\n");
		return -1;
	}
	
	if (::signal(SIGINT, SignalExit) == SIG_ERR) {
		::printf("ERROR: main, signal(SIGINT) failed!\n");
		return -1;
	}
	
	if (::signal(SIGTERM, SignalExit) == SIG_ERR) {
		::printf("ERROR: main, signal(SIGINT) failed!\n");
		return -1;
	}

	return 0;
}

int Usage();
int ParseArgs(int argc, char* argv[]);
int ServiceStart();
void ServiceStop();

std::string g_log4cxx_config_path;
std::string g_mia_config_path;

int main(int argc, char* argv[]) {
  if (ParseArgs(argc, argv)) {
    return Usage();
  }

  if (g_log4cxx_config_path.empty() || g_mia_config_path.empty()) {
    std::cout << "main" << std::endl;
    return Usage();
  }

  std::cout << "init log4cxx with " << g_log4cxx_config_path << std::endl;
  std::cout << "init mia with " << g_mia_config_path << std::endl;

  ma::MediaServerApi::Config cfg;
  if (config(cfg, g_log4cxx_config_path, g_mia_config_path)) {
    std::cout << "config failed exit" << std::endl;
    return -1;
  }

  if (RegisterSignalHandler()) {
    return -1;
  }
  
  class ServerMonitor : public rtc::MessageHandler {
    enum { MSG_TIMEOUT };
   public:
    ServerMonitor(ma::MediaServerApi* p, rtc::Thread* pthread) 
      : server_{p}, thread_{pthread} {
      thread_->PostDelayed(RTC_FROM_HERE, 5000, this, MSG_TIMEOUT, nullptr);
    }
    void OnMessage(rtc::Message* msg) override {
      if (MSG_TIMEOUT == msg->message_id) {
        server_->Dump();
        thread_->PostDelayed(RTC_FROM_HERE, 5000, this, MSG_TIMEOUT, nullptr);
      }
    }
   private:
    ma::MediaServerApi* server_;
    rtc::Thread*        thread_;
  };

  ma::MediaServerApi* server = ma::MediaServerFactory().Create();
  int ret = server->Init(cfg);
  if (ma::kma_ok != ret) {
    MIA_LOG("initialize failed, code:%d", ret);
    return ret;
  }

  g_thdMain = rtc::Thread::Current();
  
  if (ServiceStart()) {
    MIA_LOG("ServiceStart failed!");
    server->Close();
    return -1;
  }
  
  ServerMonitor monitor(server, g_thdMain);  
  MIA_LOG("mia start pid:%u", ::getpid());
  g_thdMain->Run();
  ServiceStop();
  server->Close();
  MIA_LOG("mia stop");

  return 0;
}

